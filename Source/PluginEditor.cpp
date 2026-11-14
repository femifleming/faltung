// Partials: the window. Part of the faltung series (stdio line): a pale blue-white field, slanted bars,
// lowercase Inter / IBM Plex Mono, the partials drawn as Oval-style coloured lines, the yellow cube.
#include "PluginProcessor.h"
#include "BinaryData.h"

using namespace pt;

namespace
{
const juce::Colour cBg (0xffeef4f7), cFg (0xff1d1c15), cDim (0xff55636b), cLine (0xffcddbe2), cTrack (0xffb4c6cf), cHot (0xffe06a2e),
                   cFill (0xff4b9cc8), cChev (0xff9fd3ec);
const juce::Colour cOval[4] = { juce::Colour (0xff2f7fd1), juce::Colour (0xffe06a2e), juce::Colour (0xffd9828c), juce::Colour (0xff8f6bb8) };
juce::Typeface::Ptr face (const char* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); }
juce::Font fnt (float h, bool bold = false)
{
    static auto r = face (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static auto m = face (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    return juce::Font (juce::FontOptions (bold ? m : r).withHeight (h));
}
juce::Font mono (float h, bool bold = false)
{
    static auto r = face (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    static auto m = face (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
    return juce::Font (juce::FontOptions (bold ? m : r).withHeight (h));
}
/** the faltung parallelogram */
juce::Path skew (juce::Rectangle<float> r, float k = 0.55f)
{
    const float d = r.getHeight() * k; juce::Path p;
    p.startNewSubPath (r.getX() + d, r.getY()); p.lineTo (r.getRight() + d, r.getY()); p.lineTo (r.getRight(), r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath();
    return p;
}
void chevrons (juce::Graphics& g, juce::Rectangle<float> b, int n, float alpha)
{
    for (int i = 0; i < n; ++i)
    {
        const float w = b.getWidth() * 0.16f, h = b.getHeight() * 0.9f, d = h * 0.42f;
        const float x = b.getX() + b.getWidth() * 0.12f + (float) i * b.getWidth() * 0.13f, y = b.getY() + b.getHeight() * 0.05f;
        juce::Path c; c.startNewSubPath (x, y + h); c.lineTo (x + w, y + h); c.lineTo (x + w + d, y + h * 0.5f); c.lineTo (x + w, y); c.lineTo (x, y); c.lineTo (x + d, y + h * 0.5f); c.closeSubPath();
        g.setColour (cChev.withAlpha (alpha * (i % 2 ? 1.0f : 0.75f))); g.fillPath (c);
    }
}
void cube (juce::Graphics& g, float x, float y, float s, const juce::String& txt)
{
    auto P = [&] (float a, float b) { return juce::Point<float> (x + a * s, y + b * s); };
    juce::Path t, l, r;
    t.startNewSubPath (P (0, 30)); t.lineTo (P (52, 0)); t.lineTo (P (104, 30)); t.lineTo (P (52, 60)); t.closeSubPath();
    l.startNewSubPath (P (0, 30)); l.lineTo (P (52, 60)); l.lineTo (P (52, 112)); l.lineTo (P (0, 82)); l.closeSubPath();
    r.startNewSubPath (P (52, 60)); r.lineTo (P (104, 30)); r.lineTo (P (104, 82)); r.lineTo (P (52, 112)); r.closeSubPath();
    g.setColour (juce::Colour (0xffffe15a)); g.fillPath (t);
    g.setColour (juce::Colour (0xfff2c100)); g.fillPath (l);
    g.setColour (juce::Colour (0xffd8a800)); g.fillPath (r);
    g.setColour (juce::Colour (0xff2a2100)); g.setFont (fnt (13.0f * s * 1.6f, true));
    g.drawText (txt, juce::Rectangle<float> (x + 18 * s, y + 20 * s, 68 * s, 20 * s), juce::Justification::centred);
}
juce::String U (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* kDown = " \xe2\x96\xbe";

juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String hzStr (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
juce::String timeStr (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s"; }

juce::String fmtParam (const PDef& d, float v)
{
    const juce::String id (d.id);
    if (d.kind == C) { auto ch = juce::StringArray::fromTokens (d.choices, "|", ""); return ch[juce::jlimit (0, ch.size() - 1, (int) v)]; }
    if (d.kind == B) return v > 0.5f ? "on" : "off";
    if (d.kind == I) return juce::String ((int) v);
    if (id == "f1Freq" || id == "f2Freq") return hzStr (20.0f * std::pow (1000.0f, v));
    if (id == "envVol" || id == "vol" || id == "masterVol" || id == "fxPre" || id == "fxPost") return v <= -59.9f ? juce::String ("-inf") : juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
    if (id == "pitch" || id.endsWith ("ResOfs") || id == "imgFormant" || id == "vibDepth") return juce::String (juce::roundToInt (v)) + " ct";
    if (id == "glideLimit") return v < 1.0f ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " ct";
    if (id == "vibSpeed") return juce::String (v, 1) + " Hz";
    if (id == "phaseStart" || id == "tremGap") return juce::String (juce::roundToInt (v)) + U ("\xc2\xb0");
    if (id == "tremSpeed") return timeStr (0.016f * std::pow (1000.0f, v));
    if (id == "declick") return juce::String (v, 1) + " ms";
    if (id == "glideTime" || id == "predelay") { if (std::abs (v) < 0.004f) return "off"; return v < 0 ? timeStr (v * v * (id == "glideTime" ? 1.5f : 1.0f)) : juce::String (v * 16.0f, 1) + " steps"; }
    if (id == "ramp") { if (std::abs (v) < 0.004f) return "0 ms"; return juce::String (juce::roundToInt (std::abs (v) * 200.0f)) + (v < 0 ? " ms abs" : " ms rel"); }
    if (id == "dlTime") return v >= 0 ? juce::String ((int) juce::jmax (1.0f, std::round (v * 16.0f))) + " steps" : timeStr (v * v * 2.0f);
    if (id == "phSpeed") { if (std::abs (v) <= 0.02f) return "still"; return juce::String (v > 0 ? "+" : "-") + juce::String (std::pow (128.0f, 1.0f - std::abs (v)), 1) + " st"; }
    if (id == "imgCoarse") return "x" + juce::String (std::pow (64.0f, v), 3);
    if (id == "freqMul" || id == "detMul") return juce::String (v, 3);
    if (id == "rvDecay") return timeStr (0.1f * std::pow (512.0f, v));
    if (id == "rvPredelay") return timeStr (v * 0.3f);
    if (id == "distHiCut") return v >= 0.995f ? juce::String ("off") : hzStr (500.0f * std::pow (40.0f, v));
    if (id.endsWith ("LowCut") || id.endsWith ("HiCut") || id == "rvDamp") return hzStr (20.0f * std::pow (1000.0f, v));
    if (id == "uniPhase" && v >= 0.99f) return "blur";
    if (id == "pluckTime" && v >= 0.999f) return "off";
    if ((d.lo == 0 || d.lo == -1 || d.lo == -2) && d.hi <= 2) return pct (v);
    return juce::String (v, 2);
}

//==================================================================================================
// one control: a label, a thin bar and a value. Also used for menus and switches.
struct Row : juce::Component
{
    juce::String label; Kind kind = F; float lo = 0, hi = 1, def = 0; juce::StringArray choices;
    std::function<float()> get; std::function<void (float)> set; std::function<juce::String (float)> fmt; std::function<void()> begin, end;
    std::function<void (juce::PopupMenu&)> menuExtra; std::function<void (int)> menuResult;
    float shown = -1.0e9f, startV = 0; int labelW = 62, valueW = 58; bool gesture = false;

    void refresh() { const float v = get ? get() : 0.0f; if (v != shown) repaint(); }
    void paint (juce::Graphics& g) override
    {
        const float v = get ? get() : 0.0f; shown = v;
        const int h = getHeight(), w = getWidth();
        g.setFont (fnt (11.0f)); g.setColour (cFg.withAlpha (0.82f));
        g.drawText (label.toLowerCase(), 0, 0, labelW, h, juce::Justification::centredLeft);
        juce::String txt = fmt ? fmt (v) : juce::String (v, 2);
        g.setColour (cFg); g.setFont (mono (10.0f));
        if (kind == F || kind == I)
        {
            // thin rule · slanted fill · ink edge (the faltung slider)
            const int bx = labelW, bw = juce::jmax (4, w - labelW - valueW - 6), cy = h / 2;
            g.setColour (cFg.withAlpha (0.3f)); g.fillRect ((float) bx, (float) cy, (float) bw, 1.0f);
            const float n = clamp01 ((v - lo) / (hi - lo)), zero = lo < 0 ? clamp01 (-lo / (hi - lo)) : 0.0f;
            const float x0 = (float) bx + juce::jmin (n, zero) * (float) bw, x1 = (float) bx + juce::jmax (n, zero) * (float) bw;
            if (x1 - x0 > 0.5f) { g.setColour (cFill.withAlpha (gesture ? 1.0f : 0.85f)); g.fillPath (skew ({ x0, (float) cy - 3.5f, x1 - x0, 7.0f })); }
            g.setColour (cFg); g.fillRect ((float) bx + n * (float) bw - 1.0f, (float) cy - 5.0f, 2.0f, 11.0f);
            g.drawText (txt.toLowerCase(), w - valueW, 0, valueW, h, juce::Justification::centredRight);
        }
        else if (kind == C)
        {
            g.drawText (txt.toLowerCase(), labelW, 0, w - labelW, h, juce::Justification::centredRight);
            g.setColour (cFg.withAlpha (0.3f)); g.fillRect ((float) labelW + 4.0f, (float) h - 2.0f, (float) (w - labelW - 4), 1.0f);
        }
        else
        {
            const auto r = juce::Rectangle<float> ((float) w - 20.0f, (float) h * 0.5f - 4.5f, 14.0f, 9.0f);
            if (v > 0.5f) { g.setColour (cFill); g.fillPath (skew (r)); }
            g.setColour (cFg); g.strokePath (skew (r), juce::PathStrokeType (1.0f));
        }
    }
    void change (float v)
    {
        v = clampf (v, lo, hi);
        if (kind != F) v = std::round (v);
        if (set) set (v);
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { popup(); return; }
        const float v = get ? get() : 0.0f;
        if (kind == B) { if (begin) begin(); change (v > 0.5f ? 0.0f : 1.0f); if (end) end(); return; }
        if (kind == C)
        {
            juce::PopupMenu m;
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == (int) v);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) { if (begin) begin(); change ((float) (r - 1)); if (end) end(); } });
            return;
        }
        startV = v; gesture = true; if (begin) begin();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! gesture) return;
        const float bw = (float) juce::jmax (40, getWidth() - labelW - valueW - 6);
        const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY()) / bw * (e.mods.isShiftDown() ? 0.1f : 1.0f);
        change (startV + d * (hi - lo));
    }
    void mouseUp (const juce::MouseEvent&) override { if (gesture) { gesture = false; if (end) end(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (kind == F || kind == I) { if (begin) begin(); change (def); if (end) end(); } }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (kind == B) return;
        const float v = get ? get() : 0.0f;
        const float step = kind == F ? (hi - lo) * 0.01f : 1.0f;
        if (std::abs (w.deltaY) > 0.0f) { if (begin) begin(); change (v + (w.deltaY > 0 ? step : -step)); if (end) end(); }
    }
    void popup()
    {
        juce::PopupMenu m;
        m.addItem (1, "Reset");
        if (menuExtra) menuExtra (m);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r == 1) { if (begin) begin(); change (def); if (end) end(); }
            else if (r > 1 && menuResult) menuResult (r);
        });
    }
};

struct Section : juce::Component
{
    juce::String title; std::unique_ptr<Row> head; juce::OwnedArray<Row> rows;
    int wanted() const { return 22 + rows.size() * 16; }
    void paint (juce::Graphics& g) override
    {
        g.setColour (cFg); g.setFont (mono (10.5f, true));
        g.drawText (title.toLowerCase(), 0, 0, getWidth(), 14, juce::Justification::centredLeft);
        g.setColour (cFill.withAlpha (0.7f)); g.fillRect (0, 16, getWidth(), 1);
    }
    void resized() override
    {
        if (head) head->setBounds (70, 0, getWidth() - 70, 15);
        for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, 21 + i * 16, getWidth(), 16);
    }
};

