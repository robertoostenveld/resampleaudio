#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "portaudio.h"
#include "samplerate.h"

#define smooth(old, new, lambda) ((1.0-lambda)*old + lambda*new)
#define min(x, y) (x<y ? x : y)
#define max(x, y) (x>y ? x : y)

#define SAMPLE_TYPE               paFloat32
#define CHANNEL_COUNT             (1)
#define BLOCKSIZE                 (0.01) // in seconds
#define BUFFER                    (2.00) // in seconds

typedef struct {
				float *data;
				unsigned long frames;
} dataBuffer_t;

dataBuffer_t inputData, outputData;

SRC_STATE* state = NULL;
SRC_DATA data;

short enable_resample = 0, enable_update = 0;
float input_rate, output_rate;
int input_blocksize, output_blocksize, input_buffer, output_buffer;
float ratio;

/*******************************************************************************************************/
int resample_buffers(void)
{
				data.src_ratio      = ratio;
				data.end_of_input   = 0;
				data.data_in        = inputData.data;
				data.input_frames   = inputData.frames;
				data.data_out       = outputData.data + outputData.frames * CHANNEL_COUNT * sizeof(float);
				data.output_frames  = output_buffer - outputData.frames;

				int err = src_process (state, &data);
				if (err)
				{
								printf("ERROR: src_process returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								return err;
				}

				/* the output data buffer increased */
				outputData.frames += data.output_frames_gen;

				/* the input data buffer decreased */
				size_t used = data.input_frames_used * CHANNEL_COUNT * sizeof(float);
				size_t remaining = (inputData.frames - data.input_frames_used) * CHANNEL_COUNT * sizeof(float);
				memcpy(inputData.data, inputData.data + used, remaining);
				inputData.frames -= data.input_frames_used;
				return 0;
}

/*******************************************************************************************************/
int update_ratio(void)
{
				float nominal = (float)output_rate/input_rate;
				float estimate = nominal + (0.5*output_buffer - outputData.frames) / input_blocksize;

				/* do not change the ratio by too much */
				estimate = min(estimate, 1.1*nominal);
				estimate = max(estimate, 0.9*nominal);

				/* allow some variation around the target buffer size */
				float lower = (0.49*output_buffer);
				float upper = (0.51*output_buffer);

				if (outputData.frames<lower || outputData.frames>upper)
								/* increase or decrease the ratio to the value that appears to be needed */
								ratio = smooth(ratio, estimate, 0.001);
				else
								/* change the ratio towards the nominal value */
								ratio = smooth(ratio, nominal, 0.1);

				printf("%0.f\t%lu\t%0.f\t%f\t%f\n", lower, outputData.frames, upper, estimate, ratio);

				return 0;
}
/*******************************************************************************************************/
static int inputCallback( const void *input,
                          void *output,
                          unsigned long frameCount,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
				float *dat = (float *)input;
				dataBuffer_t *inputData = (dataBuffer_t *)userData;

				for (int i=0; i<frameCount; i++) {
								if (inputData->frames<input_buffer)
								{
												for (int j=0; j<CHANNEL_COUNT; j++)
																inputData->data[inputData->frames+j] = dat[i*CHANNEL_COUNT+j];
												inputData->frames++;
								}
				}

				if (enable_resample)
								resample_buffers();
				if (enable_update)
								update_ratio();

				return 0;
}

/*******************************************************************************************************/
static int outputCallback( const void *input,
                           void *output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
				float *dat = (float *)output;
				dataBuffer_t *outputData = (dataBuffer_t *)userData;

				if (outputData->frames == 0)
				{
								/* there is no data */
								bzero(dat, frameCount*CHANNEL_COUNT*sizeof(float));
				}
				else if (outputData->frames > frameCount)
				{
								/* there is enough data */
								size_t len = frameCount*CHANNEL_COUNT*sizeof(float);
								memcpy(dat, outputData->data, len);
								memmove(outputData->data, outputData->data+len, len);
								outputData->frames -= frameCount;
				}
				else
				{
								/* there is some data, but not enough */
								size_t len = outputData->frames*CHANNEL_COUNT*sizeof(float);
								memcpy(dat, outputData->data, len);
								bzero(dat+len, frameCount*CHANNEL_COUNT*sizeof(float) - len);
								outputData->frames = 0;
				}

				return 0;
}

/*******************************************************************************************************/
int main(int argc, char *argv[]) {
				char *line = NULL;
				size_t linecap = 0;

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
				printf("Version text: '%s'\n", Pa_GetVersionInfo()->versionText );

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
				getline(&line, &linecap, stdin);

				inputParameters.device = atoi(line);
				inputParameters.channelCount = CHANNEL_COUNT;
				inputParameters.sampleFormat = SAMPLE_TYPE;
				inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
				inputParameters.hostApiSpecificStreamInfo = NULL;

				printf("Give input sampling rate: ");
				getline(&line, &linecap, stdin);
				input_rate = atoi(line);
				input_buffer = BUFFER * input_rate;
				input_blocksize = BLOCKSIZE * input_rate;

				err = Pa_OpenStream(
								&inputStream,
								&inputParameters,
								NULL,
								input_rate,
								input_blocksize,
								0,
								inputCallback,
								&inputData );
				if( err != paNoError )
				{
								printf("ERROR: Cannot open input stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error1;
				}
				else {
								printf("Opened input stream.\n");
				}

				printf("Select output device: ");
				getline(&line, &linecap, stdin);

				outputParameters.device = atol(line);
				outputParameters.channelCount = CHANNEL_COUNT;
				outputParameters.sampleFormat = SAMPLE_TYPE;
				outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
				outputParameters.hostApiSpecificStreamInfo = NULL;

				printf("Give output sampling rate: ");
				getline(&line, &linecap, stdin);
				output_rate = atoi(line);
				output_buffer = BUFFER * output_rate;
				output_blocksize = BLOCKSIZE * output_rate;

				err = Pa_OpenStream(
								&outputStream,
								NULL,
								&outputParameters,
								output_rate,
								output_blocksize,
								paClipOff,
								outputCallback,
								&outputData );
				if( err != paNoError )
				{
								printf("ERROR: Cannot open output stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error1;
				}
				else {
								printf("Opened output stream.\n");
				}

				/* STAGE 2: Initialize the inputData and outputData for use by the callbacks. */

				inputData.frames = 0;
				inputData.data = NULL;
				if ((inputData.data = malloc(input_buffer*CHANNEL_COUNT*sizeof(float))) == NULL)
								goto error2;
				else
								bzero(inputData.data, input_buffer*CHANNEL_COUNT*sizeof(float));

				outputData.frames = 0;
				outputData.data = NULL;
				if ((outputData.data = malloc(output_buffer*CHANNEL_COUNT*sizeof(float))) == NULL)
								goto error2;
				else
								bzero(outputData.data, output_buffer*CHANNEL_COUNT*sizeof(float));

				/* STAGE 3: Initialize the resampling. */

				ratio = output_rate / input_rate;
				printf("Initial ratio = %f\n", ratio);

				printf("Setting up %s rate converter with %s\n",
				       src_get_name (SRC_SINC_MEDIUM_QUALITY),
				       src_get_description (SRC_SINC_MEDIUM_QUALITY));

				state = src_new (SRC_SINC_MEDIUM_QUALITY, CHANNEL_COUNT, &err);
				if (state == NULL)
				{
								printf("ERROR: src_new returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								goto error3;
				}

				err = src_set_ratio (state, ratio);
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

				/* Wait one second to fill the input BUFFER to 50% */
				Pa_Sleep(1000);
				enable_resample = 1;

				/* Wait one second to enable the updating of the resampling ratio */
				Pa_Sleep(1000);
				enable_update = 1;

				while (1)
								Pa_Sleep(1000);

				printf("Running.\n");

				err = Pa_StopStream( outputStream );
				if( err != paNoError ) goto error3;
				err = Pa_CloseStream( outputStream );
				if( err != paNoError ) goto error3;

error3:
				if (state)
								src_delete (state);

error2:
				if (inputData.data)
								free(inputData.data);
				if (outputData.data)
								free(outputData.data);
				if (line)
								free(line);

error1:
				Pa_Terminate();

				if (err)
								printf("Error number: %d\n", err );
				else
								printf("Finished.");
				return err;
}
