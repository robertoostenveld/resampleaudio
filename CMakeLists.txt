cmake_minimum_required(VERSION 3.10)

project(resampleaudio VERSION 1.0)

add_executable(resampleaudio resampleaudio.c)
add_executable(resamplelsl resamplelsl.c)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(/usr/local/include)

target_link_libraries(resampleaudio -L/usr/local/lib)
target_link_libraries(resampleaudio -lportaudio -lsamplerate)

target_link_libraries(resamplelsl -L/usr/local/lib)
target_link_libraries(resamplelsl -lportaudio -lsamplerate -llsl)
