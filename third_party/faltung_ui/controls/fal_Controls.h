#pragma once

namespace fal::ui
{
// ---- the drawing shared by the parameter and the plain-value controls ------------------------------
namespace draw
{
    /** label · thin rule · slanted fill (from zero01) · white edge · value text; ghost < 0: none */
    void slider (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, const juce::String& label, const juce::String& valueText,
                 float value01, float zero01, float ghost, bool dragging, int labelW);
    /** "label  value" with an underline */
    void choice (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, const juce::String& label, const juce::String& valueText, int labelW);
    /** every option as a row of tabs, the current one filled */
    void tabs (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, const juce::String& label, const juce::StringArray& items, int index);
    /** small parallelogram, filled when on, then the label */
    void toggle (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, const juce::String& label, bool on);
    /** which tab a click at x hits */
    int tabAt (juce::Rectangle<float> bounds, const juce::String& label, int numItems, float x);
}

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
    void setLabelWidth (int w) { labelW = w; repaint(); }
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

// ---- plain value slider (no parameter): the same look and feel, std::function callbacks -----------
class ValueSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueSlider (Theme t, juce::String label, juce::NormalisableRange<float> range = { 0.0f, 1.0f },
                 float value = 0.0f, float defaultValue = 0.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float getValue() const { return value; }
    void setValue (float v, juce::NotificationType n = juce::sendNotificationSync);
    void setRange (juce::NormalisableRange<float> r) { range = r; setValue (value, juce::dontSendNotification); }
    void setDefault (float v) { defaultValue = v; }
    void setTheme (Theme t) { theme = t; repaint(); }
    void setFill (juce::Colour c) { theme.fill = c; repaint(); }
    void setLabel (juce::String l) { label = std::move (l); repaint(); }
    void setLabelWidth (int w) { labelW = w; repaint(); }
    void setGhost (float g) { if (std::abs (g - ghost) > 0.002f) { ghost = g; repaint(); } }

    std::function<void (float)> onChange;            // every change made by the user (or setValue with notification)
    std::function<void()> onDragStart, onDragEnd;    // gesture begin / end
    std::function<juce::String (float)> textFromValue;   // default: 2 decimals

private:
    Theme theme; juce::String label;
    juce::NormalisableRange<float> range;
    float value = 0, defaultValue = 0, dragStart = 0, ghost = -1.0f;
    int labelW = 64;
    bool dragging = false;
    juce::Rectangle<float> track() const;
    void setFrom01 (float v01);
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

// ---- plain choice (no parameter) ---------------------------------------------------------------------
class ValueChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueChoice (Theme t, juce::String label, juce::StringArray items, int index = 0);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int getIndex() const { return index; }
    void setIndex (int i, juce::NotificationType n = juce::sendNotificationSync);
    void setItems (juce::StringArray s) { items = std::move (s); setIndex (index, juce::dontSendNotification); }
    void setTheme (Theme t) { theme = t; repaint(); }
    bool asTabs = false;
    int labelW = 64;
    std::function<void (int)> onChange;

private:
    Theme theme; juce::String label; juce::StringArray items;
    int index = 0, dragStartIndex = 0; bool moved = false;
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

// ---- plain toggle (no parameter) ---------------------------------------------------------------------
class ValueToggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueToggle (Theme t, juce::String label, bool on = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setOn (! on); }
    bool isOn() const { return on; }
    void setOn (bool o, juce::NotificationType n = juce::sendNotificationSync);
    void setTheme (Theme t) { theme = t; repaint(); }
    std::function<void (bool)> onChange;

private:
    Theme theme; juce::String label; bool on = false;
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
    void setTheme (Theme t) { theme = t; repaint(); }
    std::function<void()> onClick;
    juce::String text; Theme theme; bool lit = false;
};

/** A processor whose presets can be loaded explicitly, even the current one again (the host's setCurrentProgram
    ignores re-selecting the current program, so that restoring a session doesn't put a preset over it) */
struct ProgramLoader
{
    virtual void loadProgram (int) = 0;
protected:
    ~ProgramLoader() = default;
};

// ---- preset bar: ‹ name › ---------------------------------------------------------------------------
class PresetBar : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    PresetBar (juce::AudioProcessor& p, Theme t);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setTheme (Theme t) { theme = t; repaint(); }
private:
    void timerCallback() override { if (p.getCurrentProgram() != shown) { shown = p.getCurrentProgram(); repaint(); } }
    static void load (juce::AudioProcessor& proc, int i) { if (auto* l = dynamic_cast<ProgramLoader*> (&proc)) l->loadProgram (i); else proc.setCurrentProgram (i); }
    juce::AudioProcessor& p; Theme theme; int shown = -1;
};
} // namespace fal::ui
