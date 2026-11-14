#pragma once
#include "Ui.h"
#include "Processor.h"

namespace fal
{
/** The picture at the top of every module and effect. It is drawn from the parameters, flashes on
    hits and can be clicked to audition (modules). Four looks: Fell, Snd, Oval, Stdio. */
class Art : public juce::Component
{
public:
    explicit Art (Processor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();          // ~30 Hz from the editor
    int focus = -1;       // slot being edited (highlighted)

private:
    Processor& p;
    const ProductInfo& pi;
    std::array<int, 16> seenHits {};
    std::array<float, 16> flash {};
    float time = 0;
    struct Spark { float x, y, life; };
    std::vector<Spark> sparks;

    void paintFell (juce::Graphics&, juce::Rectangle<float>);
    void paintSnd (juce::Graphics&, juce::Rectangle<float>);
    void paintOval (juce::Graphics&, juce::Rectangle<float>);
    void paintStdio (juce::Graphics&, juce::Rectangle<float>);
    void paintTitle (juce::Graphics&, juce::Rectangle<float>);
    std::vector<std::pair<juce::String, float>> values() const;  // short label, 0..1
};

/** the snd-style folded strip chain, shared with the kit's voice selector */
struct Fold
{
    static juce::Colour palette (int i);
    /** draws one folded segment: a front face + a darker fold, returns the end point */
    static juce::Point<float> segment (juce::Graphics&, juce::Point<float> start, float len, float depth, bool up,
                                       juce::Colour c, const juce::String& label, juce::Colour ink, bool outline, float glow);
};
} // namespace fal
