/*

   Copyright (C) 2022, Robert Oostenveld

   This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define STRLEN 80
#define smooth(old, new, lambda) ((1.0-lambda)*(old) + (lambda)*(new))

#define SAMPLETYPE    paFloat32
#define BLOCKSIZE     (0.01)  // in seconds
#define BUFFER        (2.00)  // in seconds
#define DEFAULTRATE   (44100.0)
#define TIMEOUT       (3.0)   // for LSL
#define STREAMCOUNT   (32)    //maximum number of LSL streams

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
float outputLimit = 1.;

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
        float estimate = nominal + (0.5*outputBufsize - outputData.frames) / outputBlocksize;

        /* do not change the ratio by too much */
        estimate = min(estimate, 1.2*nominal);
        estimate = max(estimate, 0.8*nominal);

        /* allow some variation of the target buffer size */
        /* it should fall between the lower and upper range */
        float verylow   = (0.40*outputBufsize);
        float low       = (0.48*outputBufsize);
        float high      = (0.52*outputBufsize);
        float veryhigh  = (0.60*outputBufsize);

        /* this is called every 0.01 seconds, hence lambda=1.0*BLOCKSIZE implements a 1 second smoothing
           and 10*BLOCKSIZE implements a 0.1 second smoothing */
        if (outputData.frames<verylow)
                resampleRatio = smooth(resampleRatio, estimate, 10. * BLOCKSIZE);
        else if (outputData.frames<low)
                resampleRatio = smooth(resampleRatio, estimate, 1. * BLOCKSIZE);
        else if (outputData.frames>high)
                resampleRatio = smooth(resampleRatio, estimate, 1. * BLOCKSIZE);
        else if (outputData.frames>veryhigh)
                resampleRatio = smooth(resampleRatio, estimate, 10. * BLOCKSIZE);
        else
                resampleRatio = smooth(resampleRatio, nominal, 10. * BLOCKSIZE);

        //printf("%lu\t%f\t%f\t%f\n", outputData.frames, nominal, estimate, resampleRatio);

        return 0;
}

/*******************************************************************************************************/
static int output_callback(const void *input,
                           void *output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData)
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

        for (unsigned int i = 0; i < (newFrames * channelCount); i++)
          outputLimit = max(outputLimit, fabsf(data[i]));
            
        if (enableResample)
                resample_buffers();

        if (enableUpdate)
                update_ratio();

        return paContinue;
}

/*******************************************************************************************************/
void stream_finished(void *userData)
{
        keepRunning = 0;
        return;
}

