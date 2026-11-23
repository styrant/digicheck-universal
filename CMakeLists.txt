cmake_minimum_required(VERSION 3.24)   # muscriptor.cpp needs 3.24

# Fully static MSVC runtime: the .exe runs on any Windows 10/11 PC without installers.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# One version for everything (app, tools, installer, a future plugin): major.minor from the VERSION file,
# the patch number from the CI build number (0 for local builds).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DCU_VERSION_MAJOR_MINOR LIMIT_COUNT 1)
if(DEFINED ENV{GITHUB_RUN_NUMBER})
    set(DCU_VERSION "${DCU_VERSION_MAJOR_MINOR}.$ENV{GITHUB_RUN_NUMBER}")
else()
    set(DCU_VERSION "${DCU_VERSION_MAJOR_MINOR}.0")
endif()

project(Spectroscope VERSION ${DCU_VERSION} LANGUAGES C CXX)
message(STATUS "DigiCheck Universal ${DCU_VERSION}")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# JUCE: use a local checkout if given (-DJUCE_DIR=...), otherwise download the pinned release.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- Chords & melody: NeuralNote v2's transcription engine, muscriptor.cpp (MIT), pinned to the commit
# that styrant/NeuralNote uses as its submodule. It fetches ggml itself. With the Vulkan SDK installed
# it is built with GPU support (Vulkan, loaded lazily so the app still starts without it); otherwise
# it runs on the CPU. Its headers need C++23, so it sits behind dcu_transcribe, a small C++23 library
# with a plain C++20 interface (Source/transcribe/TranscribeEngine.h): the app itself stays C++20.
set(MUSCRIPTOR_DIR "" CACHE PATH "Path to a local muscriptor.cpp checkout")
set(MUSCRIPTOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MUSCRIPTOR_BUILD_BENCH OFF CACHE BOOL "" FORCE)
if(MUSCRIPTOR_DIR)
    add_subdirectory(${MUSCRIPTOR_DIR}/cpp muscriptor EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(muscriptor
        GIT_REPOSITORY https://github.com/DamRsn/muscriptor.cpp.git
        GIT_TAG 8589219cc3b584bce970f95aec2ec4a04e53b280
        SOURCE_SUBDIR cpp)
    FetchContent_MakeAvailable(muscriptor)
endif()

add_library(dcu_transcribe STATIC Source/transcribe/TranscribeEngine.cpp)
target_compile_features(dcu_transcribe PRIVATE cxx_std_23)
target_link_libraries(dcu_transcribe PRIVATE muscriptor_ggml)      # PRIVATE: C++23 doesn't reach the app
if(MUSCRIPTOR_VULKAN)
    target_compile_definitions(dcu_transcribe PRIVATE DCU_TRANSCRIBE_GPU)
endif()

juce_add_gui_app(Spectroscope
    PRODUCT_NAME "Spectroscope"
    COMPANY_NAME "DigiCheck Universal"
    BUNDLE_ID "com.digicheckuniversal.spectroscope"      # fixed now so it never changes (docs/VST3.md)
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")

juce_generate_juce_header(Spectroscope)

# Everything but the app shell (Main.cpp) and the standalone-only audio input (AudioEngine, WASAPI
# loopback) is shared code that a future plugin target builds unchanged: see docs/VST3.md.
set(DCU_SHARED_SOURCES
    Source/AnalysisEngine.cpp
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
    Source/key/KeyPage.cpp
    Source/balance/TonalAnalysis.cpp
    Source/balance/LiveBalance.cpp
    Source/balance/TargetLibrary.cpp
    Source/balance/BalanceView.cpp
    Source/transcribe/Harmony.cpp
    Source/transcribe/Resample.cpp
    Source/transcribe/ModelStore.cpp
    Source/transcribe/TranscriptionSession.cpp
    Source/transcribe/TranscriptionPlayer.cpp
    Source/transcribe/NoteFilter.cpp
    Source/scope/ScopeModel.cpp
    Source/scope/ScopeTheme.cpp
    Source/scope/ScopePage.cpp
    Source/monitor/MonitorModel.cpp
    Source/monitor/MonitorPage.cpp)

target_sources(Spectroscope PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/LoopbackCapture.cpp
    ${DCU_SHARED_SOURCES})

# Key finder model (OpenKeyScan KeyNet, MIT; exported by tools/export_keynet.py)
juce_add_binary_data(SpectroscopeData SOURCES Resources/keynet.bin)
target_link_libraries(Spectroscope PRIVATE SpectroscopeData)

# the network's matrix maths is the only hot loop that benefits from AVX2 / FMA
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")     # clang-cl: /arch:AVX2 alone doesn't turn on FMA
    set_source_files_properties(Source/key/KeyNet.cpp PROPERTIES COMPILE_OPTIONS "/O2;/arch:AVX2;/fp:fast;/clang:-mfma")
elseif(MSVC)
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
        dcu_transcribe
        juce::juce_cryptography
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
add_executable(harmony_test tests/harmony_test.cpp Source/transcribe/Harmony.cpp Source/transcribe/Resample.cpp Source/transcribe/NoteFilter.cpp)   # chords, melody, key, resampling
add_executable(monitor_test tests/monitor_test.cpp Source/monitor/MonitorModel.cpp Source/LoudnessMeter.cpp)   # the Monitor page's meters
add_executable(scope_test tests/scope_test.cpp Source/scope/ScopeModel.cpp)                  # the oscilloscope engine
add_executable(transcribe_test tests/transcribe_test.cpp Source/transcribe/Harmony.cpp Source/transcribe/Resample.cpp)  # the engine end to end
target_link_libraries(transcribe_test PRIVATE dcu_transcribe)
juce_add_console_app(player_test PRODUCT_NAME "player_test")       # the Key page's playback, without a sound card
target_sources(player_test PRIVATE tests/player_test.cpp Source/transcribe/TranscriptionPlayer.cpp)
target_compile_definitions(player_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(player_test PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
juce_generate_juce_header(player_test)

juce_add_console_app(target_loader_test)                                                    # reading target files
juce_generate_juce_header(target_loader_test)
target_sources(target_loader_test PRIVATE tests/target_loader_test.cpp Source/balance/TargetLibrary.cpp Source/balance/TonalAnalysis.cpp)
target_compile_definitions(target_loader_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(target_loader_test PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags)

# TargetBuilder: window app (and --cli mode) that makes tonal balance targets from songs
juce_add_gui_app(TargetBuilder
    PRODUCT_NAME "TargetBuilder"
    COMPANY_NAME "DigiCheck Universal"
    BUNDLE_ID "com.digicheckuniversal.targetbuilder"
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
