#pragma once
#include "Art.h"

namespace fal
{
/** base for the three page types */
class View : public juce::Component
{
public:
    virtual void tick() {}
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    Processor& proc;
    std::unique_ptr<View> view;
    juce::TooltipWindow tips { this, 600 };
    int nominalW = 760, nominalH = 480;
};
} // namespace fal
