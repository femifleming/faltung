# umleitung.driver — the AudioServerPlugIn that publishes umleitung's virtual loopback devices.
# Builds as part of the umleitung project, or on its own: cmake -S driver -B build-driver [-DUMLEITUNG_DRIVER_ASAN=ON]
cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(umleitung_driver VERSION 1.0.0 LANGUAGES C)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "" FORCE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    endif()
    enable_testing()
endif()

option(UMLEITUNG_DRIVER_ASAN "build the driver and its test harness with AddressSanitizer" OFF)

if(PROJECT_VERSION)
    set(UM_DRIVER_VERSION ${PROJECT_VERSION})
else()
    set(UM_DRIVER_VERSION 1.0.0)
endif()
# (not next to the harness executable: CFBundle would take that folder for the main bundle, with our identifier)
configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/bundle-plist/Info.plist @ONLY)

add_library(umleitung_driver MODULE src/umleitung_driver.c)
set_target_properties(umleitung_driver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION driver
    OUTPUT_NAME umleitung
    C_STANDARD 11
    C_VISIBILITY_PRESET hidden
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/bundle-plist/Info.plist
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(umleitung_driver PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(umleitung_driver PRIVATE "-framework CoreFoundation")
if(UMLEITUNG_DRIVER_ASAN)
    target_compile_options(umleitung_driver PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(umleitung_driver PRIVATE -fsanitize=address)
endif()
# ad-hoc signature (coreaudiod loads unsigned-by-a-developer drivers when they are ad-hoc signed)
add_custom_command(TARGET umleitung_driver POST_BUILD
    COMMAND codesign --force --sign - --timestamp=none $<TARGET_BUNDLE_DIR:umleitung_driver>
    COMMENT "codesign (ad-hoc) umleitung.driver")

# the app's DriverControl finds the built driver here when it isn't inside an app bundle
if(TARGET umleitung_platform)
    target_compile_definitions(umleitung_platform INTERFACE UM_DRIVER_BUILD_PATH="${CMAKE_CURRENT_BINARY_DIR}/umleitung.driver")
endif()

# ---- the in-process test harness: loads the built bundle with CFPlugIn and drives it through a fake host ----
add_executable(umleitung_driver_test tests/DriverHarness.c)
set_target_properties(umleitung_driver_test PROPERTIES C_STANDARD 11)
add_dependencies(umleitung_driver_test umleitung_driver)
target_compile_definitions(umleitung_driver_test PRIVATE UM_DRIVER_BUNDLE="$<TARGET_BUNDLE_DIR:umleitung_driver>")
target_compile_options(umleitung_driver_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(umleitung_driver_test PRIVATE "-framework CoreFoundation")
if(UMLEITUNG_DRIVER_ASAN)
    target_compile_options(umleitung_driver_test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(umleitung_driver_test PRIVATE -fsanitize=address)
endif()
add_test(NAME umleitung_driver_test COMMAND umleitung_driver_test)

# ---- DriverControl (Source/platform/DriverControl.mm) on its own, without installing: part of the umleitung build ----
if(TARGET juce::juce_core)
    add_executable(umleitung_drivercontrol_check tests/DriverControlCheck.mm ${CMAKE_CURRENT_SOURCE_DIR}/../Source/platform/DriverControl.mm)
    target_include_directories(umleitung_drivercontrol_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
    target_compile_definitions(umleitung_drivercontrol_check PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
        UM_DRIVER_BUILD_PATH="${CMAKE_CURRENT_BINARY_DIR}/umleitung.driver")
    target_link_libraries(umleitung_drivercontrol_check PRIVATE juce::juce_core juce::juce_recommended_config_flags
        "-framework CoreAudio" "-framework Foundation")
    add_dependencies(umleitung_drivercontrol_check umleitung_driver)
    # ARC as in the app (DriverControl.mm uses no Objective-C objects, so it is the same either way); not on JUCE's own sources
    set_source_files_properties(tests/DriverControlCheck.mm ${CMAKE_CURRENT_SOURCE_DIR}/../Source/platform/DriverControl.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()
