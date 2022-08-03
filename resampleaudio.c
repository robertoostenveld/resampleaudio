	#include <stdio.h>
	#include <string.h>
	#include <stdlib.h>
	#include <math.h>
	#include <ncurses.h>

	#include "portaudio.h"
	#include "samplerate.h"

	#define NUM_SECONDS               (2)
	#define SAMPLE_TYPE               paFloat32
	#define CHANNEL_COUNT             (1)
	#define INPUT_SAMPLE_RATE         (8000)
	#define OUTPUT_SAMPLE_RATE        (48000)
	#define INPUT_FRAMES_PER_BUFFER   (200)
	#define OUTPUT_FRAMES_PER_BUFFER  (200)
	#define BUFFER_LENGTH             (1)
	#define INPUT_BUFFER_LENGTH       (INPUT_SAMPLE_RATE*2*BUFFER_LENGTH)
	#define OUTPUT_BUFFER_LENGTH      (OUTPUT_SAMPLE_RATE*2*BUFFER_LENGTH)

typedef struct {
				float *data;
				long count;
} dataBuffer_t;

unsigned long inputCallbackCount = 0, outputCallbackCount=0;

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
								if (inputData->count<INPUT_BUFFER_LENGTH)
								{
												for (int j=0; j<CHANNEL_COUNT; j++)
																inputData->data[inputData->count+j] = dat[i*CHANNEL_COUNT+j];
												inputData->count++;
								}
				}

				inputCallbackCount += frameCount;
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

				if (outputData->count == 0)
				{
								/* there is no data */
								bzero(dat, frameCount*CHANNEL_COUNT*sizeof(float));
				}
				else if (outputData->count > frameCount)
				{
								/* there is enough data */
								size_t len = frameCount*CHANNEL_COUNT*sizeof(float);
								memcpy(dat, outputData->data, len);
								memmove(outputData->data, outputData->data+len, len);
								outputData->count -= frameCount;
				}
				else
				{
								/* there is some data, but not enough */
								size_t len = outputData->count*CHANNEL_COUNT*sizeof(float);
								memcpy(dat, outputData->data, len);
								bzero(dat+len, frameCount*CHANNEL_COUNT*sizeof(float) - len);
								outputData->count = 0;
				}

				outputCallbackCount+=frameCount;
				return 0;
}

