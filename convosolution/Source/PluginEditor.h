#pragma once
#include "PluginProcessor.h"
#include "Ui.h"
#include "../../Source/Ui.h"

namespace ecr
{
// The window in the faltung look: one flat slate-blue field, lowercase mono text,
// the response drawn as a thin wireframe shell with the attack-release window as
// a white highlight face, the buttons as text links that get a skewed chip when
// lit, and the knobs as the suite's slider rows (label · rule · slanted fill ·
// white edge · value). Nothing is a child component: everything is painted here
// and hit-tested through `hits`, so the page scales as one picture.
class EcrEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EcrEditor (EcrProcessor&);
    ~EcrEditor() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override { repaint(); }

    static constexpr int kW = 560, kH = 372;   // page size at 100 %
    int tab = 0;                               // 0 = knobs, 1 = modulation

private:
    enum class Hit { None, Knob, Screen, Button, Prev, Next, Push, Folder, Tab, LfoShape, LfoRate, Cell };
    struct Target { Hit type = Hit::None; int index = 0, sub = 0; float width = 100.0f; };

    void timerCallback() override;
    Target hitTest (juce::Point<float> pagePos) const;
    juce::Point<float> toPage (juce::Point<float> p) const { return p * ((float) kW / (float) getWidth()); }
    bool shiftNow (const juce::ModifierKeys& m) const { return m.isShiftDown() || proc.ui->shiftLatched(); }
    juce::RangedAudioParameter* paramOf (Target, bool shift) const;
    void announce (Target, bool shift);
    void drawPage (juce::Graphics&);
    void drawScreen (juce::Graphics&, juce::Rectangle<float>);     // the module display, as the picture
    void drawResponse (juce::Graphics&, juce::Rectangle<float>, int slot);   // the wireframe of a response
    void drawPopup (juce::Graphics&, juce::Rectangle<float>);
    void drawLfoFigure (juce::Graphics&, juce::Rectangle<float>, int lfo);
    float link (juce::Graphics&, const juce::String& text, float x, float y, Target, bool lit);
    float separator (juce::Graphics&, float x, float y);
    void showFolderMenu();

    EcrProcessor& proc;
    std::vector<std::pair<juce::Rectangle<float>, Target>> hits;   // rebuilt on every paint
    Target active;
    bool dragShift = false, moved = false, holdFired = false;
    float dragStartValue = 0.0f, selectAccum = 0.0f, wheelAccum = 0.0f;
    juce::Point<float> lastDrag;
    juce::uint32 downTime = 0;
    // what the LFOs have been doing lately, sampled by the timer
    struct Trail { int shape = -1; std::vector<juce::Point<float>> path; std::vector<float> values; float view[Lfo::kViewSize] {}; };
    Trail trails[kNumLfos];
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EcrEditor)
};
} // namespace ecr
