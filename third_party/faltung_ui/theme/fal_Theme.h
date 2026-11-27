#pragma once

namespace fal::ui
{
/** UTF-8 literal -> String (literals with "·", "–", "…") */
inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

// ---- type -----------------------------------------------------------------------------------------
/** Where the house type comes from. Order of resolution for each role:
      1. data handed to Fonts::set()            (or auto-registered from BinaryData by faltung_ui.cmake)
      2. a TTF found in the folder given to Fonts::loadFromFolder()   (Inter-Regular.ttf, IBMPlexMono-Medium.ttf, ...)
      3. a system font (Helvetica for sans/cond/light, Menlo for mono) */
struct Fonts
{
    enum class Role { Sans, SansMedium, Mono, MonoMedium, Cond, CondMedium, Light, numRoles };

    /** register a TTF/OTF from memory for a role (the data is copied) */
    static void set (Role, const void* data, size_t size);
    /** look for the kit's TTF files (assets/fonts) in this folder for every role not set from memory */
    static void loadFromFolder (const juce::File& folder);
    /** the typeface for a role, or nullptr when it falls back to a system font */
    static juce::Typeface::Ptr typeface (Role);
    /** the file name the kit uses for a role, e.g. "Inter-Regular.ttf" */
    static const char* fileName (Role);
};

juce::Font sans (float size, bool medium = false);   // Inter
juce::Font mono (float size, bool medium = false);   // IBM Plex Mono
/** condensed DIN-like (Barlow Condensed) and light text (Barlow): the former faltung deep sleeve type */
juce::Font cond (float size, bool medium = false);
juce::Font light (float size);

// ---- ink ------------------------------------------------------------------------------------------
/** text colour for a ground */
inline juce::Colour inkFor (juce::Colour ground)
{
    return ground.getPerceivedBrightness() > palette::ink::inkThreshold ? juce::Colour (palette::ink::onLight)
                                                                        : juce::Colour (palette::ink::onDark);
}
/** the slider "edge" mark: white on mid grounds, ink on pale grounds */
inline juce::Colour edgeFor (juce::Colour ground)
{
    return ground.getPerceivedBrightness() > palette::ink::edgeThreshold ? inkFor (ground) : juce::Colours::white;
}

struct Theme
{
    juce::Colour ground, ink, fill, edge;
    bool mirror = false;   // (unused: the former faltung deep sleeve look)
};

// ---- light / dark (runtime) -----------------------------------------------------------------------
// Dark mode is the same layout, type and shapes with every colour field replaced by its dark counterpart.
// Colours are written once, as the light ones, and pass through field() / accent() / paperInk() where used.
/** the process-wide default for every `dark` parameter below (false = light) */
void setDark (bool dark);
bool isDark();

/** the dark counterpart of a colour field: the same hue, printed on dark stock */
juce::Colour darkField (juce::Colour light);
/** a strip / line / fill colour as it sits on a dark field: the same hue, a little dimmer, never glaring */
juce::Colour darkAccent (juce::Colour light);
inline juce::Colour field (juce::Colour c, bool dark = isDark())  { return dark ? darkField (c) : c; }
inline juce::Colour accent (juce::Colour c, bool dark = isDark()) { return dark ? darkAccent (c) : c; }
/** the near-black of marks drawn straight on a pale field (dots, hairlines, small type): pale on the dark one */
inline juce::Colour paperInk (bool dark = isDark()) { return dark ? juce::Colour (palette::ink::onDark) : juce::Colour (palette::ink::paperInk); }
/** a whole theme from a light ground and a light accent */
Theme themeOf (juce::Colour lightGround, juce::Colour lightAccent, bool dark = isDark());
inline Theme themeOf (const palette::Field& f, bool dark = isDark()) { return themeOf (f.ground(), f.accent(), dark); }

// ---- a parallelogram, the house shape -------------------------------------------------------------
juce::Path skewRect (juce::Rectangle<float> r, float skew = 0.55f);
} // namespace fal::ui
