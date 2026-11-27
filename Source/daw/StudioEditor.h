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
    juce::PopupMenu editMenu();
    juce::PopupMenu viewMenu();
    /** the size of everything in the window: 0.5 … 2 (a smaller window shrinks it further, so everything fits) */
    void setUiScale (float);
    float getUiScale() const { return uiScale; }
    /** the scale actually drawn at, and the room the pages get at it */
    float effectiveScale() const;
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
    static constexpr int kMinW = 1200, kMinH = 720;   // the least room the pages are laid out for
    float uiScale = 1.0f;
};
} // namespace fal::daw
