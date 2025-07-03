/*

   Copyright (C) 2022-2025, Robert Oostenveld

   This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if defined __linux__ || defined __APPLE__
// Linux and macOS code goes here
#include <unistd.h>
#define min(x, y) ((x)<(y) ? x : y)
#define max(x, y) ((x)>(y) ? x : y)
#elif defined _WIN32
// Windows code goes here
#endif

#include "portaudio.h"
#include "samplerate.h"
#include "lsl_c.h"

/* Helper function to generate random UID string. */
void rand_str(char *, size_t);

#define STRLEN        (80)
#define SAMPLETYPE    paFloat32
#define BLOCKSIZE     (0.01)  // in seconds
#define BUFFERSIZE    (2.00)  // in seconds
#define DEFAULTRATE   (44100.0)
#define TIMEOUT       (3.0)   // for LSL
#define FSAMPLE       (250.0)
#define LSLSTREAM     "Audio"
#define LSLTYPE       "EEG"
#define LSLBUFFER     (360)

typedef struct {
        float *data;
        unsigned long frames;
} dataBuffer_t;

dataBuffer_t inputData, outputData;
lsl_outlet outlet;

SRC_STATE* resampleState = NULL;
SRC_DATA resampleData;
int srcErr;

float inputRate, outputRate, resampleRatio;
short keepRunning = 1;
int channelCount, inputBlocksize, outputBlocksize, inputBufsize, outputBufsize;
unsigned long inputCounter = 0, outputCounter = 0;

/*******************************************************************************************************/
int output_lsl(void)
{
        for (int sample=0; sample<outputData.frames; sample++)
        {
                /* write the available output samples to LSL */
                float *dat = outputData.data + sample * channelCount * sizeof(float);
                lsl_push_sample_f(outlet, dat);
        }
        outputData.frames = 0;

        return 0;
}

