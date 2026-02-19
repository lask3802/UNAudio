cmake_minimum_required(VERSION 3.10.2)
project(UNAudio)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    ../Core/UNAudioCore.h
    UNAudioAndroid.h
    UNAudioAndroid.cpp
)

# Create shared library
add_library(UNAudio SHARED ${SOURCES})

# Link AAudio library
target_link_libraries(UNAudio
    aaudio
    log
)

# Include directories
target_include_directories(UNAudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Core
)

# Compiler flags for optimization
target_compile_options(UNAudio PRIVATE
    -Wall
    -O3
    -fvisibility=hidden
)
