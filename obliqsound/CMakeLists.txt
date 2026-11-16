# ObliqSound / ObliqSonic: part of the faltung bundle. JUCE, the VST2 headers and the faltung fonts and
# controls (Source/Ui.*, FaltungData) live at the top of the repository, so build it from there:
#   obliqsound/sc/fetch_sc.sh        (once: fetches SuperCollider 3.13 and builds its server as a library)
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target ObliqSound_All ObliqSonic_All ObliqTest
# The sound engine is scsynth, embedded. OBLIQ_SC_ROOT is the folder holding supercollider/ (source) and
# sc-build/ (the built server); without it these two plug-ins are skipped and the rest of the bundle builds.
set(OBLIQ_VERSION 2.3.0)
set(OBLIQ_SC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "folder with supercollider/ and sc-build/ (from sc/fetch_sc.sh)")
if(NOT TARGET FaltungData)
    message(FATAL_ERROR "ObliqSound builds as part of faltung: configure the repository root, not obliqsound/")
endif()
set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SC ${OBLIQ_SC_ROOT}/supercollider)
set(SCB ${OBLIQ_SC_ROOT}/sc-build)
if(NOT EXISTS "${SC}/include/plugin_interface/SC_PlugIn.h" OR NOT EXISTS "${SCB}")
    message(WARNING "ObliqSound / ObliqSonic skipped: SuperCollider's server is not built. Run obliqsound/sc/fetch_sc.sh (or set OBLIQ_SC_ROOT).")
    return()
endif()

set(SC_INCLUDES ${SCB}/common ${SC}/external_libraries ${SC}/external_libraries/nova-simd ${SC}/external_libraries/nova-tt
    ${SC}/external_libraries/boost ${SC}/include/common ${SC}/common ${SC}/include/server ${SC}/include/plugin_interface ${SC}/server/scsynth
    ${SC}/external_libraries/boost_sync/include ${SC}/external_libraries/TLSF-2.4.6/src)
set(SC_DEFS BOOST_CHRONO_HEADER_ONLY BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE NOVA_SIMD NO_LIBSNDFILE SC_MEMORY_ALIGNMENT=32)
if(APPLE)
    list(APPEND SC_DEFS SC_FFT_VDSP)
elseif(WIN32)
    list(APPEND SC_DEFS SC_FFT_GREEN WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0600 BOOST_ALL_NO_LIB _USE_MATH_DEFINES _ENABLE_ATOMIC_ALIGNMENT_FIX _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
else()
    list(APPEND SC_DEFS SC_FFT_GREEN SC_AUDIO_API=2)       # 2 = JACK in SC_CoreAudio.h; its back end is not linked
endif()
# the static libraries of the SuperCollider build, wherever the generator put them
function(sc_lib var)
    set(found "")
    foreach(pat ${ARGN})
        file(GLOB_RECURSE hit ${SCB}/${pat})
        if(hit)
            list(GET hit 0 found)
            break()
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "SuperCollider library not found (${ARGN}): run obliqsound/sc/fetch_sc.sh first")
    endif()
    set(${var} ${found} PARENT_SCOPE)
