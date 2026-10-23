cmake_minimum_required(VERSION 3.22)
project(UNAudio VERSION 2.0.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()
# iOS, tvOS and visionOS: RemoteIO + AVAudioSession, static plugin.
if(IOS OR CMAKE_SYSTEM_NAME MATCHES "^(tvOS|visionOS)$")
    set(UNA_APPLE_MOBILE ON)
else()
    set(UNA_APPLE_MOBILE OFF)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # Static CRT everywhere (no VC++ redistributable next to the player); sources are UTF-8.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(UNAUDIO_BUILD_TESTS "Build native unit tests" OFF)
option(UNAUDIO_SIMD "SSE2 / NEON mixer and resampler kernels (OFF = scalar fallback)" ON)
option(UNAUDIO_FETCH_OBOE "Android: fetch Oboe with FetchContent" ON)
set(UNAUDIO_OBOE_DIR "" CACHE PATH "Android: local Oboe checkout (overrides fetch)")

# ── Sources ──────────────────────────────────────────────────────

set(UNA_CORE_SOURCES
    Source/Core/Platform.cpp
    Source/Decode/VorbisSource.cpp
    Source/Decode/WavSource.cpp
    Source/Decode/Resampler.cpp
    Source/Decode/DecodePipeline.cpp
    Source/Engine/Clip.cpp
    Source/Engine/Clock.cpp
    Source/Engine/Stream.cpp
    Source/Engine/Services.cpp
    Source/Engine/Engine.cpp
    Source/Engine/Mixer.cpp
    Source/Engine/MixKernels.cpp
    Source/Platform/NullDevice.cpp
)

if(WIN32)
    list(APPEND UNA_CORE_SOURCES Source/Platform/Windows/WasapiDevice.cpp)
elseif(APPLE)
    list(APPEND UNA_CORE_SOURCES Source/Platform/Apple/CoreAudioDevice.mm)
elseif(ANDROID)
    list(APPEND UNA_CORE_SOURCES Source/Platform/Android/OboeDevice.cpp)
elseif(UNIX)
    list(APPEND UNA_CORE_SOURCES Source/Platform/Linux/AlsaDevice.cpp)
endif()

set(UNA_API_SOURCES Source/Engine/Api.cpp)

# ── Core static library (shared by the plugin and the tests) ─────

add_library(unaudio_core STATIC ${UNA_CORE_SOURCES})
target_include_directories(unaudio_core PUBLIC Include Source)
target_compile_definitions(unaudio_core PUBLIC UNAUDIO_BUILDING)
if(NOT UNAUDIO_SIMD)
    target_compile_definitions(unaudio_core PUBLIC UNA_NO_SIMD)
endif()
set_target_properties(unaudio_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
    target_compile_options(unaudio_core PRIVATE /W3 /permissive- /Zc:__cplusplus $<$<CONFIG:Release>:/O2 /Ob2>)
else()
    target_compile_options(unaudio_core PRIVATE -Wall -Wextra -Wno-unused-parameter $<$<CONFIG:Release>:-O3>)
endif()

find_package(Threads REQUIRED)
target_link_libraries(unaudio_core PUBLIC Threads::Threads)

if(WIN32)
    target_compile_definitions(unaudio_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(unaudio_core PUBLIC ole32 avrt ksuser)
elseif(APPLE)
    target_compile_options(unaudio_core PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    if(UNA_APPLE_MOBILE)
        target_link_libraries(unaudio_core PUBLIC "-framework AudioToolbox" "-framework AVFoundation" "-framework Foundation")
    else()
        target_link_libraries(unaudio_core PUBLIC "-framework AudioToolbox" "-framework CoreAudio" "-framework Foundation")
    endif()
elseif(ANDROID)
    # Oboe 1.9.3 declares cmake_minimum_required(3.4.1), which CMake 4 rejects.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    if(UNAUDIO_OBOE_DIR)
        add_subdirectory(${UNAUDIO_OBOE_DIR} ${CMAKE_BINARY_DIR}/oboe)
    elseif(UNAUDIO_FETCH_OBOE)
        include(FetchContent)
        FetchContent_Declare(oboe
            GIT_REPOSITORY https://github.com/google/oboe.git
            GIT_TAG 1.9.3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(oboe)
    endif()
    target_link_libraries(unaudio_core PUBLIC oboe log android)
elseif(UNIX)
    find_package(ALSA REQUIRED)
    target_link_libraries(unaudio_core PUBLIC ALSA::ALSA)
endif()

# ── Plugin ───────────────────────────────────────────────────────

if(UNA_APPLE_MOBILE)
    # Unity links iOS plugins statically ("__Internal").
    add_library(UNAudio STATIC ${UNA_API_SOURCES} ${UNA_CORE_SOURCES})
    target_include_directories(UNAudio PRIVATE Include Source)
    target_compile_definitions(UNAudio PRIVATE UNAUDIO_BUILDING)
    target_compile_options(UNAudio PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
else()
    add_library(UNAudio SHARED ${UNA_API_SOURCES})
    target_link_libraries(UNAudio PRIVATE unaudio_core)
    set_target_properties(UNAudio PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    if(MSVC)
        target_compile_options(UNAudio PRIVATE /W3 /permissive-)
    endif()
    if(ANDROID)
        # Keep Oboe (static) symbols private so they cannot clash with other plugins.
        target_link_options(UNAudio PRIVATE "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL" "-Wl,-z,max-page-size=16384")
    endif()
endif()

# ── Tests ────────────────────────────────────────────────────────

# Mobile targets build the same executables; run them on a device or simulator
# (adb push / simctl spawn) with UNA_TEST_FIXTURES_DIR pointing at a copy of Tests/fixtures.
if(UNAUDIO_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(UNAudioTests
        Tests/test_main.cpp
        Tests/TestUtil.cpp
        Tests/test_queues.cpp
        Tests/test_decode.cpp
        Tests/test_resampler.cpp
        Tests/test_clock.cpp
        Tests/test_engine.cpp
        Tests/test_timing.cpp
        Tests/test_stream.cpp
        Tests/test_stress.cpp
        Tests/test_regressions.cpp
        Tests/test_simd.cpp
        Tests/test_stream_errors.cpp
        ${UNA_API_SOURCES})
    target_link_libraries(UNAudioTests PRIVATE unaudio_core GTest::gtest)
    target_compile_definitions(UNAudioTests PRIVATE
        UNA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
    if(NOT CMAKE_CROSSCOMPILING)
        include(GoogleTest)
        gtest_discover_tests(UNAudioTests DISCOVERY_TIMEOUT 60)
    endif()
endif()

# Developer tool: plays a test tone through the platform device.
if(UNAUDIO_BUILD_TESTS)
    add_executable(una_device_probe Tests/device_probe.cpp)
    target_link_libraries(una_device_probe PRIVATE unaudio_core)
    # Mixer throughput benchmark (manual device).
    add_executable(una_bench Tests/bench_mixer.cpp ${UNA_API_SOURCES})
    target_link_libraries(una_bench PRIVATE unaudio_core)
endif()