struct Btn : juce::Component
{
    juce::String text; std::function<bool()> isOn; std::function<void()> click; std::function<void()> rightClick; bool boxed = true;
    void paint (juce::Graphics& g) override
    {
        const bool on = isOn && isOn();
        const auto r = getLocalBounds().toFloat().reduced (5.0f, 1.0f).withX (2.0f).withWidth ((float) getWidth() - 4.0f - (float) getHeight() * 0.35f);
        auto shape = skew (r, 0.35f);
        if (on) { g.setColour (cFill); g.fillPath (shape); }
        else if (boxed) { g.setColour (cFg); g.strokePath (shape, juce::PathStrokeType (1.0f)); }
        g.setColour (cFg); g.setFont (mono (10.5f, on));
        g.drawText (text.toLowerCase(), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && rightClick) rightClick(); else if (click) click(); repaint(); }
};
} // namespace

//==================================================================================================
class PartialsEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::Timer
{
public:
    static constexpr int BW = 1440, BH = 900;
    PartialsProcessor& proc;
    int part = 0; bool link = false;
    bool selShape = false; int selIndex = TG::Volume, selArt = 0;       // what the bottom editor shows
    int tab = 0;
    bool snap = true, stepEdit = false, freeze = false, slide = true;
    juce::Component root;
    std::vector<Row*> allRows;
    juce::OwnedArray<Section> sections;
    juce::OwnedArray<Btn> buttons;
    std::unique_ptr<juce::FileChooser> chooser;

    //----------------------------------------------------------------------------------------------
    Row* paramRow (int scope, int index, const juce::String& label)
    {
        auto* r = new Row();
        const PDef& d = scope == 0 ? globalDefs[index] : partDefs[index];
        r->label = label; r->kind = d.kind; r->lo = d.lo; r->hi = d.hi; r->def = d.def;
        if (d.kind == C) r->choices = juce::StringArray::fromTokens (d.choices, "|", "");
        r->get = [this, scope, index] { return scope == 0 ? proc.globalValue (index) : proc.partValue (part, index); };
        auto param = [this, scope, index] (int pt_) { return scope == 0 ? proc.globalParam (index) : proc.partParam (pt_, index); };
        r->set = [this, scope, param] (float v)
        {
            if (auto* p = param (part)) p->setValueNotifyingHost (p->convertTo0to1 (v));
            if (scope == 1 && link) if (auto* p = param (1 - part)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        r->begin = [this, param] { if (auto* p = param (part)) p->beginChangeGesture(); };
        r->end = [this, param] { if (auto* p = param (part)) p->endChangeGesture(); };
        r->fmt = [&d] (float v) { return fmtParam (d, v); };
        if (scope == 1)
        {
            int target = -1;
            for (int t = 0; t < TG::Count; ++t) if (targetInfo[t].pp == index) target = t;
            r->menuExtra = [target] (juce::PopupMenu& m)
            {
                m.addItem (2, "Copy to the other part");
                if (target >= 0) { m.addSeparator(); m.addItem (3, "Edit articulator"); m.addItem (4, "Quick map to X"); m.addItem (5, "Quick map to Y"); m.addItem (6, "Quick map to Z"); }
            };
            r->menuResult = [this, index, target] (int res)
            {
                if (res == 2) { if (auto* p = proc.partParam (1 - part, index)) p->setValueNotifyingHost (p->convertTo0to1 (proc.partValue (part, index))); }
                else if (res == 3 && target >= 0) select (false, target, target >= TG::FirstMapping ? 2 : 0);
                else if (res >= 4 && target >= 0)
                {
                    const int m = MAP::ModX + (res - 4);
                    auto& td = proc.edit (part).tg[target];
                    td.mapOn[m] = true;
                    if (td.map[m].empty()) { if (targetInfo[target].mode == BSum) td.map[m].line ({ { 0, 0.5f }, { 1, 1.0f } }); else td.map[m].line ({ { 0, 0 }, { 1, 1 } }); }
                    proc.commit (part); select (false, target, 2 + m);
                }
            };
        }
        allRows.push_back (r);
        return r;
    }
    Row* valueRow (const juce::String& label, Kind kind, float lo, float hi, float def, std::function<float()> get, std::function<void (float)> set, std::function<juce::String (float)> fmt = nullptr)
    {
        auto* r = new Row();
        r->label = label; r->kind = kind; r->lo = lo; r->hi = hi; r->def = def; r->get = std::move (get); r->set = std::move (set);
        r->fmt = fmt ? std::move (fmt) : (kind == B ? std::function<juce::String (float)> ([] (float v) { return juce::String (v > 0.5f ? "on" : "off"); }) : std::function<juce::String (float)> (pct));
        allRows.push_back (r);
        return r;
    }
    Section* section (juce::Component& parent, const juce::String& title, int headScope = -1, int headIndex = -1)
    {
        auto* s = sections.add (new Section());
        s->title = title;
        if (headScope >= 0) { s->head.reset (paramRow (headScope, headIndex, "")); s->head->labelW = 0; s->addAndMakeVisible (*s->head); }
        parent.addAndMakeVisible (s);
        return s;
    }
    void add (Section* s, int scope, int index, const juce::String& label) { auto* r = s->rows.add (paramRow (scope, index, label)); s->addAndMakeVisible (r); }
    void add (Section* s, Row* r) { s->rows.add (r); s->addAndMakeVisible (r); }
    Btn* button (juce::Component& parent, const juce::String& text, std::function<void()> click, std::function<bool()> isOn = nullptr)
    {
        auto* b = buttons.add (new Btn());
        b->text = text; b->click = std::move (click); b->isOn = std::move (isOn);
        parent.addAndMakeVisible (b);
        return b;
    }

    //----------------------------------------------------------------------------------------------
    // the partial display
    struct Spectro : juce::Component
    {
        PartialsEditor& ed; juce::Image img; bool hzScale = false; float hover = -1; VizFrame frame;
        explicit Spectro (PartialsEditor& e) : ed (e) {}
        float yOf (float hz) const
        {
            const float h = (float) getHeight();
            if (hzScale) return h - hz / 20000.0f * h;
            return h - (std::log2 (juce::jmax (20.0f, hz)) - std::log2 (30.0f)) / (std::log2 (20000.0f) - std::log2 (30.0f)) * h;
        }
        void tick()
        {
            const int w = getWidth(), h = getHeight();
            if (w < 4 || h < 4) return;
            if (! img.isValid() || img.getWidth() != w || img.getHeight() != h) { img = juce::Image (juce::Image::RGB, w, h, true); juce::Graphics g (img); g.fillAll (cBg); }
            img.moveImageSection (0, 0, 2, 0, w - 2, h);
            {
                juce::Graphics g (img);
                g.setColour (cBg); g.fillRect (w - 2, 0, 2, h);
                if (ed.proc.engine.activeVoices() > 0 && ed.proc.engine.readViz (frame))
                    for (int n = 0; n < frame.count && n < NP; ++n)
                    {
                        const float a = frame.amp[n];
                        if (a < 2.0e-4f) continue;
                        const float y = yOf (frame.freq[n]);
                        if (y < 0 || y >= (float) h) continue;
                        const float b = clamp01 ((20.0f * std::log10 (a) + 66.0f) / 60.0f);
                        g.setColour ((b > 0.93f ? cFg : cOval[(n / 6) % 4]).withAlpha (juce::jmin (1.0f, b * 1.1f)));
                        g.fillRect ((float) (w - 2), y - 0.6f, 2.0f, n < 24 ? 1.5f : 1.0f);
                    }
            }
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg);
            if (img.isValid()) g.drawImageAt (img, 0, 0);
            chevrons (g, getLocalBounds().toFloat(), 4, 0.16f);
            g.setFont (mono (10.0f));
            for (float hz : { 100.0f, 1000.0f, 10000.0f })
            {
                const float y = yOf (hz);
                g.setColour (cLine); g.fillRect (0.0f, y, (float) getWidth(), 1.0f);
                g.setColour (cDim.withAlpha (0.7f)); g.drawText (hz >= 1000 ? juce::String ((int) hz / 1000) + "k" : juce::String ((int) hz), 6, (int) y - 13, 40, 12, juce::Justification::left);
            }
            g.setColour (cDim); g.setFont (mono (10.0f));
            g.drawText (juce::String ("partials ") + U ("\xc2\xb7 ") + (hzScale ? "hz" : "oct"), getWidth() - 130, getHeight() - 20, 120, 12, juce::Justification::right);
            if (hover >= 0) g.drawText (hzStr (hover), getWidth() - 130, 8, 120, 12, juce::Justification::right);
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const float t = 1.0f - (float) e.y / (float) getHeight();
            hover = hzScale ? t * 20000.0f : 30.0f * std::pow (20000.0f / 30.0f, t);
        }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; }
        void mouseDown (const juce::MouseEvent&) override { hzScale = ! hzScale; if (img.isValid()) { juce::Graphics g (img); g.fillAll (cBg); } }
    };