endfunction()
sc_lib(LIB_SCSYNTH server/scsynth/libscsynth.a server/scsynth/libscsynth.lib server/scsynth/*scsynth*.lib)
sc_lib(LIB_TLSF external_libraries/libtlsf.a external_libraries/tlsf.lib external_libraries/*tlsf*.lib)
sc_lib(LIB_BFS external_libraries/libboost_filesystem_lib.a external_libraries/boost_filesystem_lib.lib external_libraries/*boost_filesystem*.lib)
sc_lib(LIB_BSYS external_libraries/libboost_system_lib.a external_libraries/boost_system_lib.lib external_libraries/*boost_system*.lib)

add_library(scengine STATIC sc/ScEngine.cpp)
set_target_properties(scengine PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(scengine PRIVATE ${SC_DEFS})
target_compile_options(scengine PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
target_include_directories(scengine PRIVATE ${SC_INCLUDES})
target_link_libraries(scengine PUBLIC ${LIB_SCSYNTH} ${LIB_TLSF} ${LIB_BFS} ${LIB_BSYS})
if(APPLE)
    target_link_libraries(scengine PUBLIC "-framework CoreAudio" "-framework CoreServices" "-framework Accelerate" "-framework Foundation" "-framework AppKit" "-framework AudioToolbox")
elseif(WIN32)
    target_link_libraries(scengine PUBLIC ws2_32 winmm iphlpapi)
else()
    target_link_libraries(scengine PUBLIC pthread dl)
endif()

# ---- our UGens, as a server plug-in (.scx; .so on Linux, as the server expects there)
set(OBLIQ_SCPLUGINS ${CMAKE_CURRENT_BINARY_DIR}/scplugins)
add_library(OblqUGens MODULE sc/ugens/OblqUGens.cpp)
set_target_properties(OblqUGens PROPERTIES CXX_STANDARD 17 PREFIX "" CXX_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY ${OBLIQ_SCPLUGINS})
if(APPLE OR WIN32)
    set_target_properties(OblqUGens PROPERTIES SUFFIX ".scx")
endif()
target_compile_definitions(OblqUGens PRIVATE ${SC_DEFS})
target_compile_options(OblqUGens PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
target_include_directories(OblqUGens PRIVATE ${SC_INCLUDES})

# the stock UGens the SynthDefs use. macOS / Linux: built by fetch_sc.sh. Windows: SuperCollider's own UGen
# targets drag in its language interpreter there, so they are built here from its sources instead (one file each).
set(STOCK_UGENS BinaryOpUGens UnaryOpUGens MulAddUGens IOUGens OscUGens LFUGens NoiseUGens DynNoiseUGens FilterUGens DelayUGens
    PanUGens TriggerUGens ReverbUGens GrainUGens ChaosUGens DemandUGens PhysicalModelingUGens)
set(STOCK_FILES "")
set(STOCK_TARGETS "")
if(WIN32)
    foreach(p ${STOCK_UGENS})
        add_library(${p} MODULE ${SC}/server/plugins/${p}.cpp)
        set_target_properties(${p} PROPERTIES CXX_STANDARD 17 PREFIX "" SUFFIX ".scx" LIBRARY_OUTPUT_DIRECTORY ${OBLIQ_SCPLUGINS})
        target_compile_definitions(${p} PRIVATE ${SC_DEFS})
        target_compile_options(${p} PRIVATE /w /Zc:__cplusplus)
        target_include_directories(${p} PRIVATE ${SC_INCLUDES})
        target_link_libraries(${p} PRIVATE wsock32 ws2_32)
        list(APPEND STOCK_FILES $<TARGET_FILE:${p}>)
        list(APPEND STOCK_TARGETS ${p})
    endforeach()
else()
    if(APPLE)
        set(SCX scx)
    else()
        set(SCX so)
    endif()
    foreach(p ${STOCK_UGENS})
        if(NOT EXISTS "${SCB}/server/plugins/${p}.${SCX}")
            message(FATAL_ERROR "SuperCollider UGen ${p}.${SCX} missing in ${SCB}/server/plugins: run obliqsound/sc/fetch_sc.sh")
        endif()
        list(APPEND STOCK_FILES "${SCB}/server/plugins/${p}.${SCX}")
    endforeach()
endif()

# ---- engine smoke test: starts the server, loads the SynthDefs, plays 16 voices, checks the output
add_executable(obliq_smoke sc/voicetest.cpp)
set_target_properties(obliq_smoke PROPERTIES CXX_STANDARD 17)
target_link_libraries(obliq_smoke PRIVATE scengine)

# ---- what the plug-ins load at run time: server plug-ins, SynthDefs, the recordings, the layout, the presets, the manual.
# Staged once per product and copied into every built bundle (Contents/Resources, or a Resources folder beside the program).
function(obliq_resources NAME)
    set(R ${CMAKE_CURRENT_BINARY_DIR}/res/${NAME})
    add_custom_target(${NAME}Resources
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${R}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${R}/plugins" "${R}/defs" "${R}/samples"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/sc/defs-compiled" "${R}/defs"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/samples" "${R}/samples"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/plugin/Resources/${NAME}/layout.json"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/plugin/Resources/${NAME}/presets.json"
                                         "${CMAKE_CURRENT_SOURCE_DIR}/docs/manual/obliqsound Manual.pdf" "${R}/"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OblqUGens> ${STOCK_FILES} "${R}/plugins/"
        COMMENT "${NAME}: staging the run-time resources"
        VERBATIM)
    add_dependencies(${NAME}Resources OblqUGens ${STOCK_TARGETS})
endfunction()
# copies the staged resources into a built target. Bundles (every macOS format, every VST3) keep them in
# Contents/Resources; a bare program or library (Windows / Linux apps, LV2, VST2) in a Resources folder beside it.
function(obliq_give_resources T NAME IS_BUNDLE)
    set(R ${CMAKE_CURRENT_BINARY_DIR}/res/${NAME})
    if(IS_BUNDLE)
        set(DEST "$<TARGET_FILE_DIR:${T}>/../Resources")
    else()
        set(DEST "$<TARGET_FILE_DIR:${T}>/Resources")
    endif()
    add_custom_command(TARGET ${T} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${R}" "${DEST}"
        COMMENT "${T}: resources" VERBATIM)
    if(APPLE AND IS_BUNDLE)
        add_custom_command(TARGET ${T} POST_BUILD COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:${T}>" VERBATIM)
    endif()
    add_dependencies(${T} ${NAME}Resources)
endfunction()

if(APPLE)
    set(OBLIQ_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(OBLIQ_FORMATS VST3 LV2 Standalone)
else()
    set(OBLIQ_FORMATS VST3 Standalone)
endif()
if(FALTUNG_BUILD_VST2)
    list(APPEND OBLIQ_FORMATS VST)
endif()

set(OBLIQ_UI ${BUNDLE_ROOT}/Source/Ui.cpp)
set(OBLIQ_SOURCES plugin/Source/PluginProcessor.cpp plugin/Source/PluginEditor.cpp plugin/Source/Engine.cpp ${OBLIQ_UI})
set(OBLIQ_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0 JUCE_MODAL_LOOPS_PERMITTED=0 _USE_MATH_DEFINES=1)

function(obliq NAME SONIC CODE ICON DESC)
    juce_add_plugin(${NAME}
        VERSION ${OBLIQ_VERSION}
        COMPANY_NAME "ObliqSound"
        COMPANY_WEBSITE "https://github.com/femifleming/faltung"
        BUNDLE_ID com.obliqsound.${NAME}
        PRODUCT_NAME "${NAME}"
        DESCRIPTION "${DESC}"
        PLUGIN_MANUFACTURER_CODE Oblq
        PLUGIN_CODE ${CODE}
        FORMATS ${OBLIQ_FORMATS}
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/app/${ICON}"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Synth
        VST2_CATEGORY kPlugCategSynth
        LV2URI "urn:obliqsound:${NAME}"
        MICROPHONE_PERMISSION_ENABLED FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(${NAME} PRIVATE ${OBLIQ_SOURCES})
    target_include_directories(${NAME} PRIVATE plugin/Source)
    target_compile_definitions(${NAME} PUBLIC ${OBLIQ_DEFS} OBLIQ_SONIC=${SONIC})
    if(MSVC)
        target_compile_options(${NAME} PRIVATE /utf-8)
    endif()
    target_link_libraries(${NAME}
        PRIVATE scengine FaltungData juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
    if(FALTUNG_LTO)
        target_link_libraries(${NAME} PUBLIC juce::juce_recommended_lto_flags)
    endif()
    obliq_resources(${NAME})
    foreach(F ${OBLIQ_FORMATS})
        if(APPLE OR F STREQUAL "VST3")
            obliq_give_resources(${NAME}_${F} ${NAME} TRUE)
        else()
            obliq_give_resources(${NAME}_${F} ${NAME} FALSE)
        endif()
    endforeach()

    # the offline test: the real processor and editor without a window (sound, state, screenshots)
    juce_add_console_app(${NAME}Test PRODUCT_NAME "${NAME}Test")
    target_sources(${NAME}Test PRIVATE tests/Test.cpp ${OBLIQ_SOURCES})
    target_include_directories(${NAME}Test PRIVATE plugin/Source)
    target_compile_definitions(${NAME}Test PRIVATE ${OBLIQ_DEFS} OBLIQ_SONIC=${SONIC} JucePlugin_Name="${NAME}" JucePlugin_Build_Standalone=0
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_ManufacturerCode=0x4f626c71 JucePlugin_VersionString="${OBLIQ_VERSION}" JucePlugin_Manufacturer="ObliqSound"
        JucePlugin_Desc="${DESC}" OBLIQ_TEST=1)
    if(MSVC)
        target_compile_options(${NAME}Test PRIVATE /utf-8)
    endif()
    target_link_libraries(${NAME}Test PRIVATE scengine FaltungData juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
    obliq_give_resources(${NAME}Test ${NAME} FALSE)
endfunction()

obliq(ObliqSound 0 Obsd icon.png "Polyphonic synthesizer and field-recording instrument")
obliq(ObliqSonic 1 Obsc icon_sonic.png "ObliqSound as a third-order Ambisonics sound source")
add_custom_target(obliq_all DEPENDS ObliqSound_All ObliqSonic_All)