/*******************************************************************************************************/
int main(int argc, char *argv[]) {
        char str[256];
				dataBuffer_t inputData, outputData;

				SRC_STATE* state = NULL;
				SRC_DATA data;
        float ratio;

				PaStream *inputStream, *outputStream;
				PaStreamParameters inputParameters, outputParameters;
				PaError err = paNoError;
				int numDevices, defaultDisplayed;
				const PaDeviceInfo *deviceInfo;

				/* STAGE 1: Initialize the inputData and outputData for use by the callbacks. */
				inputData.count = 0;
				inputData.data = NULL;
				if ((inputData.data = malloc(INPUT_BUFFER_LENGTH*CHANNEL_COUNT*sizeof(float))) == NULL)
								goto error1;
				else
								bzero(inputData.data, INPUT_BUFFER_LENGTH*CHANNEL_COUNT);
				outputData.count = 0;
				outputData.data = NULL;
				if ((outputData.data = malloc(OUTPUT_BUFFER_LENGTH*CHANNEL_COUNT*sizeof(float))) == NULL)
								goto error1;
				else
								bzero(outputData.data, OUTPUT_BUFFER_LENGTH*CHANNEL_COUNT);

				/* STAGE 2: Initialize the resampling. */
				printf("Setting up %s rate converter with %s\n",
				       src_get_name (SRC_SINC_MEDIUM_QUALITY),
				       src_get_description (SRC_SINC_MEDIUM_QUALITY));

				state = src_new (SRC_SINC_MEDIUM_QUALITY, CHANNEL_COUNT, &err);
				if (state == NULL)
				{
								fprintf(stderr, "ERROR: src_new returned 0x%x\n", err );
                fprintf(stderr, "ERROR message: %s\n", src_strerror(err));
								goto error2;
				}

				/* specify the input and output buffers for the sample rate conversion */
				data.data_in = NULL;
				data.data_out = NULL;

        ratio = ((float)OUTPUT_SAMPLE_RATE) / ((float)INPUT_SAMPLE_RATE);
        printf("ratio = %f\n", ratio);
				err = src_set_ratio (state, ratio);
				if (err)
				{
								fprintf(stderr, "ERROR: src_set_ratio returned 0x%x\n", err );
                fprintf(stderr, "ERROR message: %s\n", src_strerror(err));
								goto error2;
				}

				/* STAGE 3: Initialize the audio input and output. */
				inputParameters.device = paNoDevice;
				outputParameters.device = paNoDevice;

				err = Pa_Initialize();
				if( err != paNoError )
				{
								fprintf(stderr, "ERROR: Pa_Initialize returned 0x%x\n", err );
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				printf( "PortAudio version: 0x%08X\n", Pa_GetVersion());
				printf( "Version text: '%s'\n", Pa_GetVersionInfo()->versionText );

				numDevices = Pa_GetDeviceCount();
				if( numDevices < 0 )
				{
								fprintf(stderr, "ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								err = numDevices;
								goto error3;
				}

				printf( "Number of devices = %d\n", numDevices );
				for( int i=0; i<numDevices; i++ )
				{
								deviceInfo = Pa_GetDeviceInfo( i );
								printf( "--------------------------------------- device #%d\n", i );

								/* Mark global and API specific default devices */
								defaultDisplayed = 0;
								if( i == Pa_GetDefaultInputDevice() )
								{
												printf( "[ Default Input" );
												defaultDisplayed = 1;
								}
								else if( i == Pa_GetHostApiInfo( deviceInfo->hostApi )->defaultInputDevice )
								{
												const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo( deviceInfo->hostApi );
												printf( "[ Default %s Input", hostInfo->name );
												defaultDisplayed = 1;
								}

								if( i == Pa_GetDefaultOutputDevice() )
								{
												printf( (defaultDisplayed ? "," : "[") );
												printf( " Default Output" );
												defaultDisplayed = 1;
								}
								else if( i == Pa_GetHostApiInfo( deviceInfo->hostApi )->defaultOutputDevice )
								{
												const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo( deviceInfo->hostApi );
												printf( (defaultDisplayed ? "," : "[") );
												printf( " Default %s Output", hostInfo->name );
												defaultDisplayed = 1;
								}

								if( defaultDisplayed )
												printf( " ]\n" );

								printf( "Name        = %s\n", deviceInfo->name );
								printf( "Host API    = %s\n", Pa_GetHostApiInfo( deviceInfo->hostApi )->name );
								printf( "Max inputs  = %d\n", deviceInfo->maxInputChannels  );
								printf( "Max outputs = %d\n", deviceInfo->maxOutputChannels  );
				}

        initscr();
        printf()
        err = getnstr(str, 255);
        if (err == ERR) {
          fprintf(stderr, "Error: Invalid input.\n");
          goto error2;
          
        }

				inputParameters.device = Pa_GetDefaultInputDevice();                                                 /* default input device */
				if (inputParameters.device == paNoDevice) {
								fprintf(stderr, "Error: No default input device.\n");
								goto error3;
				}
				inputParameters.channelCount = CHANNEL_COUNT;
				inputParameters.sampleFormat = SAMPLE_TYPE;
				inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
				inputParameters.hostApiSpecificStreamInfo = NULL;

				outputParameters.device = Pa_GetDefaultOutputDevice();                                                 /* default output device */
				if (outputParameters.device == paNoDevice) {
								fprintf(stderr, "Error: No default output device.\n");
								goto error3;
				}
				outputParameters.channelCount = CHANNEL_COUNT;
				outputParameters.sampleFormat = SAMPLE_TYPE;
				outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
				outputParameters.hostApiSpecificStreamInfo = NULL;

				/* Initialize library before making any other calls. */
				err = Pa_Initialize();
				if( err != paNoError )
				{
								fprintf(stderr, "ERROR: Cannot initialize PortAudio.\n");
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				err = Pa_OpenStream(
								&outputStream,
								NULL,
								&outputParameters,
								OUTPUT_SAMPLE_RATE,
								OUTPUT_FRAMES_PER_BUFFER,
								paClipOff,
								outputCallback,
								&outputData );
				if( err != paNoError )
				{
								fprintf(stderr, "ERROR: Cannot open output stream.\n");
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				err = Pa_StartStream( outputStream );
				if( err != paNoError )
				{
								fprintf(stderr, "ERROR: Cannot start output stream.\n");
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				err = Pa_OpenStream(
								&inputStream,
								&inputParameters,
								NULL,
								INPUT_SAMPLE_RATE,
								INPUT_FRAMES_PER_BUFFER,
								0,
								inputCallback,
								&inputData );
				if( err != paNoError )
				{
								fprintf(stderr, "ERROR: Cannot open input stream.\n");
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				err = Pa_StartStream( inputStream );
				if( err != paNoError )
				{
								fprintf(stderr, "ERROR: Cannot start input stream.\n");
								fprintf(stderr, "ERROR message: %s\n", Pa_GetErrorText( err ) );
								goto error3;
				}

				/* Sleep for some time to fill the buffer. */
				Pa_Sleep(1000);

				while (1) {
								Pa_Sleep(100);
								printf("inputData.count  = %ld\n", inputData.count);
								printf("outputData.count = %ld\n", outputData.count);
								data.data_in = inputData.data;
								data.input_frames = inputData.count;
								data.data_out = outputData.data + outputData.count * CHANNEL_COUNT * sizeof(float);
								data.output_frames = OUTPUT_BUFFER_LENGTH - outputData.count;
                data.end_of_input = 0;
                data.src_ratio =  ratio;

								err = src_process (state, &data);

                printf("data.src_ratio  = %f\n", data.src_ratio);
                printf("data.input_frames_used  = %ld\n", data.input_frames_used);
                printf("data.output_frames_gen  = %ld\n", data.output_frames_gen);

								if (err)
								{
												fprintf(stderr, "ERROR: src_process returned 0x%x\n", err );
                        fprintf(stderr, "ERROR message: %s\n", src_strerror(err));
												goto error3;
								}

								outputData.count += data.output_frames_gen;
								inputData.count -= data.input_frames_used;
								size_t len = data.input_frames_used * CHANNEL_COUNT * sizeof(float);
								memcpy(inputData.data, inputData.data + len, len);

				}

				err = Pa_StopStream( outputStream );
				if( err != paNoError ) goto error3;
				err = Pa_CloseStream( outputStream );
				if( err != paNoError ) goto error3;

				Pa_Terminate();
				printf("Test finished.\n");
				printf("inputCallbackCount  = %lu\n", inputCallbackCount);
				printf("outputCallbackCount = %lu\n", outputCallbackCount);
				printf("inputData.count     = %lu\n", inputData.count);
				printf("outputData.count    = %lu\n", outputData.count);

				free(inputData.data);
				free(outputData.data);
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
				fprintf( stderr, "Error number: %d\n", err );
        
        endwin();
				return err;
}