    // the two timbre pictures
    struct Wave : juce::Component
    {
        PartialsEditor& ed; int which;
        Wave (PartialsEditor& e, int w) : ed (e), which (w) {}
        void paint (juce::Graphics& g) override
        {
            const bool sel = ed.selShape && ed.selIndex == (which == 0 ? SH::Timbre1 : SH::Timbre2);
            g.setColour (sel ? cFg : cLine); g.drawRect (getLocalBounds(), 1);
            const PartData& d = ed.proc.edit (ed.part);
            const float* lv = d.tab[which == 0 ? SH::Timbre1 : SH::Timbre2]; const float* ph = d.tab[SH::Phase];
            const float th = ed.proc.partValue (ed.part, PP::phaseStart) / 360.0f;
            const int w = getWidth() - 4;
            std::vector<float> y ((size_t) w, 0.0f); float mx = 1.0e-6f;
            for (int x = 0; x < w; ++x)
            {
                const float t = (float) x / (float) w + th; float s = 0;
                for (int n = 0; n < 48; ++n) if (lv[n] > 0) s += lv[n] / (float) (n + 1) * std::sin (kTwoPi * ((float) (n + 1) * t + ph[n] * 0.5f));
                y[(size_t) x] = s; mx = juce::jmax (mx, std::abs (s));
            }
            juce::Path p;
            for (int x = 0; x < w; ++x) { const float yy = (float) getHeight() * 0.5f - y[(size_t) x] / mx * ((float) getHeight() * 0.5f - 5.0f); if (x == 0) p.startNewSubPath (2.0f, yy); else p.lineTo ((float) x + 2.0f, yy); }
            g.setColour (cFg); g.strokePath (p, juce::PathStrokeType (1.2f));
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int sh = which == 0 ? SH::Timbre1 : SH::Timbre2;
            if (! e.mods.isPopupMenu()) { ed.select (true, sh, 0); return; }
            juce::PopupMenu m;
            m.addItem (1, "Analyze single-cycle waveform..."); m.addItem (2, "Randomize"); m.addSeparator();
            m.addItem (3, "Saw"); m.addItem (4, "Square"); m.addItem (5, "Triangle"); m.addItem (6, "Sine");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, sh] (int r)
            {
                if (r == 1)
                {
                    ed.chooser.reset (new juce::FileChooser ("Single-cycle waveform", {}, "*.wav;*.aif;*.aiff;*.flac"));
                    ed.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                             [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) { ed.proc.analyzeWaveform (ed.part, which, fc.getResult()); ed.repaint(); } });
                    return;
                }
                if (r < 2) return;
                Curve& c = ed.proc.edit (ed.part).shape[sh];
                c.pts.clear(); c.raw.assign ((size_t) NH, 0.0f);
                juce::Random rnd;
                for (int h = 0; h < NH; ++h)
                {
                    float l = 0;
                    if (r == 2) l = rnd.nextFloat() < 0.5f ? 0.0f : rnd.nextFloat() * (h < 40 ? 1.0f : 0.5f);
                    else if (r == 3) l = 1.0f;
                    else if (r == 4) l = (h & 1) ? 0.0f : 1.0f;
                    else if (r == 5) l = (h & 1) ? 0.0f : 1.0f / (float) (h + 1);
                    else l = h == 0 ? 1.0f : 0.0f;
                    c.raw[(size_t) h] = yFromLevel (l);
                }
                ed.proc.commit (ed.part, sh); ed.select (true, sh, 0);
            });
        }
    };

    //----------------------------------------------------------------------------------------------
    // the list of things the bottom editor can show
    struct TargetList : juce::Component
    {
        PartialsEditor& ed;
        struct Item { juce::String name; int kind; int index; };
        std::vector<Item> items;
        explicit TargetList (PartialsEditor& e) : ed (e)
        {
            items.push_back ({ "ARTICULATION", 0, 0 });
            for (int t = 0; t < TG::FirstMapping; ++t) items.push_back ({ targetInfo[t].name, 1, t });
            items.push_back ({ "MAPPING", 0, 0 });
            for (int t = TG::FirstMapping; t < TG::Count; ++t) items.push_back ({ targetInfo[t].name, 1, t });
            items.push_back ({ "SHAPING", 0, 0 });
            for (int s = 0; s < SH::Count; ++s) items.push_back ({ shapeDefs[s].name, 2, s });
            setSize (200, (int) items.size() * 17 + 4);
        }
        void paint (juce::Graphics& g) override
        {
            const PartData& d = ed.proc.edit (ed.part);
            PartData* def = nullptr; juce::ignoreUnused (def);
            for (int i = 0; i < (int) items.size(); ++i)
            {
                const auto& it = items[(size_t) i]; const int y = i * 17;
                if (it.kind == 0) { g.setColour (cFg); g.setFont (mono (10.0f, true)); g.drawText (it.name.toLowerCase(), 0, y + 3, getWidth(), 14, juce::Justification::left); continue; }
                const bool sel = (it.kind == 2) == ed.selShape && it.index == ed.selIndex;
                const bool active = it.kind == 1 ? d.tg[it.index].active() : false;
                if (sel) { g.setColour (cFill.withAlpha (0.85f)); g.fillPath (skew ({ 0.0f, (float) y + 2.0f, (float) getWidth() - 22.0f, 13.0f }, 0.35f)); }
                g.setFont (fnt (11.0f, sel)); g.setColour (sel ? cFg : cFg.withAlpha (0.7f));
                g.drawText (it.name.toLowerCase(), 6, y, getWidth() - 20, 17, juce::Justification::centredLeft);
                if (active) { g.setColour (cFg); g.fillPath (skew ({ (float) getWidth() - 14.0f, (float) y + 5.0f, 8.0f, 6.0f })); }
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = e.y / 17;
            if (i < 0 || i >= (int) items.size() || items[(size_t) i].kind == 0) return;
            const auto& it = items[(size_t) i];
            ed.select (it.kind == 2, it.index, it.kind == 2 ? 0 : (it.index >= TG::FirstMapping ? 2 : 0));
        }
    };

    //----------------------------------------------------------------------------------------------
    // the line editor
    struct CurveView : juce::Component
    {
        PartialsEditor& ed; int dragPt = -1, dragTen = -1; float lastPaintX = -1, lastPaintY = 0;
        explicit CurveView (PartialsEditor& e) : ed (e) {}
        bool isEnv() const { return ! ed.selShape && ed.selArt == 0; }
        bool isLfo() const { return ! ed.selShape && ed.selArt == 1; }
        Curve* curve() const
        {
            PartData& d = ed.proc.edit (ed.part);
            if (ed.selShape) return &d.shape[ed.selIndex];
            if (ed.selArt == 0) return &d.tg[ed.selIndex].env.c;
            if (ed.selArt == 1) return nullptr;
            return &d.tg[ed.selIndex].map[ed.selArt - 2];
        }
        float xMax() const { if (! isEnv()) return 1.0f; const Curve* c = curve(); return juce::jmax (1.0f, c->endX() * 1.2f); }
        float px (float x) const { return 8.0f + x / xMax() * ((float) getWidth() - 16.0f); }
        float py (float y) const { return 8.0f + (1.0f - y) * ((float) getHeight() - 16.0f); }
        float ux (float p) const { return (p - 8.0f) / ((float) getWidth() - 16.0f) * xMax(); }
        float uy (float p) const { return clamp01 (1.0f - (p - 8.0f) / ((float) getHeight() - 16.0f)); }
        bool bars() const { const Curve* c = curve(); return c != nullptr && ed.selShape && c->raw.size() == (size_t) NH; }
        float barX (int h) const { return shapeDefs[ed.selIndex].axis == AxHarm ? harmToX (h) : (float) h / (float) (NH - 1); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg);
            const float xm = xMax();
            g.setColour (cLine);
            if (isEnv()) { const float stepX = xm > 8 ? 1.0f : (xm > 2 ? 0.5f : 0.1f); for (float x = 0; x <= xm; x += stepX) g.fillRect (px (x), 0.0f, 1.0f, (float) getHeight()); }
            else for (int i = 0; i <= 16; ++i) { g.setColour (cLine.withAlpha (i % 4 == 0 ? 1.0f : 0.5f)); g.fillRect (px ((float) i / 16.0f), 0.0f, 1.0f, (float) getHeight()); }
            g.setColour (cLine);
            for (int i = 0; i <= 4; ++i) g.fillRect (0.0f, py ((float) i / 4.0f), (float) getWidth(), 1.0f);
            const bool bipolar = ed.selShape ? (shapeDefs[ed.selIndex].ymap == YBi || shapeDefs[ed.selIndex].ymap == YEq || shapeDefs[ed.selIndex].ymap == YLevel)
                                             : (targetInfo[ed.selIndex].mode != UProd && ! (isEnv() && (ed.selIndex == TG::F1Freq || ed.selIndex == TG::F2Freq)));
            const float baseY = bipolar ? py (0.5f) : py (0.0f);

            if (isLfo())
            {
                const LfoDef& l = ed.proc.edit (ed.part).tg[ed.selIndex].lfo;
                juce::Path p;
                for (int x = 0; x < getWidth(); x += 2)
                {
                    const float t = (float) x / (float) getWidth() * 2.0f;
                    const float y = py (0.5f + 0.5f * l.amt * lfoShape (t + l.phase, l));
                    if (x == 0) p.startNewSubPath (0, y); else p.lineTo ((float) x, y);
                }
                g.setColour (l.on ? cFg : cDim); g.strokePath (p, juce::PathStrokeType (1.5f));
                return;
            }
            const Curve* c = curve();
            if (c == nullptr) return;
            if (bars())
            {
                g.setColour (cFg);
                for (int h = 0; h < NH; ++h)
                {
                    const float x0 = px (barX (h)), x1 = h + 1 < NH ? px (barX (h + 1)) : (float) getWidth() - 8.0f;
                    const float y = py (c->raw[(size_t) h]);
                    g.fillRect (x0, juce::jmin (y, baseY), juce::jmax (1.0f, x1 - x0 - (x1 - x0 > 3 ? 1.0f : 0.0f)), std::abs (baseY - y) + 1.0f);
                }
                return;
            }
            if (c->pts.empty()) { g.setColour (cDim); g.setFont (fnt (11.0f)); g.drawText ("right-click to add a point", getLocalBounds(), juce::Justification::centred); return; }
            juce::Path line, fill;
            for (int x = 8; x <= getWidth() - 8; x += 2)
            {
                const float y = py (c->eval (ux ((float) x)));
                if (x == 8) { line.startNewSubPath ((float) x, y); fill.startNewSubPath ((float) x, baseY); fill.lineTo ((float) x, y); } else { line.lineTo ((float) x, y); fill.lineTo ((float) x, y); }
            }
            fill.lineTo ((float) getWidth() - 8.0f, baseY); fill.closeSubPath();
            g.setColour (cFill.withAlpha (0.22f)); g.fillPath (fill);
            g.setColour (cOval[0]); g.strokePath (line, juce::PathStrokeType (2.0f));
            const EnvDef* env = isEnv() ? &ed.proc.edit (ed.part).tg[ed.selIndex].env : nullptr;
            g.setFont (fnt (9.5f));
            for (int i = 0; i < (int) c->pts.size(); ++i)
            {
                const Pt& p = c->pts[(size_t) i];
                const float X = px (p.x), Y = py (p.y);
                g.setColour (cBg); g.fillRect (X - 3.0f, Y - 3.0f, 6.0f, 6.0f);
                g.setColour (cFg); g.drawRect (X - 3.5f, Y - 3.5f, 7.0f, 7.0f, 1.2f);
                if (i > 0)
                {
                    const Pt& q = c->pts[(size_t) i - 1];
                    const float mx = 0.5f * (q.x + p.x);
                    g.setColour (cDim); g.drawEllipse (px (mx) - 2.5f, py (c->eval (mx)) - 2.5f, 5.0f, 5.0f, 1.0f);
                }
                juce::String tag;
                if (env != nullptr) { if (i == env->sustain) tag << "S"; if (i == env->loopStart) tag << "L"; }
                if (p.arp == 1) tag << U ("\xe2\x86\x93"); else if (p.arp == 2) tag << "="; else if (p.arp == 3) tag << U ("\xe2\x86\x91");
                if (tag.isNotEmpty()) { g.setColour (cHot); g.drawText (tag, (int) X - 12, (int) Y - 17, 24, 12, juce::Justification::centred); }
            }
        }
        int hitPoint (juce::Point<float> m) const
        {
            const Curve* c = curve(); if (c == nullptr) return -1;
            for (int i = 0; i < (int) c->pts.size(); ++i) if (std::abs (px (c->pts[(size_t) i].x) - m.x) < 7 && std::abs (py (c->pts[(size_t) i].y) - m.y) < 7) return i;
            return -1;
        }
        int hitTension (juce::Point<float> m) const
        {
            const Curve* c = curve(); if (c == nullptr) return -1;
            for (int i = 1; i < (int) c->pts.size(); ++i)
            {
                const float mx = 0.5f * (c->pts[(size_t) i - 1].x + c->pts[(size_t) i].x);
                if (std::abs (px (mx) - m.x) < 6 && std::abs (py (c->eval (mx)) - m.y) < 6) return i;
            }
            return -1;
        }
        void paintBars (juce::Point<float> m)
        {
            Curve* c = curve();
            const float x = clamp01 (ux (m.x)), y = uy (m.y);
            auto idx = [this] (float xx) { int best = 0; for (int h = 0; h < NH; ++h) if (barX (h) <= xx) best = h; return best; };
            const int h1 = idx (x), h0 = lastPaintX < 0 ? h1 : idx (lastPaintX);
            for (int h = juce::jmin (h0, h1); h <= juce::jmax (h0, h1); ++h)
                c->raw[(size_t) h] = h0 == h1 ? y : lastPaintY + (y - lastPaintY) * (float) (h - h0) / (float) (h1 - h0);
            lastPaintX = x; lastPaintY = y;
            ed.curveChanged();
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            Curve* c = curve();
            if (c == nullptr || ed.freeze) return;
            const auto m = e.position;
            if (bars()) { if (e.mods.isPopupMenu()) { ed.curveOptions (this); return; } lastPaintX = -1; paintBars (m); return; }
            dragPt = hitPoint (m); dragTen = dragPt < 0 ? hitTension (m) : -1;
            if (e.mods.isPopupMenu() || (e.getNumberOfClicks() > 1 && dragPt < 0 && dragTen < 0))
            {
                if (dragPt >= 0) { pointMenu (dragPt); dragPt = -1; return; }
                if (dragTen >= 0) { c->pts[(size_t) dragTen].t = 0; dragTen = -1; ed.curveChanged(); return; }
                Pt p; p.x = juce::jmax (0.0f, snapX (ux (m.x))); p.y = snapY (uy (m.y)); p.type = Single;
                int at = 0; while (at < (int) c->pts.size() && c->pts[(size_t) at].x <= p.x) ++at;
                c->pts.insert (c->pts.begin() + at, p);
                if (isEnv()) { EnvDef& en = ed.proc.edit (ed.part).tg[ed.selIndex].env; if (en.sustain >= at) ++en.sustain; if (en.loopStart >= at) ++en.loopStart; }
                dragPt = at; ed.curveChanged(); return;
            }
            if (dragPt >= 0 && e.mods.isAltDown()) { removePoint (dragPt); dragPt = -1; }
        }
        float snapX (float x) const { if (! ed.snap) return x; const float gq = isEnv() ? (xMax() > 8 ? 0.25f : (xMax() > 2 ? 0.05f : 0.01f)) : 1.0f / 32.0f; return std::round (x / gq) * gq; }
        float snapY (float y) const { return ed.snap ? std::round (y * 16.0f) / 16.0f : y; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            Curve* c = curve();
            if (c == nullptr || ed.freeze) return;
            if (bars()) { paintBars (e.position); return; }
            if (dragPt >= 0 && dragPt < (int) c->pts.size())
            {
                Pt& p = c->pts[(size_t) dragPt];
                float nx = snapX (ux (e.position.x));
                const float lo = dragPt > 0 ? c->pts[(size_t) dragPt - 1].x : 0.0f;
                const bool last = dragPt == (int) c->pts.size() - 1;
                if (dragPt == 0) nx = 0.0f;
                else if (! isEnv() && last) nx = 1.0f;
                nx = juce::jmax (lo, nx);
                if (! isEnv()) nx = juce::jmin (nx, last ? 1.0f : c->pts[(size_t) dragPt + 1].x);
                else if (ed.slide) { const float d = nx - p.x; for (int i = dragPt + 1; i < (int) c->pts.size(); ++i) c->pts[(size_t) i].x += d; }
                else if (! last) nx = juce::jmin (nx, c->pts[(size_t) dragPt + 1].x);
                if (! e.mods.isAltDown()) p.x = nx;
                if (! e.mods.isShiftDown()) p.y = snapY (uy (e.position.y));
                ed.curveChanged();
            }
            else if (dragTen > 0 && dragTen < (int) c->pts.size())
            {
                Pt& p = c->pts[(size_t) dragTen]; const Pt& q = c->pts[(size_t) dragTen - 1];
                const float dir = p.y >= q.y ? 1.0f : -1.0f;
                p.t = clampf (p.t + dir * (float) (e.getDistanceFromDragStartY() - lastTenY) * (e.mods.isCommandDown() ? 0.002f : 0.012f), -1.0f, 1.0f);
                lastTenY = e.getDistanceFromDragStartY();
                ed.curveChanged();
            }
        }
        int lastTenY = 0;
        void mouseUp (const juce::MouseEvent&) override { dragPt = dragTen = -1; lastTenY = 0; lastPaintX = -1; }
        void removePoint (int i)
        {
            Curve* c = curve();
            if (c == nullptr || c->pts.size() <= 1) return;
            c->pts.erase (c->pts.begin() + i);
            if (isEnv()) { EnvDef& en = ed.proc.edit (ed.part).tg[ed.selIndex].env; if (en.sustain == i) en.sustain = -1; else if (en.sustain > i) --en.sustain; if (en.loopStart == i) en.loopStart = -1; else if (en.loopStart > i) --en.loopStart; }
            ed.curveChanged();
        }
        void pointMenu (int i)
        {
            Curve* c = curve();
            juce::PopupMenu m, types, arp;
            for (int t = 0; t < NumSegTypes; ++t) types.addItem (100 + t, segTypeNames[t], true, c->pts[(size_t) i].type == t);
            m.addItem (1, "Delete"); m.addSubMenu ("Segment type", types);
            if (isEnv())
            {
                const EnvDef& en = ed.proc.edit (ed.part).tg[ed.selIndex].env;
                m.addItem (2, "Sustain / loop end", true, en.sustain == i); m.addItem (3, "Loop start", true, en.loopStart == i);
                const char* an[] = { "None", "Previous note", "Same note", "Next note" };
                for (int a = 0; a < 4; ++a) arp.addItem (200 + a, an[a], true, c->pts[(size_t) i].arp == a);
                m.addSubMenu ("Arpeggiator break", arp);
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, i] (int r)
            {
                Curve* cc = curve(); if (cc == nullptr || i >= (int) cc->pts.size() || r == 0) return;
                if (r == 1) { removePoint (i); return; }
                if (r >= 200) cc->pts[(size_t) i].arp = (uint8_t) (r - 200);
                else if (r >= 100) cc->pts[(size_t) i].type = (uint8_t) (r - 100);
                else if (isEnv()) { EnvDef& en = ed.proc.edit (ed.part).tg[ed.selIndex].env; if (r == 2) en.sustain = en.sustain == i ? -1 : i; else en.loopStart = en.loopStart == i ? -1 : i; }
                ed.curveChanged();
            });
        }
    };

    //----------------------------------------------------------------------------------------------
    // the image
    struct ImageView : juce::Component
    {
        PartialsEditor& ed; juce::Image cache; std::shared_ptr<const ImageData> shown; int shownPlane = -1, plane = 0; float selA = -1, selB = -1; float lastPlay = -1;
        explicit ImageView (PartialsEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg);
            auto img = ed.proc.edit (ed.part).image;
            if (img == nullptr || ! img->valid())
            {
                g.setColour (cDim); g.setFont (fnt (12.0f));
                g.drawText ("drop a sound or a picture here", getLocalBounds(), juce::Justification::centred);
                shown.reset(); return;
            }
            if (img != shown || plane != shownPlane)
            {
                // the analysis picture, re-inked: pale field → blue → ink, like the rest of the window
                cache = img->toPicture (plane).convertedToFormat (juce::Image::ARGB);
                juce::Image::BitmapData bd (cache, juce::Image::BitmapData::readWrite);
                for (int y = 0; y < cache.getHeight(); ++y)
                    for (int x = 0; x < cache.getWidth(); ++x)
                    {
                        const auto c = bd.getPixelColour (x, y);
                        const float b = c.getBrightness();
                        const juce::Colour hue = c.getSaturation() > 0.2f ? cHot : cOval[0];
                        bd.setPixelColour (x, y, b < 0.5f ? cBg.interpolatedWith (hue, b * 2.0f) : hue.interpolatedWith (cFg, (b - 0.5f) * 1.4f));
                    }
                shown = img; shownPlane = plane;
            }
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImage (cache, getLocalBounds().toFloat());
            const float w = (float) getWidth(), h = (float) getHeight();
            if (img->hasLoop) { g.setColour (cHot.withAlpha (0.18f)); g.fillRect (img->loopStart * w, 0.0f, (img->loopEnd - img->loopStart) * w, h); }
            if (selA >= 0 && selB >= 0) { g.setColour (cFg.withAlpha (0.15f)); g.fillRect (juce::jmin (selA, selB) * w, 0.0f, std::abs (selB - selA) * w, h); }
            g.setColour (cHot); g.fillRect (ed.proc.partValue (ed.part, PP::imgTime) * (w - 1.0f), 0.0f, 1.0f, h);
            const float pp_ = ed.proc.engine.playPos[ed.part].load();
            if (ed.proc.engine.activeVoices() > 0 && pp_ >= 0) { g.setColour (cFg); g.fillRect (clamp01 (pp_) * (w - 1.0f), 0.0f, 1.0f, h); }
            g.setColour (cDim); g.setFont (fnt (10.0f));
            g.drawText (img->name + "  " + juce::String (img->w) + " x " + juce::String (img->h) + (img->resynth ? "  resynthesis  " : "  image  ") + juce::String (img->seconds(), 2) + " s",
                        8, 6, getWidth() - 16, 12, juce::Justification::left);
        }
        void setTime (float x)
        {
            if (auto* p = ed.proc.partParam (ed.part, PP::imgTime)) p->setValueNotifyingHost (clamp01 (x / (float) getWidth()));
        }
        bool loopDrag = false;
        void mouseDown (const juce::MouseEvent& e) override
        {
            loopDrag = e.mods.isCtrlDown() || e.mods.isCommandDown();
            if (e.mods.isShiftDown() || loopDrag) { selA = selB = clamp01 (e.position.x / (float) getWidth()); } else { selA = selB = -1; setTime (e.position.x); }
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { if (loopDrag && std::abs (selB - selA) > 0.01f) ed.imageOp (10); loopDrag = false; }
        void mouseDrag (const juce::MouseEvent& e) override { if (selA >= 0) selB = clamp01 (e.position.x / (float) getWidth()); else setTime (e.position.x); repaint(); }
    };

    struct OrderList : juce::Component
    {
        juce::String title; int count = 0; std::function<int (int)> get; std::function<juce::String (int)> name; std::function<void (int, int)> swap;
        void paint (juce::Graphics& g) override
        {
            g.setColour (cFg); g.setFont (mono (10.5f, true)); g.drawText (title.toLowerCase(), 0, 0, getWidth(), 14, juce::Justification::left); g.setColour (cFill.withAlpha (0.7f)); g.fillRect (0, 16, getWidth(), 1);
            g.setFont (fnt (11.0f));
            for (int i = 0; i < count; ++i) { g.setColour (cDim); g.drawText (juce::String (i + 1), 0, 21 + i * 16, 16, 16, juce::Justification::left); g.setColour (cFg); g.drawText (name (get (i)).toLowerCase(), 20, 21 + i * 16, getWidth() - 20, 16, juce::Justification::left); }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = (e.y - 21) / 16; if (i < 0 || i >= count) return;
            const int j = e.mods.isPopupMenu() ? i - 1 : i + 1;
            if (j >= 0 && j < count) { swap (i, j); repaint(); }
        }
    };

    //----------------------------------------------------------------------------------------------
    Spectro spectro { *this }; Wave wave1 { *this, 0 }, wave2 { *this, 1 };
    juce::Component pageEnv, pageImg, pageFx, pageAdv;
    juce::Viewport listPort; TargetList targetList { *this };
    CurveView curveView { *this }; ImageView imageView { *this };
    OrderList unitList, fxList;
    float envKnob[4] = { 0, 0, 0, 0 }, envBase[4] = { 0, 0, 0, 0 };
    juce::Label selTitle;
    Row* artMenu = nullptr; Row* artOn = nullptr; Row* smoothRow = nullptr;
    Section* envSec = nullptr; Section* lfoSec = nullptr;
    juce::Label status;

    TargetDef& td() { return proc.edit (part).tg[selShape ? TG::Volume : selIndex]; }
    void curveChanged() { if (selShape) proc.commit (part, selIndex); else proc.commit (part); curveView.repaint(); wave1.repaint(); wave2.repaint(); targetList.repaint(); }
    void select (bool shape, int index, int art)
    {
        selShape = shape; selIndex = index; selArt = art;
        if (! shape && index >= TG::FirstMapping && art < 2) selArt = 2;
        tab = 0; layoutPages(); root.repaint();
    }

    explicit PartialsEditor (PartialsProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        addAndMakeVisible (root);
        root.setSize (BW, BH);
        build();
        setResizable (true, true);
        getConstrainer()->setFixedAspectRatio ((double) BW / (double) BH);
        setResizeLimits (BW / 2, BH / 2, BW * 2, BH * 2);
        setSize (1260, 1260 * BH / BW);
        startTimerHz (30);
    }
    ~PartialsEditor() override { stopTimer(); }

    void resized() override
    {
        root.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) BW));
        root.setBounds (0, 0, BW, BH);
    }
    void paint (juce::Graphics& g) override { g.fillAll (cBg); }

    struct Backdrop : juce::Component
    {
        PartialsEditor& ed; explicit Backdrop (PartialsEditor& e) : ed (e) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg);
            g.setColour (cFg); g.fillRect (0, 44, BW, 1); g.fillRect (0, 580, BW, 1);
            g.setColour (cLine); for (int i = 1; i <= 5; ++i) g.fillRect (i * 196, 45, 1, 535);
            cube (g, 16.0f, 6.0f, 0.29f, "");
            g.setFont (fnt (17.0f, true)); g.setColour (cFg); g.drawText ("partials", 52, 0, 90, 44, juce::Justification::centredLeft);
            // meter and counters
            const float l = clamp01 (ed.proc.meterL.load()), r = clamp01 (ed.proc.meterR.load());
            g.setColour (cTrack); g.fillRect (1340, 18, 80, 3); g.fillRect (1340, 24, 80, 3);
            g.setColour (l > 0.98f ? cHot : cFill); g.fillPath (skew ({ 1340.0f, 17.0f, 80.0f * std::sqrt (l), 4.0f }));
            g.setColour (r > 0.98f ? cHot : cFill); g.fillPath (skew ({ 1340.0f, 24.0f, 80.0f * std::sqrt (r), 4.0f }));
        }
    };
    Backdrop backdrop { *this };

    void build()
    {
        root.addAndMakeVisible (backdrop); backdrop.setBounds (0, 0, BW, BH);

        // top bar
        auto* preset = button (root, "", [this] { presetMenu(); });
        preset->boxed = false; preset->setBounds (130, 12, 220, 20); presetBtn = preset;
        button (root, "A", [this] { setPart (0); }, [this] { return part == 0; })->setBounds (560, 12, 28, 20);
        button (root, "B", [this] { setPart (1); }, [this] { return part == 1; })->setBounds (594, 12, 28, 20);
        auto top = [this] (Row* r, int x, int w, int lw) { r->labelW = lw; root.addAndMakeVisible (r); r->setBounds (x, 14, w, 16); topRows.add (r); };
        top (paramRow (0, GP::partAOn, "A on"), 636, 56, 34); top (paramRow (0, GP::partBOn, "B on"), 702, 56, 34);
        top (valueRow ("link", B, 0, 1, 0, [this] { return link ? 1.0f : 0.0f; }, [this] (float v) { link = v > 0.5f; }), 768, 46, 26);
        top (paramRow (0, GP::abMix, "mix"), 836, 150, 30);
        top (paramRow (1, PP::lfoAmt, "lfo"), 1000, 150, 26);
        top (paramRow (0, GP::masterVol, "volume"), 1164, 160, 48);

        // the five columns
        const int cw = 196, pad = 14, iw = cw - 2 * pad;
        root.addAndMakeVisible (wave1); root.addAndMakeVisible (wave2);
        wave1.setBounds (pad, 54, 81, 44); wave2.setBounds (pad + 87, 54, 81, 44);
        std::vector<std::vector<Section*>> cols (5);
        {
            auto* s = section (root, "TIMBRE", 1, PP::blendMode);
            add (s, 1, PP::timbreMix, "mix"); add (s, 1, PP::phaseStart, "phase"); add (s, 1, PP::phaseRand, "rand");
            add (s, 1, PP::sub1, "sub 1"); add (s, 1, PP::sub2, "sub 2"); add (s, 1, PP::sub3, "sub 3"); add (s, 1, PP::subMode, "subs");
            add (s, 1, PP::prot, "prot"); add (s, 1, PP::clip, "clip"); add (s, 1, PP::clipMode, "mode");
            add (s, 1, PP::envVol, "env"); add (s, 1, PP::vol, "vol"); add (s, 1, PP::autoGain, "gain"); add (s, 1, PP::autoGainMode, "gain mode"); add (s, 1, PP::fxMix, "fx dry");
            cols[0].push_back (s);
            s = section (root, "VELOCITY");
            add (s, 1, PP::velVol, "to volume"); add (s, 1, PP::velAtkSkip, "skips attack"); add (s, 1, PP::velAtkSpeed, "attack speed"); add (s, 1, PP::velRel, "release scale");
            for (auto* r : s->rows) r->labelW = 110;
            cols[0].push_back (s);
            s = section (root, "PLUCK"); add (s, 1, PP::pluckTime, "time"); add (s, 1, PP::pluckBlur, "blur decay"); cols[0].push_back (s);

            s = section (root, "PITCH", 1, PP::pitchMode);
            add (s, 1, PP::freqMul, "freq x"); add (s, 1, PP::freqDiv, "freq /"); add (s, 1, PP::detMul, "detune x"); add (s, 1, PP::detDiv, "detune /");
            add (s, 1, PP::pitch, "pitch"); add (s, 1, PP::pitchEnv, "env"); add (s, 1, PP::pan, "pan"); add (s, 1, PP::vibDepth, "vibrato"); add (s, 1, PP::vibSpeed, "speed");
            cols[1].push_back (s);
            s = section (root, "GLIDE");
            add (s, 1, PP::legato, "legato"); add (s, 1, PP::porta, "portamento"); add (s, 1, PP::glideTime, "time"); add (s, 1, PP::glideLimit, "limit");
            add (s, 1, PP::glideLog, "logarithmic"); add (s, 1, PP::glideVar, "variable time"); add (s, 1, PP::glideVel, "velocity");
            for (auto* r : s->rows) if (r->kind == B) r->labelW = 110;
            cols[1].push_back (s);
            s = section (root, "UNISON", 1, PP::uniType);
            add (s, 1, PP::uniOrder, "order"); add (s, 1, PP::uniPan, "pan"); add (s, 1, PP::uniPitch, "pitch"); add (s, 1, PP::uniPhase, "phase"); add (s, 1, PP::uniAlt, "alt");
            cols[1].push_back (s);
            s = section (root, "PRISM", 1, PP::prismMode); add (s, 1, PP::prismAmt, "amount"); add (s, 1, PP::prismLvl, "by level"); cols[1].push_back (s);

            for (int f = 0; f < 2; ++f)
            {
                const int o = f * kFilterStride;
                s = section (root, f == 0 ? "FILTER 1" : "FILTER 2", 1, PP::f1Type + o);
                add (s, 1, PP::f1Env + o, "env"); add (s, 1, PP::f1Freq + o, "freq"); add (s, 1, PP::f1Kb + o, "kb"); add (s, 1, PP::f1Width + o, "width");
                add (s, 1, PP::f1Res + o, "res"); add (s, 1, PP::f1ResWidth + o, "r.width"); add (s, 1, PP::f1ResOfs + o, "offset"); add (s, 1, PP::f1ResOsc + o, "osc");
                add (s, 1, PP::f1ResType + o, "res type"); add (s, 1, PP::f1Scale + o, "scale"); add (s, 1, PP::f1ResScale + o, "res scale");
                add (s, 1, PP::f1Adapt + o, "adaptive res"); add (s, 1, PP::f1AdaptEnv + o, "adaptive env");
                for (auto* r : s->rows) if (r->kind == B) r->labelW = 110;
                cols[(size_t) (2 + f)].push_back (s);
            }
            s = section (root, "BLUR");
            add (s, 1, PP::blurMix, "mix"); add (s, 1, PP::blurHarm, "harm"); add (s, 1, PP::blurTop, "top"); add (s, 1, PP::blurBottom, "bottom"); add (s, 1, PP::blurAttack, "attack"); add (s, 1, PP::blurDecay, "decay");
            cols[2].push_back (s);
            s = section (root, "FILTERS"); add (s, 1, PP::filtMix, "1 & 2 mix"); add (s, 1, PP::eqAmt, "eq"); cols[2].push_back (s);
            s = section (root, "PHASER", 1, PP::phType);
            add (s, 1, PP::phMix, "mix"); add (s, 1, PP::phWidth, "width"); add (s, 1, PP::phOfs, "offset"); add (s, 1, PP::phSpeed, "speed"); add (s, 1, PP::phKb, "kb"); add (s, 1, PP::phScale, "scale"); add (s, 1, PP::phLfo, "width lfo"); s->rows.getLast()->labelW = 110;
            cols[3].push_back (s);
            s = section (root, "TREMOLO"); add (s, 1, PP::tremSpeed, "speed"); add (s, 1, PP::tremGap, "gap"); add (s, 1, PP::tremDepth, "depth"); cols[3].push_back (s);

            s = section (root, "HARMONIZER");
            add (s, 1, PP::hzMix, "amount"); add (s, 1, PP::hzWidth, "width"); add (s, 1, PP::hzStr, "strength");
            add (s, 1, PP::hzShiftMul, "shift"); add (s, 1, PP::hzShiftOfs, "offset"); add (s, 1, PP::hzShiftStep, "step");
            add (s, 1, PP::hzGapMul, "gap"); add (s, 1, PP::hzGapOfs, "offset"); add (s, 1, PP::hzGapStep, "step");
            cols[4].push_back (s);
            s = section (root, "STRUM", 0, GP::strumMode); add (s, 0, GP::strumTime, "time"); add (s, 0, GP::strumTension, "tension"); cols[4].push_back (s);
            s = section (root, "MODULATION"); add (s, 0, GP::modX, "x"); add (s, 0, GP::modY, "y"); add (s, 0, GP::modZ, "z"); add (s, 0, GP::smoothMod, "smooth"); cols[4].push_back (s);
            s = section (root, "VOICES"); add (s, 0, GP::mono, "mono"); add (s, 0, GP::bendRange, "bend"); cols[4].push_back (s);
        }
        for (int c = 0; c < 5; ++c)
        {
            int y = c == 0 ? 108 : 56;
            for (auto* s : cols[(size_t) c]) { s->setBounds (c * cw + pad, y, iw, s->wanted()); y += s->wanted() + 12; }
        }
        root.addAndMakeVisible (spectro); spectro.setBounds (5 * cw + 1, 45, BW - 5 * cw - 1, 535);

        // bottom: tabs and pages
        const char* tn[] = { "ENV", "IMG", "FX", "ADV" };
        for (int i = 0; i < 4; ++i) button (root, tn[i], [this, i] { tab = i; layoutPages(); root.repaint(); }, [this, i] { return tab == i; })->setBounds (20 + i * 49, 592, 50, 20);
        for (auto* pg : { &pageEnv, &pageImg, &pageFx, &pageAdv }) { root.addAndMakeVisible (pg); pg->setBounds (0, 620, BW, 280); }
        buildEnvPage(); buildImgPage(); buildFxPage(); buildAdvPage();
        status.setFont (fnt (10.0f)); status.setColour (juce::Label::textColourId, cDim); status.setJustificationType (juce::Justification::centredRight);
        root.addAndMakeVisible (status); status.setBounds (900, 594, 520, 16);
        layoutPages();
    }
    juce::OwnedArray<Row> topRows; Btn* presetBtn = nullptr;

    //----------------------------------------------------------------------------------------------
    void buildEnvPage()
    {
        listPort.setViewedComponent (&targetList, false); listPort.setScrollBarsShown (true, false); listPort.setScrollBarThickness (6);
        listPort.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, cTrack);
        pageEnv.addAndMakeVisible (listPort); listPort.setBounds (20, 0, 226, 276);
        pageEnv.addAndMakeVisible (curveView); curveView.setBounds (262, 30, 884, 250);

        selTitle.setFont (fnt (12.0f, true)); selTitle.setColour (juce::Label::textColourId, cFg); pageEnv.addAndMakeVisible (selTitle); selTitle.setBounds (258, 4, 210, 20);
        artMenu = valueRow ("part", C, 0, 10, 0, [this] { return (float) selArt; }, [this] (float v) { selArt = (int) v; if (selIndex >= TG::FirstMapping && selArt < 2) selArt = 2; layoutPages(); root.repaint(); },
                            [this] (float v) { return (int) v == 0 ? juce::String ("Envelope") : ((int) v == 1 ? juce::String ("LFO") : juce::String (mapNames[juce::jlimit (0, MAP::Count - 1, (int) v - 2)])); });
        artMenu->choices = { "Envelope", "LFO" }; for (auto* n : mapNames) artMenu->choices.add (n);
        artMenu->labelW = 30; pageEnv.addAndMakeVisible (artMenu); artMenu->setBounds (470, 6, 210, 16);
        artOn = valueRow ("on", B, 0, 1, 0, [this] { auto& t = td(); return (selArt == 0 ? t.env.on : (selArt == 1 ? t.lfo.on : t.mapOn[selArt - 2])) ? 1.0f : 0.0f; },
                          [this] (float v)
                          {
                              auto& t = td(); const bool on = v > 0.5f;
                              if (selArt == 0) { t.env.on = on; if (on && t.env.c.pts.empty()) t.env.adsr (0.01f, 0.3f, 0.5f, 0.3f); }
                              else if (selArt == 1) t.lfo.on = on;
                              else { t.mapOn[selArt - 2] = on; if (on && t.map[selArt - 2].empty()) { if (targetInfo[selIndex].mode == BSum) t.map[selArt - 2].line ({ { 0, 0.5f }, { 1, 0.5f } }); else t.map[selArt - 2].line ({ { 0, 1 }, { 1, 1 } }); } }
                              curveChanged();
                          });
        artOn->labelW = 20; pageEnv.addAndMakeVisible (artOn); artOn->setBounds (700, 6, 40, 16);
        smoothRow = valueRow ("smooth", F, 0, 1, 0, [this] { return td().smooth; }, [this] (float v) { td().smooth = v; proc.commit (part); }, [] (float v) { return timeStr (v * v * 0.5f); });
        smoothRow->labelW = 46; pageEnv.addAndMakeVisible (smoothRow); smoothRow->setBounds (760, 6, 170, 16);
        auto tog = [this] (const char* name, bool* flag, int x)
        {
            auto* r = valueRow (name, B, 0, 1, 0, [flag] { return *flag ? 1.0f : 0.0f; }, [flag, this] (float v) { *flag = v > 0.5f; curveView.repaint(); });
            r->labelW = 30; pageEnv.addAndMakeVisible (r); r->setBounds (x, 6, 46, 16); return r;
        };
        tog ("snap", &snap, 950); tog ("slide", &slide, 1006); tog ("lock", &freeze, 1062);
        button (pageEnv, "options", [this] { curveOptions (nullptr); })->setBounds (1100, 4, 62, 20);

        envSec = section (pageEnv, "ENVELOPE");
        auto envPt = [this] (int i) -> Pt* { auto& c = td().env.c; return (int) c.pts.size() > i ? &c.pts[(size_t) i] : nullptr; };
        auto seg = [this, envPt] (int i) { auto* a = envPt (i); auto* b = envPt (i + 1); return (a && b) ? b->x - a->x : 0.0f; };
        auto setSeg = [this, envPt] (int i, float v) { auto* a = envPt (i); auto* b = envPt (i + 1); if (! a || ! b) return; const float d = (a->x + v) - b->x; auto& c = td().env.c; for (int k = i + 1; k < (int) c.pts.size(); ++k) c.pts[(size_t) k].x += d; curveChanged(); };
        // the four knobs scale what is drawn, and spring back when let go
        static const char* an[4] = { "attack", "decay", "sustain", "release" };
        for (int k = 0; k < 4; ++k)
        {
            auto* r = valueRow (an[k], F, -1, 1, 0, [this, k] { return envKnob[k]; },
                                [this, k, setSeg, envPt] (float v)
                                {
                                    envKnob[k] = v;
                                    if (k == 2) { if (auto* p = envPt (2)) { p->y = clamp01 (envBase[2] * (1.0f + v)); curveChanged(); } }
                                    else setSeg (k == 3 ? 2 : k, envBase[k] * std::pow (4.0f, v));
                                },
                                [k] (float v) { return "x" + juce::String (k == 2 ? 1.0f + v : std::pow (4.0f, v), 2); });
            r->begin = [this, k, seg, envPt] { envBase[k] = k == 2 ? (envPt (2) ? envPt (2)->y : 0.0f) : seg (k == 3 ? 2 : k); };
            r->end = [this, k] { envKnob[k] = 0.0f; };
            add (envSec, r);
        }
        add (envSec, valueRow ("tempo", B, 0, 1, 0, [this] { return td().env.tempo ? 1.0f : 0.0f; }, [this] (float v) { td().env.tempo = v > 0.5f; curveChanged(); }));
        add (envSec, valueRow ("global", B, 0, 1, 0, [this] { return td().env.global ? 1.0f : 0.0f; }, [this] (float v) { td().env.global = v > 0.5f; curveChanged(); }));
        envSec->setBounds (1166, 0, 254, envSec->wanted());

        lfoSec = section (pageEnv, "LFO");
        auto lf = [this] (const char* name, float lo, float hi, float def, float LfoDef::* m, std::function<juce::String (float)> f = nullptr)
        { add (lfoSec, valueRow (name, F, lo, hi, def, [this, m] { return td().lfo.*m; }, [this, m] (float v) { td().lfo.*m = v; curveChanged(); }, std::move (f))); };
        lf ("amount", -1, 1, 1, &LfoDef::amt);
        lf ("speed", 0, 1, 0.5f, &LfoDef::speed, [this] (float v) { return td().lfo.tempo ? juce::String (16.0f * std::exp2 (-8.0f * v), 2) + " beats" : juce::String (0.05f * std::pow (800.0f, v), 2) + " Hz"; });
        lf ("tension", 0, 1, 0.32f, &LfoDef::tension); lf ("skew", -1, 1, 0, &LfoDef::skew); lf ("pw", -1, 1, 0, &LfoDef::pw); lf ("phase", 0, 1, 0, &LfoDef::phase);
        add (lfoSec, valueRow ("tempo", B, 0, 1, 0, [this] { return td().lfo.tempo ? 1.0f : 0.0f; }, [this] (float v) { td().lfo.tempo = v > 0.5f; curveChanged(); }));
        add (lfoSec, valueRow ("global", B, 0, 1, 0, [this] { return td().lfo.global ? 1.0f : 0.0f; }, [this] (float v) { td().lfo.global = v > 0.5f; curveChanged(); }));
        lfoSec->setBounds (1166, envSec->wanted() + 8, 254, lfoSec->wanted());
    }

    void buildImgPage()
    {
        pageImg.addAndMakeVisible (imageView); imageView.setBounds (20, 0, 840, 272);
        auto* s = section (pageImg, "IMAGE");
        add (s, valueRow ("plane", C, 0, 2, 0, [this] { return (float) imageView.plane; }, [this] (float v) { imageView.plane = (int) v; imageView.repaint(); }, [] (float v) { return juce::String ((int) v == 0 ? "Gain" : ((int) v == 1 ? "Freq" : "All")); }));
        s->rows.getLast()->choices = { "Gain", "Freq", "All" };
        add (s, 1, PP::imgSpeed, "speed"); add (s, 1, PP::imgCoarse, "coarse"); add (s, 1, PP::imgTime, "time"); add (s, 1, PP::imgSmooth, "smooth");
        add (s, 1, PP::imgLoop, "looping"); add (s, 1, PP::imgSharp, "sharp"); add (s, 1, PP::imgDenoise, "denoise");
        s->setBounds (880, 0, 250, s->wanted());
        auto* l = section (pageImg, "LEVEL"); add (l, 1, PP::imgGainScale, "scale"); add (l, 1, PP::imgGainMix, "mix"); add (l, 1, PP::imgGainInterp, "curve");
        l->setBounds (880, s->wanted() + 10, 250, l->wanted());
        auto* f = section (pageImg, "FREQUENCY");
        add (f, 1, PP::imgFreqScale, "scale"); add (f, 1, PP::imgFreqMode, "mode"); add (f, 1, PP::imgFormant, "formant"); add (f, 1, PP::imgFormMix, "form mix"); add (f, 1, PP::imgFreqInterp, "curve");
        f->setBounds (1160, 0, 260, f->wanted());
        button (pageImg, "options", [this] { imageOptions(); })->setBounds (1160, f->wanted() + 14, 70, 20);
        button (pageImg, "open sound", [this] { openImageFile (true); })->setBounds (1240, f->wanted() + 14, 86, 20);
        button (pageImg, "open picture", [this] { openImageFile (false); })->setBounds (1336, f->wanted() + 14, 84, 20);
    }

    void buildFxPage()
    {
        const int w = 250, gap = 30;
        auto place = [&] (Section* s, int col, int y = 0) { s->setBounds (20 + col * (w + gap), y, w, s->wanted()); };
        auto* s = section (pageFx, "DISTORTION", 0, GP::distType);
        add (s, 0, GP::distAmt, "amount"); add (s, 0, GP::distAsym, "asym"); add (s, 0, GP::distWet, "wet"); add (s, 0, GP::distMix, "mix"); add (s, 0, GP::distHiCut, "high cut");
        place (s, 0);
        auto* pp_ = section (pageFx, "LEVELS"); add (pp_, 0, GP::fxPre, "pre"); add (pp_, 0, GP::fxPost, "post"); place (pp_, 0, s->wanted() + 12);

        s = section (pageFx, "CHORUS");
        static const float chP[9][7] = { { 4, .5f, .75f, .06f, 1, -.25f, .5f }, { 5, 1, .85f, 0, 1, .53f, .76f }, { 7, .5f, .5f, 1, 1, .42f, .5f }, { 6, .55f, .83f, 0, 0, .3f, 1 }, { 1, .12f, .3f, 0, 0, 1, -1 },
                                         { 2, .08f, .62f, 0, 0, 0, .75f }, { 1, .04f, .93f, 0, 0, -.17f, 1 }, { 9, .07f, .16f, 0, 0, -.51f, .9f }, { 7, .03f, .96f, 0, 0, 0, .69f } };
        s->head.reset (valueRow ("", C, 0, 8, 0, [] { return -1.0f; }, [this] (float v)
        {
            const int ids[7] = { GP::chOrder, GP::chDepth, GP::chSpeed, GP::chDelay, GP::chSpread, GP::chCross, GP::chMix };
            for (int i = 0; i < 7; ++i) proc.setParam (proc.globalParam (ids[i]), chP[(int) v][i]);
        }, [] (float) { return juce::String ("presets"); }));
        s->head->choices = { "Classic", "Thick", "Deep chorus", "Flanger", "Alt flanger", "Stereoized", "Modulation", "Surround flanger", "Vibration" };
        s->head->labelW = 0; s->addAndMakeVisible (*s->head);
        add (s, 0, GP::chOrder, "order"); add (s, 0, GP::chDepth, "depth"); add (s, 0, GP::chSpeed, "speed"); add (s, 0, GP::chDelay, "delay"); add (s, 0, GP::chSpread, "spread"); add (s, 0, GP::chCross, "cross"); add (s, 0, GP::chMix, "mix");
        place (s, 1);

        s = section (pageFx, "DELAY", 0, GP::dlMode);
        add (s, 0, GP::dlOn, "on"); add (s, 0, GP::dlVol, "input"); add (s, 0, GP::dlPan, "pan"); add (s, 0, GP::dlFb, "feedback"); add (s, 0, GP::dlTime, "time");
        add (s, 0, GP::dlSofs, "st. offset"); add (s, 0, GP::dlLowCut, "low cut"); add (s, 0, GP::dlHiCut, "high cut"); add (s, 0, GP::dlDamp, "fb damping");
        place (s, 2);

        s = section (pageFx, "REVERB");
        // colour, low cut Hz, high cut Hz, predelay s, size, diffusion, decay s, damp Hz, wet
        static const float rvP[6][9] = { { 2, 105, 6300, 0, .18f, 1, .503f, 3000, .55f }, { 2, 20, 11516, .048f, .4f, 1, 2.232f, 5082, .6f }, { 3, 35, 2181, .074f, .9f, 1, 4.589f, 3107, .6f },
                                         { 1, 391, 6086, .083f, .6f, .65f, 2.359f, 3746, .53f }, { 2, 100, 3644, 0, .35f, 1, .602f, 3569, .55f }, { 2, 100, 6086, 0, .17f, 1, .492f, 3043, .55f } };
        s->head.reset (valueRow ("", C, 0, 5, 0, [] { return -1.0f; }, [this] (float v)
        {
            const float* q = rvP[(int) v];
            auto cut = [] (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); };
            proc.setParam (proc.globalParam (GP::rvOn), 1); proc.setParam (proc.globalParam (GP::rvColor), q[0]); proc.setParam (proc.globalParam (GP::rvLowCut), cut (q[1])); proc.setParam (proc.globalParam (GP::rvHiCut), cut (q[2]));
            proc.setParam (proc.globalParam (GP::rvPredelay), q[3] / 0.3f); proc.setParam (proc.globalParam (GP::rvSize), q[4]); proc.setParam (proc.globalParam (GP::rvDiff), q[5]);
            proc.setParam (proc.globalParam (GP::rvDecay), std::log (q[6] / 0.1f) / std::log (512.0f)); proc.setParam (proc.globalParam (GP::rvDamp), cut (q[7])); proc.setParam (proc.globalParam (GP::rvWet), q[8]);
        }, [] (float) { return juce::String ("presets"); }));
        s->head->choices = { "Classic", "Large hall", "Cathedral", "The venue", "Drum room", "Small studio" };
        s->head->labelW = 0; s->addAndMakeVisible (*s->head);
        add (s, 0, GP::rvOn, "on"); add (s, 0, GP::rvColor, "color"); add (s, 0, GP::rvLowCut, "low cut"); add (s, 0, GP::rvHiCut, "high cut"); add (s, 0, GP::rvPredelay, "predelay");
        add (s, 0, GP::rvSize, "room size"); add (s, 0, GP::rvDiff, "diffusion"); add (s, 0, GP::rvDecay, "decay"); add (s, 0, GP::rvDamp, "damping"); add (s, 0, GP::rvWet, "wet");
        place (s, 3);

        s = section (pageFx, "COMPRESSION", 0, GP::cpType);
        add (s, 0, GP::cpAmt, "amount"); add (s, 0, GP::cpLow, "low"); add (s, 0, GP::cpMid, "mid"); add (s, 0, GP::cpHigh, "high");
        place (s, 4);
    }

    void buildAdvPage()
    {
        auto* s = section (pageAdv, "CURRENT PART");
        add (s, 1, PP::predelay, "predelay"); add (s, 1, PP::polyRel, "poly rel"); add (s, 1, PP::ramp, "ramp"); add (s, 1, PP::declick, "declick");
        s->setBounds (20, 0, 250, s->wanted());
        auto* g = section (pageAdv, "GLOBAL");
        add (g, 0, GP::randomness, "randomness"); add (g, 0, GP::sameMaps, "same for all maps"); add (g, 0, GP::invertArp, "invert arp"); add (g, 0, GP::sideStereo, "A/B as stereo sides");
        add (g, 0, GP::partials, "partials"); add (g, 0, GP::precision, "precision"); add (g, 0, GP::hq, "HQ when rendering");
        for (auto* r : g->rows) r->labelW = r->kind == B ? 130 : 70;
        g->setBounds (300, 0, 280, g->wanted());
        unitList.title = "UNIT ORDER (A/B PART)"; unitList.count = UNIT::Count;
        unitList.get = [this] (int i) { return proc.edit (part).unitOrder[i]; }; unitList.name = [] (int u) { return juce::String (unitNames[u]); };
        unitList.swap = [this] (int i, int j) { auto& o = proc.edit (part).unitOrder; std::swap (o[i], o[j]); proc.commit (part); };
        pageAdv.addAndMakeVisible (unitList); unitList.setBounds (620, 0, 220, 190);
        fxList.title = "EFFECTS ORDER"; fxList.count = FXU::Count;
        fxList.get = [this] (int i) { return proc.fxOrder[i]; }; fxList.name = [] (int u) { return juce::String (fxNames[u]); };
        fxList.swap = [this] (int i, int j) { int o[FXU::Count]; for (int k = 0; k < FXU::Count; ++k) o[k] = proc.fxOrder[k]; std::swap (o[i], o[j]); proc.setFxOrder (o); };
        pageAdv.addAndMakeVisible (fxList); fxList.setBounds (880, 0, 220, 110);
        auto* note = new juce::Label ({}, "click an entry to move it later, right-click to move it earlier");
        note->setFont (fnt (10.0f)); note->setColour (juce::Label::textColourId, cDim); pageAdv.addAndMakeVisible (note); note->setBounds (616, 196, 400, 16); notes.add (note);
    }
    juce::OwnedArray<juce::Label> notes;

    void layoutPages()
    {
        pageEnv.setVisible (tab == 0); pageImg.setVisible (tab == 1); pageFx.setVisible (tab == 2); pageAdv.setVisible (tab == 3);
        const bool art = ! selShape;
        artMenu->setVisible (art); artOn->setVisible (art); smoothRow->setVisible (art);
        envSec->setVisible (art && selIndex < TG::FirstMapping); lfoSec->setVisible (art && selIndex < TG::FirstMapping);
        proc.engine.vizPart.store (part);
        selTitle.setText (juce::String (selShape ? shapeDefs[selIndex].name : targetInfo[selIndex].name).toLowerCase(), juce::dontSendNotification);
    }
    void setPart (int p) { part = p; layoutPages(); root.repaint(); }

    //----------------------------------------------------------------------------------------------
    void timerCallback() override
    {
        for (auto* r : allRows) if (r->isShowing()) r->refresh();
        spectro.tick();
        backdrop.repaint (1330, 10, 100, 24);
        if (tab == 1) imageView.repaint();
        if (presetBtn != nullptr) { const juce::String t = "preset  " + proc.presetName + U (kDown); if (presetBtn->text != t) { presetBtn->text = t; presetBtn->repaint(); } }
        status.setText (juce::String (part == 0 ? "part A    " : "part B    ") + juce::String (proc.engine.activeVoices()) + " voices  "
                        + juce::String (proc.engine.activePartials()) + " partials  " + juce::String (juce::roundToInt (proc.cpu.load() * 100.0f)) + "%", juce::dontSendNotification);
        if (++slow % 15 == 0) { wave1.repaint(); wave2.repaint(); }
    }
    int slow = 0;

    void paintOverChildren (juce::Graphics&) override {}

    //----------------------------------------------------------------------------------------------
    // files
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        if (files.isEmpty()) return;
        const juce::File f (files[0]);
        const auto pt_ = root.getLocalPoint (this, juce::Point<int> (x, y));
        const bool picture = f.hasFileExtension ("png;jpg;jpeg;gif;bmp");
        if (f.hasFileExtension ("partials")) { loadPreset (f); return; }
        if (! picture && wave1.getBounds().contains (pt_)) { proc.analyzeWaveform (part, 0, f); root.repaint(); return; }
        if (! picture && wave2.getBounds().contains (pt_)) { proc.analyzeWaveform (part, 1, f); root.repaint(); return; }
        if (! picture && tab == 0 && ! selShape && selArt == 0 && pageEnv.getBounds().contains (pt_)) { envelopeFromAudio (f); return; }
        if (picture) proc.loadPictureFile (part, f, imageView.plane == 1); else proc.loadAudioFile (part, f, highPrecision, proc.globalValue (GP::sideStereo) > 0.5f ? part + 1 : 0);
        tab = 1; layoutPages(); root.repaint();
    }
    bool highPrecision = false;
    void openImageFile (bool sound)
    {
        chooser.reset (new juce::FileChooser (sound ? "Sound to resynthesize" : "Picture", {}, sound ? "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg" : "*.png;*.jpg;*.jpeg;*.gif"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, sound] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (! f.existsAsFile()) return;
            if (sound) proc.loadAudioFile (part, f, highPrecision, proc.globalValue (GP::sideStereo) > 0.5f ? part + 1 : 0); else proc.loadPictureFile (part, f, imageView.plane == 1);
            root.repaint();
        });
    }
    void envelopeFromAudio (const juce::File& f)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples < 64) return;
        const int len = (int) std::min ((juce::int64) r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
        juce::AudioBuffer<float> b (1, len); r->read (&b, 0, len, 0, true, false);
        const int n = 64; std::vector<float> lv ((size_t) n); float mx = 1.0e-6f;
        for (int i = 0; i < n; ++i) { lv[(size_t) i] = b.getRMSLevel (0, i * len / n, juce::jmax (1, len / n)); mx = juce::jmax (mx, lv[(size_t) i]); }
        auto& e = td().env; e.c.pts.clear(); e.sustain = -1; e.loopStart = -1; e.on = true;
        const float dur = (float) ((double) len / r->sampleRate);
        e.c.pts.push_back ({ 0, 0, 0, Single, 0 });
        for (int i = 0; i < n; ++i) e.c.pts.push_back ({ dur * (float) (i + 1) / (float) (n + 1), lv[(size_t) i] / mx, 0, Smooth, 0 });
        e.c.pts.push_back ({ dur, 0, 0, Single, 0 });
        curveChanged();
    }

    //----------------------------------------------------------------------------------------------
    // menus
    void curveOptions (juce::Component* target)
    {
        juce::PopupMenu m, seq;
        const bool env = ! selShape && selArt == 0;
        m.addItem (1, "Reset"); m.addItem (2, "Flip vertically"); m.addItem (3, "Normalize levels"); m.addItem (4, "Decimate points"); m.addItem (5, "Smooth up");
        m.addItem (6, "Turn all points smooth"); m.addItem (7, "Scale levels: 50%"); m.addItem (8, "Scale levels: 200%");
        if (selShape) { m.addSeparator(); m.addItem (20, "Draw per harmonic (bars)"); m.addItem (21, "Back to a line"); }
        if (env)
        {
            m.addSeparator(); m.addItem (10, "Make all loop"); m.addItem (11, "Analyze audio file...");
            seq.addItem (30, "8 steps"); seq.addItem (31, "16 steps"); seq.addItem (32, "16 steps, random gates"); seq.addItem (33, "8 steps, arpeggio up"); seq.addItem (34, "8 steps, arpeggio down"); seq.addItem (35, "16 steps, random arpeggio");
            m.addSubMenu ("Create sequence", seq);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target != nullptr ? target : (juce::Component*) &curveView), [this] (int r) { if (r > 0) curveOp (r); });
    }
    void curveOp (int r)
    {
        Curve* c = curveView.curve();
        if (c == nullptr) return;
        const bool env = ! selShape && selArt == 0;
        auto each = [c] (std::function<float (float)> f) { for (auto& p : c->pts) p.y = clamp01 (f (p.y)); for (auto& v : c->raw) v = clamp01 (f (v)); };
        switch (r)
        {
            case 1: if (selShape) proc.edit (part).defaultShape (selIndex); else if (env) td().env.adsr (0.002f, 0.3f, 1.0f, 0.15f); else c->pts.clear(); break;
            case 2: each ([] (float y) { return 1.0f - y; }); break;
            case 3: { float lo = 1, hi = 0; each ([&] (float y) { lo = juce::jmin (lo, y); hi = juce::jmax (hi, y); return y; }); if (hi - lo > 1.0e-4f) each ([&] (float y) { return (y - lo) / (hi - lo); }); break; }
            case 4:
            {
                if (! c->raw.empty()) { Curve n; for (int h = 0; h < NH; h += (h < 16 ? 1 : (h < 64 ? 4 : 32))) n.pts.push_back ({ harmToX (h), c->raw[(size_t) h], 0, Single, 0 }); n.pts.push_back ({ 1, c->raw.back(), 0, Single, 0 }); *c = n; break; }
                if (c->pts.size() > 4 && ! env) { std::vector<Pt> k; for (size_t i = 0; i < c->pts.size(); ++i) if (i == 0 || i + 1 == c->pts.size() || (i & 1) == 0) k.push_back (c->pts[i]); c->pts = k; }
                break;
            }
            case 5:
                if (! c->raw.empty()) { auto s = c->raw; for (size_t i = 1; i + 1 < s.size(); ++i) c->raw[i] = 0.25f * s[i - 1] + 0.5f * s[i] + 0.25f * s[i + 1]; }
                else { auto s = c->pts; for (size_t i = 1; i + 1 < s.size(); ++i) c->pts[i].y = 0.25f * s[i - 1].y + 0.5f * s[i].y + 0.25f * s[i + 1].y; }
                break;
            case 6: for (auto& p : c->pts) p.type = Smooth; break;
            case 7: each ([] (float y) { return 0.5f + (y - 0.5f) * 0.5f; }); break;
            case 8: each ([] (float y) { return 0.5f + (y - 0.5f) * 2.0f; }); break;
            case 10: if (env && c->pts.size() > 1) { td().env.loopStart = 0; td().env.sustain = (int) c->pts.size() - 1; } break;
            case 11:
                chooser.reset (new juce::FileChooser ("Sound to follow", {}, "*.wav;*.aif;*.aiff;*.flac;*.mp3"));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) envelopeFromAudio (fc.getResult()); });
                return;
            case 20: { std::vector<float> rw ((size_t) NH); const bool harm = shapeDefs[selIndex].axis == AxHarm; for (int h = 0; h < NH; ++h) rw[(size_t) h] = c->eval (harm ? harmToX (h) : (float) h / (float) (NH - 1)); c->pts.clear(); c->raw = rw; break; }
            case 21: if (! c->raw.empty()) { curveOp (4); return; } break;
            default:
                if (r >= 30 && env)
                {
                    const int steps = (r == 30 || r == 33 || r == 34) ? 8 : 16;
                    juce::Random rnd; auto& e = td().env;
                    e.c.pts.clear(); e.tempo = true; e.on = true;
                    for (int i = 0; i < steps; ++i)
                    {
                        const float t0 = (float) i * 0.25f;
                        const bool gate = r != 32 || rnd.nextFloat() < 0.7f;
                        uint8_t arp = 0;
                        if (r == 33) arp = 3; else if (r == 34) arp = 1; else if (r == 35) arp = (uint8_t) (1 + rnd.nextInt (3));
                        e.c.pts.push_back ({ t0, gate ? 1.0f : 0.0f, 0, Hold, arp });
                        e.c.pts.push_back ({ t0 + 0.2f, gate ? 0.25f : 0.0f, -0.5f, Single, 0 });
                    }
                    e.c.pts.push_back ({ (float) steps * 0.25f, 0.0f, 0, Single, 0 });
                    e.loopStart = 0; e.sustain = (int) e.c.pts.size() - 1;
                }
                break;
        }
        curveChanged();
    }

    void imageOptions()
    {
        juce::PopupMenu m;
        auto img = proc.edit (part).image;
        const bool has = img != nullptr && img->valid();
        m.addItem (1, "Analyze audio file..."); m.addItem (2, "Open image file..."); m.addItem (3, "High precision analysis", true, highPrecision);
        m.addItem (4, "Convert to image synthesis", has && img->resynth); m.addItem (5, "Remove image", has); m.addItem (6, "Save image as PNG...", has);
        m.addSeparator();
        m.addItem (10, "Set selection as loop region (shift-drag in the image)", has && imageView.selA >= 0); m.addItem (11, "Remove loop region", has && img->hasLoop);
        m.addItem (12, "Map audio regions / slices to keys", has); m.addItem (13, "Prepare time envelope for drum loop", has);
        m.addItem (14, "Prepare image time envelope", has); m.addItem (15, "Create bouncing loop time envelope", has); m.addItem (16, "Map time offset to last hit key", has);
        m.addSeparator();
        m.addItem (20, "Generate random cloud"); m.addItem (21, "Degrade horizontally", has); m.addItem (22, "Degrade vertically", has);
        m.addItem (23, "Crossfade 10%", has); m.addItem (24, "Crossfade 25%", has); m.addItem (25, "Crossfade 50%", has);
        m.addItem (26, "Crop top", has); m.addItem (27, "Invert", has); m.addItem (28, "Flip vertically", has);
        m.addItem (29, "Edit the column at the time offset", has);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&imageView), [this] (int r) { if (r > 0) imageOp (r); });
    }
    void imageOp (int r)
    {
        auto img = proc.edit (part).image;
        const bool has = img != nullptr && img->valid();
        auto timeEnv = [this, &img] (bool bounce)
        {
            auto& t = proc.edit (part).tg[TG::ImgTime]; const float dur = img->seconds();
            t.env.on = true; t.env.tempo = false; t.env.c.pts.clear();
            t.env.c.pts.push_back ({ 0, 0.5f, 0, Single, 0 }); t.env.c.pts.push_back ({ dur, 1.0f, 0, Single, 0 });
            t.env.sustain = -1; t.env.loopStart = -1;
            if (bounce) { t.env.c.pts.push_back ({ 2 * dur, 0.5f, 0, Single, 0 }); t.env.loopStart = 0; t.env.sustain = 2; }
            proc.setParam (proc.partParam (part, PP::imgSpeed), 0.0f); proc.setParam (proc.partParam (part, PP::imgTime), 0.0f);
            proc.commit (part); select (false, TG::ImgTime, 0);
        };
        switch (r)
        {
            case 1: openImageFile (true); return;
            case 2: openImageFile (false); return;
            case 3: highPrecision = ! highPrecision; return;
            case 4: if (has) proc.setImage (part, img->toImageSynthesis()); break;
            case 5: proc.setImage (part, nullptr); proc.edit (part).image.reset(); proc.commit (part); break;
            case 6:
                chooser.reset (new juce::FileChooser ("Save image", {}, "*.png"));
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, img] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult(); if (f == juce::File()) return;
                    f = f.withFileExtension ("png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; if (os.openedOk()) png.writeImageToStream (img->toPicture (imageView.plane), os);
                });
                return;
            case 10: if (has) { proc.setImage (part, img->withLoop (imageView.selA, imageView.selB)); if ((int) proc.partValue (part, PP::imgLoop) == 0) proc.setParam (proc.partParam (part, PP::imgLoop), 1.0f); imageView.selA = imageView.selB = -1; } break;
            case 11: if (has) { auto d = img->clone(); d->hasLoop = false; proc.setImage (part, d); } break;
            case 12: case 13:
                if (has)
                {
                    auto sl = img->slices(); auto& t = proc.edit (part).tg[TG::ImgTime];
                    Curve& c = t.map[MAP::Key]; c.pts.clear();
                    c.pts.push_back ({ 0, 0.5f, 0, Hold, 0 });
                    for (int i = 0; i < (int) sl.size() && 60 + i < 127; ++i) c.pts.push_back ({ ((float) (60 + i) - 0.5f) / 127.0f, 0.5f + 0.5f * sl[(size_t) i], 0, Hold, 0 });
                    c.pts.push_back ({ 1, c.pts.back().y, 0, Hold, 0 });
                    t.mapOn[MAP::Key] = true;
                    proc.setParam (proc.partParam (part, PP::imgTime), 0.0f);
                    proc.commit (part); select (false, TG::ImgTime, 2 + MAP::Key);
                }
                return;
            case 14: if (has) timeEnv (false); return;
            case 15: if (has) timeEnv (true); return;
            case 16:
                if (has && proc.engine.lastKey.load() >= 0)
                {
                    auto& t = proc.edit (part).tg[TG::ImgTime]; Curve& c = t.map[MAP::Key];
                    if (c.pts.empty()) { c.pts.push_back ({ 0, 0.5f, 0, Hold, 0 }); c.pts.push_back ({ 1, 0.5f, 0, Hold, 0 }); }
                    c.pts.push_back ({ ((float) proc.engine.lastKey.load() - 0.5f) / 127.0f, 0.5f + 0.5f * proc.partValue (part, PP::imgTime), 0, Hold, 0 });
                    c.sort(); t.mapOn[MAP::Key] = true; proc.commit (part);
                }
                return;
            case 20: { auto d = ImageData::randomCloud ((uint32_t) juce::Random::getSystemRandom().nextInt()); d->name = "cloud"; proc.setImage (part, d); break; }
            case 21: if (has) proc.setImage (part, img->degraded (true)); break;
            case 22: if (has) proc.setImage (part, img->degraded (false)); break;
            case 23: case 24: case 25: if (has) proc.setImage (part, img->crossfaded (r == 23 ? 0.1f : (r == 24 ? 0.25f : 0.5f))); break;
            case 26: if (has) proc.setImage (part, img->croppedTop()); break;
            case 27: if (has) proc.setImage (part, img->inverted (imageView.plane)); break;
            case 28: if (has) proc.setImage (part, img->flippedVertically()); break;
            case 29:
                if (has)
                {
                    // the column becomes timbre 1's harmonic levels, where it can be drawn on
                    const int col = juce::jlimit (0, img->w - 1, (int) (proc.partValue (part, PP::imgTime) * (float) (img->w - 1)));
                    Curve& c = proc.edit (part).shape[SH::Timbre1]; c.pts.clear(); c.raw.assign ((size_t) NH, 0.0f);
                    for (int h = 0; h < img->h; ++h) c.raw[(size_t) h] = yFromLevel (img->g (col)[h]);
                    proc.commit (part, SH::Timbre1); select (true, SH::Timbre1, 0);
                }
                return;
            default: break;
        }
        root.repaint();
    }

    //----------------------------------------------------------------------------------------------
    // presets
    void loadPreset (const juce::File& f)
    {
        if (auto xml = juce::parseXML (f)) { proc.applyState (juce::ValueTree::fromXml (*xml)); proc.presetName = f.getFileNameWithoutExtension(); root.repaint(); }
    }
    void factory (int i)
    {
        proc.initPreset();
        auto P = [this] (int pt_, int id, float v) { proc.setParam (proc.partParam (pt_, id), v); };
        auto G = [this] (int id, float v) { proc.setParam (proc.globalParam (id), v); };
        PartData& a = proc.edit (0);
        switch (i)
        {
            case 1: proc.presetName = "Pluck";
                P (0, PP::pluckTime, 0.42f); P (0, PP::uniOrder, 3); P (0, PP::uniPitch, 0.2f); a.tg[TG::Volume].env.adsr (0.001f, 1.2f, 0.0f, 0.4f); a.tg[TG::Volume].env.on = true; G (GP::rvOn, 1); G (GP::rvWet, 0.3f); break;
            case 2: proc.presetName = "Wide pad";
                P (0, PP::uniOrder, 7); P (0, PP::uniPitch, 0.45f); P (0, PP::uniPan, 0.9f); P (0, PP::f1Type, 1); P (0, PP::f1Freq, 0.62f); P (0, PP::f1Res, 0.2f); P (0, PP::blurAttack, 0.3f);
                a.tg[TG::Volume].env.adsr (0.6f, 0.5f, 0.8f, 1.5f); a.tg[TG::Volume].env.on = true; a.tg[TG::F1Freq].lfo.on = true; a.tg[TG::F1Freq].lfo.amt = 0.1f; a.tg[TG::F1Freq].lfo.speed = 0.2f;
                G (GP::rvOn, 1); G (GP::rvDecay, 0.6f); G (GP::chOrder, 4); break;
            case 3: proc.presetName = "Phaser bass";
                P (0, PP::freqDiv, 2); P (0, PP::phMix, 0.8f); P (0, PP::phSpeed, 0.5f); P (0, PP::f1Type, 2); P (0, PP::f1Freq, 0.55f); P (0, PP::f1Res, 0.5f); P (0, PP::f1Env, 0.4f); P (0, PP::sub1, 0.4f);
                a.tg[TG::F1Freq].env.adsr (0.001f, 0.25f, 0.0f, 0.2f); a.tg[TG::F1Freq].env.on = true; a.tg[TG::Volume].env.on = true; G (GP::distType, 1); G (GP::distAmt, 0.3f); break;
            case 4: proc.presetName = "Prism bell";
                P (0, PP::timbreMix, 1); P (0, PP::prismAmt, 0.55f); P (0, PP::pluckTime, 0.6f); P (0, PP::hzMix, 0.4f); P (0, PP::hzShiftStep, 3);
                a.tg[TG::Volume].env.adsr (0.001f, 2.5f, 0.0f, 1.5f); a.tg[TG::Volume].env.on = true; G (GP::rvOn, 1); G (GP::rvDecay, 0.55f); break;
            case 5: proc.presetName = "White noise";
                P (0, PP::pitchMode, 1); P (0, PP::freqDiv, 9); P (0, PP::detDiv, 6); P (0, PP::uniOrder, 3); P (0, PP::uniPan, 0); P (0, PP::uniPitch, 1); P (0, PP::uniType, 2); P (0, PP::phaseRand, -1); P (0, PP::ramp, 0);
                a.shape[SH::Timbre1].line ({ { 0, 0.2f }, { 1, 0.95f } });
                a.tg[TG::Pitch].mapOn[MAP::Key] = true; a.tg[TG::Pitch].map[MAP::Key].line ({ { 0, 0.5f + 60.0f / 96.0f * 0.5f }, { 1, 0.5f - 67.0f / 96.0f * 0.5f } }); break;
            case 6: proc.presetName = "Arp sequence";
                P (0, PP::f1Type, 1); P (0, PP::f1Freq, 0.6f); P (0, PP::f1Res, 0.4f); P (0, PP::pluckTime, 0.5f); select (false, TG::Volume, 0); curveOp (33); a.tg[TG::Volume].env.on = true; G (GP::dlOn, 1); break;
            case 7: proc.presetName = "Vowel";
                P (0, PP::f1Type, 14); P (0, PP::f1Freq, 0.55f); P (0, PP::f1Width, 0.7f); P (0, PP::f1Res, 0.5f); P (0, PP::f1ResType, 3); P (0, PP::vibDepth, 12); P (0, PP::uniOrder, 2);
                a.tg[TG::F1Freq].mapOn[MAP::ModX] = true; a.tg[TG::F1Freq].map[MAP::ModX].line ({ { 0, 0.3f }, { 1, 0.7f } }); a.tg[TG::Volume].env.adsr (0.05f, 0.3f, 0.9f, 0.3f); a.tg[TG::Volume].env.on = true; break;
            default: proc.presetName = "Default"; break;
        }
        proc.commit (0); root.repaint();
    }
    void presetMenu()
    {
        juce::PopupMenu m, fac, user;
        const char* fn[] = { "Default", "Pluck", "Wide pad", "Phaser bass", "Prism bell", "White noise", "Arp sequence", "Vowel" };
        for (int i = 0; i < 8; ++i) fac.addItem (100 + i, fn[i]);
        presetFiles = PartialsProcessor::presetFolder().findChildFiles (juce::File::findFiles, true, "*.partials");
        presetFiles.sort();
        for (int i = 0; i < presetFiles.size(); ++i) user.addItem (1000 + i, presetFiles[i].getRelativePathFrom (PartialsProcessor::presetFolder()).upToLastOccurrenceOf (".", false, false));
        m.addSubMenu ("Built in", fac); m.addSubMenu ("Saved", user, presetFiles.size() > 0);
        m.addSeparator();
        m.addItem (1, "Save preset as..."); m.addItem (2, "Open preset file...");
        m.addSeparator();
        m.addItem (3, "Copy preset"); m.addItem (4, "Paste preset"); m.addItem (5, "Copy this part"); m.addItem (6, "Paste into this part"); m.addItem (7, juce::String ("Copy part ") + (part == 0 ? "A to B" : "B to A"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn), [this] (int r)
        {
            if (r >= 1000 && r - 1000 < presetFiles.size()) loadPreset (presetFiles[r - 1000]);
            else if (r >= 100) factory (r - 100);
            else if (r == 1)
            {
                chooser.reset (new juce::FileChooser ("Save preset", PartialsProcessor::presetFolder().getChildFile (proc.presetName + ".partials"), "*.partials"));
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult(); if (f == juce::File()) return;
                    f = f.withFileExtension ("partials"); proc.presetName = f.getFileNameWithoutExtension();
                    if (auto xml = proc.makeState().createXml()) xml->writeTo (f);
                });
            }
            else if (r == 2)
            {
                chooser.reset (new juce::FileChooser ("Open preset", PartialsProcessor::presetFolder(), "*.partials"));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadPreset (fc.getResult()); });
            }
            else if (r == 3) juce::SystemClipboard::copyTextToClipboard (proc.makeState().toXmlString());
            else if (r == 5) juce::SystemClipboard::copyTextToClipboard (proc.makePartState (part).toXmlString());
            else if (r == 4 || r == 6)
            {
                if (auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard()))
                {
                    auto v = juce::ValueTree::fromXml (*xml);
                    if (r == 4 && v.hasType ("Partials")) proc.applyState (v); else if (r == 6 && v.hasType ("Part")) proc.applyPartState (part, v);
                }
            }
            else if (r == 7) proc.copyPart (part, 1 - part);
            root.repaint();
        });
    }
    juce::Array<juce::File> presetFiles;
};

juce::AudioProcessorEditor* PartialsProcessor::createEditor() { return new PartialsEditor (*this); }
