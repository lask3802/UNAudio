cmake_minimum_required(VERSION 3.22.1)
project(UNAudio VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Source files ──────────────────────────────────────────────────

set(CORE_SOURCES
    Source/Core/AudioEngine.cpp
)

set(DECODER_SOURCES
    Source/Decoder/PCMDecoder.cpp
    Source/Decoder/MP3Decoder.cpp
    Source/Decoder/VorbisDecoder.cpp
    Source/Decoder/FLACDecoder.cpp
    Source/Decoder/ResamplingDecoder.cpp
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
    target_link_libraries(UNAudio PRIVATE winmm ole32)
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

# ── Tests ─────────────────────────────────────────────────────────

option(UNAUDIO_BUILD_TESTS "Build unit tests" OFF)

if(UNAUDIO_BUILD_TESTS)
    enable_testing()

    unset(UNA_GTEST_INCLUDE_DIRS)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        set(UNA_GTEST_INCLUDE_DIRS
            "${googletest_SOURCE_DIR}/googletest/include"
            "${googletest_SOURCE_DIR}/googletest"
        )
    endif()

    set(TEST_SOURCES
        Tests/test_main.cpp
        Tests/test_ring_buffer.cpp
        Tests/test_frame_allocator.cpp
        Tests/test_simd_utils.cpp
        Tests/test_pcm_decoder.cpp
        Tests/test_vorbis_decoder.cpp
        Tests/test_mixer.cpp
        Tests/test_audio_output.cpp
        Tests/test_resampling.cpp
    )

    add_executable(UNAudioTests
        ${TEST_SOURCES}
        # Re-use library sources (link statically for tests)
        ${CORE_SOURCES}
        ${DECODER_SOURCES}
        ${MIXER_SOURCES}
        ${PLATFORM_SOURCES}
    )

    target_include_directories(UNAudioTests PRIVATE
        Source
        Source/Core
        Source/Decoder
        Source/Mixer
        Source/Platform
    )
    if(UNA_GTEST_INCLUDE_DIRS)
        # Force matching headers with the fetched gtest library on MSVC setups
        # where another gtest copy may exist in the global INCLUDE path.
        target_include_directories(UNAudioTests BEFORE PRIVATE ${UNA_GTEST_INCLUDE_DIRS})
        if(MSVC)
            foreach(_gtest_dir IN LISTS UNA_GTEST_INCLUDE_DIRS)
                target_compile_options(UNAudioTests PRIVATE "/I${_gtest_dir}")
            endforeach()
        endif()
    endif()

    # Force scalar SIMD for predictable test results on CI.
    # To also exercise SSE2/AVX2/NEON paths, build a second target
    # without UNA_FORCE_SCALAR (see Native/Tests/README for details).
    target_compile_definitions(UNAudioTests PRIVATE UNA_FORCE_SCALAR)

    target_link_libraries(UNAudioTests PRIVATE GTest::gtest)

    # Keep GoogleTest static registrars in optimized MSVC builds.
    if(MSVC)
        target_link_options(UNAudioTests PRIVATE /OPT:NOREF /OPT:NOICF)
    endif()

    if(WIN32)
        target_link_libraries(UNAudioTests PRIVATE winmm ole32)
    endif()

    include(GoogleTest)
    gtest_discover_tests(UNAudioTests)
endif()
