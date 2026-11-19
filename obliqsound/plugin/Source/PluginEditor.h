#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The whole panel is one component drawn from layout.json (written by gen.py + export_layout.py): the same
// positions, pages and controls as the original panel, drawn as vectors in the faltung look — a flat colour
// field, skewed slider bars, a row of folded paper strips for the pages, a chip grid for the pin matrix.
class ObliqPanel : public juce::Component, private juce::Timer
{
public:
    explicit ObliqPanel (ObliqProcessor&);
    ~ObliqPanel() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    int baseW = 960, baseH = 630;
    std::function<void (float)> onScale;
    float scale = 1.0f;

    // the pages, for the host and the test tool
    int numPages() const { return pageNames.size(); }
    void showPage (int);
    juce::String pageName (int) const;
    void showHelp (bool);
    void tickForTest() { timerCallback(); }           // one timer tick without a message loop
    juce::Colour groundColour() const { return ground; }

private:
    enum Type { Image, Knob, VSlider, NSlider, Check, Combo, Button, Label, Scope, PresetBox, PresetSave, PresetRemove, SizeBox, Help };
    struct Item
    {
        Type type = Image; juce::Rectangle<int> r; int page = -1; int param = -1;
        juce::String channel, popup, group, name; juce::StringArray text;
        float alpha = 1.0f; bool visible = true, section = false, pin = false, tab = false, sampler = false; int align = 0;
        int dyn = 0, dynIndex = 0;        // engine-driven behaviour
        juce::StringArray signals;
    };
    void timerCallback() override;
    void build();
    int hit (juce::Point<float>) const;
    bool covered (int item) const;
    void drawItem (juce::Graphics&, const Item&);
    void drawStatic (juce::Graphics&, bool pageItems);
    void drawDynamic (juce::Graphics&);
    void drawTabs (juce::Graphics&);
    void drawSlider (juce::Graphics&, juce::Rectangle<float>, int param, bool hot);
    void drawVSlider (juce::Graphics&, juce::Rectangle<float>, int param, bool hot);
    void drawPin (juce::Graphics&, juce::Rectangle<float>, float amount);
    void drawReadout (juce::Graphics&, const Item&);
    void setParam (int item, float norm);
    void showMenu (int item);
    void changePage (int to);
    void drawHelp (juce::Graphics&);
    bool helpClick (juce::Point<float>);
    std::vector<int> helpTopics() const;
    juce::Rectangle<float> helpWindow() const { return { (float) baseW * 0.5f - 370.0f, 52.0f, 740.0f, 540.0f }; }
    juce::Rectangle<float> tabFace (int tab) const;
    juce::Point<float> toPanel (juce::Point<float> p) const { return p / scale; }

    ObliqProcessor& proc;
    ParamStore& params;
    std::vector<Item> items;
    juce::StringArray pageNames;                      // container channels, in tab order
    std::vector<juce::Rectangle<int>> pageBounds; std::vector<int> pageStart; std::vector<bool> pageBoard;
    std::vector<int> tabParams, tabItems;
    juce::Colour ground, ink, accent, edge;

    // pages
    int page = 0, pending = -1, tickCount = 0, fade = 0;
    enum { kFade = 5 };
    juce::Image layerMain, layerPage; int layerChange = -1, layerPageNo = -1; float layerScale = 0;
    juce::Image geezSheet;                         // the 26 Ge'ez numerals of the sampler squares, as masks (Resources/geez.png)
    bool drawGeez (juce::Graphics&, int n, float cx, float cy, float h) const;

    // interaction
    int dragItem = -1, hoverItem = -1; float dragStart = 0; juce::Point<float> dragPos, mousePos;
    juce::Random random;
    std::array<float, 24> lzx {}, lzy {};
    std::vector<float> lfoH[2];
    int lastPresetVersion = -1;
    bool helpOpen = false; int helpTopic = 0;
    std::map<int, int> heldKeys; int kbOctave = 0, kbVelocity = 100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ObliqPanel)
};

class ObliqEditor : public juce::AudioProcessorEditor
{
public:
    explicit ObliqEditor (ObliqProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (panel.groundColour()); }
    int numPages() const { return panel.numPages(); }
    void showPage (int p) { panel.showPage (p); }
    juce::String pageName (int p) const { return panel.pageName (p); }
    void showHelp (bool on) { panel.showHelp (on); }
    void tickForTest() { panel.tickForTest(); }
private:
    ObliqPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ObliqEditor)
};
