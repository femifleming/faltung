# ConvoSolution: part of the faltung bundle. JUCE, the VST2 headers and the faltung fonts and controls
# (Source/Ui.*, FaltungData) live at the top of the repository, so build it from there:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target ConvoSolution_All ECRTest
set(ECR_VERSION 1.1.0)
option(ECR_COPY_AFTER_BUILD "Install into the user plug-in folders after building" OFF)
if(NOT TARGET FaltungData)
    message(FATAL_ERROR "ConvoSolution builds as part of faltung: configure the repository root, not convosolution/")
endif()
set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux; VST2 everywhere (FST headers)
if(APPLE)
    set(ECR_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(ECR_FORMATS VST3 LV2 Standalone)
else()
    set(ECR_FORMATS VST3 Standalone)
endif()
if(FALTUNG_BUILD_VST2)
    list(APPEND ECR_FORMATS VST)
endif()

# the eight built-in responses (CC BY 4.0, OpenAIR; see Resources/IR-CREDITS.txt)
file(GLOB ECR_INTERNAL_IRS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/internal/*.flac")
juce_add_binary_data(ECRData HEADER_NAME EcrData.h NAMESPACE EcrData SOURCES ${ECR_INTERNAL_IRS})

add_library(ecr_pffft STATIC third_party/pffft/pffft.c)
target_include_directories(ecr_pffft PUBLIC third_party/pffft)
target_compile_definitions(ecr_pffft PRIVATE _USE_MATH_DEFINES=1)
set_target_properties(ecr_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ecr_pffft PRIVATE /W0)
else()
    target_compile_options(ecr_pffft PRIVATE -w)
endif()

set(ECR_UI ${BUNDLE_ROOT}/Source/Ui.cpp)
set(ECR_SOURCES
    Source/dsp/ConvEngine.cpp Source/dsp/IrData.cpp Source/Library.cpp
    Source/PluginProcessor.cpp Source/Ui.cpp Source/PluginEditor.cpp ${ECR_UI})
set(ECR_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
             JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
             JUCE_MODAL_LOOPS_PERMITTED=0 _USE_MATH_DEFINES=1)

juce_add_plugin(ConvoSolution
    VERSION ${ECR_VERSION}
    COMPANY_NAME "ConvoSolution"
    COMPANY_WEBSITE "https://github.com/femifleming/faltung"
    BUNDLE_ID "com.convosolution.ConvoSolution"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE CvSl
    FORMATS ${ECR_FORMATS}
    PRODUCT_NAME "ConvoSolution"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    DESCRIPTION "Dual true-stereo convolution reverb with LFO modulation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    VST2_CATEGORY kPlugCategRoomFx
    LV2URI "urn:convosolution:convosolution"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio input is processed by the reverb in the standalone app."
    COPY_PLUGIN_AFTER_BUILD ${ECR_COPY_AFTER_BUILD})
target_sources(ConvoSolution PRIVATE ${ECR_SOURCES})
target_include_directories(ConvoSolution PRIVATE Source third_party/font5x7)
target_compile_definitions(ConvoSolution PUBLIC ${ECR_DEFS})
if(MSVC)
    target_compile_options(ConvoSolution PRIVATE /utf-8)
endif()
target_link_libraries(ConvoSolution
    PRIVATE ECRData ecr_pffft FaltungData juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
if(FALTUNG_LTO)
    target_link_libraries(ConvoSolution PUBLIC juce::juce_recommended_lto_flags)
endif()

# tests: the convolution engine against a direct convolution, and the whole plug-in offline (+ screenshots)
add_executable(ECREngineTest tests/engine_test.cpp Source/dsp/ConvEngine.cpp)
target_include_directories(ECREngineTest PRIVATE Source)
target_link_libraries(ECREngineTest PRIVATE ecr_pffft)

juce_add_console_app(ECRTest PRODUCT_NAME "ECRTest" VERSION ${ECR_VERSION})
target_sources(ECRTest PRIVATE tests/ECRTest.cpp ${ECR_SOURCES})
target_include_directories(ECRTest PRIVATE Source third_party/font5x7)
target_compile_definitions(ECRTest PRIVATE ${ECR_DEFS} JucePlugin_Name="ConvoSolution" JucePlugin_VersionString="${ECR_VERSION}"
    JucePlugin_Build_Standalone=1 JUCE_STANDALONE_APPLICATION=1 ECR_TEST_BUILD=1
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
if(MSVC)
    target_compile_options(ECRTest PRIVATE /utf-8)
endif()
target_link_libraries(ECRTest PRIVATE ECRData ecr_pffft FaltungData juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
