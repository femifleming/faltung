cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    endif()
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # no VC++ redistributable needed

project(ConvoSolution VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ECR_COPY_AFTER_BUILD "Install the plug-ins into your user plug-in folders after building" OFF)
option(ECR_BUILD_TESTS "Build the offline test tool (ECRTest)" ON)
option(ECR_BUILD_VST2 "Also build the VST2-compatible plug-in (uses the FST headers)" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing - run: git submodule update --init")
endif()
add_subdirectory(JUCE)

if(APPLE)
    set(ECR_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(ECR_FORMATS VST3 LV2 Standalone)
else()
    set(ECR_FORMATS VST3 Standalone)
endif()
if(ECR_BUILD_VST2)
    # FST: clean-room, GPL-licensed headers for the VST2 plug-in interface
    juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2-fst")
    list(APPEND ECR_FORMATS VST)
endif()

# built-in responses and the page fonts (Liberation: metric-compatible with Times and Arial)
file(GLOB ECR_INTERNAL_IRS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/internal/*.flac")
juce_add_binary_data(ECRData SOURCES ${ECR_INTERNAL_IRS}
    Resources/fonts/LiberationSerif-Regular.ttf Resources/fonts/LiberationSerif-Bold.ttf
    Resources/fonts/LiberationSerif-Italic.ttf Resources/fonts/LiberationSans-Regular.ttf)

add_library(ecr_pffft STATIC third_party/pffft/pffft.c)
target_include_directories(ecr_pffft PUBLIC third_party/pffft)
target_compile_definitions(ecr_pffft PRIVATE _USE_MATH_DEFINES=1)
if(MSVC)
    target_compile_options(ecr_pffft PRIVATE /W0)
else()
    target_compile_options(ecr_pffft PRIVATE -w)
endif()

set(ECR_SOURCES
    Source/dsp/ConvEngine.cpp Source/dsp/IrData.cpp Source/Library.cpp
    Source/PluginProcessor.cpp Source/Ui.cpp Source/PluginEditor.cpp)
set(ECR_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
             JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
             _USE_MATH_DEFINES=1)

juce_add_plugin(ConvoSolution
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "FreakiePie"
    BUNDLE_ID "com.freakiepie.ConvoSolution"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE CvSl
    FORMATS ${ECR_FORMATS}
    PRODUCT_NAME "ConvoSolution"
    DESCRIPTION "Dual true-stereo convolution reverb with LFO modulation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    VST2_CATEGORY kPlugCategRoomFx
    LV2URI "urn:freakiepie:convosolution"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio input is processed by the reverb in the standalone app."
    COPY_PLUGIN_AFTER_BUILD ${ECR_COPY_AFTER_BUILD})
target_sources(ConvoSolution PRIVATE ${ECR_SOURCES})
target_include_directories(ConvoSolution PRIVATE Source third_party/font5x7)
target_compile_definitions(ConvoSolution PUBLIC ${ECR_DEFS})
target_link_libraries(ConvoSolution
    PRIVATE ECRData ecr_pffft juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

if(ECR_BUILD_TESTS)
    add_executable(ECREngineTest tests/engine_test.cpp Source/dsp/ConvEngine.cpp)
    target_link_libraries(ECREngineTest PRIVATE ecr_pffft)

    juce_add_console_app(ECRTest PRODUCT_NAME "ECRTest" VERSION ${PROJECT_VERSION})
    target_sources(ECRTest PRIVATE tests/ECRTest.cpp ${ECR_SOURCES})
    target_include_directories(ECRTest PRIVATE Source third_party/font5x7)
    target_compile_definitions(ECRTest PRIVATE ${ECR_DEFS} JucePlugin_Name="ConvoSolution" JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_Build_Standalone=1 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 ECR_TEST_BUILD=1
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_LV2=1 JUCE_PLUGINHOST_VST=$<BOOL:${ECR_BUILD_VST2}>)
    target_include_directories(ECRTest PRIVATE third_party/vst2-fst)
    target_link_libraries(ECRTest PRIVATE ECRData ecr_pffft juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
