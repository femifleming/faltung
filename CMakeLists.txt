cmake_minimum_required(VERSION 3.22)
# faltung — drums and processes for minimal / microhouse.
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target faltung_all faltungTest
# macOS universal: add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
project(Faltung VERSION 1.4.5 LANGUAGES C CXX)
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
# DEEP builds the "faltung deep" flavour (deep house sounds, drawn as faltung's dark mode) as faltung_deep_<short>.
function(faltung_plugin SHORT INDEX CODE KIND DESC)
    set(DEEP 0)
    if(ARGC GREATER 5 AND ARGV5 STREQUAL "DEEP")
        set(DEEP 1)
    endif()
    if(DEEP)
        set(T faltung_deep_${SHORT})
        set(PNAME "faltung deep ${SHORT}")
        set(BID "com.faltung.deep.${SHORT}")
        set(LV2 "urn:faltung:deep:${SHORT}")
    else()
        set(T faltung_${SHORT})
        set(PNAME "faltung ${SHORT}")
        set(BID "com.faltung.${SHORT}")
        set(LV2 "urn:faltung:${SHORT}")
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
        COMPANY_NAME "faltung"
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

# sndstudio: the daw (Source/daw). A standalone application only (no plug-in formats): its own audio callback reaches
# up to 128 outputs of the chosen interface, it hosts VST3 / AU plug-ins.
set(FAL_DAW_SOURCES Source/daw/Device.cpp Source/daw/Session.cpp Source/daw/Engine.cpp Source/daw/Project.cpp Source/daw/Arrange.cpp
    Source/daw/StudioProcessor.cpp Source/daw/StudioEditor.cpp Source/daw/devices/StudioDevices.cpp)
set(FAL_HOST_DEFS JUCE_PLUGINHOST_VST3=1)
if(APPLE)
    list(APPEND FAL_HOST_DEFS JUCE_PLUGINHOST_AU=1)
