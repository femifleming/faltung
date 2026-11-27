cmake_minimum_required(VERSION 3.22)
# umleitung — a Mac app for routing, capturing, processing and recording sound: virtual devices (like Loopback),
# app / system capture with effects, plug-ins and a recorder (like Audio Hijack), a system-wide eq (like eqMac),
# in the faltung look. macOS 14.2 or later (process taps).
project(umleitung VERSION 1.0.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "" FORCE)
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()

# JUCE: the faltung repo's copy next to this one, or -DUMLEITUNG_JUCE=path
set(UMLEITUNG_JUCE "${CMAKE_CURRENT_LIST_DIR}/../faltung/JUCE" CACHE PATH "JUCE")
add_subdirectory(${UMLEITUNG_JUCE} JUCE)
include(third_party/faltung_ui.cmake)

# ---- the platform layer: CoreAudio (devices, processes, taps, the aggregate engine) and the driver's control ----
# (an INTERFACE library: its sources are compiled into each program that links it, next to that program's own
#  JUCE modules, so JUCE is never built twice into one binary)
file(GLOB UM_PLATFORM_SOURCES CONFIGURE_DEPENDS Source/platform/*.mm Source/platform/*.cpp)
set_source_files_properties(${UM_PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
add_library(umleitung_platform INTERFACE)
target_sources(umleitung_platform INTERFACE ${UM_PLATFORM_SOURCES})
target_include_directories(umleitung_platform INTERFACE Source)
target_link_libraries(umleitung_platform INTERFACE juce::juce_core juce::juce_events
    "-framework CoreAudio" "-framework AudioToolbox" "-framework AppKit" "-framework Foundation")
target_compile_definitions(umleitung_platform INTERFACE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

# ---- the virtual device driver (an AudioServerPlugIn bundle) ----
option(UM_WITH_DRIVER "build the driver" ON)
option(UM_WITH_HOST_CHECK "build the platform checks" ON)
if(UM_WITH_DRIVER AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/driver/CMakeLists.txt)
    add_subdirectory(driver)
endif()
# ---- the platform layer's own checks ----
if(UM_WITH_HOST_CHECK AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/cmake/host.cmake)
    include(cmake/host.cmake)
endif()
# ---- the engine, the app and their tests ----
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/cmake/app.cmake)
    include(cmake/app.cmake)
endif()
