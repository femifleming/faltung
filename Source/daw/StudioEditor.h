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
    /** the file menu and what its entries do (the app's menu bar shows the same menu) */
    juce::PopupMenu fileMenu();
    void fileAction (int id);
    Track* renderProject (bool preMaster);
    bool autoMaster();
    void captureJam();
    juce::Component* pageComponent (int page);
    void selectTrack (Track*);
    bool key (const juce::KeyPress&);

private:
    void timerCallback() override;
    StudioProcessor& proc;
    std::unique_ptr<StudioView> root;
    juce::TooltipWindow tips { this, 700 };
    int nominalW = 1440, nominalH = 900;
};
} // namespace fal::daw