endif()
function(faltung_studio DEEP)
    if(DEEP)
        set(T faltung_deep_sndstudio)
        set(PNAME "faltung deep sndstudio")
        set(BID "com.faltung.deep.sndstudio")
        set(CODE Dsnd)
        set(DESC "deep house studio (standalone daw): tracks, scenes, song, sends, 128 outputs")
    else()
        set(T faltung_sndstudio)
        set(PNAME "faltung sndstudio")
        set(BID "com.faltung.sndstudio")
        set(CODE Fsnd)
        set(DESC "microhouse studio (standalone daw): tracks, scenes, song, sends, 128 outputs")
    endif()
    juce_add_plugin(${T}
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "faltung"
        BUNDLE_ID "${BID}"
        PLUGIN_MANUFACTURER_CODE Frkp
        PLUGIN_CODE ${CODE}
        FORMATS Standalone
        PRODUCT_NAME "${PNAME}"
        DESCRIPTION "${DESC}"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "sndstudio records from your audio inputs."
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(${T} PRIVATE ${FAL_SOURCES} ${FAL_DAW_SOURCES} Source/daw/StudioApp.cpp Source/daw/StudioEntry.cpp)
    target_compile_definitions(${T} PUBLIC ${FAL_DEFS} ${FAL_HOST_DEFS} FALTUNG_PRODUCT=18 FALTUNG_DEEP=${DEEP}
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_include_directories(${T} PRIVATE Source)
    target_link_libraries(${T}
        PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(APPLE)
        target_link_libraries(${T} PRIVATE "-framework CoreAudioKit")
    endif()
    if(FALTUNG_LTO)
        target_link_libraries(${T} PUBLIC juce::juce_recommended_lto_flags)
    endif()
    if(DEEP)
        set(FAL_DEEP_TARGETS ${FAL_DEEP_TARGETS} ${T}_All PARENT_SCOPE)
    else()
        set(FAL_ALL_TARGETS ${FAL_ALL_TARGETS} ${T}_All PARENT_SCOPE)
    endif()
endfunction()
faltung_studio(0)
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
    faltung_studio(1)
    add_custom_target(faltung_deep_all DEPENDS ${FAL_DEEP_TARGETS})
endif()

# Partials (additive / image resynthesis synth) lives in partials/ and builds with the bundle
add_subdirectory(partials)
add_dependencies(faltung_all Partials_All)
# Cloudfin (the floating multi-effect) lives in cloudfin/ and builds with the bundle
add_subdirectory(cloudfin)
add_dependencies(faltung_all Cloudfin_All)
# ConvoSolution (the dual convolution reverb) lives in convosolution/ and builds with the bundle
add_subdirectory(convosolution)
add_dependencies(faltung_all ConvoSolution_All)
# ObliqSound and ObliqSonic (the synth and its Ambisonics twin) live in obliqsound/; they need SuperCollider's
# server (obliqsound/sc/fetch_sc.sh) and are skipped with a warning without it
add_subdirectory(obliqsound)
if(TARGET ObliqSound)
    add_dependencies(faltung_all ObliqSound_All ObliqSonic_All)
endif()
# ballonwinkel and ballonpaleis (the faltung-series twins of Pinkie Pie's Balloon Shop and Pinkie Pie's Balloon
# Palace, which stay where they are) live in ballon/ and build with the bundle
add_subdirectory(ballon)
add_dependencies(faltung_all ballonwinkel_All ballonpaleis_All)
if(FALTUNG_BUILD_DEEP)
    add_dependencies(faltung_deep_all ballonwinkel_deep_All ballonpaleis_deep_All)
endif()

if(FALTUNG_BUILD_TESTS)
    juce_add_console_app(faltungTest PRODUCT_NAME "faltungTest")
    target_sources(faltungTest PRIVATE tests/Test.cpp ${FAL_SOURCES})
    target_include_directories(faltungTest PRIVATE Source)
    target_compile_definitions(faltungTest PRIVATE ${FAL_DEFS} JucePlugin_Name="faltung" JucePlugin_Build_Standalone=1
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_ManufacturerCode=0x46726b70 JucePlugin_PluginCode=0x466b7431 JucePlugin_VersionString="1.4.5"
        JucePlugin_Manufacturer="faltung" JucePlugin_Desc="faltung" JucePlugin_VersionCode=0x10200 FALTUNG_TEST=1)
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

    # the studio's own tests: renders projects offline (timing, locks, sends, outputs, automation, save / load, hosting)
    function(faltung_studio_test T DEEP)
        juce_add_console_app(${T} PRODUCT_NAME "${T}")
        target_sources(${T} PRIVATE tests/StudioTest.cpp ${FAL_SOURCES} ${FAL_DAW_SOURCES})
        target_include_directories(${T} PRIVATE Source)
        get_target_property(FT_DEFS faltungTest COMPILE_DEFINITIONS)
        list(FILTER FT_DEFS EXCLUDE REGEX "^FALTUNG_DEEP=")
        target_compile_definitions(${T} PRIVATE ${FT_DEFS} ${FAL_HOST_DEFS} FALTUNG_DEEP=${DEEP})
        if(APPLE)
            target_link_libraries(${T} PRIVATE "-framework CoreAudioKit")
        endif()
        if(MSVC)
            target_link_options(${T} PRIVATE /STACK:16777216)   # (the test's main holds many projects at once; Windows' default stack is 1 MB)
        endif()
        target_link_libraries(${T} PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endfunction()
    faltung_studio_test(sndstudioTest 0)
    if(FALTUNG_BUILD_DEEP)
        faltung_studio_test(sndstudioDeepTest 1)
    endif()
endif()

# Windows gives a program's main thread 1 MB of stack (macOS / Linux: 8 MB). The tests keep whole processors as
# locals of main(), which grew past it in 1.1.0: every test program gets the same 8 MB everywhere.
if(MSVC)
    foreach(T faltungTest faltungDeepTest faltungHostCheck sndstudioTest sndstudioDeepTest PartialsTest CloudfinTest CloudfinHostTest ECRTest ECREngineTest
              ObliqSoundTest ObliqSonicTest obliq_smoke
              ballonwinkelTest ballonwinkelDspTest ballonpaleisTest ballonpaleisDspTest ballonpaleisSphereTest)
        if(TARGET ${T})
            target_link_options(${T} PRIVATE /STACK:8388608)
        endif()
    endforeach()
elseif(MINGW)
    foreach(T faltungTest faltungDeepTest faltungHostCheck sndstudioTest sndstudioDeepTest PartialsTest CloudfinTest CloudfinHostTest ECRTest ECREngineTest
              ObliqSoundTest ObliqSonicTest obliq_smoke
              ballonwinkelTest ballonwinkelDspTest ballonpaleisTest ballonpaleisDspTest ballonpaleisSphereTest)
        if(TARGET ${T})
            target_link_options(${T} PRIVATE -Wl,--stack,8388608)
        endif()
    endforeach()
endif()
