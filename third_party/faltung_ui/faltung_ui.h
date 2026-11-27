/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 faltung_ui
  vendor:             faltung
  version:            1.0.0
  name:               faltung ui
  description:        The faltung visual style: palette, light/dark theme, house type, controls and the four header pictures.
  license:            Proprietary
  minimumCppStandard: 17

  dependencies:       juce_gui_basics juce_audio_processors

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#pragma once
#define FALTUNG_UI_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <functional>
#include <vector>

/** Config: FALTUNG_UI_BINARYDATA
    Set to 1 (faltung_ui.cmake does it) when the target links the FaltungFonts binary data, so the house
    type registers itself from it on first use.
*/
#ifndef FALTUNG_UI_BINARYDATA
 #define FALTUNG_UI_BINARYDATA 0
#endif

#include "palette/fal_Palette.h"
#include "theme/fal_Theme.h"
#include "controls/fal_Controls.h"
#include "art/fal_Art.h"
