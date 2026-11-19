# ballonwinkel and ballonpaleis: the faltung-series twins of Pinkie Pie's Balloon Shop and Pinkie Pie's Balloon
# Palace (github.com/femifleming/pinkiepiesballoonshop, which stays as it is). The same engine, the same
# controls, played as balloons, drawn the faltung way. JUCE, the VST2 headers and the faltung fonts and controls
# (Source/Ui.*, FaltungData) live at the top of the repository, so build them from there:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target ballonwinkel_All ballonpaleis_All ballonwinkelTest ballonpaleisTest
set(BAL_VERSION 1.0.0)
option(BALLON_COPY_AFTER_BUILD "Install into the user plug-in folders after building" OFF)
if(NOT TARGET FaltungData)
    message(FATAL_ERROR "ballonwinkel and ballonpaleis build as part of faltung: configure the repository root, not ballon/")
endif()
set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux; VST2 everywhere (FST headers)
if(APPLE)
    set(BAL_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(BAL_FORMATS VST3 LV2 Standalone)
else()
    set(BAL_FORMATS VST3 Standalone)
endif()
if(FALTUNG_BUILD_VST2)
    list(APPEND BAL_FORMATS VST)
endif()

set(BAL_UI ${BUNDLE_ROOT}/Source/Ui.cpp)
set(BAL_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
             JUCE_MODAL_LOOPS_PERMITTED=0 _USE_MATH_DEFINES=1)

# ballon_plugin(<target> <folder> <name> <code> <bundle id> <lv2 uri> <description> <vst3 categories> <vst2 category> <product index> <dark 0|1>)
# The dark twins (… deep) are the same plug-ins on the dark counterparts of their colour fields, as faltung deep
# is of faltung: their own names, codes and bundle ids, so both can be installed side by side.
function(ballon_plugin T DIR PNAME CODE BID LV2 DESC V3CAT V2CAT INDEX DARK)
    juce_add_plugin(${T}
        VERSION ${BAL_VERSION}
        COMPANY_NAME "FreakiePie"
        COMPANY_WEBSITE "https://github.com/femifleming/faltung"
        BUNDLE_ID "${BID}"
        PLUGIN_MANUFACTURER_CODE Frkp
        PLUGIN_CODE ${CODE}
        FORMATS ${BAL_FORMATS}
        PRODUCT_NAME "${PNAME}"
        DESCRIPTION "${DESC}"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/${DIR}_1024.png"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES ${V3CAT}
        VST2_CATEGORY ${V2CAT}
        LV2URI "${LV2}"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Audio input is processed by the effects in the standalone app."
        COPY_PLUGIN_AFTER_BUILD ${BALLON_COPY_AFTER_BUILD})
    target_sources(${T} PRIVATE ${DIR}/Source/PluginProcessor.cpp ${DIR}/Source/Presets.cpp ${DIR}/Source/PluginEditor.cpp ${BAL_UI})
    target_include_directories(${T} PRIVATE ${DIR}/Source ${CMAKE_CURRENT_SOURCE_DIR} ${BUNDLE_ROOT}/Source)
    target_compile_definitions(${T} PUBLIC ${BAL_DEFS} BALLON_PRODUCT=${INDEX} FALTUNG_DEEP=${DARK} BALLON_NAME="${PNAME}")
    if(MSVC)
        target_compile_options(${T} PRIVATE /utf-8 /bigobj)
    endif()
    target_link_libraries(${T}
        PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
    if(FALTUNG_LTO)
        target_link_libraries(${T} PUBLIC juce::juce_recommended_lto_flags)
    endif()
endfunction()

ballon_plugin(ballonwinkel winkel "ballonwinkel" Fbwk "com.freakiepie.faltung.ballonwinkel" "urn:freakiepie:faltung:ballonwinkel"
              "A multi-effect played as balloons" "Fx" kPlugCategEffect 0 0)
ballon_plugin(ballonpaleis paleis "ballonpaleis" Fbpl "com.freakiepie.faltung.ballonpaleis" "urn:freakiepie:faltung:ballonpaleis"
              "A spatial multi-effect played as balloons in a sphere (an Ambisonics encoder, 1st to 7th order)" "Fx;Spatial" kPlugCategSpacializer 1 0)
if(FALTUNG_BUILD_DEEP)
    ballon_plugin(ballonwinkel_deep winkel "ballonwinkel deep" Dbwk "com.freakiepie.faltungdeep.ballonwinkel" "urn:freakiepie:faltungdeep:ballonwinkel"
                  "A multi-effect played as balloons (dark)" "Fx" kPlugCategEffect 0 1)
    ballon_plugin(ballonpaleis_deep paleis "ballonpaleis deep" Dbpl "com.freakiepie.faltungdeep.ballonpaleis" "urn:freakiepie:faltungdeep:ballonpaleis"
                  "A spatial multi-effect played as balloons in a sphere (dark)" "Fx;Spatial" kPlugCategSpacializer 1 1)
endif()

#==============================================================================
# tests
#   ballon<…>DspTest   the engine by itself (the originals' offline checks, unchanged)
#   ballon<…>Test      the plug-in headless: against the original's own processor (tests/original/, compiled in
#                      beside it: the same script through both must null), parameters, layouts, state, every page
#                      of the window; --params prints the parameter table, --screenshots <dir> saves the pictures
#==============================================================================
option(BALLON_TEST_NO_EDITOR "Build ballon<…>Test without the window (engine, parameter, layout and state checks only)" OFF)
if(FALTUNG_BUILD_TESTS)
    function(ballon_tests NAME DIR INDEX)
        juce_add_console_app(${NAME}DspTest PRODUCT_NAME "${NAME}DspTest")
        target_sources(${NAME}DspTest PRIVATE ${DIR}/tests/OfflineTest.cpp)
        target_compile_definitions(${NAME}DspTest PRIVATE ${BAL_DEFS})
        target_link_libraries(${NAME}DspTest PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

        juce_add_console_app(${NAME}Test PRODUCT_NAME "${NAME}Test")
        target_sources(${NAME}Test PRIVATE tests/BallonTest.cpp ${DIR}/Source/PluginProcessor.cpp ${DIR}/Source/Presets.cpp)
        if(BALLON_TEST_NO_EDITOR)
            target_compile_definitions(${NAME}Test PRIVATE BALLON_TEST_NO_EDITOR=1)
        else()
            target_sources(${NAME}Test PRIVATE ${DIR}/Source/PluginEditor.cpp ${BAL_UI})
        endif()
        target_include_directories(${NAME}Test PRIVATE ${DIR}/Source ${CMAKE_CURRENT_SOURCE_DIR} ${BUNDLE_ROOT}/Source
                                                       ${CMAKE_CURRENT_SOURCE_DIR}/tests/original/${DIR})
        target_compile_definitions(${NAME}Test PRIVATE ${BAL_DEFS} BALLON_PRODUCT=${INDEX} BALLON_NAME="${NAME}" BALLON_TEST=1
            JucePlugin_Name="${NAME}" JucePlugin_VersionString="${BAL_VERSION}" JucePlugin_Build_Standalone=1 JUCE_STANDALONE_APPLICATION=1)
        if(MSVC)
            target_compile_options(${NAME}Test PRIVATE /utf-8 /bigobj)
        endif()
        target_link_libraries(${NAME}Test PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
            PUBLIC juce::juce_recommended_config_flags)
    endfunction()
    ballon_tests(ballonwinkel winkel 0)
    ballon_tests(ballonpaleis paleis 1)
    # the sphere's own checks (Ambisonics, flights, distance), from the Balloon Palace
    juce_add_console_app(ballonpaleisSphereTest PRODUCT_NAME "ballonpaleisSphereTest")
    target_sources(ballonpaleisSphereTest PRIVATE paleis/tests/SphereTest.cpp paleis/Source/PluginProcessor.cpp paleis/Source/Presets.cpp
                                                  paleis/Source/PluginEditor.cpp ${BAL_UI})
    target_include_directories(ballonpaleisSphereTest PRIVATE paleis/Source ${CMAKE_CURRENT_SOURCE_DIR} ${BUNDLE_ROOT}/Source)
    target_compile_definitions(ballonpaleisSphereTest PRIVATE ${BAL_DEFS} BALLON_PRODUCT=1 BALLON_NAME="ballonpaleis" BALLON_TEST=1
        JucePlugin_Name="ballonpaleis" JucePlugin_VersionString="${BAL_VERSION}" JucePlugin_Build_Standalone=1 JUCE_STANDALONE_APPLICATION=1)
    if(MSVC)
        target_compile_options(ballonpaleisSphereTest PRIVATE /utf-8 /bigobj)
    endif()
    target_link_libraries(ballonpaleisSphereTest PRIVATE FaltungData juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()
