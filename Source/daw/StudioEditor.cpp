#include "StudioEditor.h"

namespace fal::daw
{
// placeholder root view until the pages land
class StudioView : public juce::Component
{
public:
    explicit StudioView (StudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (studioInfo().ground));
        g.setColour (ui::inkFor (juce::Colour (studioInfo().ground)));
        g.setFont (ui::mono (12.0f));
        g.drawText (juce::String (studioInfo().code) + " " + studioInfo().name + "  ·  " + proc.session.projectName, getLocalBounds().reduced (36), juce::Justification::topLeft);
    }
    void tick() { repaint(); }
    void setPage (int) {}
    int pages() const { return 1; }
    StudioProcessor& proc;
};

StudioEditor::StudioEditor (StudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    root = std::make_unique<StudioView> (p);
    nominalW = studioInfo().w; nominalH = studioInfo().h;
    root->setBounds (0, 0, nominalW, nominalH);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (nominalW / 2, nominalH / 2, nominalW * 2, nominalH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) nominalW / nominalH);
    setSize (nominalW, nominalH);
    startTimerHz (30);
}
StudioEditor::~StudioEditor() { stopTimer(); }
void StudioEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (studioInfo().ground)); }
void StudioEditor::resized() { root->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) nominalW)); }
void StudioEditor::timerCallback() { root->tick(); }
void StudioEditor::showPage (int p) { root->setPage (p); }
int StudioEditor::numPages() const { return root->pages(); }
void StudioEditor::tickForTest() { root->tick(); }
} // namespace fal::daw
