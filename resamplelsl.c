/*

   Copyright (C) 2022, Robert Oostenveld

   This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined __linux__ || defined __APPLE__
//linux code goes here
#include <unistd.h>
#define min(x, y) (x<y ? x : y)
#define max(x, y) (x>y ? x : y)

#elif defined _WIN32
// windows code goes here
#define bzero(b,len) (memset((b), '\0', (len)), (void) 0)

#else
#error Platform not supported
#endif

#include "portaudio.h"
#include "samplerate.h"
#include "lsl_c.h"

#define STRLEN 80
#define smooth(old, new, lambda) ((1.0-lambda)*old + lambda*new)

#define SAMPLE_TYPE   paFloat32
#define BLOCKSIZE     (0.01) // in seconds
#define BUFFER        (2.00) // in seconds

typedef struct {
				float *data;
				unsigned long frames;
} dataBuffer_t;

dataBuffer_t inputData, outputData;

SRC_STATE* resampleState = NULL;
SRC_DATA resampleData;

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

				int paErr = src_process (resampleState, &resampleData);
				if (paErr)
				{
								printf("ERROR: src_process returned 0x%x\n", paErr );
								printf("ERROR message: %s\n", src_strerror(paErr));
								exit(paErr);
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

        if (outputData.frames<verylow)
                resampleRatio = smooth(resampleRatio, estimate, 0.1);
        else if (outputData.frames<low)
								resampleRatio = smooth(resampleRatio, estimate, 0.01);
        else if (outputData.frames>high)
              resampleRatio = smooth(resampleRatio, estimate, 0.01);
        else if (outputData.frames>veryhigh)
							resampleRatio = smooth(resampleRatio, estimate, 0.1);
				else
							resampleRatio = smooth(resampleRatio, nominal, 0.1);

				// printf("%lu\t%f\t%f\t%f\n", outputData.frames, nominal, estimate, resampleRatio);

				return 0;
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
				bzero(data + newFrames * channelCount, len);

				len = (outputData->frames - newFrames) * channelCount * sizeof(float);
				memcpy(outputData->data, outputData->data + newFrames * channelCount, len);

				outputData->frames -= newFrames;

				if (enableResample)
								resample_buffers();

        if (enableUpdate)
								update_ratio();

				return 0;
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
				lsl_streaminfo info;
				lsl_inlet inlet;
				int lslErr = 0;
				float *eegdata = NULL;
				double timestamp, timestamp0;
        unsigned long samplesReceived = 0;
        const char *type, *name;

				/* STAGE 1: Initialize the EEG input and audio output. */

				printf("LSL version: %s\n", lsl_library_info());

				printf("Waiting for an LSL stream of type EEG...\n");
				lsl_resolve_byprop(&info, 1, "type", "EEG", 1, LSL_FOREVER);

				type = lsl_get_type(info);
				name = lsl_get_name(info);
        /* the channelCount and inputRate are global variables */
				channelCount = lsl_get_channel_count(info);
				inputRate = lsl_get_nominal_srate(info);

				printf("type         = %s\n", type);
				printf("name         = %s\n", name);
				printf("channelCount = %d\n", channelCount);
				printf("inputRate    = %f\n", inputRate);

				inputBufsize = BUFFER * inputRate;
				inputBlocksize = 1;

				eegdata = malloc(channelCount * sizeof(float));
				if (eegdata == NULL)
				{
								printf("ERROR: malloc()");
								goto error1;
				}

				outputParameters.device = paNoDevice;

				paErr = Pa_Initialize();
				if( paErr != paNoError )
				{
								printf("ERROR: Pa_Initialize returned 0x%x\n", paErr );
								printf("ERROR message: %s\n", Pa_GetErrorText( paErr ) );
								goto error1;
				}

				printf("PortAudio version: 0x%08X\n", Pa_GetVersion());

				/* Initialize library before making any other calls. */
				paErr = Pa_Initialize();
				if( paErr != paNoError )
				{
								printf("ERROR: Cannot initialize PortAudio.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( paErr ) );
								goto error1;
				}

				numDevices = Pa_GetDeviceCount();
				if( numDevices < 0 )
				{
								printf("ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
								printf("ERROR message: %s\n", Pa_GetErrorText( paErr ) );
								paErr = numDevices;
								goto error1;
				}

				printf("Number of devices = %d\n", numDevices );
				for( int i=0; i<numDevices; i++ )
				{
								deviceInfo = Pa_GetDeviceInfo( i );
								printf("device %d %s (%d in, %d out)\n", i,
								       deviceInfo->name,
								       deviceInfo->maxInputChannels,
								       deviceInfo->maxOutputChannels  );
				}

				printf("Select output device: ");
				fgets(line, STRLEN, stdin);
				outputDevice = atoi(line);

				printf("Output sampling rate: ");
				fgets(line, STRLEN, stdin);
				outputRate = atof(line);

				outputParameters.device = outputDevice;
				outputParameters.channelCount = channelCount;
				outputParameters.sampleFormat = SAMPLE_TYPE;
				outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
				outputParameters.hostApiSpecificStreamInfo = NULL;

				outputBufsize = BUFFER * outputRate;
				outputBlocksize = BLOCKSIZE * outputRate;

				paErr = Pa_OpenStream(
								&outputStream,
								NULL,
								&outputParameters,
								outputRate,
								outputBlocksize,
								paClipOff,
								output_callback,
								&outputData );
				if( paErr != paNoError )
				{
								printf("ERROR: Cannot open output stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( paErr ) );
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
								bzero(inputData.data, inputBufsize * channelCount * sizeof(float));

				outputData.frames = 0;
				outputData.data = NULL;
				if ((outputData.data = malloc(outputBufsize * channelCount * sizeof(float))) == NULL)
								goto error2;
				else
								bzero(outputData.data, outputBufsize * channelCount * sizeof(float));

				/* STAGE 3: Initialize the resampling. */

				printf("Setting up %s rate converter with %s\n",
				       src_get_name (SRC_SINC_MEDIUM_QUALITY),
				       src_get_description (SRC_SINC_MEDIUM_QUALITY));

				resampleState = src_new (SRC_SINC_MEDIUM_QUALITY, channelCount, &paErr);
				if (resampleState == NULL)
				{
								printf("ERROR: src_new returned 0x%x\n", paErr );
								printf("ERROR message: %s\n", src_strerror(paErr));
								goto error3;
				}

				/* STAGE 4: Start the streams. */

				paErr = Pa_StartStream( outputStream );
				if( paErr != paNoError )
				{
								printf("ERROR: Cannot start output stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( paErr ) );
								goto error3;
				}

				inlet = lsl_create_inlet(info, 30, LSL_NO_PREFERENCE, 1);
				lsl_open_stream(inlet, LSL_FOREVER, &lslErr);
				if (lslErr != 0)
				{
								printf("%s\n", lsl_last_error());
								goto error2;
				}

        printf("Filling buffer...\n");
        timestamp0 = lsl_pull_sample_f(inlet, eegdata, channelCount, LSL_FOREVER, &lslErr);

        /* fill the input buffer halfway */
        while (samplesReceived<inputBufsize/2)
        {
          timestamp = lsl_pull_sample_f(inlet, eegdata, channelCount, LSL_FOREVER, &lslErr);
          if (lslErr != 0)
          {
                  printf("%s\n", lsl_last_error());
                  goto error3;
          }
          samplesReceived++;

          /* add the data to the input buffer and increment the frame counter */
          size_t len = channelCount * sizeof(float);
          memcpy(inputData.data + inputData.frames * channelCount, eegdata, len);
          inputData.frames++;
        }

        /* estimate the input sample rate */
        inputRate = (float)samplesReceived/(timestamp - timestamp0);
        printf("Estimated inputRate = %f\n", inputRate);
        timestamp0 = timestamp;
        
        resampleRatio = outputRate / inputRate;
        printf("Initial resampleRatio = %f\n", resampleRatio);

        paErr = src_set_ratio (resampleState, resampleRatio);
        if (paErr)
        {
                printf("ERROR: src_set_ratio returned 0x%x\n", paErr );
                printf("ERROR message: %s\n", src_strerror(paErr));
                goto error3;
        }

        printf("Receiving data...\n");

        enableResample = 1;
        enableUpdate = 1;
        
				while (1)
				{
								timestamp = lsl_pull_sample_f(inlet, eegdata, channelCount, LSL_FOREVER, &lslErr);
								if (lslErr != 0)
								{
												printf("%s\n", lsl_last_error());
												goto error3;
								}
                samplesReceived++;

								if (inputData.frames == inputBufsize) {
												/* input buffer overrun, drop the oldest sample */
												size_t len = (inputData.frames - 1) * channelCount * sizeof(float);
												memcpy(inputData.data, inputData.data + 1 * channelCount, len);
												inputData.frames--;
								}

								/* add the current sample to the input buffer and increment the counter */
								size_t len = channelCount * sizeof(float);
								memcpy(inputData.data + inputData.frames * channelCount, eegdata, len);
								inputData.frames++;
                
                /* update the estimated input sample rate, smooth over 1000 seconds */
                inputRate = smooth(inputRate, 1.0/(timestamp - timestamp0), 0.001/lsl_get_nominal_srate(info));
                timestamp0 = timestamp;

                if ((samplesReceived % (unsigned long)lsl_get_nominal_srate(info)) == 0)
                {
                    printf("inputRate = %8.2f\t", inputRate);
                    printf("resampleRatio = %8.2f\t", resampleRatio);
                    printf("inputData = %8lu\t", inputData.frames);
                    printf("outputData = %8lu\t", outputData.frames);
                    printf("\n");
                }
				}


error3:
				lsl_destroy_inlet(inlet);

error2:
				if (eegdata)
								free(eegdata);

error1:
				return lslErr;
}
