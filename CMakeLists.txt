cmake_minimum_required(VERSION 3.22)
# faltung — drums and processes for minimal / microhouse.
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target faltung_all faltungTest
# macOS universal: add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
project(Faltung VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(FALTUNG_COPY_AFTER_BUILD "Install into the user plug-in folders after building" OFF)
option(FALTUNG_BUILD_VST2 "Also build VST2 (FST headers)" ON)
option(FALTUNG_BUILD_TESTS "Build the offline test tool" ON)
option(FALTUNG_LTO "Link-time optimisation for the plug-ins (slow links; CI turns it off)" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing - run: git submodule update --init")
endif()
if(MSVC)
    add_compile_options(/utf-8)   # sources contain UTF-8 text (· – …)
endif()
add_subdirectory(JUCE)

if(APPLE)
    set(FAL_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(FAL_FORMATS VST3 LV2 Standalone)
else()
    set(FAL_FORMATS VST3 Standalone)
endif()
if(FALTUNG_BUILD_VST2)
    juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2-fst")
    list(APPEND FAL_FORMATS VST)
endif()

juce_add_binary_data(FaltungData SOURCES
    Resources/fonts/Inter-Regular.ttf Resources/fonts/Inter-Medium.ttf
    Resources/fonts/IBMPlexMono-Regular.ttf Resources/fonts/IBMPlexMono-Medium.ttf
    Resources/fonts/BarlowCondensed-Regular.ttf Resources/fonts/BarlowCondensed-Medium.ttf
    Resources/fonts/Barlow-Light.ttf Resources/fonts/Barlow-Regular.ttf)

set(FAL_SOURCES Source/Products.cpp Source/Processor.cpp Source/Ui.cpp Source/Art.cpp Source/Editor.cpp)
set(FAL_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0 _USE_MATH_DEFINES=1 JUCE_USE_FLAC=1)
set(FAL_ALL_TARGETS "")

# faltung_plugin(<short> <index> <4-char code> <kind: machine|module|effect> <description> [DEEP])
# DEEP builds the "faltung deep" flavour (deep house sounds, mirror look) as faltung_deep_<short>.
function(faltung_plugin SHORT INDEX CODE KIND DESC)
    set(DEEP 0)
    if(ARGC GREATER 5 AND ARGV5 STREQUAL "DEEP")
        set(DEEP 1)
    endif()
    if(DEEP)
        set(T faltung_deep_${SHORT})
        set(PNAME "faltung deep ${SHORT}")
        set(BID "com.freakiepie.faltungdeep.${SHORT}")
        set(LV2 "urn:freakiepie:faltungdeep:${SHORT}")
    else()
        set(T faltung_${SHORT})
        set(PNAME "faltung ${SHORT}")
        set(BID "com.freakiepie.faltung.${SHORT}")
        set(LV2 "urn:freakiepie:faltung:${SHORT}")
    endif()
    if(KIND STREQUAL "effect")
        set(SYNTH FALSE)
        set(MIDI_IN FALSE)
        set(AUTYPE kAudioUnitType_Effect)
        set(V3CAT Fx)
        set(V2CAT kPlugCategEffect)
    else()
        set(SYNTH TRUE)
        set(MIDI_IN TRUE)
        set(AUTYPE kAudioUnitType_MusicDevice)
        set(V3CAT Instrument Drum)
        set(V2CAT kPlugCategSynth)
    endif()
    juce_add_plugin(${T}
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "FreakiePie"
        BUNDLE_ID "${BID}"
        PLUGIN_MANUFACTURER_CODE Frkp
        PLUGIN_CODE ${CODE}
        FORMATS ${FAL_FORMATS}
        PRODUCT_NAME "${PNAME}"
        DESCRIPTION "${DESC}"
        IS_SYNTH ${SYNTH}
        NEEDS_MIDI_INPUT ${MIDI_IN}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE ${AUTYPE}
        VST3_CATEGORIES ${V3CAT}
        VST2_CATEGORY ${V2CAT}
        LV2URI "${LV2}"
        COPY_PLUGIN_AFTER_BUILD ${FALTUNG_COPY_AFTER_BUILD})
    target_sources(${T} PRIVATE ${FAL_SOURCES} Source/Entry.cpp)
    target_compile_definitions(${T} PUBLIC ${FAL_DEFS} FALTUNG_PRODUCT=${INDEX} FALTUNG_DEEP=${DEEP})
    target_include_directories(${T} PRIVATE Source)
    target_link_libraries(${T}
        PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(FALTUNG_LTO)
        target_link_libraries(${T} PUBLIC juce::juce_recommended_lto_flags)
    endif()
    if(DEEP)
        set(FAL_DEEP_TARGETS ${FAL_DEEP_TARGETS} ${T}_All PARENT_SCOPE)
    else()
        set(FAL_ALL_TARGETS ${FAL_ALL_TARGETS} ${T}_All PARENT_SCOPE)
    endif()
endfunction()

faltung_plugin(kt1  0 Fkt1 machine "eight-voice drum synth + microtime sequencer")
faltung_plugin(kt2  1 Fkt2 machine "loop slicer + sequencer")
faltung_plugin(bd1  2 Fbd1 module  "kick")
faltung_plugin(sd2  3 Fsd2 module  "snare")
faltung_plugin(rm3  4 Frm3 module  "rim")
faltung_plugin(hh4  5 Fhh4 module  "hats")
faltung_plugin(cp6  6 Fcp6 module  "clap")
faltung_plugin(pc7  7 Fpc7 module  "perc")
faltung_plugin(cl8  8 Fcl8 module  "click")
faltung_plugin(tm9  9 Ftm9 module  "tom")
faltung_plugin(mc3 10 Fmc3 effect  "micro-sampler")
faltung_plugin(dl4 11 Fdl4 effect  "dub delay")
faltung_plugin(rv5 12 Frv5 effect  "room")
faltung_plugin(sw6 13 Fsw6 effect  "swing")
faltung_plugin(fl7 14 Ffl7 effect  "filter")
faltung_plugin(gl8 15 Fgl8 effect  "glue")
faltung_plugin(wb1 16 Fwb1 module  "dubstep + neuro bass synth")
faltung_plugin(ms0 17 Fms0 effect  "mastering bus")
add_custom_target(faltung_all DEPENDS ${FAL_ALL_TARGETS})

# faltung deep: the deep house suite (everything except the mastering bus and Partials)
option(FALTUNG_BUILD_DEEP "Also build the faltung deep suite" ON)
if(FALTUNG_BUILD_DEEP)
    faltung_plugin(kt1  0 Dkt1 machine "deep house drum machine" DEEP)
    faltung_plugin(kt2  1 Dkt2 machine "loop slicer" DEEP)
    faltung_plugin(bd1  2 Dbd1 module  "kick" DEEP)
    faltung_plugin(sd2  3 Dsd2 module  "snare" DEEP)
    faltung_plugin(rm3  4 Drm3 module  "rim" DEEP)
    faltung_plugin(hh4  5 Dhh4 module  "hats" DEEP)
    faltung_plugin(cp6  6 Dcp6 module  "clap" DEEP)
    faltung_plugin(pc7  7 Dpc7 module  "perc" DEEP)
    faltung_plugin(cl8  8 Dcl8 module  "dust" DEEP)
    faltung_plugin(tm9  9 Dtm9 module  "tom" DEEP)
    faltung_plugin(mc3 10 Dmc3 effect  "micro" DEEP)
    faltung_plugin(dl4 11 Ddl4 effect  "dub" DEEP)
    faltung_plugin(rv5 12 Drv5 effect  "room" DEEP)
    faltung_plugin(sw6 13 Dsw6 effect  "swing" DEEP)
    faltung_plugin(fl7 14 Dfl7 effect  "filter" DEEP)
    faltung_plugin(gl8 15 Dgl8 effect  "glue" DEEP)
    faltung_plugin(sb1 16 Dsb1 module  "chord stabs and bass" DEEP)
    add_custom_target(faltung_deep_all DEPENDS ${FAL_DEEP_TARGETS})
endif()

# Partials (additive / image resynthesis synth) lives in partials/ and builds with the bundle
add_subdirectory(partials)
add_dependencies(faltung_all Partials_All)
# Cloudfin (the floating multi-effect) lives in cloudfin/ and builds with the bundle
add_subdirectory(cloudfin)
add_dependencies(faltung_all Cloudfin_All)

if(FALTUNG_BUILD_TESTS)
    juce_add_console_app(faltungTest PRODUCT_NAME "faltungTest")
    target_sources(faltungTest PRIVATE tests/Test.cpp ${FAL_SOURCES})
    target_include_directories(faltungTest PRIVATE Source)
    target_compile_definitions(faltungTest PRIVATE ${FAL_DEFS} JucePlugin_Name="faltung" JucePlugin_Build_Standalone=1
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_ManufacturerCode=0x46726b70 JucePlugin_PluginCode=0x466b7431 JucePlugin_VersionString="1.0.0"
        JucePlugin_Manufacturer="FreakiePie" JucePlugin_Desc="faltung" JucePlugin_VersionCode=0x10000 FALTUNG_TEST=1)
    target_link_libraries(faltungTest PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    # loads the built plug-ins like a DAW and checks their output (mac: VST3 + AU)
    juce_add_console_app(faltungHostCheck PRODUCT_NAME "faltungHostCheck")
    target_sources(faltungHostCheck PRIVATE tests/HostCheck.cpp)
    target_compile_definitions(faltungHostCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(APPLE)
        target_link_libraries(faltungHostCheck PRIVATE "-framework CoreAudioKit")
    endif()
    target_link_libraries(faltungHostCheck PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
    if(FALTUNG_BUILD_DEEP)
        juce_add_console_app(faltungDeepTest PRODUCT_NAME "faltungDeepTest")
        target_sources(faltungDeepTest PRIVATE tests/Test.cpp ${FAL_SOURCES})
        target_include_directories(faltungDeepTest PRIVATE Source)
        get_target_property(FT_DEFS faltungTest COMPILE_DEFINITIONS)
        target_compile_definitions(faltungDeepTest PRIVATE ${FT_DEFS} FALTUNG_DEEP=1)
        target_link_libraries(faltungDeepTest PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endif()
endif()
