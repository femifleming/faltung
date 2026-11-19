#pragma once
#include "Art.h"

namespace fal
{
/** the colours and the style of a product's window, from its entry in the product table: the one place the suite's
    palette is turned into a theme (sndstudio takes its own from here too) */
ui::Theme themeFor (const ProductInfo&);

/** base for the three page types */
class View : public juce::Component
{
public:
    virtual void tick() {}
    virtual void setPage (int) {}
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    /** advance the displays once (offline screenshots) */
    void tickForTest() { if (view) view->tick(); }
    void pageForTest (int pg) { if (view) view->setPage (pg); }

private:
    void timerCallback() override;
    Processor& proc;
    std::unique_ptr<View> view;
    juce::TooltipWindow tips { this, 600 };
    int nominalW = 760, nominalH = 480;
};
} // namespace fal
