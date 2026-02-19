cmake_minimum_required(VERSION 3.22.1)
project(UNAudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Source files ──────────────────────────────────────────────────

set(CORE_SOURCES
    Source/Core/AudioEngine.cpp
)

set(DECODER_SOURCES
    Source/Decoder/MP3Decoder.cpp
    Source/Decoder/VorbisDecoder.cpp
    Source/Decoder/FLACDecoder.cpp
)

set(MIXER_SOURCES
    Source/Mixer/AudioMixer.cpp
)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SOURCES Source/Platform/Windows/WASAPIOutput.cpp)
elseif(APPLE)
    if(IOS)
        set(PLATFORM_SOURCES Source/Platform/iOS/iOSAudioOutput.mm)
    else()
        set(PLATFORM_SOURCES Source/Platform/macOS/CoreAudioOutput.cpp)
    endif()
elseif(ANDROID)
    set(PLATFORM_SOURCES Source/Platform/Android/OboeAudioOutput.cpp)
elseif(UNIX)
    set(PLATFORM_SOURCES Source/Platform/Linux/ALSAOutput.cpp)
endif()

# ── Shared library ────────────────────────────────────────────────

add_library(UNAudio SHARED
    ${CORE_SOURCES}
    ${DECODER_SOURCES}
    ${MIXER_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(UNAudio PRIVATE
    Source
    Source/Core
    Source/Decoder
    Source/Mixer
    Source/Platform
)

# ── Platform-specific link libraries ─────────────────────────────

if(WIN32)
    # TODO: target_link_libraries(UNAudio PRIVATE ole32 winmm)
elseif(APPLE)
    # TODO: target_link_libraries(UNAudio PRIVATE "-framework AudioToolbox" "-framework CoreAudio")
elseif(ANDROID)
    find_package(oboe QUIET CONFIG)
    if(oboe_FOUND)
        target_link_libraries(UNAudio PRIVATE oboe::oboe log android)
    else()
        target_link_libraries(UNAudio PRIVATE log android)
    endif()
elseif(UNIX)
    # TODO: target_link_libraries(UNAudio PRIVATE asound pthread)
endif()

# ── Third-party libraries (to be added) ──────────────────────────

# TODO: add_subdirectory(ThirdParty/lz4)
# TODO: find_package / FetchContent for libmpg123, libvorbis, libflac

# ── Export symbols ────────────────────────────────────────────────

if(WIN32)
    target_compile_definitions(UNAudio PRIVATE UNAUDIO_BUILDING_DLL)
else()
    set_target_properties(UNAudio PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()