/*******************************************************************************************************/
int main(int argc, char* argv[]) {
        char line[STRLEN];

        /* variables that are specific for PortAudio */
        unsigned int outputDevice;
        PaStream *outputStream;
        PaStreamParameters outputParameters;
        PaError paErr = paNoError;
        unsigned int numDevices;
        const PaDeviceInfo *deviceInfo;

        /* variables that are specific for LSL */
        unsigned int inputStream;
        lsl_streaminfo info[STREAMCOUNT];
        lsl_inlet inlet;
        int lslErr = 0;
        float *eegdata = NULL;
        double timestamp, timestampPrev, timestampPerSample;
        unsigned long samplesReceived = 0;
        const char *type, *name;
        int streamCount = 0;

        /* STAGE 1: Initialize the EEG input and audio output. */

        printf("LSL version: %s\n", lsl_library_info());
        printf("Looking for LSL streams...\n");

        streamCount = lsl_resolve_all(info, STREAMCOUNT, TIMEOUT);
        printf("Number of LSL streams = %d\n", streamCount);
        
        for (int i=0; i<streamCount; i++)
        {
            printf("stream %d - ", i);
            printf("type = %s, ", lsl_get_type(info[i]));
            printf("name = %s, ", lsl_get_name(info[i]));
            printf("channelCount = %d, ", lsl_get_channel_count(info[i]));
            printf("inputRate = %.4f\n", lsl_get_nominal_srate(info[i]));
        }
        printf("Select input stream [%d]: ", 0);
        fgets(line, STRLEN, stdin);
        if (strlen(line)==1)
                inputStream = 0;
        else
                inputStream = atoi(line);

        /* continute with the selected stream */
        type = lsl_get_type(info[inputStream]);
        name = lsl_get_name(info[inputStream]);

        /* the channelCount and inputRate are global variables */
        channelCount = lsl_get_channel_count(info[inputStream]);
        inputRate = lsl_get_nominal_srate(info[inputStream]);

        printf("type = %s\n", type);
        printf("name = %s\n", name);
        printf("channelCount = %d\n", channelCount);
        printf("inputRate = %f\n", inputRate);

        inputBufsize = BUFFER * inputRate;
        inputBlocksize = 1;

        eegdata = malloc(channelCount * sizeof(float));
        if (eegdata == NULL)
        {
                printf("ERROR: Cannot allocate memory.");
                goto error1;
        }

        printf("PortAudio version: 0x%08X\n", Pa_GetVersion());

        outputParameters.device = paNoDevice;

        /* Initialize library before making any other calls. */
        paErr = Pa_Initialize();
        if(paErr != paNoError)
        {
                printf("ERROR: Cannot initialize PortAudio.\n");
                printf("ERROR: %s\n", Pa_GetErrorText(paErr));
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
        for (int i = 0; i < numDevices; i++)
        {
            deviceInfo = Pa_GetDeviceInfo(i);
            if (Pa_GetHostApiCount() == 1)
                printf("device %2d - %s (%d in, %d out)\n", i,
                    deviceInfo->name,
                    deviceInfo->maxInputChannels,
                    deviceInfo->maxOutputChannels);
            else
                printf("device %2d - %s - %s (%d in, %d out)\n", i,
                    Pa_GetHostApiInfo(deviceInfo->hostApi)->name,
                    deviceInfo->name,
                    deviceInfo->maxInputChannels,
                    deviceInfo->maxOutputChannels);
        }
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

        deviceInfo = Pa_GetDeviceInfo(outputDevice);
        printf("Number of channels [%d]: ", min(channelCount, deviceInfo->maxOutputChannels));
        fgets(line, STRLEN, stdin);
        if (strlen(line) == 1)
            channelCount = min(channelCount, deviceInfo->maxOutputChannels);
        else
            channelCount = min(channelCount, atoi(line));

        printf("outputDevice = %d\n", outputDevice);
        printf("outputRate = %f\n", outputRate);
        printf("channelCount = %d\n", channelCount);

        outputParameters.device = outputDevice;
        outputParameters.channelCount = channelCount;
        outputParameters.sampleFormat = SAMPLETYPE;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;

        outputBufsize = BUFFER * outputRate;
        outputBlocksize = BLOCKSIZE * outputRate;

        paErr = Pa_OpenStream(
                &outputStream,
                NULL,
                &outputParameters,
                outputRate,
                outputBlocksize,
                paNoFlag,
                output_callback,
                &outputData);
        if(paErr != paNoError)
        {
                printf("ERROR: Cannot open output stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText(paErr));
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

        /* STAGE 4: Start the streams. */

        paErr = Pa_StartStream(outputStream);
        if(paErr != paNoError)
        {
                printf("ERROR: Cannot start output stream.\n");
                printf("ERROR: %s\n", Pa_GetErrorText(paErr));
                goto error3;
        }

        inlet = lsl_create_inlet(info[inputStream], 30, LSL_NO_PREFERENCE, 1);
        lsl_open_stream(inlet, TIMEOUT, &lslErr);
        if (lslErr != 0)
        {
                printf("ERROR: Cannot open input stream\n");
                //printf("ERROR: %s\n", lsl_last_error());
                goto error4;
        }

        printf("Filling buffer...\n");
        timestampPrev = lsl_pull_sample_f(inlet, eegdata, lsl_get_channel_count(info[inputStream]), TIMEOUT, &lslErr);
        if (timestamp == 0)
        {
                printf("ERROR: Cannot pull sample.\n");
                //printf("ERROR: %s\n", lsl_last_error());
                goto error4;
        }

        /* wait one second to fill the input buffer halfway */
        while (samplesReceived<inputBufsize/2)
        {
                timestamp = lsl_pull_sample_f(inlet, eegdata, lsl_get_channel_count(info[inputStream]), TIMEOUT, &lslErr);
                if (timestamp == 0)
                {
                        printf("ERROR: Cannot pull sample.\n");
                        //printf("ERROR: %s\n", lsl_last_error());
                        goto error4;
                }
                samplesReceived++;

                /* add the current sample to the input buffer and increment the counter */
                for (unsigned int i = 0; i < channelCount; i++)
                {
                    outputLimit = max(outputLimit, fabsf(eegdata[i]));
                    inputData.data[inputData.frames * channelCount + i] = eegdata[i] / outputLimit;
                }
                inputData.frames++;
        }

        printf("Nominal inputRate = %f\n", inputRate);

        /* estimate the input sample rate */
        timestampPerSample = (timestamp - timestampPrev)/samplesReceived;
        inputRate = 1.0/timestampPerSample;
        printf("Estimated inputRate = %f\n", inputRate);
        timestampPrev = timestamp;

        resampleRatio = outputRate / inputRate;
        printf("Initial resampleRatio = %f\n", resampleRatio);

        srcErr = src_set_ratio (resampleState, resampleRatio);
        if (srcErr)
        {
                printf("ERROR: Cannot set resampling ratio.\n");
                printf("ERROR: %s\n", src_strerror(srcErr));
                goto error4;
        }

        printf("Processing data...\n");

        enableResample = 1;
        enableUpdate = 1;

        while (1)
        {
                timestamp = lsl_pull_sample_f(inlet, eegdata, lsl_get_channel_count(info[inputStream]), TIMEOUT, &lslErr);
                if (timestamp == 0)
                {
                        printf("ERROR: Cannot pull sample.\n");
                        //printf("ERROR: %s\n", lsl_last_error());
                        goto error4;
                }
                samplesReceived++;

                /* update the estimated input sample rate, smooth over 100 seconds */
                timestampPerSample = smooth(timestampPerSample, timestamp - timestampPrev, 0.01/lsl_get_nominal_srate(info[inputStream]));
                timestampPrev = timestamp;
                inputRate = 1.0/timestampPerSample;

                if (inputData.frames == inputBufsize) {
                        /* input buffer overrun, drop the oldest sample */
                        size_t len = (inputData.frames - 1) * channelCount * sizeof(float);
                        memcpy(inputData.data, inputData.data + 1 * channelCount, len);
                        inputData.frames--;
                }

                /* add the current sample to the input buffer and increment the counter */
                for (unsigned int i = 0; i < channelCount; i++)
                {
                    outputLimit = max(outputLimit, fabsf(eegdata[i]));
                    inputData.data[inputData.frames * channelCount + i] = eegdata[i] / outputLimit;
                }
                inputData.frames++;

                if ((samplesReceived % (unsigned long)lsl_get_nominal_srate(info[inputStream])) == 0)
                {
                        printf("inputRate = %8.4f, ", inputRate);
                        printf("resampleRatio = %8.4f, ", resampleRatio);
                        printf("outputLimit = %8.4f, ", outputLimit);
                        printf("inputData = %4lu, ", inputData.frames);
                        printf("outputData = %6lu", outputData.frames);
                        printf("\n");
                }
        }

error4:
        lsl_destroy_inlet(inlet); \

error3:
        if (resampleState)
                src_delete (resampleState);

error2:
        if (eegdata)
                free(eegdata);

error1:
        Pa_Terminate();

        if (srcErr)
                printf("Samplerate error number: %d\n", srcErr);
        if (paErr)
                printf("PortAudio error number: %d\n", paErr);

error0:
        printf("Finished.");
        return lslErr;
}
