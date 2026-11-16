#pragma once
#include "StudioProcessor.h"
#include "../Ui.h"

namespace fal::daw
{
class StudioView;

class StudioEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StudioEditor (StudioProcessor&);
    ~StudioEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    /** scripted use (tests): show a page, advance the displays */
    void showPage (int page);
    int numPages() const;
    void tickForTest();
    StudioView& view() { return *root; }

private:
    void timerCallback() override;
    StudioProcessor& proc;
    std::unique_ptr<StudioView> root;
    juce::TooltipWindow tips { this, 700 };
    int nominalW = 1440, nominalH = 900;
};
} // namespace fal::daw
