cmake_minimum_required(VERSION 3.10)

project(resampleaudio VERSION 1.0)

add_executable(resampleaudio resampleaudio.c)
add_executable(resamplelsl resamplelsl.c)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (WIN32)
include_directories(external/portaudio/include external/samplerate/include external/lsl/include)
endif()

if (UNIX)
include_directories(/usr/local/include)
endif()

find_library(PORTAUDIO NAMES libportaudio.a portaudio.lib portaudio_static_x64.lib portaudio_x64.lib PATHS /usr/local/lib external/portaudio/lib)
find_library(RESAMPLE NAMES libsamplerate.a samplerate.lib PATHS /usr/local/lib external/samplerate/lib)
find_library(LSL NAMES liblsl.a lsl.lib PATHS /usr/local/lib external/lsl/lib)

target_link_libraries(resampleaudio ${PORTAUDIO} ${RESAMPLE})
target_link_libraries(resamplelsl ${PORTAUDIO} ${RESAMPLE} ${LSL})


