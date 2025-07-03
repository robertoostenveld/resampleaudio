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

#define STRLEN 80
#define smooth(old, new, lambda) ((1.0-lambda)*(old) + (lambda)*(new))

#define SAMPLETYPE          paFloat32
#define BLOCKSIZE           (0.01) // in seconds
#define BUFFERSIZE          (2.00) // in seconds
#define DEFAULTRATE         (44100.0)

typedef struct {
        float *data;
        unsigned long frames;
} dataBuffer_t;

dataBuffer_t inputData, outputData;

SRC_STATE* resampleState = NULL;
SRC_DATA resampleData;
int srcErr;

float inputRate, outputRate, resampleRatio;
short enableResample = 0, enableUpdate = 0, keepRunning = 1;
int channelCount, inputBlocksize, outputBlocksize, inputBufsize, outputBufsize;

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

        /* the output data buffer increased */
        outputData.frames += resampleData.output_frames_gen;

        /* the input data buffer decreased */
        size_t len = (inputData.frames - resampleData.input_frames_used) * channelCount * sizeof(float);
        memcpy(inputData.data, inputData.data + resampleData.input_frames_used * channelCount, len);
        inputData.frames -= resampleData.input_frames_used;

        return 0;
}

/*******************************************************************************************************/
int update_ratio(void)
{
        float nominal = (float)outputRate/inputRate;
        float estimate = nominal + (0.5*outputBufsize - outputData.frames) / inputBlocksize;

        /* do not change the ratio by too much */
        estimate = min(estimate, 1.1*nominal);
        estimate = max(estimate, 0.9*nominal);

        /* allow some variation of the target buffer size */
        /* it should fall between the lower and upper range */
        float lower = (0.49*outputBufsize);
        float upper = (0.51*outputBufsize);

        if (outputData.frames<lower || outputData.frames>upper)
                /* increase or decrease the ratio to the value that appears to be needed */
                resampleRatio = smooth(resampleRatio, estimate, 0.001);
        else
                /* change the ratio towards the nominal value */
                resampleRatio = smooth(resampleRatio, nominal, 0.1);

        //printf("%lu\t%f\t%f\t%f\n", outputData.frames, nominal, estimate, resampleRatio);

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

        if (enableResample)
                resample_buffers();
        if (enableUpdate)
                update_ratio();

        return paContinue;
}

