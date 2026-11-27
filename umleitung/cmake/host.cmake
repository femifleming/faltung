# umleitung — the platform layer's own checks (CoreAudioHost): a console program that proves devices, processes,
# the aggregate engine and the taps on this machine, and a tiny tone player (a bundle, so it has a bundle id) that
# the tap tests capture. Run: build/umleitung_host_check [section ...]   (sections: list loop two rates restart
# remove taps; none = all). It never changes the default devices and plays nothing audible.
add_executable(umleitung_host_check_tone MACOSX_BUNDLE ${CMAKE_CURRENT_LIST_DIR}/../tests/HostCheckTone.mm)
set_target_properties(umleitung_host_check_tone PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "studio.umleitung.hostcheck.tone"
    MACOSX_BUNDLE_BUNDLE_NAME "umleitung tone"
    OUTPUT_NAME "umleitung_host_check_tone")
target_compile_options(umleitung_host_check_tone PRIVATE -fobjc-arc)
target_link_libraries(umleitung_host_check_tone PRIVATE "-framework CoreAudio" "-framework Foundation")

add_executable(umleitung_host_check ${CMAKE_CURRENT_LIST_DIR}/../tests/HostCheck.mm)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../tests/HostCheck.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(umleitung_host_check PRIVATE umleitung_platform juce::juce_recommended_config_flags)
target_compile_definitions(umleitung_host_check PRIVATE JUCE_STANDALONE_APPLICATION=1
    HOSTCHECK_TONE_PATH="$<TARGET_FILE:umleitung_host_check_tone>")
add_dependencies(umleitung_host_check umleitung_host_check_tone)
