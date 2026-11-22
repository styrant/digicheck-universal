cmake_minimum_required(VERSION 3.22)

# Fully static MSVC runtime: the .exe runs on any Windows 10/11 PC without installers.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Spectroscope VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
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
    COMPANY_NAME "DigiCheck Universal"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")

juce_generate_juce_header(Spectroscope)

target_sources(Spectroscope PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/LoopbackCapture.cpp
    Source/Reassigner.cpp
    Source/LoudnessMeter.cpp
    Source/SpectrogramView.cpp
    Source/SettingsDialog.cpp
    Source/BottomBar.cpp
    Source/analyzer/PluginProcessor.cpp
    Source/analyzer/ui/AnalyzerView.cpp
    Source/analyzer/ui/SettingsPanel.cpp
    Source/key/KeyNet.cpp
    Source/key/KeyCqt.cpp
    Source/key/KeyFinder.cpp
    Source/balance/TonalAnalysis.cpp
    Source/balance/LiveBalance.cpp
    Source/balance/TargetLibrary.cpp
    Source/balance/BalanceView.cpp)

# Key finder model (OpenKeyScan KeyNet, MIT; exported by tools/export_keynet.py)
juce_add_binary_data(SpectroscopeData SOURCES Resources/keynet.bin)
target_link_libraries(Spectroscope PRIVATE SpectroscopeData)

# the network's matrix maths is the only hot loop that benefits from AVX2 / FMA
if(MSVC)
    set_source_files_properties(Source/key/KeyNet.cpp PROPERTIES COMPILE_OPTIONS "/O2;/arch:AVX2;/fp:fast")
else()
    set_source_files_properties(Source/key/KeyNet.cpp PROPERTIES COMPILE_OPTIONS "-O3;-mavx2;-mfma")
endif()

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
        juce::juce_opengl
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
add_executable(analyzer_dsp_test tests/analyzer_dsp_test.cpp)   # Spectrum Analyzer 2's own DSP tests
add_executable(key_test tests/key_test.cpp Source/key/KeyNet.cpp Source/key/KeyCqt.cpp)   # key finder vs PyTorch / librosa
add_executable(balance_test tests/balance_test.cpp Source/balance/TonalAnalysis.cpp)       # tonal balance measurement
juce_add_console_app(target_loader_test)                                                    # reading target files
juce_generate_juce_header(target_loader_test)
target_sources(target_loader_test PRIVATE tests/target_loader_test.cpp Source/balance/TargetLibrary.cpp Source/balance/TonalAnalysis.cpp)
target_compile_definitions(target_loader_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(target_loader_test PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags)

# TargetBuilder: window app (and --cli mode) that makes tonal balance targets from songs
juce_add_gui_app(TargetBuilder
    PRODUCT_NAME "TargetBuilder"
    COMPANY_NAME "DigiCheck Universal"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")
juce_generate_juce_header(TargetBuilder)
target_sources(TargetBuilder PRIVATE
    Source/tools/TargetBuilderApp.cpp
    Source/tools/TargetBuilderCli.cpp
    Source/tools/TargetCore.cpp
    Source/balance/TonalAnalysis.cpp)
target_compile_definitions(TargetBuilder PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:TargetBuilder,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:TargetBuilder,JUCE_VERSION>")
target_link_libraries(TargetBuilder
    PRIVATE juce::juce_audio_formats juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