/*******************************************************************************************************/
int resample_buffers(void)
{
        resampleData.src_ratio      = resampleRatio;
        resampleData.end_of_input   = 0;
        resampleData.data_in        = inputData.data;
        resampleData.input_frames   = inputData.frames;
        resampleData.data_out       = outputData.data + outputData.frames * channelCount;
        resampleData.output_frames  = outputBufsize - outputData.frames;

        /* check whether there is data in the input buffer */
        if (inputData.frames==0)
                return 0;

        /* check whether there is room for new data in the output buffer */
        if (outputData.frames==outputBufsize)
                return 0;

        int srcErr = src_process (resampleState, &resampleData);
        if (srcErr)
        {
                printf("ERROR: Cannot resample the input data\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                exit(srcErr);
        }

        /* the input data buffer decreased */
        size_t len = (inputData.frames - resampleData.input_frames_used) * channelCount * sizeof(float);
        memcpy(inputData.data, inputData.data + resampleData.input_frames_used * channelCount, len);
        inputData.frames -= resampleData.input_frames_used;

        /* the output data buffer increased */
        outputData.frames += resampleData.output_frames_gen;

        /* keep track of how many samples were converted */
        inputCounter += resampleData.input_frames_used;
        outputCounter += resampleData.output_frames_gen;

        return 0;
}


/*******************************************************************************************************/
static int input_callback( const void *input,
                           void *output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
        float *data = (float *)input;
        dataBuffer_t *inputData = (dataBuffer_t *)userData;
        unsigned int newFrames = min(frameCount, inputBufsize - inputData->frames);

        size_t len = newFrames * channelCount * sizeof(float);
        memcpy(inputData->data + inputData->frames * channelCount, data, len);
        inputData->frames += newFrames;

        /* the data can be resampled and streamed out immediately */
        resample_buffers();
        output_lsl();

        return paContinue;
}

/*******************************************************************************************************/
void stream_finished(void *userData)
{
        keepRunning = 0;
        return;
}

/*******************************************************************************************************/
int main(int argc, char *argv[]) {
        char line[STRLEN];
        float blockSize;

        /* variables that are specific for PortAudio */
        unsigned int inputDevice;
        PaStream *inputStream;
        PaStreamParameters inputParameters;
        PaError paErr = paNoError;
        unsigned int numDevices;
        const PaDeviceInfo *deviceInfo;

        /* variables that are specific for LSL */
        char outputStream[STRLEN], outputUID[STRLEN];
        int lslErr = 0;

        /* STAGE 1: Initialize the audio input and output. */

        printf("PortAudio version: 0x%08X\n", Pa_GetVersion());

        printf("Block size in seconds [%.4f]: ", BLOCKSIZE);
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
                blockSize = BLOCKSIZE;
        else
                blockSize = atof(line);

        inputParameters.device = paNoDevice;

        /* Initialize library before making any other calls. */
        paErr = Pa_Initialize();
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot initialize PortAudio.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto cleanup1;
        }

        numDevices = Pa_GetDeviceCount();
        if (numDevices <= 0)
        {
                printf("ERROR: No audio devices available.\n");
                paErr = numDevices;
                goto cleanup1;
        }

        printf("Number of host APIs = %d\n", Pa_GetHostApiCount());
        printf("Number of devices = %d\n", numDevices);
        for( int i=0; i<numDevices; i++ )
        {
                deviceInfo = Pa_GetDeviceInfo( i );
                if (Pa_GetHostApiCount()==1)
                        printf("device %2d - %s (%d in, %d out)\n", i,
                               deviceInfo->name,
                               deviceInfo->maxInputChannels,
                               deviceInfo->maxOutputChannels  );
                else
                        printf("device %2d - %s - %s (%d in, %d out)\n", i,
                               Pa_GetHostApiInfo(deviceInfo->hostApi)->name,
                               deviceInfo->name,
                               deviceInfo->maxInputChannels,
                               deviceInfo->maxOutputChannels);
        }

        printf("Select input device [%d]: ", Pa_GetDefaultInputDevice());
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                inputDevice = Pa_GetDefaultInputDevice();
        else
                inputDevice = atoi(line);

        printf("Input sampling rate [%.0f]: ", DEFAULTRATE);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                inputRate = DEFAULTRATE;
        else
                inputRate = atof(line);

        deviceInfo = Pa_GetDeviceInfo(inputDevice);
        printf("Number of channels [%d]: ", deviceInfo->maxInputChannels);
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
                channelCount = deviceInfo->maxInputChannels;
        else
                channelCount = atoi(line);

        inputParameters.device = inputDevice;
        inputParameters.channelCount = channelCount;
        inputParameters.sampleFormat = SAMPLETYPE;
        inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
        inputParameters.hostApiSpecificStreamInfo = NULL;

        inputBufsize = BUFFERSIZE * inputRate;
        inputBlocksize = blockSize * inputRate;

        paErr = Pa_OpenStream(
                &inputStream,
                &inputParameters,
                NULL,
                inputRate,
                inputBlocksize,
                paNoFlag,
                input_callback,
                &inputData );
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot open input stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto cleanup1;
        }

        printf("Opened input stream with %d channels at %.0f Hz.\n", channelCount, inputRate);
        Pa_SetStreamFinishedCallback(&inputStream, stream_finished);

        memset(outputStream, 0, STRLEN);
        sprintf(outputStream, LSLSTREAM);
        printf("LSL stream name [%s]: ", LSLSTREAM);
        fgets(line, STRLEN, stdin);
        if (strlen(line)>1)
                strncpy(outputStream, line, strlen(line)-1);

        printf("Output sampling rate [%.0f]: ", FSAMPLE);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                outputRate = FSAMPLE;
        else
                outputRate = atof(line);

        outputBufsize = BUFFERSIZE * outputRate;

        /* STAGE 2: Initialize the inputData and outputData for use by the callbacks. */

        inputData.frames = 0;
        inputData.data = NULL;
        if ((inputData.data = malloc(inputBufsize * channelCount * sizeof(float))) == NULL)
                goto cleanup2;
        else
                memset(inputData.data, 0, inputBufsize * channelCount * sizeof(float));

        outputData.frames = 0;
        outputData.data = NULL;
        if ((outputData.data = malloc(outputBufsize * channelCount * sizeof(float))) == NULL)
                goto cleanup2;
        else
                memset(outputData.data, 0, outputBufsize * channelCount * sizeof(float));

        /* STAGE 3: Initialize the resampling. */

        resampleRatio = outputRate / inputRate;
        printf("Resampling ratio = %f\n", resampleRatio);

        printf("Setting up %s rate converter with %s\n",
               src_get_name (SRC_SINC_MEDIUM_QUALITY),
               src_get_description (SRC_SINC_MEDIUM_QUALITY));

        resampleState = src_new (SRC_SINC_MEDIUM_QUALITY, channelCount, &srcErr);
        if (resampleState == NULL)
        {
                printf("ERROR: Cannot set up resample state.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto cleanup3;
        }

        srcErr = src_set_ratio (resampleState, resampleRatio);
        if (srcErr)
        {
                printf("ERROR: Cannot set resampling ratio.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto cleanup3;
        }

        /* initialize the LSL stream */
        rand_str(outputUID, 8);
        lsl_streaminfo info = lsl_create_streaminfo(outputStream, LSLTYPE, channelCount, outputRate, cft_float32, outputUID);
        printf("Opened LSL stream.\n");
        printf("LSL name = %s\n", outputStream);
        printf("LSL type = %s\n", LSLTYPE);
        printf("LSL uid = %s\n", outputUID);

        outlet = lsl_create_outlet(info, 0, LSLBUFFER);

        /* STAGE 4: Start the streams. */

        paErr = Pa_StartStream( inputStream );
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot start input stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto cleanup3;
        }

        printf("Processing data...\n");

        while (keepRunning)
        {
                Pa_Sleep(1000);
                printf("inputCounter = %lu, ", inputCounter);
                printf("outputCounter = %lu", outputCounter);
                printf("\n");
        }

        paErr = Pa_StopStream( outputStream );
        if( paErr != paNoError ) goto cleanup3;
        paErr = Pa_CloseStream( outputStream );
        if( paErr != paNoError ) goto cleanup3;

cleanup3:
        lsl_destroy_outlet(outlet);

        if (resampleState)
                src_delete (resampleState);

cleanup2:
        if (inputData.data)
                free(inputData.data);
        if (outputData.data)
                free(outputData.data);

cleanup1:
        Pa_Terminate();

        if (srcErr)
                printf("Samplerate error number: %d\n", srcErr);
        if (paErr)
                printf("PortAudio error number: %d\n", paErr);

        printf("Finished.");
        return paErr;
}

/* Helper function to generate random UID string. */
void rand_str(char *dest, size_t length) {
        char charset[] = "0123456789"
                         "abcdefghijklmnopqrstuvwxyz";

        while (length-- > 0) {
                size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
                *dest++ = charset[index];
        }
        *dest = '\0';
}
