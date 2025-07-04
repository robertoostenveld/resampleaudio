cmake_minimum_required(VERSION 3.10)

project(resampleaudio VERSION 1.0)

add_executable(resampleaudio resampleaudio.c)
add_executable(lsl2audio lsl2audio.c)
add_executable(audio2lsl audio2lsl.c)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(/usr/local/include)
include_directories(/opt/homebrew/include)
include_directories(external/portaudio/include external/samplerate/include external/lsl/include)

if (UNIX)
endif()

if (WIN32)
endif()

if (APPLE)
# these are needed by the static libportaudio.a that is installed by homebrew
target_link_libraries(resampleaudio "-framework CoreServices -framework CoreFoundation -framework AudioUnit -framework AudioToolbox -framework CoreAudio")
target_link_libraries(lsl2audio "-framework CoreServices -framework CoreFoundation -framework AudioUnit -framework AudioToolbox -framework CoreAudio")
target_link_libraries(audio2lsl "-framework CoreServices -framework CoreFoundation -framework AudioUnit -framework AudioToolbox -framework CoreAudio")
# this is needed for the static liblsl
target_link_libraries(lsl2audio c++)
target_link_libraries(audio2lsl c++)
endif()

# use static libraries where possible to facilitate distribution of the executable
find_library(PORTAUDIO NAMES libportaudio.a portaudio.lib portaudio_static_x64.lib portaudio_x64.lib PATHS external/portaudio/lib /usr/local/lib /opt/homebrew/lib)
find_library(RESAMPLE NAMES libsamplerate.a samplerate.lib PATHS external/samplerate/lib /usr/local/lib /opt/homebrew/lib)
find_library(LSL NAMES liblsl.a lsl.lib PATHS external/lsl/lib /usr/local/lib /opt/homebrew/lib)

target_link_libraries(resampleaudio ${PORTAUDIO} ${RESAMPLE})
target_link_libraries(lsl2audio ${PORTAUDIO} ${RESAMPLE} ${LSL})
target_link_libraries(audio2lsl ${PORTAUDIO} ${RESAMPLE} ${LSL})
