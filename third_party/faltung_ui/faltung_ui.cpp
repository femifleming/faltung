#ifdef FALTUNG_UI_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using. */
 #error "Incorrect use of faltung_ui cpp file"
#endif

#include "faltung_ui.h"

#if FALTUNG_UI_BINARYDATA
 #include "FaltungFonts.h"
#endif

#include "theme/fal_Theme.cpp"
#include "controls/fal_Controls.cpp"
#include "art/fal_Art.cpp"
