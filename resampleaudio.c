/*

Copyright (C) 2022, Robert Oostenveld

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

				int err = src_process (resampleState, &resampleData);
				if (err)
				{
								printf("ERROR: src_process returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								exit(err);
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

/*
        printf("%0.f\t%lu\t%0.f\t%f\t%f\n", lower, outputData.frames, upper, estimate, resampleRatio);
 */
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

				return 0;
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

				int inputDevice, outputDevice;
				PaStream *inputStream, *outputStream;
				PaStreamParameters inputParameters, outputParameters;
				PaError err = paNoError;
				int numDevices;
				const PaDeviceInfo *deviceInfo;

				/* STAGE 1: Initialize the audio input and output. */

				inputParameters.device = paNoDevice;
				outputParameters.device = paNoDevice;

				err = Pa_Initialize();
				if( err != paNoError )
				{
								printf("ERROR: Pa_Initialize returned 0x%x\n", err );
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error1;
				}

				printf("PortAudio version: 0x%08X\n", Pa_GetVersion());
//				printf("Version text: '%s'\n", Pa_GetVersionInfo()->versionText );

				/* Initialize library before making any other calls. */
				err = Pa_Initialize();
				if( err != paNoError )
				{
								printf("ERROR: Cannot initialize PortAudio.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error1;
				}

				numDevices = Pa_GetDeviceCount();
				if( numDevices < 0 )
				{
								printf("ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								err = numDevices;
								goto error1;
				}

				printf("Number of devices = %d\n", numDevices );
				for( int i=0; i<numDevices; i++ )
				{
								deviceInfo = Pa_GetDeviceInfo( i );
								// Pa_GetDefaultOutputDevice
								printf("device %d %s (%d in, %d out)\n", i,
								       deviceInfo->name,
								       deviceInfo->maxInputChannels,
								       deviceInfo->maxOutputChannels  );
				}

				printf("Select input device: ");
				fgets(line, STRLEN, stdin);
				inputDevice = atoi(line);

				printf("Input sampling rate: ");
				fgets(line, STRLEN, stdin);
				inputRate = atof(line);

				printf("Number of channels: ");
				fgets(line, STRLEN, stdin);
				channelCount = atoi(line);

				inputParameters.device = inputDevice;
				inputParameters.channelCount = channelCount;
				inputParameters.sampleFormat = SAMPLE_TYPE;
				inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
				inputParameters.hostApiSpecificStreamInfo = NULL;

				inputBufsize = BUFFER * inputRate;
				inputBlocksize = BLOCKSIZE * inputRate;

				err = Pa_OpenStream(
								&inputStream,
								&inputParameters,
								NULL,
								inputRate,
								inputBlocksize,
								0,
								input_callback,
								&inputData );
				if( err != paNoError )
				{
								printf("ERROR: Cannot open input stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error1;
				}

				printf("Opened input stream with %d channels at %.0f Hz.\n", channelCount, inputRate);
				Pa_SetStreamFinishedCallback(&inputStream, stream_finished);

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

				err = Pa_OpenStream(
								&outputStream,
								NULL,
								&outputParameters,
								outputRate,
								outputBlocksize,
								paClipOff,
								output_callback,
								&outputData );
				if( err != paNoError )
				{
								printf("ERROR: Cannot open output stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
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

				resampleRatio = outputRate / inputRate;
				printf("Initial resampleRatio = %f\n", resampleRatio);

				printf("Setting up %s rate converter with %s\n",
				       src_get_name (SRC_SINC_MEDIUM_QUALITY),
				       src_get_description (SRC_SINC_MEDIUM_QUALITY));

				resampleState = src_new (SRC_SINC_MEDIUM_QUALITY, channelCount, &err);
				if (resampleState == NULL)
				{
								printf("ERROR: src_new returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								goto error3;
				}

				err = src_set_ratio (resampleState, resampleRatio);
				if (err)
				{
								printf("ERROR: src_set_ratio returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								goto error3;
				}

				/* STAGE 4: Start the streams. */

				err = Pa_StartStream( outputStream );
				if( err != paNoError )
				{
								printf("ERROR: Cannot start output stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				err = Pa_StartStream( inputStream );
				if( err != paNoError )
				{
								printf("ERROR: Cannot start input stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				printf("Filling buffer.\n");

				/* Wait one second to fill the input buffer halfway */
				Pa_Sleep(1000);
				enableResample = 1;

				/* Wait one second to enable the updating of the resampling ratio */
				Pa_Sleep(1000);
				enableUpdate = 1;

				printf("Running.\n");

				while (keepRunning)
								Pa_Sleep(1000);

				err = Pa_StopStream( outputStream );
				if( err != paNoError ) goto error3;
				err = Pa_CloseStream( outputStream );
				if( err != paNoError ) goto error3;

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

				if (err)
								printf("Error number: %d\n", err );
				else
								printf("Finished.");
				return err;
}
