cmake_minimum_required(VERSION 3.22)

# Fully static MSVC runtime: the .exe runs on any Windows 10/11 PC without installers.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Spectroscope VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout if given (-DJUCE_DIR=...), otherwise download the pinned release.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(Spectroscope
    PRODUCT_NAME "Spectroscope"
    COMPANY_NAME "DigiCheck Universal")

juce_generate_juce_header(Spectroscope)

target_sources(Spectroscope PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/LoopbackCapture.cpp
    Source/Reassigner.cpp
    Source/LoudnessMeter.cpp
    Source/SpectrogramView.cpp
    Source/SettingsDialog.cpp)

target_compile_definitions(Spectroscope PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Spectroscope,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Spectroscope,JUCE_VERSION>"
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=1)               # ASIO SDK headers are bundled with JUCE (GPLv3), personal use

target_link_libraries(Spectroscope
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(WIN32)
    target_link_libraries(Spectroscope PRIVATE ole32)
endif()

# Stand-alone DSP calibration test (no JUCE)
add_executable(dsp_test tests/dsp_test.cpp Source/Reassigner.cpp)
add_executable(loudness_test tests/loudness_test.cpp Source/LoudnessMeter.cpp)
