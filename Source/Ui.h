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
/** faltung deep: condensed DIN-like (Barlow Condensed) and light text (Barlow) */
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
    bool mirror = false;   // faltung deep look
};

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
    std::function<void()> onTouch;
    juce::RangedAudioParameter& param;

private:
    Theme theme;
    juce::String label;
    int labelW = 64;
    float value01 = 0, dragStart = 0;
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
    juce::RangedAudioParameter& param;

private:
    Theme theme; juce::String label; bool on = false;
    juce::ParameterAttachment attach;
};

// ---- text button in the house style ----------------------------------------------------------------
class TextLink : public juce::Component
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