/*******************************************************************************************************/
static int output_callback( const void *input,
                            void *output,
                            unsigned long frameCount,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData )
{
        float *data = (float *)output;
        dataBuffer_t *outputData = (dataBuffer_t *)userData;
        unsigned int newFrames = min(frameCount, outputData->frames);

        size_t len = newFrames * channelCount * sizeof(float);
        memcpy(data, outputData->data, len);

        len = (frameCount - newFrames) * channelCount * sizeof(float);
        memset(data + newFrames * channelCount, 0, len);

        len = (outputData->frames - newFrames) * channelCount * sizeof(float);
        memcpy(outputData->data, outputData->data + newFrames * channelCount, len);

        outputData->frames -= newFrames;

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
        float bufferSize, blockSize;

        int inputDevice, outputDevice;
        PaStream *inputStream, *outputStream;
        PaStreamParameters inputParameters, outputParameters;
        PaError paErr = paNoError;
        int numDevices;
        const PaDeviceInfo *deviceInfo;

        /* STAGE 1: Initialize the audio input and output. */

        printf("PortAudio version: 0x%08X\n", Pa_GetVersion());

        printf("Buffer size in seconds [%.4f]: ", BUFFERSIZE);
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
            bufferSize = BUFFERSIZE;
        else
            bufferSize = atof(line);

        printf("Block size in seconds [%.4f]: ", BLOCKSIZE);
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
            blockSize = BLOCKSIZE;
        else
            blockSize = atof(line);

        inputParameters.device = paNoDevice;
        outputParameters.device = paNoDevice;

        /* Initialize library before making any other calls. */
        paErr = Pa_Initialize();
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot initialize PortAudio.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto error1;
        }

        numDevices = Pa_GetDeviceCount();
        if (numDevices <= 0)
        {
                printf("ERROR: No audio devices available.\n");
                paErr = numDevices;
                goto error1;
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

        inputBufsize = bufferSize * inputRate;
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
                goto error1;
        }

        printf("Opened input stream with %d channels at %.0f Hz.\n", channelCount, inputRate);
        Pa_SetStreamFinishedCallback(&inputStream, stream_finished);

        printf("Select output device [%d]: ", Pa_GetDefaultOutputDevice());
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                outputDevice = Pa_GetDefaultOutputDevice();
        else
                outputDevice = atoi(line);

        printf("Output sampling rate [%.0f]: ", DEFAULTRATE);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                outputRate = DEFAULTRATE;
        else
                outputRate = atof(line);

        outputParameters.device = outputDevice;
        outputParameters.channelCount = channelCount;
        outputParameters.sampleFormat = SAMPLETYPE;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;

        outputBufsize = bufferSize * outputRate;
        outputBlocksize = blockSize * outputRate;

        paErr = Pa_OpenStream(
                &outputStream,
                NULL,
                &outputParameters,
                outputRate,
                outputBlocksize,
                paNoFlag,
                output_callback,
                &outputData );
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot open output stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto error1;
        }

        printf("Opened output stream with %d channels at %.0f Hz.\n", channelCount, outputRate);
        Pa_SetStreamFinishedCallback(&outputStream, stream_finished);

        /* STAGE 2: Initialize the inputData and outputData for use by the callbacks. */

        inputData.frames = 0;
        inputData.data = NULL;
        if ((inputData.data = malloc(inputBufsize * channelCount * sizeof(float))) == NULL)
                goto error2;
        else
                memset(inputData.data, 0, inputBufsize * channelCount * sizeof(float));

        outputData.frames = 0;
        outputData.data = NULL;
        if ((outputData.data = malloc(outputBufsize * channelCount * sizeof(float))) == NULL)
                goto error2;
        else
                memset(outputData.data, 0, outputBufsize * channelCount * sizeof(float));

        /* STAGE 3: Initialize the resampling. */

        resampleRatio = outputRate / inputRate;
        printf("Nominal resampleRatio = %f\n", resampleRatio);

        printf("Setting up %s rate converter with %s\n",
               src_get_name (SRC_SINC_MEDIUM_QUALITY),
               src_get_description (SRC_SINC_MEDIUM_QUALITY));

        resampleState = src_new (SRC_SINC_MEDIUM_QUALITY, channelCount, &srcErr);
        if (resampleState == NULL)
        {
                printf("ERROR: Cannot set up resample state.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto error3;
        }

        srcErr = src_set_ratio (resampleState, resampleRatio);
        if (srcErr)
        {
                printf("ERROR: Cannot set resampling ratio.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto error3;
        }

        /* STAGE 4: Start the streams. */

        paErr = Pa_StartStream( outputStream );
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot start output stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto error3;
        }

        paErr = Pa_StartStream( inputStream );
        if( paErr != paNoError )
        {
                printf("ERROR: Cannot start input stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText( paErr ) );
                goto error3;
        }

        printf("Filling buffer...\n");

        /* Wait one second to fill the input buffer halfway */
        Pa_Sleep(1000);
        enableResample = 1;

        /* Wait one second to enable the updating of the resampling ratio */
        Pa_Sleep(1000);
        enableUpdate = 1;

        printf("Processing data...\n");

        while (keepRunning)
        {
                Pa_Sleep(1000);
                printf("inputRate = %8.4f, ", inputRate);
                printf("resampleRatio = %8.4f, ", resampleRatio);
                printf("inputData = %4lu, ", inputData.frames);
                printf("outputData = %6lu", outputData.frames);
                printf("\n");
        }

        paErr = Pa_StopStream( outputStream );
        if( paErr != paNoError ) goto error3;
        paErr = Pa_CloseStream( outputStream );
        if( paErr != paNoError ) goto error3;

error3:
        if (resampleState)
                src_delete (resampleState);

error2:
        if (inputData.data)
                free(inputData.data);
        if (outputData.data)
                free(outputData.data);

error1:
        Pa_Terminate();

        if (srcErr)
                printf("Samplerate error number: %d\n", srcErr);
        if (paErr)
                printf("PortAudio error number: %d\n", paErr);

        printf("Finished.");
        return paErr;
}
