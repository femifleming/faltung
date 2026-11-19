#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace fal::ui
{
/** UTF-8 literal → String (literals with "·", "–", "…") */
inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

// ---- type -----------------------------------------------------------------------------------------
juce::Font sans (float size, bool medium = false);
juce::Font mono (float size, bool medium = false);
/** condensed DIN-like (Barlow Condensed) and light text (Barlow): the former faltung deep sleeve type, no longer used by the plug-ins */
juce::Font cond (float size, bool medium = false);
juce::Font light (float size);

/** text colour for a ground */
inline juce::Colour inkFor (juce::Colour ground)
{
    return ground.getPerceivedBrightness() > 0.42f ? juce::Colour (0xff1d1c15) : juce::Colour (0xfff6f6f0);
}
/** the slider "edge" mark: white on mid grounds, ink on pale grounds */
inline juce::Colour edgeFor (juce::Colour ground)
{
    return ground.getPerceivedBrightness() > 0.86f ? inkFor (ground) : juce::Colours::white;
}

struct Theme
{
    juce::Colour ground, ink, fill, edge;
    bool mirror = false;   // (unused: the former faltung deep sleeve look; the dark mode draws like faltung)
};

// ---- light / dark ---------------------------------------------------------------------------------
// A product that is built with FALTUNG_DEEP=1 is the dark mode of its faltung twin: the same layout, type and
// shapes, every colour field replaced by its dark counterpart. Colours are written once, as the light ones,
// and pass through field() / accent() / paperInk() where they are used.
#ifndef FALTUNG_DEEP
 #define FALTUNG_DEEP 0
#endif
constexpr bool kDark = FALTUNG_DEEP != 0;
/** the dark counterpart of a colour field: the same hue, printed on dark stock */
juce::Colour darkField (juce::Colour light);
/** a strip / line / fill colour as it sits on a dark field: the same hue, a little dimmer, never glaring */
juce::Colour darkAccent (juce::Colour light);
inline juce::Colour field (juce::Colour c, bool dark = kDark)  { return dark ? darkField (c) : c; }
inline juce::Colour accent (juce::Colour c, bool dark = kDark) { return dark ? darkAccent (c) : c; }
/** the near-black of marks drawn straight on a pale field (dots, hairlines, small type): pale on the dark one */
inline juce::Colour paperInk (bool dark = kDark) { return dark ? juce::Colour (0xfff6f6f0) : juce::Colour (0xff1d1d1d); }
/** a whole theme from a light ground and a light accent */
Theme themeOf (juce::Colour lightGround, juce::Colour lightAccent, bool dark = kDark);

// ---- a parallelogram, the house shape -------------------------------------------------------------
juce::Path skewRect (juce::Rectangle<float> r, float skew = 0.55f);

// ---- parameter slider: label · thin rule · slanted fill · white edge · value ------------------------
class ParamSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamSlider (juce::RangedAudioParameter& p, Theme t, juce::String label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setFill (juce::Colour c) { theme.fill = c; repaint(); }
    void setLabelWidth (int w) { labelW = w; resized(); repaint(); }
    void setTheme (Theme t) { theme = t; repaint(); }
    /** where modulation has moved it (0..1), drawn as a hollow tick; < 0: none */
    void setGhost (float g) { if (std::abs (g - ghost) > 0.002f) { ghost = g; repaint(); } }
    std::function<void()> onTouch;
    juce::RangedAudioParameter& param;

private:
    Theme theme;
    juce::String label;
    int labelW = 64;
    float value01 = 0, dragStart = 0, ghost = -1.0f;
    bool dragging = false;
    juce::ParameterAttachment attach;
    juce::Rectangle<float> track() const;
};

// ---- choice: "label  value" — click for a menu, drag to step ----------------------------------------
class ChoiceBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceBox (juce::RangedAudioParameter& p, Theme t, juce::String label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::RangedAudioParameter& param;
    bool asTabs = false;   // draw every option as a row of tabs
    int labelW = 64;
    void setTheme (Theme t) { theme = t; repaint(); }

private:
    Theme theme; juce::String label;
    int index = 0, dragStartIndex = 0; bool moved = false;
    juce::ParameterAttachment attach;
    void set (int i);
    int numItems() const;
};

// ---- toggle: small parallelogram, filled when on --------------------------------------------------
class ToggleBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleBox (juce::RangedAudioParameter& p, Theme t, juce::String label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setTheme (Theme t) { theme = t; repaint(); }
    juce::RangedAudioParameter& param;

private:
    Theme theme; juce::String label; bool on = false;
    juce::ParameterAttachment attach;
};

// ---- text button in the house style ----------------------------------------------------------------
class TextLink : public juce::Component, public juce::SettableTooltipClient
{
public:
    TextLink (juce::String t, Theme th) : text (std::move (t)), theme (th) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void setText (juce::String t) { text = std::move (t); repaint(); }
    void setLit (bool l) { lit = l; repaint(); }
    std::function<void()> onClick;
    juce::String text; Theme theme; bool lit = false;
};

// ---- preset bar: ‹ name › ---------------------------------------------------------------------------
class PresetBar : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    PresetBar (juce::AudioProcessor& p, Theme t);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override { if (p.getCurrentProgram() != shown) { shown = p.getCurrentProgram(); repaint(); } }
    juce::AudioProcessor& p; Theme theme; int shown = -1;
};
} // namespace fal::ui
