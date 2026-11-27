# faltung_ui.cmake - adds the faltung_ui JUCE module and the kit's fonts to a JUCE target.
#
#   add_subdirectory(path/to/JUCE)                     # (or find_package(JUCE))
#   include(path/to/faltung-kit/sdk/juce/faltung_ui.cmake)
#   juce_add_gui_app(MyApp ...)   /   juce_add_plugin(MyPlugin ...)
#   faltung_ui_add(MyApp)
#
# faltung_ui_add(<target>) links juce::faltung_ui, embeds ../../assets/fonts as the binary-data target
# <target>_faltung_fonts (namespace FaltungFonts, header FaltungFonts.h) and defines FALTUNG_UI_BINARYDATA=1,
# so Inter / IBM Plex Mono / Barlow register themselves on first use.

set(FALTUNG_UI_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")
set(FALTUNG_UI_FONTS_DIR "${CMAKE_CURRENT_LIST_DIR}/fonts" CACHE INTERNAL "")

if(NOT TARGET faltung_ui)
    juce_add_module("${CMAKE_CURRENT_LIST_DIR}/faltung_ui" ALIAS_NAMESPACE juce)
endif()

function(faltung_ui_add target)
    get_filename_component(_fonts "${FALTUNG_UI_FONTS_DIR}" REALPATH)
    juce_add_binary_data(${target}_faltung_fonts
        HEADER_NAME FaltungFonts.h
        NAMESPACE FaltungFonts
        SOURCES
            "${_fonts}/Inter-Regular.ttf"           "${_fonts}/Inter-Medium.ttf"
            "${_fonts}/IBMPlexMono-Regular.ttf"     "${_fonts}/IBMPlexMono-Medium.ttf"
            "${_fonts}/BarlowCondensed-Regular.ttf" "${_fonts}/BarlowCondensed-Medium.ttf"
            "${_fonts}/Barlow-Light.ttf"            "${_fonts}/Barlow-Regular.ttf")
    set_target_properties(${target}_faltung_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(${target} PRIVATE juce::faltung_ui ${target}_faltung_fonts)
    target_compile_definitions(${target} PRIVATE FALTUNG_UI_BINARYDATA=1)
endfunction()
