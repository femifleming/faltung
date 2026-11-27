# umleitung — the engine, the app and the engine's checks (included from the root CMakeLists.txt)

set(UM_ENGINE_SOURCES Source/engine/Engine.cpp Source/engine/PluginHost.cpp)
set(UM_JUCE_DEFS JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_LADSPA=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)

# ---- the engine's checks (offline: no devices) ----
juce_add_console_app(umleitung_engine_test PRODUCT_NAME "umleitung_engine_test")
target_sources(umleitung_engine_test PRIVATE tests/EngineTest.cpp ${UM_ENGINE_SOURCES})
target_compile_definitions(umleitung_engine_test PRIVATE ${UM_JUCE_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(umleitung_engine_test PRIVATE umleitung_platform
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_data_structures juce::juce_gui_basics
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# ---- the app ----
file(GLOB UM_UI_SOURCES CONFIGURE_DEPENDS Source/ui/*.cpp)
if(UM_UI_SOURCES)
    juce_add_gui_app(umleitung
        PRODUCT_NAME "umleitung"
        COMPANY_NAME "faltung"
        BUNDLE_ID "com.faltung.umleitung"
        VERSION ${PROJECT_VERSION}
        ICON_BIG "${CMAKE_CURRENT_LIST_DIR}/../Resources/icon.png"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "umleitung records and routes what your audio inputs bring in."
        PLIST_TO_MERGE "<plist><dict><key>NSAudioCaptureUsageDescription</key><string>umleitung captures the sound of the apps you choose (or the whole mac) to route, process and record it.</string><key>LSApplicationCategoryType</key><string>public.app-category.music</string></dict></plist>")
    target_sources(umleitung PRIVATE Source/Main.cpp ${UM_UI_SOURCES} ${UM_ENGINE_SOURCES})
    target_include_directories(umleitung PRIVATE Source)
    target_compile_definitions(umleitung PRIVATE ${UM_JUCE_DEFS} JUCE_APPLICATION_NAME_STRING="umleitung" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(umleitung PRIVATE umleitung_platform
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_data_structures juce::juce_gui_extra juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
    faltung_ui_add(umleitung)
    # the driver goes inside the app (Resources), so the app can install it
    if(TARGET umleitung_driver)
        add_dependencies(umleitung umleitung_driver)
        add_custom_command(TARGET umleitung POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:umleitung>/Contents/Resources/umleitung.driver"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:umleitung_driver>" "$<TARGET_BUNDLE_DIR:umleitung>/Contents/Resources/umleitung.driver"
            COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:umleitung>"
            VERBATIM)
    endif()
endif()

# ---- the engine on this mac's real CoreAudio (BlackHole as a silent loopback) ----
juce_add_console_app(umleitung_live_check PRODUCT_NAME "umleitung_live_check")
target_sources(umleitung_live_check PRIVATE tests/LiveCheck.cpp ${UM_ENGINE_SOURCES})
target_compile_definitions(umleitung_live_check PRIVATE ${UM_JUCE_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    UM_TONE_PATH="$<IF:$<TARGET_EXISTS:umleitung_host_check_tone>,$<TARGET_FILE:umleitung_host_check_tone>,>")
target_link_libraries(umleitung_live_check PRIVATE umleitung_platform
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_data_structures juce::juce_gui_basics
    juce::juce_recommended_config_flags)
