#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "../../Source/Ui.h"

//==============================================================================
// Cloudfin in the faltung look. Clouds heaped over calm water, drawn the way Oval's sleeves draw:
// stacked coloured lines on a flat ground, the whole picture mirrored about its centre line. The seven
// effects are snd's folded paper strips: drag them to change the order. Below, the faltung rows:
// label · slanted bar · value, and after each one a small bar for how far the random drift moves it.
//   day   : a pale ground, blues and pinks
//   dusk  : a warm pale ground, oranges and lilac (the default)
//   night : the dark ground of the series, pale lines
struct CfTheme
{
    int mode = 1;
    juce::Colour ground, ink, fill, rule, water, sun;
    std::array<juce::Colour, 5> lines;   // the cloud and water lines
    fal::ui::Theme ui() const { return { ground, ink, fill, fal::ui::edgeFor (ground), false }; }
    static const CfTheme& forMode (int m);
};

juce::Colour stageColour (int stage);   // the folded strip of each effect
const char* stageName (int stage);

//==============================================================================
class CfLook : public juce::LookAndFeel_V4
{
public:
    void setTheme (const CfTheme&);
    juce::Font getPopupMenuFont() override { return fal::ui::mono (12.0f); }
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

//==============================================================================
// the drift a control gets from the random voltage: a short bipolar bar (drag, double-click: none)
class DriftBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    DriftBar (juce::RangedAudioParameter&, fal::ui::Theme);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void setTheme (fal::ui::Theme t) { theme = t; repaint(); }
private:
    juce::RangedAudioParameter& param;
    fal::ui::Theme theme;
    juce::ParameterAttachment attach;
    float value01 = 0.5f, start = 0.5f;
};

//==============================================================================
// Draw the grain envelope with the mouse. Presets underneath.
class ShapeEditor : public juce::Component
{
public:
    explicit ShapeEditor (CloudfinProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void poll (float t, float grainSeconds);
    void setTheme (const CfTheme&);

private:
    void drawAt (juce::Point<float>);
    void push();
    void applyPreset (int which);

    CloudfinProcessor& proc;
    CfTheme th;
    std::array<float, CloudfinProcessor::kShapeN> table {};
    juce::Rectangle<float> plot;
    juce::Point<float> last;
    bool drawing = false;
    uint32_t seenVersion = 0;
    float time = 0.0f, riderPos = 0.0f;
    juce::OwnedArray<fal::ui::TextLink> presets;
};

//==============================================================================
// The module order as a row of folded paper strips. Drag a strip to move that effect in the chain;
// double-click for the default order.
class ChainStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChainStrip (CloudfinProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void poll (float dt);
    void shuffle();
    const CfTheme* theme = nullptr;

    static constexpr float chipW = 104.0f, gap = 14.0f, endW = 44.0f;   // endW: room at each end (in, and the loop back)
    static float width() { return (float) fp::numStages * chipW + (float) (fp::numStages - 1) * gap + 2.0f * endW; }

private:
    float slotX (int i) const noexcept { return endW + (float) i * (chipW + gap); }
    int stageAt (juce::Point<float>) const;

    CloudfinProcessor& proc;
    fp::StageOrder order = fp::defaultOrder();
    float chipX[fp::numStages] {};
    int dragStage = -1;
    float dragX = 0.0f, grabOffset = 0.0f;
    juce::Random rnd;
};

//==============================================================================
// presets: ‹ name ›, click the name for the list (groups, yours, save, the folder)
class CfPresets : public juce::Component
{
public:
    explicit CfPresets (CloudfinProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void poll();
    fal::ui::Theme theme;
private:
    void step (int d);
    void menu();
    CloudfinProcessor& proc;
    juce::String shownName;
    int shownProgram = -2;
    bool user = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class CloudfinEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CloudfinEditor (CloudfinProcessor&);
    ~CloudfinEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designW = 1240, designH = 850;
    static constexpr int skyH = 300;   // the picture band

private:
    struct Content : public juce::Component
    {
        explicit Content (CloudfinEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        CloudfinEditor& owner;
    };

    struct Row
    {
        std::unique_ptr<fal::ui::ParamSlider> slider;
        std::unique_ptr<DriftBar> drift;
        int modIndex = -1;
    };
    struct Section
    {
        juce::String title;
        int stage = -1;                      // which effect (for its strip colour and place in the chain)
        juce::Rectangle<int> r;              // header + rows
        std::vector<Row*> rows;
    };

    // the clouds: puffs of stacked lines; each cloud is drawn on both sides of the centre line
    struct Puff { float x, y, r; float fall = 0, vy = 0; bool wet = false; };
    struct Cloud { float dx = 0, baseY = 0, w = 100; std::vector<Puff> puffs; };

    Row* row (const char* id, const juce::String& label);
    Section* section (const juce::String& title, int stage);
    void layoutContent();
    void paintContent (juce::Graphics&);
    void drawSky (juce::Graphics&);            // the whole picture band (mirrored)
    void drawSkyHalf (juce::Graphics&);        // its left half
    void drawSection (juce::Graphics&, const Section&);
    void buildClouds();
    void setThemeMode (int mode);
    void timerCallback() override;
    void stepGravity (float dt);
    float surfaceY (float x) const noexcept;   // the water line at x (left-half coordinates)
    float modValue (const char* id) const;

    CloudfinProcessor& proc;
    CfLook look;
    CfTheme th = CfTheme::forMode (1);
    Content content { *this };

    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<Section>> sections;
    Section *sgS = nullptr, *psS = nullptr, *fmS = nullptr, *grS = nullptr, *shS = nullptr, *dlS = nullptr,
            *rvS = nullptr, *cpS = nullptr, *fbS = nullptr, *drS = nullptr;

    std::unique_ptr<fal::ui::ChoiceBox> scaleBox, hydroInto;
    std::unique_ptr<fal::ui::ToggleBox> freeze, randOct, specFreeze, gravity;
    std::unique_ptr<fal::ui::ParamSlider> hydroLevel;
    ShapeEditor shapeEditor { proc };
    ChainStrip chain { proc };
    CfPresets presets { proc };
    fal::ui::TextLink shuffleBtn { "shuffle", {} }, randomBtn { "randomize", {} };
    std::array<std::unique_ptr<fal::ui::TextLink>, 3> modeLinks;

    std::vector<Cloud> clouds;
    std::vector<float> waveH, waveV;
    float waterRise = 0.0f, splashEnergy = 0.0f, levelSm = 0.0f;
    bool gravityOn = false;
    static constexpr float horizonY = 212.0f;

    double lastTick = 0.0;
    float t = 0.0f;

    friend struct CloudfinTestAccess; // headless test drives the timer / gravity
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudfinEditor)
};
