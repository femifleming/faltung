# Cloudfin: part of the faltung bundle. JUCE, the VST2 headers and the faltung fonts and controls
# (Source/Ui.*, FaltungData) live at the top of the repository, so build it from there:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Cloudfin_All
set(CF_VERSION 1.3.0)
option(CLOUDFIN_COPY_AFTER_BUILD "Install into the user plug-in folders after building" OFF)
if(NOT TARGET FaltungData)
    message(FATAL_ERROR "Cloudfin builds as part of faltung: configure the repository root, not cloudfin/")
endif()
set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux; VST2 everywhere (FST headers)
if(APPLE)
    set(CF_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(CF_FORMATS VST3 LV2 Standalone)
else()
    set(CF_FORMATS VST3 Standalone)
endif()
if(FALTUNG_BUILD_VST2)
    list(APPEND CF_FORMATS VST)
endif()

juce_add_plugin(Cloudfin
    VERSION ${CF_VERSION}
    COMPANY_NAME "Cloudfin"
    COMPANY_WEBSITE "https://github.com/femifleming/faltung"
    BUNDLE_ID "com.cloudfin.cloudfin"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Cldf
    FORMATS ${CF_FORMATS}
    PRODUCT_NAME "Cloudfin"
    DESCRIPTION "A dreamy, floating multi-effect in any order"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    LV2URI "https://github.com/femifleming/Cloudfin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    VST2_CATEGORY kPlugCategEffect
    COPY_PLUGIN_AFTER_BUILD ${CLOUDFIN_COPY_AFTER_BUILD})

set(CF_UI ${BUNDLE_ROOT}/Source/Ui.cpp)
target_sources(Cloudfin PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp ${CF_UI})
set(CF_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 _USE_MATH_DEFINES=1)
target_compile_definitions(Cloudfin PUBLIC ${CF_DEFS})
if(MSVC)
    target_compile_options(Cloudfin PRIVATE /utf-8)
endif()

juce_add_binary_data(CloudfinAssets HEADER_NAME CloudfinData.h NAMESPACE CloudfinData SOURCES Resources/hydrophone.ogg)

target_link_libraries(Cloudfin
    PRIVATE
        CloudfinAssets
        FaltungData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# tests: the dsp offline, and the real processor + editor headless (screenshots, symmetry, frame cost)
juce_add_console_app(CloudfinTest PRODUCT_NAME "CloudfinTest")
target_sources(CloudfinTest PRIVATE tests/OfflineTest.cpp)
target_compile_definitions(CloudfinTest PRIVATE ${CF_DEFS})
target_link_libraries(CloudfinTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(CloudfinHostTest PRODUCT_NAME "CloudfinHostTest")   # (console: a plain main() everywhere, Windows included)
target_sources(CloudfinHostTest PRIVATE tests/HostTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp ${CF_UI})
target_compile_definitions(CloudfinHostTest PRIVATE ${CF_DEFS})
if(MSVC)
    target_compile_options(CloudfinHostTest PRIVATE /utf-8)
endif()
target_link_libraries(CloudfinHostTest PRIVATE CloudfinAssets FaltungData juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
