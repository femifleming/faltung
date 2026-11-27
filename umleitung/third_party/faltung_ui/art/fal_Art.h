#pragma once

namespace fal::art
{
using fal::Style;

/** the pictures are designed for this area (the header of a faltung plug-in); ArtComponent scales to fit */
constexpr float kDesignWidth = 760.0f, kDesignHeight = 290.0f;

/** one value the picture is drawn from: a short label and 0..1 */
struct Value { juce::String label; float value = 0.0f; };

/** the snd-style folded strip chain */
struct Fold
{
    /** the i-th strip colour (cycled), as it sits on the current light/dark field */
    static juce::Colour palette (int i, bool dark = ui::isDark());
    /** draws one folded segment: a front face + a darker fold, returns the end point */
    static juce::Point<float> segment (juce::Graphics&, juce::Point<float> start, float len, float depth, bool up,
                                       juce::Colour c, const juce::String& label, juce::Colour ink, bool outline, float glow);
};

// ---- helpers -----------------------------------------------------------------------------------------
/** a stdio chevron (pointing right) filled with colour */
void chevron (juce::Graphics&, float x, float y, float w, float h, juce::Colour);
/** the stdio yellow cube, 104 x 112 at scale 1, top-left at origin; code is printed on its top face */
void cube (juce::Graphics&, juce::Point<float> origin, float scale = 1.0f, const juce::String& code = {}, bool dark = ui::isDark());

// ---- the four looks (all coordinates relative to bounds; designed for kDesignWidth x kDesignHeight) ----
/** Fell: wireframe polyhedra in the tint colour with white highlight faces. values: up to 8 (0..1) */
void fell (juce::Graphics&, juce::Rectangle<float> bounds, const std::vector<Value>& values, float flash, juce::Colour tint);

/** Snd: two chains of folded pastel strips, one strip per value (up to 8), labelled.
    ground: the field (to avoid strips that vanish on it); seed rotates the palette; focus outlines a strip */
void snd (juce::Graphics&, juce::Rectangle<float> bounds, const std::vector<Value>& values, float flash,
          juce::Colour ground, int seed = 0, int focus = -1);

struct OvalParams
{
    float feedback = 0.62f;   // 0..1.1: how many arcs (repeats) survive
    bool freeze = false;      // dashed, every repeat at full level
    float wow = 0.2f;         // arc wobble 0..1
    float age = 0.4f;         // hatching 0..1
    float timeSec = 0.375f;   // delay time: arc spacing and tap positions
    float time = 0.0f;        // animation clock (seconds)
};
/** Oval: coloured arcs and a line bundle with taps on white */
void oval (juce::Graphics&, juce::Rectangle<float> bounds, const OvalParams&);

struct StdioRow { juce::String name, value; };
struct StdioParams
{
    int count = 5;            // chevrons
    float step = 80.0f;       // spacing
    float alpha = 0.34f;      // chevron opacity
    juce::String code;        // printed on the cube
    std::vector<StdioRow> rows;       // the tracklist (name / value), timecodes are generated
    juce::Colour ground { ui::palette::fields::frost.groundArgb };   // the field (for the ink)
    std::vector<float> sparks;        // flying chevrons: life 1 -> 0
};
/** Stdio: pale chevrons, the yellow cube, a monospace tracklist */
void stdio (juce::Graphics&, juce::Rectangle<float> bounds, const StdioParams&, float flash);

/** the product title in the place and type each look puts it.
    brandText: "faltung" (Fell prints it upper-case), e.g. "faltung deep" in dark builds */
void title (juce::Graphics&, juce::Rectangle<float> bounds, Style, const juce::String& code, const juce::String& name,
            const juce::String& blurb, juce::Colour ground, juce::Colour accent, const juce::String& brandText = "faltung");

// ---- a component -------------------------------------------------------------------------------------
/** Holds a look + values, fills its field, paints the picture and title scaled to fit, ticks at 30 Hz
    (decaying flash, animation clock). Colours are given LIGHT and pass through ui::field()/accent() with ui::isDark(). */
class ArtComponent : public juce::Component, private juce::Timer
{
public:
    ArtComponent (Style, juce::Colour lightGround, juce::Colour lightAccent);
    explicit ArtComponent (const ui::palette::Field& f) : ArtComponent (f.style, f.ground(), f.accent()) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setStyle (Style s) { style = s; repaint(); }
    void setColours (juce::Colour lightGround, juce::Colour lightAccent) { ground = lightGround; accentC = lightAccent; repaint(); }
    void setField (const ui::palette::Field& f) { style = f.style; setColours (f.ground(), f.accent()); }
    void setValues (std::vector<Value> v) { values = std::move (v); repaint(); }
    void setValue (int i, float v) { if (i >= 0 && i < (int) values.size()) { values[(size_t) i].value = v; repaint(); } }
    void setTitle (juce::String code, juce::String name, juce::String blurb = {}) { tCode = std::move (code); tName = std::move (name); tBlurb = std::move (blurb); repaint(); }
    void setBrand (juce::String b) { brand = std::move (b); repaint(); }
    void setShowTitle (bool s) { showTitle = s; repaint(); }
    /** extra state for Oval (time is driven by the component) and Stdio (rows default to the values) */
    void setOval (OvalParams p) { ovalP = p; repaint(); }
    void setStdio (StdioParams p) { stdioP = std::move (p); repaint(); }
    void setSeed (int s) { seed = s; repaint(); }
    /** scale the 760 x 290 design to fit (default on); off: draw at 1:1 from the top-left */
    void setScaleToFit (bool s) { scaleToFit = s; repaint(); }

    /** flash (a hit): decays by 0.82 per tick */
    void hit (float amount = 1.0f);
    float getFlash() const { return flash; }
    float getTime() const { return time; }

    Style getStyle() const { return style; }
    juce::Colour getGround() const { return ui::field (ground); }
    juce::Colour getAccent() const { return ui::accent (accentC); }

    int focus = -1;   // Snd: strip outlined
    std::function<void (const juce::MouseEvent&)> onClick;

private:
    void timerCallback() override;
    Style style;
    juce::Colour ground, accentC;
    std::vector<Value> values;
    juce::String tCode, tName, tBlurb, brand { "faltung" };
    OvalParams ovalP; StdioParams stdioP;
    std::vector<float> sparks;
    int seed = 0;
    float flash = 0.0f, time = 0.0f;
    bool showTitle = true, scaleToFit = true;
};
} // namespace fal::art
