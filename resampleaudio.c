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
#define INPUT_RATE                (8000)
#define OUTPUT_RATE               (48000)
#define BLOCKSIZE                 (0.01)  // in seconds
#define INPUT_BLOCKSIZE           (BLOCKSIZE*INPUT_RATE)
#define OUTPUT_BLOCKSIZE          (BLOCKSIZE*OUTPUT_RATE)
#define BUFFER                    (2.0)   // in seconds
#define INPUT_BUFFER              (BUFFER*INPUT_RATE)
#define OUTPUT_BUFFER             (BUFFER*OUTPUT_RATE)

typedef struct {
				float *data;
				unsigned long frames;
} dataBuffer_t;

dataBuffer_t inputData, outputData;

SRC_STATE* state = NULL;
SRC_DATA data;

short enable_resample = 0, enable_update = 0;
float input_rate = 8000, output_rate = 48000;
float blocksize = 0.01, buffer = 2.0; // in seconds
int input_buffer = 800, output_buffer = 4800;
float ratio = 6;

int resample_buffers(void)
{
				data.src_ratio      = ratio;
				data.end_of_input   = 0;
				data.data_in        = inputData.data;
				data.input_frames   = inputData.frames;
				data.data_out       = outputData.data + outputData.frames * CHANNEL_COUNT * sizeof(float);
				data.output_frames  = OUTPUT_BUFFER - outputData.frames;

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
        float nominal = (float)OUTPUT_RATE/INPUT_RATE;
        float current = nominal + (0.5*OUTPUT_BUFFER - outputData.frames) / INPUT_BLOCKSIZE;

        /* do not change the ratio by too much */
        current = min(current, 1.1*nominal);
        current = max(current, 0.9*nominal);

        /* allow some variation around the target buffer size */
        float lower = (0.49*OUTPUT_BUFFER);
        float upper = (0.51*OUTPUT_BUFFER);

        if (outputData.frames<lower || outputData.frames>upper)
                /* increase or decrease the ratio to the value that appears to be needed */
                ratio = smooth(ratio, current, 0.001);
        else
                /* change the ratio towards the nominal value */
                ratio = smooth(ratio, nominal, 0.1);
        
        printf("%0.f\t%lu\t%0.f\t%f\t%f\n", lower, outputData.frames, upper, current, ratio);
        
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
								if (inputData->frames<INPUT_BUFFER)
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
				int numDevices, defaultDisplayed;
				const PaDeviceInfo *deviceInfo;

				/* STAGE 1: Initialize the inputData and outputData for use by the callbacks. */
				inputData.frames = 0;
				inputData.data = NULL;
				if ((inputData.data = malloc(INPUT_BUFFER*CHANNEL_COUNT*sizeof(float))) == NULL)
								goto error1;
				else
								bzero(inputData.data, INPUT_BUFFER*CHANNEL_COUNT*sizeof(float));
				outputData.frames = 0;
				outputData.data = NULL;
				if ((outputData.data = malloc(OUTPUT_BUFFER*CHANNEL_COUNT*sizeof(float))) == NULL)
								goto error1;
				else
								bzero(outputData.data, OUTPUT_BUFFER*CHANNEL_COUNT*sizeof(float));

				/* STAGE 2: Initialize the resampling. */
				printf("Setting up %s rate converter with %s\n",
				       src_get_name (SRC_SINC_MEDIUM_QUALITY),
				       src_get_description (SRC_SINC_MEDIUM_QUALITY));

				state = src_new (SRC_SINC_MEDIUM_QUALITY, CHANNEL_COUNT, &err);
				if (state == NULL)
				{
								printf("ERROR: src_new returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								goto error2;
				}

				/* specify the input and output buffers for the sample rate conversion */
				data.data_in = NULL;
				data.data_out = NULL;

				ratio = ((float)OUTPUT_RATE) / ((float)INPUT_RATE);

				printf("ratio = %f\n", ratio);
				err = src_set_ratio (state, ratio);
				if (err)
				{
								printf("ERROR: src_set_ratio returned 0x%x\n", err );
								printf("ERROR message: %s\n", src_strerror(err));
								goto error2;
				}

				/* STAGE 3: Initialize the audio input and output. */
				inputParameters.device = paNoDevice;
				outputParameters.device = paNoDevice;

				err = Pa_Initialize();
				if( err != paNoError )
				{
								printf("ERROR: Pa_Initialize returned 0x%x\n", err );
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				printf("PortAudio version: 0x%08X\n", Pa_GetVersion());
				printf("Version text: '%s'\n", Pa_GetVersionInfo()->versionText );

				/* Initialize library before making any other calls. */
				err = Pa_Initialize();
				if( err != paNoError )
				{
								printf("ERROR: Cannot initialize PortAudio.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				numDevices = Pa_GetDeviceCount();
				if( numDevices < 0 )
				{
								printf("ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								err = numDevices;
								goto error3;
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
				printf("Initialized input device %d\n", inputParameters.device);

				err = Pa_OpenStream(
								&inputStream,
								&inputParameters,
								NULL,
								INPUT_RATE,
								INPUT_BLOCKSIZE,
								0,
								inputCallback,
								&inputData );
				if( err != paNoError )
				{
								printf("ERROR: Cannot open input stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
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
				printf("Initialized output device %d\n", outputParameters.device);

				err = Pa_OpenStream(
								&outputStream,
								NULL,
								&outputParameters,
								OUTPUT_RATE,
								OUTPUT_BLOCKSIZE,
								paClipOff,
								outputCallback,
								&outputData );
				if( err != paNoError )
				{
								printf("ERROR: Cannot open output stream.\n");
								printf("ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}
				else {
								printf("Opened output stream.\n");
				}

				/************************************************************/

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

				/* Sleep for some time to fill the buffer. */
				printf("Filling buffer.\n");

        Pa_Sleep(1000);
				enable_resample = 1;

        Pa_Sleep(1000);
				enable_update = 1;

				while (1)
				{
								Pa_Sleep(1000);
        }

								printf("------------------------------\n");
								printf("inputData.frames        = %ld\n", inputData.frames);
								printf("outputData.frames       = %ld\n", outputData.frames);
								printf("data.src_ratio          = %f\n", data.src_ratio);
								printf("data.input_frames_used  = %ld\n", data.input_frames_used);
								printf("data.output_frames_gen  = %ld\n", data.output_frames_gen);
								printf("input stream CPU load   = %.2f %%\n", 100*Pa_GetStreamCpuLoad(inputStream));
								printf("output stream CPU load  = %.2f %%\n", 100*Pa_GetStreamCpuLoad(outputStream));

				err = Pa_StopStream( outputStream );
				if( err != paNoError ) goto error3;
				err = Pa_CloseStream( outputStream );
				if( err != paNoError ) goto error3;

				Pa_Terminate();
				printf("Test finished.\n");
				printf("inputData.frames     = %lu\n", inputData.frames);
				printf("outputData.frames    = %lu\n", outputData.frames);

				free(inputData.data);
				free(outputData.data);
        free(line);

				return 0;

error3:
				Pa_Terminate();

error2:
				if (state)
								src_delete (state);

error1:
				if (inputData.data)
								free(inputData.data);
				if (outputData.data)
								free(outputData.data);
        if (line)
                free(line);

				printf("Error number: %d\n", err );

				return err;
}
