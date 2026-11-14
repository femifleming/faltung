cmake_minimum_required(VERSION 3.22)
# Part of the faltung bundle: JUCE and the VST2 headers live at the top of the repo.
# Builds on its own too: cmake -S partials -B build-partials
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
project(Partials VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(PARTIALS_COPY_AFTER_BUILD "Install into the user plug-in folders after building" OFF)

option(PARTIALS_BUILD_VST2 "Also build the VST2-compatible plug-in (uses the FST headers)" ON)

set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT COMMAND juce_add_plugin)
    if(NOT EXISTS "${BUNDLE_ROOT}/JUCE/CMakeLists.txt")
        message(FATAL_ERROR "JUCE is missing - run: git submodule update --init")
    endif()
    add_subdirectory("${BUNDLE_ROOT}/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
endif()

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux; VST2 everywhere
if(APPLE)
    set(PT_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(PT_FORMATS VST3 LV2 Standalone)
else()
    set(PT_FORMATS VST3 Standalone)
endif()
if(PARTIALS_BUILD_VST2)
    # FST: clean-room, GPL-licensed headers for the VST2 plug-in interface
    if(NOT TARGET juce_vst2_sdk)
        juce_set_vst2_sdk_path("${BUNDLE_ROOT}/third_party/vst2-fst")
    endif()
    list(APPEND PT_FORMATS VST)
endif()

juce_add_plugin(Partials
    VERSION 1.0.0
    COMPANY_NAME "FreakiePie"
    BUNDLE_ID "com.freakiepie.partials"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Prtl
    FORMATS ${PT_FORMATS}
    PRODUCT_NAME "Partials"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    DESCRIPTION "Additive / subtractive synthesizer with image resynthesis"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    VST2_CATEGORY kPlugCategSynth
    LV2URI "urn:freakiepie:partials"
    COPY_PLUGIN_AFTER_BUILD ${PARTIALS_COPY_AFTER_BUILD})

juce_add_binary_data(PartialsFonts SOURCES
    Resources/fonts/Inter-Regular.ttf Resources/fonts/Inter-Medium.ttf
    Resources/fonts/IBMPlexMono-Regular.ttf Resources/fonts/IBMPlexMono-Medium.ttf)
target_sources(Partials PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(PT_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=0 _USE_MATH_DEFINES=1)
target_compile_definitions(Partials PUBLIC ${PT_DEFS})
target_link_libraries(Partials
    PRIVATE PartialsFonts juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

juce_add_console_app(PartialsTest PRODUCT_NAME "PartialsTest")
target_sources(PartialsTest PRIVATE tests/Test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(PartialsTest PRIVATE ${PT_DEFS} JucePlugin_Name="Partials" JucePlugin_Build_Standalone=1
    JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
    JucePlugin_ManufacturerCode=0x46726b70 JucePlugin_PluginCode=0x5072746c JucePlugin_VersionString="1.0.0"
    JucePlugin_Manufacturer="FreakiePie" JucePlugin_Desc="Partials" JucePlugin_VersionCode=0x10000 PARTIALS_TEST=1)
target_link_libraries(PartialsTest PRIVATE PartialsFonts juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
