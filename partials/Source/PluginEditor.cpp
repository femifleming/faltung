// Partials: the window. Every section sits where a Harmor user expects it (the part's panel on top with the
// filters to its right, Global bottom left with the X / Y pad, the articulation editor and its ENV / IMG / FX / ADV
// tabs bottom centre, the partial display down the right, the keyboard along the bottom); the look is the
// faltung series' own: a deep blue-slate field, slanted bars, lowercase Inter / IBM Plex Mono, thin rules.
#include "PluginProcessor.h"
#include "Widgets.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
#include <thread>
#include <iostream>

using namespace pt;
using namespace pt::ui;

//==================================================================================================
class PartialsEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::Timer,
                       public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater
{
public:
    // laid out on this canvas (the proportions of the reference window) and scaled to the window's size
    static constexpr int BW = 1857, BHK = 1022, BHN = 914;
    PartialsProcessor& proc;
    int part = 0; bool link = false;
    bool selShape = false; int selIndex = TG::Volume, selArt = 0;       // what the editor shows: a shape, or a target and one of its parts
    int tab = 0, vizMode = 0;
    bool snap = true, stepEdit = false, freeze = false, slide = true, showKeys = true;
    juce::Component root;
    Look look;
    std::vector<Row*> allRows;
    juce::OwnedArray<Row> rows; juce::OwnedArray<Seg> segs; juce::OwnedArray<Btn> buttons; juce::OwnedArray<Section> sections;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String hintText, noteText;
    int baseH() const { return showKeys ? BHK : BHN; }

    void setHint (const juce::String& h) { if (h != hintText) { hintText = h; info.repaint(); } }
    void showMenu (juce::PopupMenu& m, juce::Component* target, std::function<void (int)> cb)
    {
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withDeletionCheck (*this), std::move (cb));
    }

    //----------------------------------------------------------------------------------------------
    // controls. scope: 0 a global parameter, 1 a parameter of the part on show, 2 one of the later globals,
    // 3 a part parameter that Harmor keeps in Global (legato, portamento, velocity): written to both parts
    int leadPart() const { return proc.globalValue (GP::partAOn) > 0.5f ? 0 : 1; }
    const PDef& defOf (int scope, int index) const { return scope == 0 ? globalDefs[index] : (scope == 2 ? extraDefs[index] : partDefs[index]); }
    std::set<std::pair<int, int>> covered;       // (scope, index) of every parameter that has a control in the window
    void cover (int scope, int index) { covered.insert ({ scope == 3 ? 1 : scope, index }); }
    Row* paramRow (int scope, int index, const juce::String& label)
    {
        auto* r = new Row();
        const PDef& d = defOf (scope, index);
        cover (scope, index);
        r->label = label; r->hintName = juce::String (d.name).toLowerCase(); r->kind = d.kind; r->lo = d.lo; r->hi = d.hi; r->def = d.def; r->look = &look;
        if (d.kind == C) r->choices = juce::StringArray::fromTokens (d.choices, "|", "");
        r->get = [this, scope, index] { return scope == 0 ? proc.globalValue (index) : (scope == 2 ? proc.extraValue (index) : proc.partValue (scope == 3 ? leadPart() : part, index)); };
        auto param = [this, scope, index] (int pt_) -> juce::RangedAudioParameter* { return scope == 0 ? proc.globalParam (index) : (scope == 2 ? proc.extraParam (index) : proc.partParam (pt_, index)); };
        r->set = [this, scope, param] (float v)
        {
            if (scope == 3) { for (int q = 0; q < 2; ++q) if (auto* p = param (q)) p->setValueNotifyingHost (p->convertTo0to1 (v)); return; }
            if (auto* p = param (part)) p->setValueNotifyingHost (p->convertTo0to1 (v));
            if (scope == 1 && link) if (auto* p = param (1 - part)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        r->begin = [this, scope, param] { if (auto* p = param (scope == 3 ? 0 : part)) p->beginChangeGesture(); };
        r->end = [this, scope, param] { if (auto* p = param (scope == 3 ? 0 : part)) p->endChangeGesture(); };
        r->fmt = [&d] (float v) { return fmtParam (d, v); };
        r->onHint = [this] (const juce::String& h) { setHint (h); };
        if (scope == 1)
        {
            int target = -1;
            for (int t = 0; t < TG::Count; ++t) if (targetInfo[t].pp == index) target = t;
            if (target >= 0)
            {
                r->lit = [this, target] { return proc.edit (part).tg[target].active(); };
                r->hintExtra = [this, target] (float) { return proc.edit (part).tg[target].active() ? juce::String ("   (articulated: right-click, edit articulator)") : juce::String(); };
            }
            r->menuExtra = [target] (juce::PopupMenu& m)
            {
                m.addItem (2, "Copy to the other part");
                if (target >= 0) { m.addSeparator(); m.addItem (3, "Edit articulator"); m.addItem (4, "Quick map to X"); m.addItem (5, "Quick map to Y"); m.addItem (6, "Quick map to Z"); }
            };
            r->menuResult = [this, index, target] (int res)
            {
                if (res == 2) { if (auto* p = proc.partParam (1 - part, index)) p->setValueNotifyingHost (p->convertTo0to1 (proc.partValue (part, index))); }
                else if (res == 3 && target >= 0) select (false, target, target >= TG::FirstMapping ? 2 : 0);
                else if (res >= 4 && target >= 0) quickMap (target, MAP::ModX + (res - 4));
            };
        }
        allRows.push_back (r);
        return r;
    }
    // "Quick map to X / Y / Z": the pad now sweeps the control over its whole travel
    void quickMap (int target, int m)
    {
        auto& td = proc.edit (part).tg[target];
        td.mapOn[m] = true;
        if (targetInfo[target].mode == BSum) td.map[m].line ({ { 0, 0.5f }, { 1, 1.0f } }); else td.map[m].line ({ { 0, 0 }, { 1, 1 } });
        proc.commit (part); select (false, target, 2 + m);
    }
    Row* valueRow (const juce::String& label, Kind kind, float lo, float hi, float def, std::function<float()> get, std::function<void (float)> set, std::function<juce::String (float)> fmt = nullptr)
    {
        auto* r = new Row();
        r->label = label; r->kind = kind; r->lo = lo; r->hi = hi; r->def = def; r->get = std::move (get); r->set = std::move (set); r->look = &look;
        r->fmt = fmt ? std::move (fmt) : (kind == B ? std::function<juce::String (float)> ([] (float v) { return juce::String (v > 0.5f ? "on" : "off"); }) : std::function<juce::String (float)> (pct));
        r->onHint = [this] (const juce::String& h) { setHint (h); };
        allRows.push_back (r);
        return r;
    }
    Row* put (juce::Component& parent, Row* r, int x, int y, int w, int h, Row::Style st) { rows.add (r); r->style = st; parent.addAndMakeVisible (r); r->setBounds (x, y, w, h); return r; }
    Row* cell (int scope, int index, const juce::String& label, int x, int y, int w, int h = 58, bool big = false) { auto* r = put (root, paramRow (scope, index, label), x, y, w, h, Row::Cell); r->big = big; return r; }
    Row* mini (int scope, int index, const juce::String& label, int x, int y, int w, int h = 30) { auto* r = put (root, paramRow (scope, index, label), x, y, w, h, Row::Cell); r->mini = true; return r; }
    Row* vbar (int scope, int index, const juce::String& label, int x, int y, int w, int h) { return put (root, paramRow (scope, index, label), x, y, w, h, Row::Vert); }
    Row* menu (int scope, int index, int x, int y, int w, int h = 26, bool plain = false) { auto* r = put (root, paramRow (scope, index, ""), x, y, w, h, Row::Cell); r->plainMenu = plain; return r; }
    Row* tog (int scope, int index, const juce::String& label, int x, int y, int w, int h = 20) { auto* r = put (root, paramRow (scope, index, label), x, y, w, h, Row::Inline); r->boxFirst = true; return r; }
    Row* num (int scope, int index, const juce::String& label, int labelW, int x, int y, int w, int h = 26) { auto* r = put (root, paramRow (scope, index, label), x, y, w, h, Row::Num); r->labelW = labelW; return r; }
    Row* inl (int scope, int index, const juce::String& label, int labelW, int valueW, int x, int y, int w, int h = 22) { auto* r = put (root, paramRow (scope, index, label), x, y, w, h, Row::Inline); r->labelW = labelW; r->valueW = valueW; return r; }
    Seg* seg (juce::Component& parent, int scope, int index, juce::StringArray items, int x, int y, int w, int h, bool vertical = false)
    {
        auto* s = segs.add (new Seg());
        const PDef& d = defOf (scope, index);
        cover (scope, index);
        s->items = std::move (items); s->vertical = vertical; s->hintName = juce::String (d.name).toLowerCase();
        s->get = [this, scope, index] { return (int) (scope == 0 ? proc.globalValue (index) : proc.partValue (part, index)); };
        s->set = [this, scope, index] (int v)
        {
            auto apply = [v] (juce::RangedAudioParameter* p) { if (p != nullptr) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) v)); p->endChangeGesture(); } };
            if (scope == 0) apply (proc.globalParam (index)); else { apply (proc.partParam (part, index)); if (link) apply (proc.partParam (1 - part, index)); }
        };
        s->onHint = [this] (const juce::String& h) { setHint (h); };
        parent.addAndMakeVisible (s); s->setBounds (x, y, w, h);
        return s;
    }
    Btn* button (juce::Component& parent, const juce::String& text, std::function<void()> click, std::function<bool()> isOn = nullptr, const juce::String& hint = {})
    {
        auto* b = buttons.add (new Btn());
        b->text = text; b->click = std::move (click); b->isOn = std::move (isOn); b->hintText = hint; b->onHint = [this] (const juce::String& h) { setHint (h); };
        parent.addAndMakeVisible (b);
        return b;
    }
    Section* section (juce::Component& parent, const juce::String& title, int headScope = -1, int headIndex = -1)
    {
        auto* s = sections.add (new Section());
        s->title = title;
        if (headScope >= 0) { s->head.reset (paramRow (headScope, headIndex, "")); s->head->labelW = 0; s->head->plainMenu = true; s->addAndMakeVisible (*s->head); }
        parent.addAndMakeVisible (s);
        return s;
    }
    void add (Section* s, int scope, int index, const juce::String& label) { auto* r = s->rows.add (paramRow (scope, index, label)); s->addAndMakeVisible (r); }
    void add (Section* s, Row* r) { s->rows.add (r); s->addAndMakeVisible (r); }

    //----------------------------------------------------------------------------------------------
    // the partial display (Harmor's visual feedback): the partials of the last note, scrolling by
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
        void clear() { if (img.isValid()) { juce::Graphics g (img); g.fillAll (cBg); } }
        void tick()
        {
            const int w = getWidth(), h = getHeight();
            if (w < 4 || h < 4 || ed.vizMode == 2) return;
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
            if (ed.vizMode == 2)
            {
                chevrons (g, getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) getWidth() * 1.4f), 4, 0.07f);
                g.setColour (cDim); g.setFont (mono (12.0f)); g.drawText ("display off", getLocalBounds().removeFromBottom (40), juce::Justification::centred);
                return;
            }
            if (img.isValid()) g.drawImageAt (img, 0, 0);
            chevrons (g, getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) getWidth() * 1.4f), 4, 0.07f);
            g.setFont (mono (12.0f));
            for (float hz : { 100.0f, 1000.0f, 10000.0f })
            {
                const float y = yOf (hz);
                g.setColour (cLine); g.fillRect (0.0f, y, (float) getWidth(), 1.0f);
                g.setColour (cDim.withAlpha (0.7f)); g.drawText (hz >= 1000 ? juce::String ((int) hz / 1000) + "k" : juce::String ((int) hz), 6, (int) y - 15, 40, 14, juce::Justification::left);
            }
            g.setColour (cDim); g.setFont (mono (12.0f));
            g.drawText (ed.noteText, 8, getHeight() - 22, getWidth() - 16, 14, juce::Justification::right);
            if (hover >= 0) g.drawText (hzStr (hover).toLowerCase(), getWidth() - 130, 8, 120, 14, juce::Justification::right);
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const float t = 1.0f - (float) e.y / (float) getHeight();
            hover = hzScale ? t * 20000.0f : 30.0f * std::pow (20000.0f / 30.0f, t);
        }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    };

    // the two timbre pictures
    struct Wave : juce::Component
    {
        PartialsEditor& ed; int which;
        Wave (PartialsEditor& e, int w) : ed (e), which (w) {}
        void paint (juce::Graphics& g) override
        {
            const bool sel = ed.selShape && ed.selIndex == (which == 0 ? SH::Timbre1 : SH::Timbre2);
            g.setColour (sel ? cFg : cLine.brighter (0.25f)); g.drawRect (getLocalBounds(), 1);
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
            for (int x = 0; x < w; ++x) { const float yy = (float) getHeight() * 0.5f - y[(size_t) x] / mx * ((float) getHeight() * 0.5f - 6.0f); if (x == 0) p.startNewSubPath (2.0f, yy); else p.lineTo ((float) x + 2.0f, yy); }
            g.setColour (cFg); g.strokePath (p, juce::PathStrokeType (1.3f));
        }
        void mouseEnter (const juce::MouseEvent&) override { ed.setHint (juce::String ("timbre ") + juce::String (which + 1) + "   click: edit its harmonic levels · right-click: load a single-cycle waveform, randomize · drop a sound here"); }
        void mouseExit (const juce::MouseEvent&) override { ed.setHint ({}); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int sh = which == 0 ? SH::Timbre1 : SH::Timbre2;
            if (! e.mods.isPopupMenu()) { ed.select (true, sh, 0); return; }
            juce::PopupMenu m;
            m.addItem (1, "Analyze single-cycle waveform..."); m.addItem (2, "Randomize"); m.addItem (7, "Edit the harmonic phases"); m.addSeparator();
            m.addItem (3, "Saw"); m.addItem (4, "Square"); m.addItem (5, "Triangle"); m.addItem (6, "Sine");
            ed.showMenu (m, this, [this, sh] (int r)
            {
                if (r == 1)
                {
                    ed.chooser.reset (new juce::FileChooser ("Single-cycle waveform", {}, "*.wav;*.aif;*.aiff;*.flac"));
                    ed.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                             [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) { ed.proc.analyzeWaveform (ed.part, which, fc.getResult()); ed.root.repaint(); } });
                    return;
                }
                if (r == 7) { ed.select (true, SH::Phase, 0); return; }
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
    // the X / Y pad: a room seen from the front, drawn in thin lines. X is left to right, Y bottom to top,
    // Z brings the far wall nearer (mouse wheel, or drag with the middle button).
    struct XYPad : juce::Component
    {
        PartialsEditor& ed; bool dragging = false, zDrag = false; float zStart = 0;
        explicit XYPad (PartialsEditor& e) : ed (e) {}
        // read from the parameter itself, so the puck and the bars always show the same value at the same moment
        float val (int k) const { return ed.modValue (k); }
        float asked[3] = { -1, -1, -1 };      // what the next paint will draw
        void sync() { for (int k = 0; k < 3; ++k) asked[k] = val (k); repaint(); }
        // the parameter moves at once (no smoothing here: the engine smooths what it hears, briefly);
        // the parameter's listener then brings the x / y / z bars along in the same frame
        void setVal (int k, float v) { if (auto* p = ed.proc.globalParam (GP::modX + k)) p->setValueNotifyingHost (clamp01 (v)); }
        void gesture (int k, bool begin) { if (auto* p = ed.proc.globalParam (GP::modX + k)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); } }
        juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (1.0f); }
        void paint (juce::Graphics& g) override
        {
            const auto o = area();
            const float x = val (0), y = val (1), z = val (2);
            g.setColour (cBg.darker (0.12f)); g.fillRect (o);
            // far wall: comes nearer as Z goes up
            const float inset = lerp (0.33f, 0.06f, z) * o.getWidth();
            const auto in = o.reduced (inset);
            g.setColour (cFill.withAlpha (0.10f + 0.10f * z)); g.fillRect (in);
            g.setColour (cLine.brighter (0.5f));
            for (int i = 1; i < 4; ++i)
            {
                const float t = (float) i / 4.0f;
                g.drawLine (in.getX() + t * in.getWidth(), in.getY(), in.getX() + t * in.getWidth(), in.getBottom(), 1.0f);
                g.drawLine (in.getX(), in.getY() + t * in.getHeight(), in.getRight(), in.getY() + t * in.getHeight(), 1.0f);
                // the lines on the floor, ceiling and walls run to the front edge
                g.drawLine (in.getX() + t * in.getWidth(), in.getBottom(), o.getX() + t * o.getWidth(), o.getBottom(), 1.0f);
                g.drawLine (in.getX() + t * in.getWidth(), in.getY(), o.getX() + t * o.getWidth(), o.getY(), 1.0f);
                g.drawLine (in.getX(), in.getY() + t * in.getHeight(), o.getX(), o.getY() + t * o.getHeight(), 1.0f);
                g.drawLine (in.getRight(), in.getY() + t * in.getHeight(), o.getRight(), o.getY() + t * o.getHeight(), 1.0f);
            }
            g.setColour (cFg.withAlpha (0.55f));
            g.drawLine (o.getX(), o.getY(), in.getX(), in.getY(), 1.0f); g.drawLine (o.getRight(), o.getY(), in.getRight(), in.getY(), 1.0f);
            g.drawLine (o.getX(), o.getBottom(), in.getX(), in.getBottom(), 1.0f); g.drawLine (o.getRight(), o.getBottom(), in.getRight(), in.getBottom(), 1.0f);
            g.drawRect (in, 1.0f);
            g.setColour (cFg); g.drawRect (o, 1.0f);
            // the puck (no second, trailing dot: what you see is the value)
            const auto pt_ = [&o] (float a, float b) { return juce::Point<float> (o.getX() + 6.0f + a * (o.getWidth() - 12.0f), o.getBottom() - 6.0f - b * (o.getHeight() - 12.0f)); };
            const auto p = pt_ (x, y);
            g.setColour (cFill.withAlpha (0.6f)); g.fillRect (p.x, o.getY(), 1.0f, o.getHeight()); g.fillRect (o.getX(), p.y, o.getWidth(), 1.0f);
            const float rr = 6.0f + 3.0f * z;
            g.setColour (cBg); g.fillEllipse (p.x - rr, p.y - rr, rr * 2, rr * 2);
            g.setColour (dragging ? cFill : cFg); g.drawEllipse (p.x - rr, p.y - rr, rr * 2, rr * 2, 1.6f);
            g.fillPath (skew ({ p.x - 3.5f, p.y - 2.5f, 5.0f, 5.0f }));
        }
        void hint() { ed.setHint ("modulation   x " + pct (val (0)) + "   y " + pct (val (1)) + "   z " + pct (val (2)) + "   drag: x / y · wheel or middle-drag: z · right-click: midi"); }
        void setFrom (const juce::MouseEvent& e)
        {
            const auto o = area();
            setVal (0, (e.position.x - o.getX() - 6.0f) / (o.getWidth() - 12.0f)); setVal (1, 1.0f - (e.position.y - o.getY() - 6.0f) / (o.getHeight() - 12.0f));
            hint(); repaint();
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { ed.modMenu (this, -1); return; }
            if (e.mods.isMiddleButtonDown() || e.mods.isAltDown()) { zDrag = true; zStart = val (2); gesture (2, true); return; }
            dragging = true; gesture (0, true); gesture (1, true); setFrom (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (zDrag) { setVal (2, zStart - (float) e.getDistanceFromDragStartY() / 120.0f); hint(); repaint(); }
            else if (dragging) setFrom (e);
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (zDrag) gesture (2, false);
            if (dragging) { gesture (0, false); gesture (1, false); }
            dragging = zDrag = false; repaint();
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (std::abs (w.deltaY) <= 0.0f) return;
            gesture (2, true); setVal (2, val (2) + (w.deltaY > 0 ? 0.04f : -0.04f)); gesture (2, false); hint(); repaint();
        }
        void mouseEnter (const juce::MouseEvent&) override { hint(); }
        void mouseExit (const juce::MouseEvent&) override { ed.setHint ({}); }
    };

    // the keyboard along the bottom, flat
    struct Keys : juce::MidiKeyboardComponent
    {
        explicit Keys (juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, juce::MidiKeyboardComponent::horizontalKeyboard)
        {
            setColour (shadowColourId, juce::Colours::transparentBlack); setColour (keySeparatorLineColourId, cBg);
            setColour (upDownButtonBackgroundColourId, cBg); setColour (upDownButtonArrowColourId, cFg);
            setScrollButtonsVisible (false); setWantsKeyboardFocus (false); setOctaveForMiddleC (4);
        }
        void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour, juce::Colour) override
        {
            g.setColour (down ? cFill : (over ? cFg.withAlpha (0.9f) : cFg.withAlpha (0.74f))); g.fillRect (a.reduced (0.75f, 0.0f).withTrimmedBottom (1.0f));
            if (note % 12 == 0) { g.setColour (cBg.withAlpha (0.75f)); g.setFont (mono (11.0f)); g.drawText ("c" + juce::String (note / 12 - 1), a.withTrimmedBottom (4.0f), juce::Justification::centredBottom); }
        }
        void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour) override
        {
            g.setColour (down ? cFill : (over ? cTrack.brighter (0.2f) : cBg)); g.fillRect (a);
            g.setColour (cBg); g.drawRect (a, 1.0f);
        }
    };

    // the box at the foot of Global: the sound's name and notes; while the mouse is over a control, what it is and its value
    struct InfoBox : juce::Component
    {
        PartialsEditor& ed; juce::TextEditor edit;
        explicit InfoBox (PartialsEditor& e) : ed (e)
        {
            addChildComponent (edit);
            edit.setMultiLine (true, true); edit.setReturnKeyStartsNewLine (false); edit.setFont (fnt (14.0f)); edit.setLookAndFeel (&ed.look);
            edit.onReturnKey = [this] { commit(); }; edit.onEscapeKey = [this] { edit.setVisible (false); }; edit.onFocusLost = [this] { commit(); };
        }
        ~InfoBox() override { edit.setLookAndFeel (nullptr); }
        void commit() { if (edit.isVisible()) { ed.proc.presetInfo = edit.getText().trim(); edit.setVisible (false); repaint(); } }
        void resized() override { edit.setBounds (4, 22, getWidth() - 8, getHeight() - 26); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (cBg.darker (0.12f)); g.fillRect (getLocalBounds());
            g.setColour (cLine.brighter (0.3f)); g.drawRect (getLocalBounds(), 1);
            g.setColour (cFg); g.setFont (fnt (14.5f, true));
            g.drawText (ed.proc.presetName.toLowerCase() + U (kDown), 8, 3, getWidth() - 16, 18, juce::Justification::centredLeft);
            if (ed.learnShown.isNotEmpty()) { g.setColour (cHot); g.setFont (mono (12.5f)); g.drawText (ed.learnShown, 8, 3, getWidth() - 16, 18, juce::Justification::centredRight); }
            if (edit.isVisible()) return;
            // while the mouse is over a control: what it is and its value. Otherwise what the editor shows, and the sound's notes.
            if (ed.hintText.isNotEmpty()) { g.setFont (mono (12.5f)); g.setColour (cFg); g.drawFittedText (ed.hintText, 8, 23, getWidth() - 16, getHeight() - 27, juce::Justification::topLeft, 3, 1.0f); return; }
            g.setFont (mono (12.0f)); g.setColour (cDim); g.drawFittedText (ed.note, 8, 23, getWidth() - 16, 30, juce::Justification::topLeft, 2, 1.0f);
            g.setFont (fnt (13.5f)); g.setColour (ed.proc.presetInfo.isNotEmpty() ? cFg.withAlpha (0.85f) : cDim.withAlpha (0.6f));
            g.drawText (ed.proc.presetInfo.isNotEmpty() ? ed.proc.presetInfo : juce::String ("click to write a note about this sound"), 8, getHeight() - 20, getWidth() - 16, 16, juce::Justification::centredLeft);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.y < 22 || e.mods.isPopupMenu()) { ed.presetMenu (this); return; }
            edit.setText (ed.proc.presetInfo, false); edit.setVisible (true); edit.grabKeyboardFocus();
        }
    };

    //----------------------------------------------------------------------------------------------
    // the line editor: envelopes, LFOs, mappings and shapes
    struct CurveView : juce::Component
    {
        PartialsEditor& ed; int dragPt = -1, dragTen = -1, lastTenY = 0; float lastPaintX = -1, lastPaintY = 0, zoom = 1.0f, scroll = 0.0f;
        explicit CurveView (PartialsEditor& e) : ed (e) {}
        bool isEnv() const { return ! ed.selShape && ed.selArt == 0; }
        bool isLfo() const { return ! ed.selShape && ed.selArt == 1; }
        bool timed() const { return isEnv() || isLfo(); }
        TargetDef& td() const { return ed.proc.edit (ed.part).tg[ed.selShape ? TG::Volume : ed.selIndex]; }
        Curve* curve() const
        {
            PartData& d = ed.proc.edit (ed.part);
            if (ed.selShape) return &d.shape[ed.selIndex];
            if (ed.selArt == 0) return &d.tg[ed.selIndex].env.c;
            if (ed.selArt == 1) return &d.tg[ed.selIndex].lfo.amp;
            return &d.tg[ed.selIndex].map[ed.selArt - 2];
        }
        float lfoHz() const { const auto& l = td().lfo; const float s = clamp01 (l.speed); return l.tempo ? 1.0f / (16.0f * std::exp2 (-8.0f * s)) : 0.05f * std::pow (800.0f, s); }   // per beat with tempo on
        float fullX() const
        {
            if (isEnv()) return juce::jmax (1.0f, curve()->endX() * 1.2f);
            if (isLfo()) return juce::jlimit (0.5f, 64.0f, juce::jmax (4.0f / juce::jmax (0.01f, lfoHz()), td().lfo.amp.endX() * 1.3f));
            return 1.0f;
        }
        float span() const { return timed() ? fullX() / zoom : 1.0f; }
        float x0() const { return timed() ? scroll * (fullX() - span()) : 0.0f; }
        float px (float x) const { return 8.0f + (x - x0()) / span() * ((float) getWidth() - 16.0f); }
        float py (float y) const { return 10.0f + (1.0f - y) * ((float) getHeight() - 20.0f); }
        float ux (float p) const { return x0() + (p - 8.0f) / ((float) getWidth() - 16.0f) * span(); }
        float uy (float p) const { return clamp01 (1.0f - (p - 10.0f) / ((float) getHeight() - 20.0f)); }
        bool bars() const { const Curve* c = curve(); return c != nullptr && ed.selShape && c->raw.size() == (size_t) NH; }
        float barX (int h) const { return shapeDefs[ed.selIndex].axis == AxHarm ? harmToX (h) : (float) h / (float) (NH - 1); }
        float gridStep() const { const float s = span(); return s > 16 ? 4.0f : (s > 8 ? 1.0f : (s > 2 ? 0.5f : (s > 0.5f ? 0.1f : 0.02f))); }
        bool bipolar() const
        {
            if (ed.selShape) { const auto ym = shapeDefs[ed.selIndex].ymap; return ym == YBi || ym == YEq || ym == YLevel; }
            if (isLfo()) return true;
            return targetInfo[ed.selIndex].mode != UProd && ! (isEnv() && (ed.selIndex == TG::F1Freq || ed.selIndex == TG::F2Freq));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg.darker (0.10f));
            const float W = (float) getWidth(), H = (float) getHeight();
            // grid: time for envelopes and LFOs, sixteenths of the travel for mappings, voices for a unison mapping
            if (timed())
            {
                const float st = gridStep();
                for (float x = std::ceil (x0() / st) * st; x <= x0() + span() + 1.0e-4f; x += st)
                {
                    const bool major = std::abs (x / (st * 5.0f) - std::round (x / (st * 5.0f))) < 0.01f || st >= 0.5f;
                    g.setColour (cLine.withAlpha (major ? 1.0f : 0.55f)); g.fillRect (px (x), 0.0f, 1.0f, H);
                    if (major) { g.setColour (cDim.withAlpha (0.7f)); g.setFont (mono (10.5f)); g.drawText (juce::String (x, st < 0.1f ? 2 : 1) + (td().env.tempo && isEnv() ? " b" : (isLfo() && td().lfo.tempo ? " b" : " s")), (int) px (x) + 3, (int) H - 14, 50, 12, juce::Justification::left); }
                }
            }
            else if (! ed.selShape && ed.selArt == 2 + MAP::Unison)
            {
                const int U = juce::jlimit (1, MAXU, (int) ed.proc.partValue (ed.part, PP::uniOrder));
                for (int u = 0; u < U; ++u) { g.setColour (cLine.brighter (0.2f)); g.fillRect (px (U == 1 ? 0.5f : (float) u / (float) (U - 1)), 0.0f, 1.0f, H); }
            }
            else if (! ed.selShape && ed.selArt == 2 + MAP::Key)
                for (int n = 0; n <= 120; n += 12) { g.setColour (cLine); g.fillRect (px ((float) n / 127.0f), 0.0f, 1.0f, H); g.setColour (cDim.withAlpha (0.7f)); g.setFont (mono (10.5f)); g.drawText ("c" + juce::String (n / 12 - 1), (int) px ((float) n / 127.0f) + 3, (int) H - 14, 30, 12, juce::Justification::left); }
            else for (int i = 0; i <= 16; ++i) { g.setColour (cLine.withAlpha (i % 4 == 0 ? 1.0f : 0.5f)); g.fillRect (px ((float) i / 16.0f), 0.0f, 1.0f, H); }
            g.setColour (cLine);
            for (int i = 0; i <= 4; ++i) g.fillRect (0.0f, py ((float) i / 4.0f), W, 1.0f);
            const float baseY = bipolar() ? py (0.5f) : py (0.0f);
            if (bipolar()) { g.setColour (cLine.brighter (0.35f)); g.fillRect (0.0f, baseY, W, 1.0f); }

            Curve* c = curve();
            if (c == nullptr) return;
            const bool live = ed.selShape || (isEnv() ? td().env.on : (isLfo() ? td().lfo.on : td().mapOn[ed.selArt - 2]));
            const juce::Colour ink = live ? cOval[0] : cDim;

            if (isLfo())
            {
                // the wave inside its amplitude line: the handle sets the amplitude (below the middle turns it over) and the start time
                const LfoDef& l = td().lfo;
                const float hz = lfoHz();
                juce::Path p, top;
                for (int x = 8; x <= getWidth() - 8; ++x)
                {
                    const float t = ux ((float) x);
                    const float a = l.amt * l.ampAt (t);
                    const float y = py (0.5f + 0.5f * a * lfoShape (hz * t + l.phase, l));
                    if (x == 8) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y);
                    const float ya = py (0.5f + 0.5f * a);
                    if (x == 8) top.startNewSubPath ((float) x, ya); else top.lineTo ((float) x, ya);
                }
                g.setColour (ink.withAlpha (0.45f)); g.strokePath (top, juce::PathStrokeType (1.0f));
                g.setColour (live ? cFg : cDim); g.strokePath (p, juce::PathStrokeType (1.5f));
                if (c->pts.empty())
                {
                    const float X = px (0.0f), Y = py (0.5f + 0.5f * l.amt);
                    g.setColour (cBg); g.fillRect (X - 3.0f, Y - 3.0f, 6.0f, 6.0f); g.setColour (cFg); g.drawRect (X - 3.5f, Y - 3.5f, 7.0f, 7.0f, 1.2f);
                }
            }
            else if (bars())
            {
                g.setColour (cFg);
                for (int h = 0; h < NH; ++h)
                {
                    const float xa = px (barX (h)), xb = h + 1 < NH ? px (barX (h + 1)) : W - 8.0f;
                    const float y = py (c->raw[(size_t) h]);
                    g.fillRect (xa, juce::jmin (y, baseY), juce::jmax (1.0f, xb - xa - (xb - xa > 3 ? 1.0f : 0.0f)), std::abs (baseY - y) + 1.0f);
                }
                return;
            }
            else
            {
                if (c->pts.empty()) { g.setColour (cDim); g.setFont (fnt (14.0f)); g.drawText ("right-click to add a point", getLocalBounds(), juce::Justification::centred); return; }
                juce::Path line, fill;
                for (int x = 8; x <= getWidth() - 8; x += 2)
                {
                    const float y = py (c->eval (ux ((float) x)));
                    if (x == 8) { line.startNewSubPath ((float) x, y); fill.startNewSubPath ((float) x, baseY); fill.lineTo ((float) x, y); } else { line.lineTo ((float) x, y); fill.lineTo ((float) x, y); }
                }
                fill.lineTo (W - 8.0f, baseY); fill.closeSubPath();
                g.setColour (cFill.withAlpha (live ? 0.25f : 0.10f)); g.fillPath (fill);
                g.setColour (ink); g.strokePath (line, juce::PathStrokeType (2.0f));
            }

            const EnvDef* env = isEnv() ? &td().env : nullptr;
            if (env != nullptr && ed.selIndex < TG::FirstMapping)
            {
                // what the modifiers under the graph make of it
                float m[EM::Count]; bool any = false;
                for (int k = 0; k < EM::Count; ++k) { m[k] = ed.proc.envModValue (ed.part, ed.selIndex, k); any = any || std::abs (m[k] - 1.0f) > 1.0e-3f; }
                const int n = (int) c->pts.size(), sus = (env->sustain >= 0 && env->sustain < n) ? env->sustain : -1;
                if (any && n > 1)
                {
                    const float tD = c->pts[(size_t) env->decayIndex()].x, tS = sus >= 0 ? c->pts[(size_t) sus].x : c->endX();
                    const float sy = sus >= 0 ? clamp01 (c->pts[(size_t) sus].y * m[EM::Sus]) : 0.0f;
                    juce::Path eff; bool startP = true;
                    for (int i = 0; i <= 240; ++i)
                    {
                        const float t = c->endX() * (float) i / 240.0f;
                        float tt = t <= tD ? t * m[EM::Att] : (t <= tS ? tD * m[EM::Att] + (t - tD) * m[EM::Dec] : tD * m[EM::Att] + (tS - tD) * m[EM::Dec] + (t - tS) * m[EM::Rel]);
                        const float X = px (tt), Y = py (c->eval (t, sus, sy));
                        if (startP) { eff.startNewSubPath (X, Y); startP = false; } else eff.lineTo (X, Y);
                    }
                    g.setColour (cHot.withAlpha (0.9f)); g.strokePath (eff, juce::PathStrokeType (1.2f));
                }
            }
            if (ed.freeze) return;
            g.setFont (mono (11.0f, true));
            for (int i = 0; i < (int) c->pts.size(); ++i)
            {
                const Pt& p = c->pts[(size_t) i];
                const float X = px (p.x), Y = py (p.y);
                juce::String tag;
                if (env != nullptr)
                {
                    if (i == env->decayIndex() && i > 0) tag << "D";
                    if (i == env->sustain) tag << "S";
                    if (i == env->loopStart) tag << "L";
                    if (tag.isNotEmpty()) { g.setColour (cFg.withAlpha (0.45f)); g.fillRect (X, 0.0f, 1.0f, H); }
                }
                g.setColour (cBg); g.fillRect (X - 3.0f, Y - 3.0f, 6.0f, 6.0f);
                g.setColour (cFg); g.drawRect (X - 3.5f, Y - 3.5f, 7.0f, 7.0f, 1.2f);
                if (i > 0 && ! isLfo())
                {
                    const Pt& q = c->pts[(size_t) i - 1];
                    const float mx = 0.5f * (q.x + p.x);
                    g.setColour (cDim); g.drawEllipse (px (mx) - 3.0f, py (c->eval (mx)) - 3.0f, 6.0f, 6.0f, 1.0f);
                }
                if (p.arp == 1) tag << U ("\xe2\x86\x93"); else if (p.arp == 2) tag << "="; else if (p.arp == 3) tag << U ("\xe2\x86\x91");
                if (tag.isNotEmpty())
                {
                    const auto box = juce::Rectangle<float> (X + 2.0f, H - 30.0f, 8.0f + 8.0f * (float) tag.length(), 14.0f);
                    g.setColour (cFg.withAlpha (0.9f)); g.fillPath (skew (box, 0.25f));
                    g.setColour (cBg); g.drawText (tag, box.translated (2.0f, 0.0f), juce::Justification::centred);
                }
            }
            // a mapping: where its source is right now (the pad for X / Y / Z, the last key for the keyboard)
            if (! ed.selShape && ed.selArt >= 2)
            {
                const int mp = ed.selArt - 2; float sx = -1.0f;
                if (mp >= MAP::ModX && mp <= MAP::ModZ) sx = ed.proc.modNow[mp - MAP::ModX].load();
                else if (mp == MAP::Key && ed.proc.engine.lastKey.load() >= 0) sx = (float) ed.proc.engine.lastKey.load() / 127.0f;
                if (sx >= 0.0f)
                {
                    g.setColour (cHot.withAlpha (0.9f)); g.fillRect (px (sx), 0.0f, 1.0f, H);
                    const float Y = py (c->eval (sx)); g.fillEllipse (px (sx) - 3.5f, Y - 3.5f, 7.0f, 7.0f);
                }
            }
            // where the newest note is
            if (timed() && ed.proc.engine.activeVoices() > 0)
            {
                const float t = isEnv() ? ed.proc.engine.viewEnvPos.load() : ed.proc.engine.viewLfoT.load() * (td().lfo.tempo ? (float) ed.proc.hostBpm.load() / 60.0f : 1.0f);
                if (t >= 0.0f && t >= x0() && t <= x0() + span()) { g.setColour (cFg.withAlpha (0.8f)); g.fillRect (px (t), 0.0f, 1.0f, H); }
            }
        }
        int hitPoint (juce::Point<float> m) const
        {
            const Curve* c = curve(); if (c == nullptr) return -1;
            for (int i = 0; i < (int) c->pts.size(); ++i) if (std::abs (px (c->pts[(size_t) i].x) - m.x) < 8 && std::abs (py (c->pts[(size_t) i].y) - m.y) < 8) return i;
            return -1;
        }
        int hitTension (juce::Point<float> m) const
        {
            const Curve* c = curve(); if (c == nullptr || isLfo()) return -1;
            for (int i = 1; i < (int) c->pts.size(); ++i)
            {
                const float mx = 0.5f * (c->pts[(size_t) i - 1].x + c->pts[(size_t) i].x);
                if (std::abs (px (mx) - m.x) < 7 && std::abs (py (c->eval (mx)) - m.y) < 7) return i;
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
        float snapStepX() const { return timed() ? gridStep() * (gridStep() >= 0.5f ? 0.25f : 0.5f) : 1.0f / 32.0f; }
        float snapX (float x) const { if (! ed.snap) return x; const float gq = snapStepX(); return std::round (x / gq) * gq; }
        float snapY (float y) const { return ed.snap ? std::round (y * 16.0f) / 16.0f : y; }
        // step editing: one point per grid step under the mouse, like drawing freehand
        void stepDraw (juce::Point<float> m, bool pulse)
        {
            Curve* c = curve();
            const float gq = snapStepX() * (timed() ? 2.0f : 2.0f);
            const float x = juce::jmax (0.0f, std::floor (ux (m.x) / gq) * gq), y = snapY (uy (m.y));
            if (! timed() && x > 1.0f) return;
            int at = -1;
            for (int i = 0; i < (int) c->pts.size(); ++i) if (std::abs (c->pts[(size_t) i].x - x) < gq * 0.25f) at = i;
            const uint8_t type = pulse ? (uint8_t) Hold : (c->pts.empty() ? (uint8_t) Single : c->pts.back().type);
            if (at >= 0) c->pts[(size_t) at].y = y;
            else
            {
                Pt p; p.x = x; p.y = y; p.type = type;
                int ins = 0; while (ins < (int) c->pts.size() && c->pts[(size_t) ins].x <= x) ++ins;
                c->pts.insert (c->pts.begin() + ins, p);
                if (isEnv()) shiftMarkers (ins, 1);
            }
            ed.curveChanged();
        }
        void shiftMarkers (int at, int by)
        {
            EnvDef& en = td().env;
            for (int* mk : { &en.sustain, &en.loopStart, &en.decay }) if (*mk >= at) *mk += by;
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            Curve* c = curve();
            if (c == nullptr || ed.freeze) return;
            const auto m = e.position;
            if (bars()) { if (e.mods.isPopupMenu()) { ed.curveOptions (this); return; } lastPaintX = -1; paintBars (m); return; }
            if (isLfo() && c->pts.empty())
            {
                // the handle the LFO starts with becomes a real point as soon as it is touched
                LfoDef& l = td().lfo;
                c->pts.push_back ({ 0.0f, 0.5f + 0.5f * clampf (l.amt, -1.0f, 1.0f), 0, Single, 0 }); l.amt = 1.0f;
            }
            if (ed.stepEdit && ! e.mods.isPopupMenu() && ! isLfo()) { stepDraw (m, e.mods.isShiftDown()); return; }
            dragPt = hitPoint (m); dragTen = dragPt < 0 ? hitTension (m) : -1;
            if (e.mods.isPopupMenu() || (e.getNumberOfClicks() > 1 && dragPt < 0 && dragTen < 0))
            {
                if (dragPt >= 0) { pointMenu (dragPt); dragPt = -1; return; }
                if (dragTen >= 0) { c->pts[(size_t) dragTen].t = 0; dragTen = -1; ed.curveChanged(); return; }
                Pt p; p.x = juce::jmax (0.0f, snapX (ux (m.x))); p.y = snapY (uy (m.y)); p.type = Single;
                if (! timed()) p.x = clamp01 (p.x);
                int at = 0; while (at < (int) c->pts.size() && c->pts[(size_t) at].x <= p.x) ++at;
                c->pts.insert (c->pts.begin() + at, p);
                if (isEnv()) shiftMarkers (at, 1);
                dragPt = at; ed.curveChanged(); return;
            }
            if (dragPt >= 0 && e.mods.isAltDown()) { removePoint (dragPt); dragPt = -1; }
            if (isLfo() && dragPt < 0) ed.curveChanged();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            Curve* c = curve();
            if (c == nullptr || ed.freeze) return;
            if (bars()) { paintBars (e.position); return; }
            if (ed.stepEdit && ! isLfo() && dragPt < 0 && dragTen < 0) { stepDraw (e.position, e.mods.isShiftDown()); return; }
            if (dragPt >= 0 && dragPt < (int) c->pts.size())
            {
                Pt& p = c->pts[(size_t) dragPt];
                float nx = snapX (ux (e.position.x));
                const float lo = dragPt > 0 ? c->pts[(size_t) dragPt - 1].x : 0.0f;
                const bool last = dragPt == (int) c->pts.size() - 1;
                if (dragPt == 0 && ! isLfo()) nx = 0.0f;
                else if (! timed() && last) nx = 1.0f;
                nx = juce::jmax (lo, nx);
                if (! timed()) nx = juce::jmin (nx, last ? 1.0f : c->pts[(size_t) dragPt + 1].x);
                else if (ed.slide) { const float d = nx - p.x; for (int i = dragPt + 1; i < (int) c->pts.size(); ++i) c->pts[(size_t) i].x += d; }
                else if (! last) nx = juce::jmin (nx, c->pts[(size_t) dragPt + 1].x);
                if (! e.mods.isCommandDown() && ! e.mods.isCtrlDown()) p.x = nx;
                if (! e.mods.isShiftDown()) p.y = snapY (uy (e.position.y));
                ed.setHint ((timed() ? "time " + juce::String (p.x, 3) : "position " + pct (p.x)) + "   level " + (bipolar() ? juce::String (juce::roundToInt ((2.0f * p.y - 1.0f) * 100.0f)) + "%" : pct (p.y)));
                ed.curveChanged();
            }
            else if (dragTen > 0 && dragTen < (int) c->pts.size())
            {
                Pt& p = c->pts[(size_t) dragTen]; const Pt& q = c->pts[(size_t) dragTen - 1];
                const float dir = p.y >= q.y ? 1.0f : -1.0f;
                p.t = clampf (p.t + dir * (float) (e.getDistanceFromDragStartY() - lastTenY) * ((e.mods.isCommandDown() || e.mods.isCtrlDown()) ? 0.002f : 0.012f), -1.0f, 1.0f);
                lastTenY = e.getDistanceFromDragStartY();
                ed.curveChanged();
            }
        }
        void mouseUp (const juce::MouseEvent&) override { dragPt = dragTen = -1; lastTenY = 0; lastPaintX = -1; }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (! timed() || std::abs (w.deltaY) + std::abs (w.deltaX) <= 0.0f) return;
            if (std::abs (w.deltaX) > std::abs (w.deltaY)) { scroll = clamp01 (scroll - w.deltaX * 0.5f); }
            else
            {
                const float at = ux (e.position.x);
                zoom = juce::jlimit (1.0f, 64.0f, zoom * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f));
                const float room = fullX() - span();
                scroll = room > 1.0e-5f ? clamp01 ((at - (e.position.x - 8.0f) / ((float) getWidth() - 16.0f) * span()) / room) : 0.0f;
            }
            repaint(); ed.scrollStrip.repaint();
        }
        void removePoint (int i)
        {
            Curve* c = curve();
            if (c == nullptr || c->pts.size() <= 1) return;
            c->pts.erase (c->pts.begin() + i);
            if (isEnv()) { EnvDef& en = td().env; for (int* mk : { &en.sustain, &en.loopStart, &en.decay }) { if (*mk == i) *mk = -1; else if (*mk > i) --*mk; } }
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
                const EnvDef& en = td().env;
                m.addItem (4, "Decay start (D)", i > 0, en.decayIndex() == i);
                m.addItem (2, "Sustain / loop end (S)", true, en.sustain == i); m.addItem (3, "Loop start (L)", true, en.loopStart == i);
                const char* an[] = { "None", "Previous note", "Same note", "Next note" };
                for (int a = 0; a < 4; ++a) arp.addItem (200 + a, an[a], true, c->pts[(size_t) i].arp == a);
                m.addSubMenu ("Arpeggiator break", arp);
            }
            ed.showMenu (m, this, [this, i] (int r)
            {
                Curve* cc = curve(); if (cc == nullptr || i >= (int) cc->pts.size() || r == 0) return;
                if (r == 1) { removePoint (i); return; }
                if (r >= 200) cc->pts[(size_t) i].arp = (uint8_t) (r - 200);
                else if (r >= 100) cc->pts[(size_t) i].type = (uint8_t) (r - 100);
                else if (isEnv()) { EnvDef& en = td().env; if (r == 2) en.sustain = en.sustain == i ? -1 : i; else if (r == 3) en.loopStart = en.loopStart == i ? -1 : i; else if (r == 4) en.decay = i; }
                ed.curveChanged();
            });
        }
    };

    // under the graph: where the view sits in the whole envelope (drag to scroll, wheel over the graph to zoom)
    struct ScrollStrip : juce::Component
    {
        PartialsEditor& ed; float startScroll = 0;
        explicit ScrollStrip (PartialsEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override
        {
            auto& cv = ed.curveView;
            g.setColour (cLine); g.fillRect (0.0f, (float) getHeight() * 0.5f, (float) getWidth(), 1.0f);
            const float w = (float) getWidth() / cv.zoom, x = cv.scroll * ((float) getWidth() - w);
            g.setColour (cTrack.brighter (cv.zoom > 1.01f ? 0.3f : 0.0f)); g.fillPath (skew ({ x, 3.0f, juce::jmax (12.0f, w - 6.0f), (float) getHeight() - 6.0f }, 0.4f));
        }
        void mouseDown (const juce::MouseEvent&) override { startScroll = ed.curveView.scroll; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto& cv = ed.curveView; const float room = (float) getWidth() * (1.0f - 1.0f / cv.zoom);
            if (room > 1.0f) { cv.scroll = clamp01 (startScroll + (float) e.getDistanceFromDragStartX() / room); cv.repaint(); repaint(); }
        }
        void mouseDoubleClick (const juce::MouseEvent&) override { ed.curveView.zoom = 1.0f; ed.curveView.scroll = 0.0f; ed.curveView.repaint(); repaint(); }
        void mouseEnter (const juce::MouseEvent&) override { ed.setHint ("view   wheel over the graph zooms the time axis · drag here to scroll · double-click to see everything"); }
        void mouseExit (const juce::MouseEvent&) override { ed.setHint ({}); }
    };

    //----------------------------------------------------------------------------------------------
    // the image
    struct ImageView : juce::Component
    {
        PartialsEditor& ed; juce::Image cache; std::shared_ptr<const ImageData> shown; int shownPlane = -1, plane = 0; float selA = -1, selB = -1; bool loopDrag = false;
        explicit ImageView (PartialsEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg.darker (0.10f));
            auto img = ed.proc.edit (ed.part).image;
            if (img == nullptr || ! img->valid())
            {
                g.setColour (cDim); g.setFont (fnt (15.0f));
                g.drawText ("drop a sound or a picture here", getLocalBounds(), juce::Justification::centred);
                shown.reset(); return;
            }
            if (img != shown || plane != shownPlane)
            {
                // the analysis picture, re-inked: field → blue → ink, like the rest of the window
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
            g.setColour (cDim); g.setFont (mono (12.0f));
            g.drawText ((img->name + "  " + juce::String (img->w) + " x " + juce::String (img->h) + (img->resynth ? "  resynthesis  " : "  image  ") + juce::String (img->seconds(), 2) + " s").toLowerCase(),
                        8, 6, getWidth() - 16, 14, juce::Justification::left);
        }
        void setTime (float x) { if (auto* p = ed.proc.partParam (ed.part, PP::imgTime)) p->setValueNotifyingHost (clamp01 (x / (float) getWidth())); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { ed.imageOptions(); return; }
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
            g.setColour (cFg); g.setFont (mono (kCaption, true)); g.drawText (title.toLowerCase(), 0, 0, getWidth(), 18, juce::Justification::left); g.setColour (cFill.withAlpha (0.7f)); g.fillRect (0, 20, getWidth(), 1);
            g.setFont (fnt (kLabel));
            for (int i = 0; i < count; ++i) { g.setColour (cDim); g.drawText (juce::String (i + 1), 0, 25 + i * 21, 20, 21, juce::Justification::left); g.setColour (cFg); g.drawText (name (get (i)).toLowerCase(), 24, 25 + i * 21, getWidth() - 24, 21, juce::Justification::left); }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = (e.y - 25) / 21; if (e.y < 25 || i < 0 || i >= count) return;
            const int j = e.mods.isPopupMenu() ? i - 1 : i + 1;
            if (j >= 0 && j < count) { swap (i, j); repaint(); }
        }
    };

    //----------------------------------------------------------------------------------------------
    // everything that is only drawn: rules, captions, labels, the cube, the meter
    struct Backdrop : juce::Component
    {
        struct Cap { juce::String t; int x, y, w; };
        struct Lab { juce::String t; juce::Rectangle<int> r; juce::Justification j; bool dim; };
        PartialsEditor& ed; std::vector<Cap> caps; std::vector<Lab> labs;
        explicit Backdrop (PartialsEditor& e) : ed (e) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (cBg);
            const int H = ed.baseH();
            // the frame: part panel over Global and the editor, the display down the right, the keyboard under it all
            g.setColour (cFg); g.fillRect (0, 497, 1532, 1); g.fillRect (1532, 0, 1, H);
            if (ed.showKeys) g.fillRect (0, 910, 1532, 1);
            g.setColour (cLine.brighter (0.15f));
            g.fillRect (583, 498, 1, 412); g.fillRect (816, 8, 1, 484);
            g.fillRect (36, 160, 536, 1); g.fillRect (350, 164, 1, 94); g.fillRect (36, 262, 536, 1); g.fillRect (240, 266, 1, 132); g.fillRect (576, 216, 1, 182);
            g.fillRect (62, 400, 746, 1); g.fillRect (412, 404, 1, 88); g.fillRect (606, 404, 1, 88);
            g.fillRect (838, 328, 650, 1); g.fillRect (994, 332, 1, 160); g.fillRect (1356, 332, 1, 160); g.fillRect (1488, 8, 1, 484);
            // name and cube, top of the left strip
            cube (g, 6.0f, 10.0f, 0.24f);
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 18.0f, 150.0f));
                g.setFont (fnt (17.0f, true)); g.setColour (cFg); g.drawText ("partials", -52, 139, 140, 22, juce::Justification::centredRight);
            }
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 826.0f, 250.0f));
                g.setFont (mono (12.0f)); g.setColour (cDim.withAlpha (0.55f)); g.drawText ("filtering section", 626, 242, 400, 16, juce::Justification::centred);
            }
            for (auto& c : caps)
            {
                g.setColour (cFg); g.setFont (mono (kCaption, true)); g.drawText (c.t.toLowerCase(), c.x, c.y, c.w, 18, juce::Justification::centredLeft);
                g.setColour (cFill.withAlpha (0.7f)); g.fillRect (c.x, c.y + 20, c.w, 1);
            }
            for (auto& l : labs) { g.setColour (l.dim ? cDim : cFg.withAlpha (0.82f)); g.setFont (l.dim ? mono (11.5f) : fnt (kLabel)); g.drawText (l.t.toLowerCase(), l.r, l.j); }
            // filter numbers
            g.setFont (mono (26.0f, true)); g.setColour (cFill.withAlpha (0.55f));
            g.drawText ("1", 1158, 24, 44, 34, juce::Justification::centred); g.drawText ("2", 1158, 270, 44, 34, juce::Justification::centred);
            // meter under the master volume
            const float l = clamp01 (ed.proc.meterL.load()), r = clamp01 (ed.proc.meterR.load());
            g.setColour (cTrack); g.fillRect (122, 537, 120, 2); g.fillRect (122, 543, 120, 2);
            g.setColour (l > 0.98f ? cHot : cFill); g.fillPath (skew ({ 122.0f, 535.5f, 120.0f * std::sqrt (l), 4.0f }));
            g.setColour (r > 0.98f ? cHot : cFill); g.fillPath (skew ({ 122.0f, 541.5f, 120.0f * std::sqrt (r), 4.0f }));
        }
    };
    void cap (const juce::String& t, int x, int y, int w) { backdrop.caps.push_back ({ t, x, y, w }); }
    void lab (const juce::String& t, int x, int y, int w, juce::Justification j = juce::Justification::centredLeft, bool dim = false) { backdrop.labs.push_back ({ t, { x, y, w, 18 }, j, dim }); }

    //----------------------------------------------------------------------------------------------
    Backdrop backdrop { *this };
    Spectro spectro { *this }; Wave wave1 { *this, 0 }, wave2 { *this, 1 };
    XYPad pad { *this }; Keys keys { proc.keyboard }; InfoBox info { *this };
    juce::Component pageEnv, pageImg, pageFx, pageAdv;
    CurveView curveView { *this }; ScrollStrip scrollStrip { *this }; ImageView imageView { *this };
    OrderList unitList, fxList;
    Btn* targetBtn = nullptr; Btn* artBtn = nullptr; Btn* partBtn[2] = { nullptr, nullptr };
    Row* artOn = nullptr; Row* smoothRow = nullptr; Row* tempoRow = nullptr; Row* globalRow = nullptr;
    Row* envMod[EM::Count] = {}; Row* lfoKnob[6] = {};
    bool highPrecision = false; int slow = 0;
    juce::Array<juce::File> presetFiles;

    TargetDef& td() { return proc.edit (part).tg[selShape ? TG::Volume : selIndex]; }
    bool artIsOn() { auto& t = td(); return selArt == 0 ? t.env.on : (selArt == 1 ? t.lfo.on : t.mapOn[selArt - 2]); }
    void setArtOn (bool on)
    {
        auto& t = td();
        if (selArt == 0) t.env.on = on; else if (selArt == 1) t.lfo.on = on; else t.mapOn[selArt - 2] = on;
    }
    // called after every edit of the line on show. Touching an LFO or a mapping switches it on, as in Harmor
    // ("simply edit the mapping from its flat state and it will control the target"); envelopes too.
    void curveChanged (bool activate = true)
    {
        if (! selShape && activate && ! artIsOn()) { setArtOn (true); }
        if (selShape) proc.commit (part, selIndex); else proc.commit (part);
        curveView.repaint(); wave1.repaint(); wave2.repaint(); scrollStrip.repaint();
    }
    // every part has a line to look at before it is switched on: the default envelope, a flat mapping
    void seedCurve()
    {
        if (selShape) return;
        auto& t = td();
        if (selArt == 0 && t.env.c.pts.empty()) { t.env.adsr (0.01f, 0.3f, 0.5f, 0.3f); proc.commit (part); }
        else if (selArt >= 2 && t.map[selArt - 2].empty())
        {
            if (targetInfo[selIndex].mode == BSum) t.map[selArt - 2].line ({ { 0, 0.5f }, { 1, 0.5f } }); else t.map[selArt - 2].line ({ { 0, 1 }, { 1, 1 } });
            proc.commit (part);
        }
    }
    void select (bool shape, int index, int art)
    {
        selShape = shape; selIndex = index; selArt = art;
        if (! shape && index >= TG::FirstMapping && art < 2) selArt = 2;
        curveView.zoom = 1.0f; curveView.scroll = 0.0f;
        seedCurve();
        tab = 0; layoutPages(); root.repaint();
    }
    juce::String combineName() const
    {
        if (selShape) return {};
        const auto m = targetInfo[selIndex].mode;
        return m == BSum ? "bipolar sum" : (m == BProd ? "bipolar product" : "unipolar product");
    }
    // one line about what is on show; warns when a product target's own control is at zero (nothing to scale)
    juce::String selectionNote()
    {
        if (selShape) return juce::String (shapeDefs[selIndex].name).toLowerCase() + "   shaping: one graph, always in use";
        const TargetInfo& ti = targetInfo[selIndex];
        juce::String s = juce::String (ti.name).toLowerCase() + "   " + combineName();
        if (ti.mode != BSum && ti.pp >= 0 && std::abs (proc.partValue (part, ti.pp)) < 1.0e-4f)
            s << "   its control is at zero: parts scale that control, so raise it to hear them";
        else if (selIndex == TG::F1Freq || selIndex == TG::F2Freq)
            s << (proc.edit (part).filtEnvAdd ? "   envelope is added above the freq control (older sound)" : "   envelope works below the freq control, by the env amount");
        return s;
    }

    explicit PartialsEditor (PartialsProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&look);
        addAndMakeVisible (root);
        root.setSize (BW, BHK);
        build();
        setResizable (true, true);
        applySize (0.75f);
        startTimerHz (30);
        for (int k = 0; k < 3; ++k) if (auto* q = proc.globalParam (GP::modX + k)) q->addListener (this);
    }
    ~PartialsEditor() override
    {
        for (int k = 0; k < 3; ++k) if (auto* q = proc.globalParam (GP::modX + k)) q->removeListener (this);
        cancelPendingUpdate(); stopTimer(); setLookAndFeel (nullptr);
    }

    //----------------------------------------------------------------------------------------------
    // X / Y / Z: the pad and the three bars follow the parameter the moment it moves, whoever moves it
    // (the pad, a bar, host automation, a MIDI controller), instead of waiting for the 30 Hz timer.
    Row* modRows[3] = { nullptr, nullptr, nullptr };
    float modValue (int k) const { auto* q = proc.globalParam (GP::modX + k); return q != nullptr ? q->convertFrom0to1 (q->getValue()) : 0.0f; }
    void syncMod() { for (auto* r : modRows) if (r != nullptr) r->refresh(); pad.sync(); }
    void parameterValueChanged (int, float) override
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread()) syncMod();
        else triggerAsyncUpdate();      // automation played on the audio thread: next pass of the message loop
    }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override { syncMod(); }

    void applySize (float scale)
    {
        const int H = baseH();
        root.setSize (BW, H); backdrop.setBounds (0, 0, BW, H);
        if (auto* c = getConstrainer()) { c->setFixedAspectRatio ((double) BW / (double) H); c->setSizeLimits (BW * 2 / 5, H * 2 / 5, BW * 2, H * 2); }
        setSize (juce::roundToInt ((float) BW * scale), juce::roundToInt ((float) H * scale));
        layoutViz(); resized();
    }
    void resized() override
    {
        root.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) BW));
        root.setBounds (0, 0, BW, baseH());
    }
    void paint (juce::Graphics& g) override { g.fillAll (cBg); }
    void layoutViz()
    {
        const int H = baseH();
        keys.setVisible (showKeys);
        if (vizMode == 1) spectro.setBounds (838, 8, 1012, H - 16); else spectro.setBounds (1538, 8, 312, H - 16);
        spectro.toFront (false); spectro.clear();
    }

    //----------------------------------------------------------------------------------------------
    void build()
    {
        root.addAndMakeVisible (backdrop); backdrop.setBounds (0, 0, BW, BHK);
        using J = juce::Justification;

        //---- left strip: about, presets, keyboard (Harmor: about, browse, preview keyboard)
        {
            auto* a = button (root, "", [this] { aboutMenu(); }, nullptr, "about partials");
            a->setBounds (5, 372, 34, 28);
            a->draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool) { cube (g, r.getCentreX() - 9.0f, r.getCentreY() - 9.5f, 0.17f); };
            a->boxed = false;
            auto* b = button (root, "", [this] { presetMenu (nullptr); }, nullptr, "sounds: built in, saved, open, save, copy and paste");
            b->setBounds (5, 414, 34, 28);
            b->draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool) { g.setColour (cFg); for (int i = 0; i < 3; ++i) g.fillRect (r.getCentreX() - 8.0f + (float) i * 1.5f, r.getCentreY() - 6.0f + (float) i * 5.0f, 13.0f, 1.5f); };
            auto* k = button (root, "", [this] { showKeys = ! showKeys; applySize ((float) getWidth() / (float) BW); }, [this] { return showKeys; }, "show or hide the keyboard");
            k->setBounds (5, 456, 34, 28);
            k->draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool)
            {
                g.setColour (cFg); const float x = r.getCentreX() - 9.0f, y = r.getCentreY() - 6.0f;
                g.drawRect (x, y, 16.0f, 12.0f, 1.2f); g.fillRect (x + 4.5f, y, 1.5f, 7.0f); g.fillRect (x + 10.0f, y, 1.5f, 7.0f);
            };
        }

        //---- timbre (top row of the part)
        cap ("phase", 40, 10, 108);
        cell (1, PP::phaseStart, "start", 36, 36, 56); cell (1, PP::phaseRand, "rand", 96, 36, 56);
        root.addAndMakeVisible (wave1); root.addAndMakeVisible (wave2);
        wave1.setBounds (160, 30, 82, 70); wave2.setBounds (315, 30, 82, 70);
        cell (1, PP::timbreMix, "mix", 247, 34, 63, 62, true);
        {
            auto* sb = button (root, "", nullptr, nullptr, "sub harmonic configuration: around or below the fundamental");
            sb->setBounds (402, 30, 30, 26);
            sb->draw = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool)
            {
                const bool below = proc.partValue (part, PP::subMode) > 0.5f;
                g.setColour (cFg);
                for (int i = 0; i < 3; ++i) { const float hh = below ? 4.0f + (float) i * 3.0f : (i == 1 ? 5.0f : 10.0f); g.fillRect (r.getX() + 7.0f + (float) i * 5.0f, r.getBottom() - 6.0f - hh, 2.0f, hh); }
            };
            sb->click = [this, sb] { choiceMenu (sb, PP::subMode); };
            cover (1, PP::subMode);
        }
        cap ("timbre", 160, 108, 250);
        menu (1, PP::blendMode, 300, 106, 110, 21, true);
        vbar (1, PP::sub1, "", 438, 12, 24, 124); vbar (1, PP::sub2, "", 462, 12, 24, 124); vbar (1, PP::sub3, "", 486, 12, 24, 124);
        lab ("sub", 438, 138, 72, J::centred);
        vbar (1, PP::prot, "prot", 530, 12, 36, 146);
        vbar (1, PP::clip, "clip", 578, 6, 36, 152);
        {
            auto* cb = button (root, "", nullptr, nullptr, "clipping mode");
            cb->setBounds (580, 162, 34, 26);
            cb->draw = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool)
            {
                const int mode = (int) proc.partValue (part, PP::clipMode);
                juce::Path p; const float x = r.getX() + 8.0f, y = r.getBottom() - 6.0f, w = 15.0f, h = 13.0f;
                if (mode == 0) { p.startNewSubPath (x, y); p.lineTo (x + w * 0.55f, y - h); p.lineTo (x + w, y - h); }
                else if (mode == 4) { p.startNewSubPath (x, y); p.lineTo (x + w * 0.45f, y); p.lineTo (x + w, y - h); }
                else { p.startNewSubPath (x, y); p.lineTo (x + w * 0.45f, y); p.lineTo (x + w * (mode == 3 ? 0.45f : 0.6f), y - h * 0.6f); p.lineTo (x + w, y - h); }
                g.setColour (cFg); g.strokePath (p, juce::PathStrokeType (1.4f));
            };
            cb->click = [this, cb] { choiceMenu (cb, PP::clipMode); };
            cover (1, PP::clipMode);
        }
        vbar (1, PP::fxMix, "fx", 629, 22, 32, 186); vbar (1, PP::vol, "vol", 661, 22, 32, 186); vbar (1, PP::envVol, "env", 693, 22, 32, 186);
        cell (1, PP::autoGain, "auto", 732, 10, 62);
        mini (1, PP::pan, "pan", 734, 72, 58);
        lab ("vel", 734, 104, 60, J::centredLeft, true);
        tog (1, PP::velAtkSkip, "time", 734, 124, 70); tog (1, PP::velAtkSpeed, "scale", 734, 146, 70); tog (1, PP::velRel, "rel", 734, 168, 70);

        //---- blur · trem
        cap ("blur", 40, 166, 304);
        cell (1, PP::blurMix, "mix", 66, 192, 60); cell (1, PP::blurAttack, "attack", 130, 192, 58); cell (1, PP::blurDecay, "decay", 190, 192, 58); cell (1, PP::blurHarm, "harm", 252, 192, 56);
        lab ("time", 140, 172, 100, J::centred, true);
        mini (1, PP::blurTop, "top", 312, 190, 34); mini (1, PP::blurBottom, "bot", 312, 222, 34);
        cap ("trem", 358, 166, 212);
        cell (1, PP::tremDepth, "depth", 388, 192, 58); cell (1, PP::tremSpeed, "speed", 448, 192, 58); cell (1, PP::tremGap, "gap", 508, 192, 58);

        //---- prism · harmonizer · unison
        cap ("prism", 70, 270, 164);
        cell (1, PP::prismAmt, "amt", 72, 300, 64, 64, true);
        lab ("mode", 146, 296, 60);
        seg (root, 1, PP::prismMode, { "+", "x" }, 144, 314, 76, 22);
        lab ("from vol", 146, 340, 80);
        seg (root, 1, PP::prismLvl, { "off", "on", "inv" }, 144, 360, 92, 22);
        cap ("harmonizer", 248, 270, 322);
        cell (1, PP::hzMix, "amt", 250, 300, 66, 64, true); cell (1, PP::hzWidth, "width", 322, 300, 66, 64, true); cell (1, PP::hzStr, "str", 392, 304, 50);
        lab ("ofs", 492, 294, 30, J::centred, true); lab ("step", 524, 294, 40, J::centred, true);
        lab ("shift", 448, 316, 44); lab ("gap", 448, 350, 44);
        num (1, PP::hzShiftOfs, "", 0, 494, 312, 28);
        num (1, PP::hzGapOfs, "", 0, 494, 346, 28);
        // step and its mode in one box, as in Harmor: drag up for additive steps (+1..+8), down for multiplying ones (x1..x8)
        for (int k = 0; k < 2; ++k)
        {
            const int stepId = k == 0 ? PP::hzShiftStep : PP::hzGapStep, mulId = k == 0 ? PP::hzShiftMul : PP::hzGapMul;
            cover (1, stepId); cover (1, mulId);
            auto* r = valueRow ("", I, -8, 8, k == 0 ? 1.0f : -1.0f,
                                [this, stepId, mulId] { const float st = proc.partValue (part, stepId); return proc.partValue (part, mulId) > 0.5f ? -st : st; },
                                [this, stepId, mulId] (float v)
                                {
                                    if (std::abs (v) < 0.5f) return;
                                    for (int q = 0; q < 2; ++q)
                                        if (q == part || link)
                                        {
                                            if (auto* p = proc.partParam (q, mulId)) p->setValueNotifyingHost (p->convertTo0to1 (v < 0 ? 1.0f : 0.0f));
                                            if (auto* p = proc.partParam (q, stepId)) p->setValueNotifyingHost (p->convertTo0to1 (std::abs (v)));
                                        }
                                },
                                [] (float v) { return juce::String (v < 0 ? "x" : "+") + juce::String ((int) std::abs (v)); });
            r->hintName = k == 0 ? "harmonizer shift step   drag up: add, down: multiply" : "harmonizer gap step   drag up: add, down: multiply";
            put (root, r, 524, k == 0 ? 312 : 346, 42, 26, Row::Num); r->labelW = 0;
        }
        cap ("unison", 584, 222, 218);
        num (1, PP::uniOrder, "order", 50, 590, 262, 92, 28);
        menu (1, PP::uniType, 588, 326, 96, 26);
        inl (1, PP::uniAlt, "alt", 30, 0, 588, 366, 96, 22);
        vbar (1, PP::uniPan, "pan", 694, 252, 36, 140); vbar (1, PP::uniPitch, "pitch", 730, 252, 36, 140); vbar (1, PP::uniPhase, "phase", 766, 252, 38, 140);

        //---- pitch · vibrato · legato
        cap ("pitch", 72, 406, 334);
        seg (root, 1, PP::pitchMode, { "hz", "oct" }, 70, 438, 48, 46, true);
        lab ("freq", 124, 436, 40); lab ("detune", 124, 464, 48);
        num (1, PP::freqMul, "", 0, 172, 432, 74); lab ("/", 246, 436, 12, J::centred, true); num (1, PP::freqDiv, "", 0, 258, 432, 32);
        num (1, PP::detMul, "", 0, 172, 460, 74); lab ("/", 246, 464, 12, J::centred, true); num (1, PP::detDiv, "", 0, 258, 460, 32);
        cell (1, PP::pitch, "pitch", 296, 432, 54); cell (1, PP::pitchEnv, "env", 352, 432, 54);
        cap ("vibrato", 420, 406, 180);
        cell (1, PP::vibDepth, "depth", 462, 432, 62); cell (1, PP::vibSpeed, "speed", 530, 432, 62);
        cap ("legato", 614, 406, 188);
        seg (root, 1, PP::glideLog, { "lin", "log" }, 614, 434, 82, 22); seg (root, 1, PP::glideVar, { "fix", "var" }, 614, 462, 82, 22);
        cell (1, PP::glideTime, "time", 700, 432, 50); cell (1, PP::glideLimit, "limit", 752, 432, 50);

        //---- the filtering section: filter 1 over filter 2, the mix between them in the middle, resonance on the right
        for (int f = 0; f < 2; ++f)
        {
            const int o = f * kFilterStride;
            const int ky = f == 0 ? 80 : 196, sy = f == 0 ? 22 : 276;       // bars, and the line with the selectors
            tog (1, PP::f1AdaptEnv + o, "adapt", 838, sy + 4, 78);
            seg (root, 1, PP::f1Scale + o, { "oct", "hz" }, 918, sy - 8, 46, 44, true);
            menu (1, PP::f1Type + o, 972, sy, 172, 26);
            cell (1, PP::f1Env + o, "env", 848, ky, 78, 66, true); cell (1, PP::f1Kb + o, "kb track", 934, ky + 6, 56); cell (1, PP::f1Width + o, "width", 994, ky + 6, 58);
            cell (1, PP::f1Freq + o, "freq", 1060, ky, 82, 66, true);
            menu (1, PP::f1ResType + o, 1218, sy, 172, 26);
            seg (root, 1, PP::f1ResScale + o, { "oct", "hz" }, 1398, sy - 8, 46, 44, true);
            cell (1, PP::f1Res + o, "res", 1214, ky, 62, 66, true); cell (1, PP::f1ResWidth + o, "width", 1282, ky + 6, 56);
            cell (1, PP::f1ResOfs + o, "ofs", 1358, ky + 6, 56); cell (1, PP::f1ResOsc + o, "osc", 1420, ky + 6, 56);
            tog (1, PP::f1Adapt + o, "adapt", 1340, f == 0 ? 150 : 172, 76, 18)->mini = true;
        }
        cell (1, PP::filtMix, "1 > 2 mix", 1148, 132, 64);
        lab ("par", 1140, 192, 34, J::centred, true); lab ("ser", 1186, 192, 34, J::centred, true);

        //---- pluck · phaser · eq
        cap ("pluck", 848, 338, 138);
        cell (1, PP::pluckTime, "time", 850, 398, 78, 66, true);
        tog (1, PP::pluckBlur, "blur", 932, 440, 60);
        cap ("phaser", 1004, 338, 250);
        menu (1, PP::phType, 1132, 336, 120, 21, true);
        seg (root, 1, PP::phScale, { "oct", "hz", "harm" }, 1264, 334, 58, 62, true);
        tog (1, PP::phLfo, "lfo", 1084, 368, 60, 18)->mini = true;
        cell (1, PP::phMix, "mix", 1004, 398, 70, 66, true); cell (1, PP::phWidth, "width", 1080, 398, 70, 66, true);
        cell (1, PP::phOfs, "ofs", 1156, 404, 56); cell (1, PP::phSpeed, "speed", 1216, 404, 62); cell (1, PP::phKb, "kb.t", 1282, 404, 56);
        cap ("eq", 1368, 338, 112);
        cell (1, PP::eqAmt, "mix", 1384, 398, 82, 66, true);
        {
            auto* eb = button (root, "curves", [this] { juce::PopupMenu m; m.addItem (1, "Edit the global EQ (fixed frequencies)"); m.addItem (2, "Edit the local EQ (follows the note)");
                                                        showMenu (m, nullptr, [this] (int r) { if (r == 1) select (true, SH::GlobalEQ, 0); else if (r == 2) select (true, SH::LocalEQ, 0); }); },
                               nullptr, "the eq curves: global (fixed frequencies) and local (follows the note)");
            eb->setBounds (1392, 366, 70, 22); eb->textSize = 12.0f;
        }

        //---- right edge: the display's switches (Harmor: visual feedback, oct / Hz)
        {
            auto* eye = button (root, "", [this] { vizMode = vizMode == 2 ? 0 : 2; layoutViz(); }, [this] { return vizMode != 2; }, "partial display on / off");
            eye->setBounds (1492, 368, 36, 26);
            eye->draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool)
            {
                juce::Path p; const float cx = r.getCentreX() - 1.0f, cy = r.getCentreY();
                p.startNewSubPath (cx - 9.0f, cy); p.quadraticTo (cx, cy - 8.0f, cx + 9.0f, cy); p.quadraticTo (cx, cy + 8.0f, cx - 9.0f, cy);
                g.setColour (cFg); g.strokePath (p, juce::PathStrokeType (1.2f)); g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
            };
            auto* ex = button (root, "", [this] { vizMode = vizMode == 1 ? 0 : 1; layoutViz(); }, [this] { return vizMode == 1; }, "partial display: wide / narrow");
            ex->setBounds (1492, 408, 36, 26);
            ex->draw = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool)
            {
                juce::Path p; const float cx = r.getCentreX() - 1.0f, cy = r.getCentreY(), d = vizMode == 1 ? 1.0f : -1.0f;
                p.addTriangle (cx - 4.0f * d, cy - 6.0f, cx - 4.0f * d, cy + 6.0f, cx + 5.0f * d, cy);
                g.setColour (cFg); g.fillPath (p);
            };
            auto* s = segs.add (new Seg());
            s->items = { "oct", "hz" }; s->vertical = true; s->hintName = "partial display scale";
            s->get = [this] { return spectro.hzScale ? 1 : 0; }; s->set = [this] (int v) { spectro.hzScale = v == 1; spectro.clear(); };
            s->onHint = [this] (const juce::String& h) { setHint (h); };
            root.addAndMakeVisible (s); s->setBounds (1490, 446, 42, 46);
        }
        root.addAndMakeVisible (spectro);

        //---- global
        {
            auto* ob = button (root, "", [this] { presetMenu (nullptr); }, nullptr, "options: sounds, copy and paste, chorus and reverb presets");
            ob->setBounds (20, 507, 30, 26);
            ob->draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool) { juce::Path p; p.addTriangle (r.getCentreX() - 5.0f, r.getCentreY() - 6.0f, r.getCentreX() - 5.0f, r.getCentreY() + 6.0f, r.getCentreX() + 5.0f, r.getCentreY()); g.setColour (cFg); g.fillPath (p); };
        }
        cap ("global", 56, 508, 340);
        inl (0, GP::masterVol, "vol", 28, 54, 122, 506, 126, 20);
        inl (1, PP::lfoAmt, "lfo", 26, 44, 262, 506, 134, 20);
        cell (2, XP::legLimit, "limit", 22, 558, 58);
        { auto* r = put (root, paramRow (3, PP::legato, "legato"), 90, 574, 84, 20, Row::Inline); r->labelW = 50; }
        { auto* r = put (root, paramRow (3, PP::porta, "porta"), 90, 624, 84, 20, Row::Inline); r->labelW = 50; }
        { auto* r = put (root, paramRow (3, PP::glideVel, "vel"), 182, 599, 62, 20, Row::Inline); r->boxFirst = true; }
        { auto* r = put (root, paramRow (3, PP::velVol, "vel"), 272, 558, 70, 20, Row::Inline); r->boxFirst = true; r->hintName = "velocity to volume"; }
        lab ("vol", 268, 596, 62, J::centred, true); lab ("pitch", 334, 596, 44, J::centred, true);
        vbar (0, GP::fxPre, "pre", 268, 614, 34, 196); vbar (0, GP::fxPost, "post", 300, 614, 34, 196);
        vbar (2, XP::gPitch, "", 344, 614, 24, 178);
        { const char* tk[] = { "+24", "+12", "0", "-12", "-24" }; for (int i = 0; i < 5; ++i) lab (tk[i], 368, 612 + i * 41, 30, J::centredLeft, true); }
        lab ("fx", 284, 808, 34, J::centred, true);
        cap ("strum", 30, 696, 212);
        menu (0, GP::strumMode, 30, 728, 96, 26);
        cell (0, GP::strumTime, "time", 136, 722, 50); cell (0, GP::strumTension, "tns", 190, 722, 50);

        for (int q = 0; q < 2; ++q)
        {
            auto* b = button (root, q == 0 ? "A" : "B", [this, q] { setPart (q); }, [this, q] { return part == q; }, q == 0 ? "edit part a" : "edit part b");
            b->setBounds (408 + q * 84, 505, 88, 42); b->textSize = 26.0f; partBtn[q] = b;
        }
        { auto* r = put (root, paramRow (0, GP::partAOn, ""), 400, 562, 24, 18, Row::Inline); r->boxFirst = true; }
        { auto* r = put (root, paramRow (0, GP::abMix, ""), 432, 560, 118, 22, Row::Inline); r->labelW = 0; r->valueW = 0; }
        { auto* r = put (root, paramRow (0, GP::partBOn, ""), 556, 562, 24, 18, Row::Inline); r->boxFirst = true; }
        lab ("part", 402, 592, 40);
        { auto* r = put (root, valueRow ("link", B, 0, 1, 0, [this] { return link ? 1.0f : 0.0f; }, [this] (float v) { link = v > 0.5f; }), 500, 592, 72, 20, Row::Inline);
          r->boxFirst = true; r->hintName = "link parts: a change to one part's control is made to the other too"; }
        root.addAndMakeVisible (pad); pad.setBounds (430, 628, 144, 144);
        lab ("mod", 392, 798, 36);
        for (int k = 0; k < 3; ++k)
        {
            static const char* nm[3] = { "x", "y", "z" };
            auto* r = paramRow (0, GP::modX + k, nm[k]);
            r->get = [this, k] { return modValue (k); };
            modRows[k] = r;
            r->menuExtra = [this, k] (juce::PopupMenu& m)
            {
                const int cc = proc.modCC[k].load();
                m.addSeparator(); m.addItem (9, cc >= 0 ? "Controller: CC " + juce::String (cc) : juce::String ("Controller: none"), false);
                m.addItem (10, "MIDI learn (then move a controller)"); m.addItem (11, "Forget the controller", cc >= 0); m.addItem (12, "Default controllers (X: CC 1, Y: CC 74, Z: CC 71)");
            };
            r->menuResult = [this, k] (int res) { modMenuResult (k, res); };
            r->hintExtra = [this, k] (float) { const int cc = proc.modCC[k].load(); return cc >= 0 ? "   cc " + juce::String (cc) : juce::String(); };
            put (root, r, 432 + k * 48, 778, 44, 44, Row::Cell); r->mini = true;
        }
        lab ("info", 32, 814, 40, J::centredLeft, true);
        root.addAndMakeVisible (info); info.setBounds (30, 834, 542, 70);

        //---- the editor: target, part, smoothing, then the tabs
        targetBtn = button (root, "", [this] { targetMenu(); }, nullptr, "editor target: what the graph below moves");
        targetBtn->setBounds (594, 506, 296, 34); targetBtn->boxed = false;
        targetBtn->draw = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool)
        {
            g.setColour (cFg); g.setFont (fnt (16.0f, true));
            g.drawText (juce::String (selShape ? shapeDefs[selIndex].name : targetInfo[selIndex].name).toLowerCase() + U (kDown), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
            g.setColour (cFill.withAlpha (0.7f)); g.fillRect (r.getX(), r.getBottom() - 2.0f, r.getWidth(), 1.0f);
        };
        artBtn = button (root, "", [this] { artMenu(); }, nullptr, "articulator part: envelope, LFO or a mapping (each can be on at once)");
        artBtn->setBounds (892, 506, 248, 34); artBtn->boxed = false;
        artBtn->draw = [this] (juce::Graphics& g, juce::Rectangle<float> full, bool)
        {
            g.setColour (cDim); g.setFont (mono (11.5f)); g.drawText (U ("\xe2\x96\xb8"), full.withWidth (18.0f), juce::Justification::centred);
            const auto r = full.withTrimmedLeft (18.0f);
            g.setColour (cFg); g.setFont (fnt (16.0f, true));
            g.drawText ((selArt == 0 ? juce::String ("Envelope") : (selArt == 1 ? juce::String ("LFO") : juce::String (mapNames[juce::jlimit (0, MAP::Count - 1, selArt - 2)]))).toLowerCase() + U (kDown), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
            g.setColour (cFill.withAlpha (0.7f)); g.fillRect (r.getX(), r.getBottom() - 2.0f, r.getWidth(), 1.0f);
        };
        smoothRow = put (root, valueRow ("", F, 0, 1, 0, [this] { return td().smooth; }, [this] (float v) { td().smooth = v; proc.commit (part); }, [] (float v) { return timeStr (v * v * 0.5f); }), 1150, 512, 88, 22, Row::Inline);
        smoothRow->labelW = 0; smoothRow->valueW = 46; smoothRow->hintName = "articulator smoothing";
        const char* tn[] = { "ENV", "IMG", "FX", "ADV" };
        const char* th[] = { "envelopes, LFOs, mappings and shapes", "image synthesis and resynthesis", "effects", "advanced" };
        for (int i = 0; i < 4; ++i) { auto* b = button (root, tn[i], [this, i] { tab = i; layoutPages(); root.repaint(); }, [this, i] { return tab == i; }, th[i]); b->setBounds (1246 + i * 69, 506, 72, 34); b->textSize = 15.0f; }

        for (auto* pg : { &pageEnv, &pageImg, &pageFx, &pageAdv }) { root.addAndMakeVisible (pg); pg->setBounds (594, 550, 930, 356); }
        buildEnvPage(); buildImgPage(); buildFxPage(); buildAdvPage();

        root.addAndMakeVisible (keys);
        keys.setAvailableRange (24, 108); keys.setBounds (55, 918, 1430, 98); keys.setKeyWidth (1430.0f / 50.0f); keys.setLowestVisibleKey (24);
        layoutViz();
        seedCurve();
        layoutPages();
    }

    // a menu for a choice parameter of the part on show, opened from a drawn button
    void choiceMenu (juce::Component* target, int ppIndex)
    {
        juce::PopupMenu m;
        auto ch = juce::StringArray::fromTokens (partDefs[ppIndex].choices, "|", "");
        const int cur = (int) proc.partValue (part, ppIndex);
        for (int i = 0; i < ch.size(); ++i) m.addItem (i + 1, ch[i], true, i == cur);
        showMenu (m, target, [this, ppIndex] (int r)
        {
            if (r <= 0) return;
            proc.setParam (proc.partParam (part, ppIndex), (float) (r - 1));
            if (link) proc.setParam (proc.partParam (1 - part, ppIndex), (float) (r - 1));
            root.repaint();
        });
    }

    //----------------------------------------------------------------------------------------------
    void buildEnvPage()
    {
        // (the page sits at 594, 550)
        pageEnv.addAndMakeVisible (curveView); curveView.setBounds (0, 6, 926, 292);
        artOn = valueRow ("", B, 0, 1, 0, [this] { return artIsOn() ? 1.0f : 0.0f; }, [this] (float v) { setArtOn (v > 0.5f); curveChanged (false); layoutPages(); });
        artOn->boxFirst = true; artOn->hintName = "this part on / off"; rows.add (artOn); artOn->style = Row::Inline; pageEnv.addAndMakeVisible (artOn); artOn->setBounds (4, 308, 24, 20);

        // attack / decay / sustain / release: modifiers of the drawn envelope (host parameters, so they can be automated).
        // The graph keeps its shape; the orange line shows what the modifiers make of it.
        for (int k = 0; k < EM::Count; ++k)
        {
            const bool time = k != EM::Sus;
            auto prm = [this, k] () -> juce::RangedAudioParameter* { return selIndex < TG::FirstMapping ? proc.envModParam (part, selIndex, k) : nullptr; };
            auto* r = valueRow (juce::String (envModNames[k]).substring (0, 3), F, time ? kEnvModMin : 0.0f, time ? kEnvModMax : 2.0f, 1.0f,
                                [this, k] { return selIndex < TG::FirstMapping ? proc.envModValue (part, selIndex, k) : 1.0f; },
                                [this, prm] (float v) { if (auto* p = prm()) p->setValueNotifyingHost (p->convertTo0to1 (v)); curveView.repaint(); },
                                [] (float v) { return "x" + juce::String (v, 2); });
            if (time)
            {
                r->toNorm = [] (float v) { return clamp01 (std::log (juce::jmax (kEnvModMin, v) / kEnvModMin) / std::log (kEnvModMax / kEnvModMin)); };
                r->fromNorm = [] (float n) { return kEnvModMin * std::pow (kEnvModMax / kEnvModMin, n); };
            }
            else { r->toNorm = [] (float v) { return clamp01 (v * 0.5f); }; r->fromNorm = [] (float n) { return n * 2.0f; }; }      // the fill starts at x1, like the others
            r->begin = [prm] { if (auto* p = prm()) p->beginChangeGesture(); };
            r->end = [prm] { if (auto* p = prm()) p->endChangeGesture(); };
            r->hintName = juce::String ("envelope ") + envModNames[k] + (time ? " length" : " level");
            r->hintExtra = [this, k] (float v) -> juce::String
            {
                // what that makes in real time for this envelope
                const EnvDef& e = td().env; const int n = (int) e.c.pts.size();
                if (n < 2) return {};
                const int sus = (e.sustain >= 0 && e.sustain < n) ? e.sustain : -1;
                const float tD = e.c.pts[(size_t) e.decayIndex()].x, tS = sus >= 0 ? e.c.pts[(size_t) sus].x : e.c.endX(), tE = e.c.endX();
                const char* unit = e.tempo ? " beats" : " s";
                if (k == EM::Att) return "   = " + juce::String (tD * v, 3) + unit;
                if (k == EM::Dec) return "   = " + juce::String ((tS - tD) * v, 3) + unit;
                if (k == EM::Rel) return "   = " + juce::String ((tE - tS) * v, 3) + unit;
                return sus >= 0 ? "   = " + pct (clamp01 (e.c.pts[(size_t) sus].y * v)) : juce::String ("   (no sustain point)");
            };
            rows.add (r); r->style = Row::Cell; pageEnv.addAndMakeVisible (r); r->setBounds (34 + k * 48, 302, 46, 50); envMod[k] = r;
        }
        // the LFO's own controls take the same place
        struct LK { const char* name; const char* hint; float lo, hi, def; float LfoDef::* m; };
        static const LK lk[6] = { { "amt", "LFO amount", -1, 1, 1, &LfoDef::amt }, { "spd", "LFO speed", 0, 1, 0.5f, &LfoDef::speed }, { "tns", "LFO tension (0 triangle, 32 sine, 100 square)", 0, 1, 0.32f, &LfoDef::tension },
                                  { "skew", "LFO skew", -1, 1, 0, &LfoDef::skew }, { "pw", "LFO pulse width", -1, 1, 0, &LfoDef::pw }, { "phase", "LFO phase", 0, 1, 0, &LfoDef::phase } };
        for (int k = 0; k < 6; ++k)
        {
            const auto m = lk[k].m;
            auto* r = valueRow (lk[k].name, F, lk[k].lo, lk[k].hi, lk[k].def, [this, m] { return td().lfo.*m; }, [this, m] (float v) { td().lfo.*m = v; curveChanged(); },
                                k == 1 ? std::function<juce::String (float)> ([this] (float v) { return td().lfo.tempo ? juce::String (16.0f * std::exp2 (-8.0f * v), 2) + "b" : juce::String (0.05f * std::pow (800.0f, v), 2) + "hz"; }) : std::function<juce::String (float)> (pct));
            r->hintName = lk[k].hint;
            rows.add (r); r->style = Row::Cell; pageEnv.addAndMakeVisible (r); r->setBounds (34 + k * 48, 302, 46, 50); lfoKnob[k] = r;
        }
        tempoRow = valueRow ("tempo", B, 0, 1, 0, [this] { return (selArt == 1 ? td().lfo.tempo : td().env.tempo) ? 1.0f : 0.0f; },
                             [this] (float v) { if (selArt == 1) td().lfo.tempo = v > 0.5f; else td().env.tempo = v > 0.5f; curveChanged (false); });
        tempoRow->hintName = "tempo: times in beats, following the host";
        globalRow = valueRow ("global", B, 0, 1, 0, [this] { return (selArt == 1 ? td().lfo.global : td().env.global) ? 1.0f : 0.0f; },
                              [this] (float v) { if (selArt == 1) td().lfo.global = v > 0.5f; else td().env.global = v > 0.5f; curveChanged (false); });
        globalRow->hintName = "global: every note shares one envelope / LFO, restarted by each new note";
        for (auto* r : { tempoRow, globalRow }) { r->boxFirst = true; r->style = Row::Inline; rows.add (r); pageEnv.addAndMakeVisible (r); }
        tempoRow->setBounds (330, 304, 86, 20); globalRow->setBounds (330, 328, 86, 20);

        pageEnv.addAndMakeVisible (scrollStrip); scrollStrip.setBounds (424, 304, 500, 16);
        auto* ob = button (pageEnv, "options", [this] { curveOptions (nullptr); }, nullptr, "tools for the graph: reset, flip, scale, smooth, sequences, copy to the other part, analyze a sound");
        ob->setBounds (424, 326, 92, 24);
        button (pageEnv, "fit", [this] { curveView.zoom = 1.0f; curveView.scroll = 0.0f; curveView.repaint(); scrollStrip.repaint(); }, nullptr, "show the whole graph again")->setBounds (520, 326, 50, 24);
        struct Tg { const char* name; bool* flag; const char* hint; };
        const Tg tg[4] = { { "lock", &freeze, "freeze: no editing, handles hidden" }, { "step", &stepEdit, "step: drag to draw a point per grid step (shift: flat steps)" },
                           { "snap", &snap, "snap points to the grid" }, { "slide", &slide, "slide: later points move with the one you drag" } };
        for (int i = 0; i < 4; ++i)
        {
            bool* f = tg[i].flag;
            auto* b = button (pageEnv, tg[i].name, [this, f] { *f = ! *f; curveView.repaint(); }, [f] { return *f; }, tg[i].hint);
            b->setBounds (708 + i * 55, 326, 56, 24);
        }
    }

    void buildImgPage()
    {
        pageImg.addAndMakeVisible (imageView); imageView.setBounds (0, 6, 548, 312);
        button (pageImg, "options", [this] { imageOptions(); }, nullptr, "image tools")->setBounds (0, 326, 92, 24);
        button (pageImg, "open sound", [this] { openImageFile (true); }, nullptr, "analyze a sound for resynthesis")->setBounds (96, 326, 120, 24);
        button (pageImg, "open picture", [this] { openImageFile (false); }, nullptr, "load a picture: brightness is level, the bottom row is the first partial")->setBounds (220, 326, 130, 24);
        auto* s = section (pageImg, "IMAGE");
        add (s, valueRow ("plane", C, 0, 2, 0, [this] { return (float) imageView.plane; }, [this] (float v) { imageView.plane = (int) v; imageView.repaint(); }, [] (float v) { return juce::String ((int) v == 0 ? "Gain" : ((int) v == 1 ? "Freq" : "All")); }));
        s->rows.getLast()->choices = { "Gain", "Freq", "All" };
        add (s, 1, PP::imgSpeed, "speed"); add (s, 1, PP::imgCoarse, "coarse"); add (s, 1, PP::imgTime, "time"); add (s, 1, PP::imgSmooth, "smooth");
        add (s, 1, PP::imgLoop, "looping"); add (s, 1, PP::imgSharp, "sharp"); add (s, 1, PP::imgDenoise, "denoise");
        for (auto* r : s->rows) { r->labelW = 62; r->valueW = 62; }
        s->setBounds (564, 6, 174, s->wanted());
        auto* l = section (pageImg, "LEVEL"); add (l, 1, PP::imgGainScale, "scale"); add (l, 1, PP::imgGainMix, "mix"); add (l, 1, PP::imgGainInterp, "curve");
        for (auto* r : l->rows) { r->labelW = 62; r->valueW = 62; }
        l->setBounds (564, 6 + s->wanted() + 12, 174, l->wanted());
        auto* f = section (pageImg, "FREQUENCY");
        add (f, 1, PP::imgFreqScale, "scale"); add (f, 1, PP::imgFreqMode, "mode"); add (f, 1, PP::imgFormant, "formant"); add (f, 1, PP::imgFormMix, "form mix"); add (f, 1, PP::imgFreqInterp, "curve");
        for (auto* r : f->rows) { r->labelW = 66; r->valueW = 62; }
        f->setBounds (752, 6, 174, f->wanted());
    }

    void buildFxPage()
    {
        const int w = 176, gap = 12;
        auto place = [&] (Section* s, int col) { for (auto* r : s->rows) { r->labelW = 70; r->valueW = 60; } s->setBounds (col * (w + gap), 6, w, s->wanted()); };
        auto* s = section (pageFx, "DISTORTION", 0, GP::distType);
        add (s, 0, GP::distAmt, "amount"); add (s, 0, GP::distAsym, "asym"); add (s, 0, GP::distWet, "wet"); add (s, 0, GP::distMix, "mix"); add (s, 0, GP::distHiCut, "high cut");
        place (s, 0);

        s = section (pageFx, "CHORUS");
        s->head.reset (valueRow ("", C, 0, 8, 0, [] { return -1.0f; }, [this] (float v) { chorusPreset ((int) v); }, [] (float) { return juce::String ("presets"); }));
        s->head->choices = { "Classic", "Thick", "Deep chorus", "Flanger", "Alt flanger", "Stereoized", "Modulation", "Surround flanger", "Vibration" };
        s->head->labelW = 0; s->head->plainMenu = true; s->addAndMakeVisible (*s->head);
        add (s, 0, GP::chOrder, "order"); add (s, 0, GP::chDepth, "depth"); add (s, 0, GP::chSpeed, "speed"); add (s, 0, GP::chDelay, "delay"); add (s, 0, GP::chSpread, "spread"); add (s, 0, GP::chCross, "cross"); add (s, 0, GP::chMix, "mix");
        place (s, 1);

        s = section (pageFx, "DELAY", 0, GP::dlMode);
        add (s, 0, GP::dlOn, "on"); add (s, 0, GP::dlVol, "input"); add (s, 0, GP::dlPan, "pan"); add (s, 0, GP::dlFb, "feedback"); add (s, 0, GP::dlTime, "time");
        add (s, 0, GP::dlSofs, "st. offset"); add (s, 0, GP::dlLowCut, "low cut"); add (s, 0, GP::dlHiCut, "high cut"); add (s, 0, GP::dlDamp, "fb damping");
        place (s, 2);

        s = section (pageFx, "REVERB");
        s->head.reset (valueRow ("", C, 0, 5, 0, [] { return -1.0f; }, [this] (float v) { reverbPreset ((int) v); }, [] (float) { return juce::String ("presets"); }));
        s->head->choices = { "Classic", "Large hall", "Cathedral", "The venue", "Drum room", "Small studio" };
        s->head->labelW = 0; s->head->plainMenu = true; s->addAndMakeVisible (*s->head);
        add (s, 0, GP::rvOn, "on"); add (s, 0, GP::rvColor, "color"); add (s, 0, GP::rvLowCut, "low cut"); add (s, 0, GP::rvHiCut, "high cut"); add (s, 0, GP::rvPredelay, "predelay");
        add (s, 0, GP::rvSize, "room size"); add (s, 0, GP::rvDiff, "diffusion"); add (s, 0, GP::rvDecay, "decay"); add (s, 0, GP::rvDamp, "damping"); add (s, 0, GP::rvWet, "wet");
        place (s, 3);

        s = section (pageFx, "COMPRESSION", 0, GP::cpType);
        add (s, 0, GP::cpAmt, "amount"); add (s, 0, GP::cpLow, "low"); add (s, 0, GP::cpMid, "mid"); add (s, 0, GP::cpHigh, "high");
        place (s, 4);
    }
    void chorusPreset (int v)
    {
        static const float chP[9][7] = { { 4, .5f, .75f, .06f, 1, -.25f, .5f }, { 5, 1, .85f, 0, 1, .53f, .76f }, { 7, .5f, .5f, 1, 1, .42f, .5f }, { 6, .55f, .83f, 0, 0, .3f, 1 }, { 1, .12f, .3f, 0, 0, 1, -1 },
                                         { 2, .08f, .62f, 0, 0, 0, .75f }, { 1, .04f, .93f, 0, 0, -.17f, 1 }, { 9, .07f, .16f, 0, 0, -.51f, .9f }, { 7, .03f, .96f, 0, 0, 0, .69f } };
        const int ids[7] = { GP::chOrder, GP::chDepth, GP::chSpeed, GP::chDelay, GP::chSpread, GP::chCross, GP::chMix };
        for (int i = 0; i < 7; ++i) proc.setParam (proc.globalParam (ids[i]), chP[juce::jlimit (0, 8, v)][i]);
    }
    void reverbPreset (int v)
    {
        // colour, low cut Hz, high cut Hz, predelay s, size, diffusion, decay s, damp Hz, wet
        static const float rvP[6][9] = { { 2, 105, 6300, 0, .18f, 1, .503f, 3000, .55f }, { 2, 20, 11516, .048f, .4f, 1, 2.232f, 5082, .6f }, { 3, 35, 2181, .074f, .9f, 1, 4.589f, 3107, .6f },
                                         { 1, 391, 6086, .083f, .6f, .65f, 2.359f, 3746, .53f }, { 2, 100, 3644, 0, .35f, 1, .602f, 3569, .55f }, { 2, 100, 6086, 0, .17f, 1, .492f, 3043, .55f } };
        const float* q = rvP[juce::jlimit (0, 5, v)];
        auto cut = [] (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); };
        proc.setParam (proc.globalParam (GP::rvOn), 1); proc.setParam (proc.globalParam (GP::rvColor), q[0]); proc.setParam (proc.globalParam (GP::rvLowCut), cut (q[1])); proc.setParam (proc.globalParam (GP::rvHiCut), cut (q[2]));
        proc.setParam (proc.globalParam (GP::rvPredelay), q[3] / 0.3f); proc.setParam (proc.globalParam (GP::rvSize), q[4]); proc.setParam (proc.globalParam (GP::rvDiff), q[5]);
        proc.setParam (proc.globalParam (GP::rvDecay), std::log (q[6] / 0.1f) / std::log (512.0f)); proc.setParam (proc.globalParam (GP::rvDamp), cut (q[7])); proc.setParam (proc.globalParam (GP::rvWet), q[8]);
    }

    void buildAdvPage()
    {
        unitList.title = "UNIT ORDER (THIS PART)"; unitList.count = UNIT::Count;
        unitList.get = [this] (int i) { return proc.edit (part).unitOrder[i]; }; unitList.name = [] (int u) { return juce::String (unitNames[u]); };
        unitList.swap = [this] (int i, int j) { auto& o = proc.edit (part).unitOrder; std::swap (o[i], o[j]); proc.commit (part); };
        pageAdv.addAndMakeVisible (unitList); unitList.setBounds (0, 6, 190, 240);
        fxList.title = "EFFECTS ORDER"; fxList.count = FXU::Count;
        fxList.get = [this] (int i) { return proc.fxOrder[i]; }; fxList.name = [] (int u) { return juce::String (fxNames[u]); };
        fxList.swap = [this] (int i, int j) { int o[FXU::Count]; for (int k = 0; k < FXU::Count; ++k) o[k] = proc.fxOrder[k]; std::swap (o[i], o[j]); proc.setFxOrder (o); };
        pageAdv.addAndMakeVisible (fxList); fxList.setBounds (214, 160, 220, 134);
        auto* s = section (pageAdv, "THIS PART");
        add (s, 1, PP::predelay, "delay"); add (s, 1, PP::polyRel, "poly rel"); add (s, 1, PP::ramp, "ramp"); add (s, 1, PP::declick, "declick"); add (s, 1, PP::autoGainMode, "auto-gain");
        for (auto* r : s->rows) { r->labelW = 74; r->valueW = 80; }
        s->setBounds (214, 6, 220, s->wanted());
        auto* v = section (pageAdv, "VOICES");
        add (v, 0, GP::mono, "monophonic"); add (v, 0, GP::bendRange, "bend range"); add (v, 0, GP::randomness, "randomness"); add (v, 0, GP::sameMaps, "same for all maps");
        add (v, 0, GP::invertArp, "invert arp"); add (v, 0, GP::smoothMod, "smooth mod"); add (v, 0, GP::sideStereo, "side (a/b as stereo)");
        for (auto* r : v->rows) { r->labelW = r->kind == B ? 160 : 92; r->valueW = 40; }
        v->setBounds (458, 6, 220, v->wanted());
        auto* g = section (pageAdv, "QUALITY");
        add (g, 0, GP::partials, "partials"); add (g, 0, GP::precision, "precision"); add (g, 0, GP::hq, "hq when rendering");
        for (auto* r : g->rows) { r->labelW = r->kind == B ? 160 : 74; r->valueW = 40; }
        g->setBounds (702, 6, 220, g->wanted());
        auto* m = section (pageAdv, "MOD CONTROLLERS");
        for (int k = 0; k < 3; ++k)
        {
            static const char* nm[3] = { "x", "y", "z" };
            auto* r = valueRow (nm[k], C, 0, 1, 0, [] { return 0.0f; }, [] (float) {}, [this, k] (float) { const int cc = proc.modCC[k].load(); return proc.learnMod.load() == k ? juce::String ("move a controller") : (cc >= 0 ? "cc " + juce::String (cc) : juce::String ("none")); });
            r->choices = { "MIDI learn (then move a controller)", "Forget the controller", "Default controllers (X: CC 1, Y: CC 74, Z: CC 71)" };
            r->set = [this, k] (float v) { modMenuResult (k, 10 + (int) v); };
            r->get = [] { return -1.0f; }; r->labelW = 40; r->hintName = juce::String ("midi controller for mod ") + nm[k];
            add (m, r);
        }
        m->setBounds (702, 6 + g->wanted() + 14, 220, m->wanted());
        auto* note = new juce::Label ({}, "click an entry to move it later,\nright-click to move it earlier");
        note->setFont (fnt (12.5f)); note->setColour (juce::Label::textColourId, cDim); pageAdv.addAndMakeVisible (note); note->setBounds (-4, 252, 210, 40); note->setMinimumHorizontalScale (1.0f); notes.add (note);
    }
    juce::OwnedArray<juce::Label> notes;

    void layoutPages()
    {
        const bool env = tab == 0;
        pageEnv.setVisible (env); pageImg.setVisible (tab == 1); pageFx.setVisible (tab == 2); pageAdv.setVisible (tab == 3);
        const bool art = ! selShape;
        const bool hasEnv = art && selArt == 0, hasLfo = art && selArt == 1;
        targetBtn->setVisible (env); artBtn->setVisible (env && art); smoothRow->setVisible (env && art);
        artOn->setVisible (art);
        for (auto* r : envMod) r->setVisible (hasEnv && selIndex < TG::FirstMapping);
        for (auto* r : lfoKnob) r->setVisible (hasLfo);
        tempoRow->setVisible (hasEnv || hasLfo); globalRow->setVisible (hasEnv || hasLfo); scrollStrip.setVisible (hasEnv || hasLfo);
        tempoRow->setTopLeftPosition (hasLfo ? 330 : 234, 304); globalRow->setTopLeftPosition (hasLfo ? 330 : 234, 328);
        proc.engine.vizPart.store (part);
        proc.engine.viewTarget.store (art ? selIndex : -1); proc.engine.viewEnvPos.store (-1.0f); proc.engine.viewLfoT.store (-1.0f);
        if (targetBtn) targetBtn->repaint();
        if (artBtn) artBtn->repaint();
        note = selectionNote();
    }
    juce::String note;
    void setPart (int p) { part = p; seedCurve(); layoutPages(); root.repaint(); }

    //----------------------------------------------------------------------------------------------
    void timerCallback() override
    {
        for (auto* r : allRows) if (r->isShowing()) r->refresh();
        for (auto* s : segs) if (s->isShowing()) s->refresh();
        for (auto* b : buttons) if (b->isShowing()) b->refresh();
        spectro.tick();
        backdrop.repaint (118, 532, 130, 16);
        const float px_ = proc.globalValue (GP::modX) + proc.globalValue (GP::modY) * 3.0f + proc.globalValue (GP::modZ) * 7.0f + proc.modNow[0].load() * 11.0f + proc.modNow[1].load() * 13.0f;
        if (px_ != padSeen) { padSeen = px_; pad.repaint(); }
        if (tab == 1) imageView.repaint();
        if (tab == 0 && curveView.timed() && (proc.engine.activeVoices() > 0 || hadVoices)) curveView.repaint();
        if (tab == 0 && ! selShape && selArt >= 2 + MAP::ModX && selArt <= 2 + MAP::ModZ && px_ != mapSeen) { mapSeen = px_; curveView.repaint(); }
        hadVoices = proc.engine.activeVoices() > 0;
        noteText = juce::String (part == 0 ? "a" : "b") + U (" \xc2\xb7 ") + juce::String (proc.engine.activeVoices()) + " voices" + U (" \xc2\xb7 ") + juce::String (proc.engine.activePartials()) + " partials" + U (" \xc2\xb7 ")
                   + juce::String (juce::roundToInt (proc.cpu.load() * 100.0f)) + "%" + U (" \xc2\xb7 ") + (spectro.hzScale ? "hz" : "oct");
        // the info box: the name, then what the mouse is over; otherwise what the editor is showing
        const int learn = proc.learnMod.load();
        juce::String want = learn >= 0 ? juce::String ("midi learn: move a controller for mod ") + (learn == 0 ? "x" : (learn == 1 ? "y" : "z")) : juce::String();
        if (++slow % 6 == 0)
        {
            const auto n = selectionNote();
            if (n != note || want != learnShown || proc.presetName != nameShown) { note = n; learnShown = want; nameShown = proc.presetName; info.repaint(); }
        }
        if (slow % 15 == 0) { wave1.repaint(); wave2.repaint(); }
    }
    float padSeen = -1, mapSeen = -1; bool hadVoices = false; juce::String learnShown, nameShown;

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
        auto& e = td().env; e.c.pts.clear(); e.sustain = -1; e.loopStart = -1; e.decay = -1; e.on = true;
        const float dur = (float) ((double) len / r->sampleRate);
        e.c.pts.push_back ({ 0, 0, 0, Single, 0 });
        for (int i = 0; i < n; ++i) e.c.pts.push_back ({ dur * (float) (i + 1) / (float) (n + 1), lv[(size_t) i] / mx, 0, Smooth, 0 });
        e.c.pts.push_back ({ dur, 0, 0, Single, 0 });
        curveChanged();
    }

    //----------------------------------------------------------------------------------------------
    // menus
    // Harmor's editor target menu: three columns, a mark next to everything that is in use
    void targetMenu()
    {
        juce::PopupMenu m;
        const PartData& d = proc.edit (part);
        auto item = [&] (int id, const char* name, bool ticked, bool active)
        {
            juce::PopupMenu::Item it (name); it.itemID = id; it.isTicked = ticked;
            if (active) it.shortcutKeyDescription = U ("\xe2\x97\x82 on");
            m.addItem (it);
        };
        m.addSectionHeader ("Articulation");
        for (int t = 0; t < TG::FirstMapping; ++t) item (1 + t, targetInfo[t].name, ! selShape && selIndex == t, d.tg[t].active());
        m.addColumnBreak(); m.addSectionHeader ("Mapping");
        for (int t = TG::FirstMapping; t < TG::Count; ++t) item (1 + t, targetInfo[t].name, ! selShape && selIndex == t, d.tg[t].active());
        m.addColumnBreak(); m.addSectionHeader ("Shaping");
        for (int s = 0; s < SH::Count; ++s) item (1000 + s, shapeDefs[s].name, selShape && selIndex == s, false);
        showMenu (m, targetBtn, [this] (int r)
        {
            if (r >= 1000) select (true, r - 1000, 0);
            else if (r >= 1) { const int t = r - 1; select (false, t, t >= TG::FirstMapping ? juce::jmax (2, selShape ? 2 : selArt) : (selShape ? 0 : selArt)); }
        });
    }
    void artMenu()
    {
        if (selShape) return;
        juce::PopupMenu m;
        const TargetDef& t = td();
        auto item = [&] (int id, const juce::String& name, bool ticked, bool active)
        {
            juce::PopupMenu::Item it (name); it.itemID = id; it.isTicked = ticked;
            if (active) it.shortcutKeyDescription = U ("\xe2\x97\x82 on");
            m.addItem (it);
        };
        if (selIndex < TG::FirstMapping) { item (1, "Envelope", selArt == 0, t.env.on); item (2, "LFO", selArt == 1, t.lfo.on); m.addSeparator(); }
        for (int k = 0; k < MAP::Count; ++k) item (3 + k, mapNames[k], selArt == 2 + k, t.mapOn[k]);
        showMenu (m, artBtn, [this] (int r) { if (r > 0) select (false, selIndex, r - 1); });
    }
    void modMenuResult (int k, int res)
    {
        if (res == 10) proc.learnMod.store (k);
        else if (res == 11) { proc.modCC[k].store (-1); proc.learnMod.store (-1); }
        else if (res == 12) { proc.modCC[0].store (1); proc.modCC[1].store (74); proc.modCC[2].store (71); proc.learnMod.store (-1); }
    }
    void modMenu (juce::Component* target, int)
    {
        juce::PopupMenu m;
        static const char* nm[3] = { "X", "Y", "Z" };
        for (int k = 0; k < 3; ++k)
        {
            const int cc = proc.modCC[k].load();
            m.addItem (100 + k, juce::String ("MIDI learn ") + nm[k] + (cc >= 0 ? "   (now CC " + juce::String (cc) + ")" : juce::String ("   (no controller)")));
        }
        m.addSeparator();
        for (int k = 0; k < 3; ++k) m.addItem (200 + k, juce::String ("Forget the controller of ") + nm[k], proc.modCC[k].load() >= 0);
        m.addItem (12, "Default controllers (X: CC 1, Y: CC 74, Z: CC 71)");
        m.addSeparator(); m.addItem (300, "Smooth modulation", true, proc.globalValue (GP::smoothMod) > 0.5f);
        m.addItem (301, "Centre X and Y");
        showMenu (m, target, [this] (int r)
        {
            if (r >= 100 && r < 103) modMenuResult (r - 100, 10);
            else if (r >= 200 && r < 203) modMenuResult (r - 200, 11);
            else if (r == 12) modMenuResult (0, 12);
            else if (r == 300) proc.setParam (proc.globalParam (GP::smoothMod), proc.globalValue (GP::smoothMod) > 0.5f ? 0.0f : 1.0f);
            else if (r == 301) { proc.setParam (proc.globalParam (GP::modX), 0.5f); proc.setParam (proc.globalParam (GP::modY), 0.5f); }
        });
    }
    void aboutMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, juce::String ("partials ") + JucePlugin_VersionString, false);
        m.addItem (2, "additive / subtractive synthesizer with image resynthesis", false);
        m.addItem (3, "part of the faltung bundle - FreakiePie", false);
        m.addSeparator();
        m.addItem (10, "Window size 60%"); m.addItem (11, "Window size 75%"); m.addItem (12, "Window size 90%"); m.addItem (13, "Window size 100%");
        showMenu (m, nullptr, [this] (int r) { static const float sz[4] = { 0.6f, 0.75f, 0.9f, 1.0f }; if (r >= 10 && r <= 13) applySize (sz[r - 10]); });
    }

    void curveOptions (juce::Component* target)
    {
        juce::PopupMenu m, seq;
        const bool env = ! selShape && selArt == 0, lfo = ! selShape && selArt == 1;
        m.addItem (1, "Reset"); m.addItem (2, "Flip vertically"); m.addItem (3, "Normalize levels"); m.addItem (4, "Decimate points"); m.addItem (5, "Smooth up");
        m.addItem (6, "Turn all points smooth"); m.addItem (7, "Scale levels: 50%"); m.addItem (8, "Scale levels: 200%");
        if (! selShape) { m.addSeparator(); m.addItem (40, juce::String ("Copy to part ") + (part == 0 ? "B" : "A")); m.addItem (41, "Copy state"); m.addItem (42, "Paste state"); }
        if (selShape) { m.addSeparator(); m.addItem (20, "Draw per harmonic (bars)"); m.addItem (21, "Back to a line"); }
        if (env)
        {
            m.addSeparator(); m.addItem (10, "Make all loop"); m.addItem (11, "Analyze audio file...");
            seq.addItem (30, "8 steps"); seq.addItem (31, "16 steps"); seq.addItem (32, "16 steps, random gates"); seq.addItem (33, "8 steps, arpeggio up"); seq.addItem (34, "8 steps, arpeggio down"); seq.addItem (35, "16 steps, random arpeggio");
            m.addSubMenu ("Create sequence", seq);
        }
        juce::ignoreUnused (lfo);
        showMenu (m, target != nullptr ? target : (juce::Component*) &curveView, [this] (int r) { if (r > 0) curveOp (r); });
    }
    void curveOp (int r)
    {
        Curve* c = curveView.curve();
        if (c == nullptr) return;
        const bool env = ! selShape && selArt == 0, lfo = ! selShape && selArt == 1;
        auto each = [c] (std::function<float (float)> f) { for (auto& p : c->pts) p.y = clamp01 (f (p.y)); for (auto& v : c->raw) v = clamp01 (f (v)); };
        switch (r)
        {
            case 1:
                if (selShape) proc.edit (part).defaultShape (selIndex);
                else if (env) { td().env.adsr (0.002f, 0.3f, 1.0f, 0.15f); for (int k = 0; k < EM::Count; ++k) if (selIndex < TG::FirstMapping) proc.setParam (proc.envModParam (part, selIndex, k), 1.0f); }
                else if (lfo) { auto& l = td().lfo; const bool on = l.on; l = LfoDef(); l.on = on; }
                else { c->pts.clear(); seedCurve(); }
                break;
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
            case 40:        // the whole target (all its parts) to the same target of the other part
                proc.edit (1 - part).tg[selIndex] = td();
                for (int k = 0; k < EM::Count; ++k) if (selIndex < TG::FirstMapping) proc.setParam (proc.envModParam (1 - part, selIndex, k), proc.envModValue (part, selIndex, k));
                proc.commit (1 - part); return;
            case 41: targetClip = td(); haveClip = true; return;
            case 42: if (haveClip) { td() = targetClip; curveChanged (false); layoutPages(); } return;
            default:
                if (r >= 30 && env)
                {
                    const int steps = (r == 30 || r == 33 || r == 34) ? 8 : 16;
                    juce::Random rnd; auto& e = td().env;
                    e.c.pts.clear(); e.tempo = true; e.on = true; e.decay = -1;
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
        curveChanged (r != 1);
        layoutPages();
    }
    TargetDef targetClip; bool haveClip = false;

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
        showMenu (m, &imageView, [this] (int r) { if (r > 0) imageOp (r); });
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
            t.env.sustain = -1; t.env.loopStart = -1; t.env.decay = -1;
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
    // sounds
    void loadPreset (const juce::File& f)
    {
        if (auto xml = juce::parseXML (f)) { proc.applyState (juce::ValueTree::fromXml (*xml)); proc.presetName = f.getFileNameWithoutExtension(); seedCurve(); layoutPages(); root.repaint(); }
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
            case 3: proc.presetName = "Phaser bass";      // the filter envelope sweeps down from the freq control by the env amount
                P (0, PP::freqDiv, 2); P (0, PP::phMix, 0.8f); P (0, PP::phSpeed, 0.5f); P (0, PP::f1Type, 2); P (0, PP::f1Freq, 0.95f); P (0, PP::f1Res, 0.5f); P (0, PP::f1Env, 0.4f); P (0, PP::sub1, 0.4f);
                a.tg[TG::F1Freq].env.adsr (0.001f, 0.25f, 0.0f, 0.2f); a.tg[TG::F1Freq].env.on = true; a.tg[TG::Volume].env.on = true; G (GP::distType, 1); G (GP::distAmt, 0.3f); break;
            case 4: proc.presetName = "Prism bell";
                P (0, PP::timbreMix, 1); P (0, PP::prismAmt, 0.55f); P (0, PP::pluckTime, 0.6f); P (0, PP::hzMix, 0.4f); P (0, PP::hzShiftStep, 3);
                a.tg[TG::Volume].env.adsr (0.001f, 2.5f, 0.0f, 1.5f); a.tg[TG::Volume].env.on = true; G (GP::rvOn, 1); G (GP::rvDecay, 0.55f); break;
            case 5: proc.presetName = "White noise";
                P (0, PP::pitchMode, 1); P (0, PP::freqDiv, 9); P (0, PP::detDiv, 6); P (0, PP::uniOrder, 3); P (0, PP::uniPan, 0); P (0, PP::uniPitch, 1); P (0, PP::uniType, 2); P (0, PP::phaseRand, -1); P (0, PP::ramp, 0); P (0, PP::vol, -24.0f);
                a.shape[SH::Timbre1].line ({ { 0, 0.2f }, { 1, 0.95f } });
                a.tg[TG::Pitch].mapOn[MAP::Key] = true; a.tg[TG::Pitch].map[MAP::Key].line ({ { 0, 0.5f + 60.0f / 96.0f * 0.5f }, { 1, 0.5f - 67.0f / 96.0f * 0.5f } }); break;
            case 6: proc.presetName = "Arp sequence";
                P (0, PP::f1Type, 1); P (0, PP::f1Freq, 0.6f); P (0, PP::f1Res, 0.4f); P (0, PP::pluckTime, 0.5f); part = 0; select (false, TG::Volume, 0); curveOp (33); a.tg[TG::Volume].env.on = true; G (GP::dlOn, 1); break;
            case 7: proc.presetName = "Vowel";
                P (0, PP::f1Type, 14); P (0, PP::f1Freq, 0.55f); P (0, PP::f1Width, 0.7f); P (0, PP::f1Res, 0.5f); P (0, PP::f1ResType, 3); P (0, PP::vibDepth, 12); P (0, PP::uniOrder, 2);
                a.tg[TG::F1Freq].mapOn[MAP::ModX] = true; a.tg[TG::F1Freq].map[MAP::ModX].line ({ { 0, 0.3f }, { 1, 0.7f } }); a.tg[TG::Volume].env.adsr (0.05f, 0.3f, 0.9f, 0.3f); a.tg[TG::Volume].env.on = true;
                proc.presetInfo = "x: the vowel (filter 1 frequency)"; break;
            default: proc.presetName = "Default"; break;
        }
        proc.commit (0); seedCurve(); layoutPages(); root.repaint();
    }
    void presetMenu (juce::Component* target)
    {
        juce::PopupMenu m, fac, user, cho, rev;
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
        m.addSeparator();
        const char* cn[] = { "Classic", "Thick", "Deep chorus", "Flanger", "Alt flanger", "Stereoized", "Modulation", "Surround flanger", "Vibration" };
        for (int i = 0; i < 9; ++i) cho.addItem (200 + i, cn[i]);
        const char* rn[] = { "Classic", "Large hall", "Cathedral", "The venue", "Drum room", "Small studio" };
        for (int i = 0; i < 6; ++i) rev.addItem (300 + i, rn[i]);
        m.addSubMenu ("Chorus", cho); m.addSubMenu ("Reverb", rev);
        showMenu (m, target, [this] (int r)
        {
            if (r >= 1000 && r - 1000 < presetFiles.size()) loadPreset (presetFiles[r - 1000]);
            else if (r >= 300 && r < 306) reverbPreset (r - 300);
            else if (r >= 200 && r < 209) chorusPreset (r - 200);
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
            seedCurve(); layoutPages(); root.repaint();
        });
    }
};

juce::AudioProcessorEditor* PartialsProcessor::createEditor() { return new PartialsEditor (*this); }

// for the test tool and the manual's pictures: put the window in a given state
void partialsEditorShow (juce::AudioProcessorEditor* e, int tab, int shape, int target, int art, int part)
{
    if (auto* ed = dynamic_cast<PartialsEditor*> (e))
    {
        ed->part = juce::jlimit (0, 1, part);
        if (shape >= 0) ed->select (true, shape, 0); else if (target >= 0) ed->select (false, target, art);
        ed->tab = tab; ed->layoutPages(); ed->root.repaint();
    }
}

#if PARTIALS_TEST
// The window driven like a user would, for the test tool: every check that fails comes back as a line of text.
juce::StringArray partialsEditorSelfTest (juce::AudioProcessorEditor* e)
{
    juce::StringArray bad;
    auto* ed = dynamic_cast<PartialsEditor*> (e);
    if (ed == nullptr) { bad.add ("not the Partials window"); return bad; }
    auto& proc = ed->proc;
    auto ev = [] (juce::Component& c, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0, &c, &c, juce::Time(), down, juce::Time(), clicks, at != down);
    };
    // every parameter has a control somewhere
    for (int i = 0; i < GP::Count; ++i) if (! ed->covered.count ({ 0, i })) bad.add (juce::String ("no control for global ") + globalDefs[i].id);
    for (int i = 0; i < PP::Count; ++i) if (! ed->covered.count ({ 1, i })) bad.add (juce::String ("no control for part ") + partDefs[i].id);
    for (int i = 0; i < XP::Count; ++i) if (! ed->covered.count ({ 2, i })) bad.add (juce::String ("no control for ") + extraDefs[i].id);
    // every control can be driven end to end without a crash, and comes back to where it was
    for (auto* r : ed->allRows)
    {
        if (! r->get || ! r->set) continue;
        const float v0 = r->value();
        if (v0 < r->lo - 1.0e-3f || v0 > r->hi + 1.0e-3f) continue;      // preset menus and the like
        r->change (r->lo); r->change (r->hi); r->change (v0);
    }
    // the pad: X left to right, Y bottom to top, Z on the wheel
    {
        auto& pad = ed->pad; const auto o = pad.getLocalBounds().toFloat().reduced (1.0f);
        const juce::Point<float> a (o.getX() + 6.0f + 0.25f * (o.getWidth() - 12.0f), o.getBottom() - 6.0f - 0.75f * (o.getHeight() - 12.0f));
        const juce::Point<float> b (o.getX() + 6.0f + 0.8f * (o.getWidth() - 12.0f), o.getBottom() - 6.0f - 0.1f * (o.getHeight() - 12.0f));
        pad.mouseDown (ev (pad, a, a));
        if (std::abs (proc.globalValue (GP::modX) - 0.25f) > 0.01f || std::abs (proc.globalValue (GP::modY) - 0.75f) > 0.01f) bad.add ("pad: a click at 25% / 75% gave x " + juce::String (proc.globalValue (GP::modX), 3) + " y " + juce::String (proc.globalValue (GP::modY), 3));
        pad.mouseDrag (ev (pad, b, a)); pad.mouseUp (ev (pad, b, a));
        if (std::abs (proc.globalValue (GP::modX) - 0.8f) > 0.01f || std::abs (proc.globalValue (GP::modY) - 0.1f) > 0.01f) bad.add ("pad: a drag to 80% / 10% gave x " + juce::String (proc.globalValue (GP::modX), 3) + " y " + juce::String (proc.globalValue (GP::modY), 3));
        const float z0 = proc.globalValue (GP::modZ);
        juce::MouseWheelDetails wh; wh.deltaX = 0; wh.deltaY = 0.2f; wh.isReversed = false; wh.isSmooth = false; wh.isInertial = false;
        pad.mouseWheelMove (ev (pad, b, b), wh);
        if (! (proc.globalValue (GP::modZ) > z0 + 0.01f)) bad.add ("pad: the wheel did not raise z");
    }
    // the pad dragged by hand: the parameter and every x / y / z bar follow the puck at once, with the window's
    // timer stopped (nothing may wait for it), and a bar or the host moving the parameter moves the puck at once
    {
        ed->stopTimer();
        auto& pad = ed->pad; const auto o = pad.getLocalBounds().toFloat().reduced (1.0f);
        auto at = [&o] (float x, float y) { return juce::Point<float> (o.getX() + 6.0f + x * (o.getWidth() - 12.0f), o.getBottom() - 6.0f - y * (o.getHeight() - 12.0f)); };
        auto same = [&] (const juce::String& when)
        {
            for (int k = 0; k < 3; ++k)
            {
                const float pv = proc.globalParam (GP::modX + k)->getValue(), raw = proc.globalValue (GP::modX + k), puck = pad.val (k);
                auto* r = ed->modRows[k];
                if (r == nullptr) { bad.add ("pad sync: no bar for mod " + juce::String (k)); continue; }
                const float bar = r->value(), drawn = std::abs (r->shown - bar) < 1.0e-6f ? r->shown : r->asked;      // painted already, or asked to be
                if (std::abs (pv - puck) > 1.0e-6f || std::abs (raw - puck) > 1.0e-6f || std::abs (bar - puck) > 1.0e-6f || std::abs (drawn - puck) > 1.0e-6f || std::abs (pad.asked[k] - puck) > 1.0e-6f)
                    bad.add ("pad sync (" + when + ") mod " + juce::String (k) + ": puck " + juce::String (puck, 4) + " param " + juce::String (pv, 4) + " raw " + juce::String (raw, 4)
                             + " bar " + juce::String (bar, 4) + " bar to draw " + juce::String (drawn, 4) + " pad to draw " + juce::String (pad.asked[k], 4));
            }
        };
        const juce::Point<float> start = at (0.1f, 0.9f);
        pad.mouseDown (ev (pad, start, start)); same ("mouse down");
        for (int i = 1; i <= 24; ++i)
        {
            const float t = (float) i / 24.0f;
            pad.mouseDrag (ev (pad, at (0.1f + 0.8f * t, 0.9f - 0.7f * t * t), start));
            same ("drag step " + juce::String (i));
        }
        pad.mouseUp (ev (pad, at (0.9f, 0.2f), start)); same ("mouse up");
        if (std::abs (pad.val (0) - 0.9f) > 0.01f || std::abs (pad.val (1) - 0.2f) > 0.01f) bad.add ("pad sync: the drag ended at x " + juce::String (pad.val (0), 3) + " y " + juce::String (pad.val (1), 3));
        // a bar moved by hand
        ed->modRows[1]->change (0.37f); same ("y bar");
        if (std::abs (pad.val (1) - 0.37f) > 1.0e-4f) bad.add ("pad sync: the y bar did not move the puck");
        // the host (automation, a state load) on the message thread
        proc.globalParam (GP::modZ)->setValueNotifyingHost (0.61f); same ("host z");
        // automation from another thread arrives through the message loop, without the timer
        {
            std::thread t ([&proc] { proc.globalParam (GP::modX)->setValueNotifyingHost (0.42f); }); t.join();
            if (! ed->isUpdatePending()) bad.add ("pad sync: automation from another thread did not ask the message thread to redraw");
            ed->handleUpdateNowIfNeeded();      // what the message loop does on its next pass
            same ("automation from another thread");
        }
        ed->startTimerHz (30);
    }
    // quick map to X, then the mapping is the thing on show and is on
    ed->quickMap (TG::F1Freq, MAP::ModX);
    if (! proc.edit (0).tg[TG::F1Freq].mapOn[MAP::ModX] || ed->selShape || ed->selIndex != TG::F1Freq || ed->selArt != 2 + MAP::ModX) bad.add ("quick map to X did not select and switch on the mapping");
    // an envelope that was never used has a line to look at, off; dragging a point switches it on
    ed->select (false, TG::PhMix, 0);
    {
        auto& t = proc.edit (0).tg[TG::PhMix]; auto& cv = ed->curveView;
        if (t.env.c.pts.size() != 4 || t.env.on) bad.add ("a fresh envelope should show the default line, switched off");
        const juce::Point<float> a (cv.px (t.env.c.pts[2].x), cv.py (t.env.c.pts[2].y)), b (a.x + 30.0f, a.y - 20.0f);
        cv.mouseDown (ev (cv, a, a)); cv.mouseDrag (ev (cv, b, a)); cv.mouseUp (ev (cv, b, a));
        if (! t.env.on) bad.add ("editing an envelope did not switch it on");
        if (! (t.env.c.pts[2].y > 0.5f)) bad.add ("dragging the sustain point up did not raise it");
        // a right-click on empty space adds a point and keeps the markers on their points
        const int susBefore = t.env.sustain; const float susX = t.env.c.pts[(size_t) susBefore].x;
        const juce::Point<float> q (cv.px (0.5f * (t.env.c.pts[1].x + t.env.c.pts[2].x)), cv.py (0.9f));
        cv.mouseDown (ev (cv, q, q, juce::ModifierKeys::rightButtonModifier)); cv.mouseUp (ev (cv, q, q));
        if (t.env.c.pts.size() != 5 || std::abs (t.env.c.pts[(size_t) t.env.sustain].x - susX) > 1.0e-4f) bad.add ("adding a point moved the sustain marker off its point");
    }
    // the four modifiers under the envelope are the host parameters
    ed->envMod[EM::Att]->change (4.0f);
    if (std::abs (proc.envModValue (0, TG::PhMix, EM::Att) - 4.0f) > 0.01f) bad.add ("the attack modifier did not reach its parameter: " + juce::String (proc.envModValue (0, TG::PhMix, EM::Att), 3));
    ed->envMod[EM::Att]->change (1.0f);
    // the LFO: its handle becomes a point when touched (amplitude, start time), and the LFO comes on
    ed->select (false, TG::Pitch, 1);
    {
        auto& t = proc.edit (0).tg[TG::Pitch]; auto& cv = ed->curveView;
        const juce::Point<float> a (cv.px (0.0f), cv.py (1.0f)), b (cv.px (0.5f), cv.py (0.75f));
        cv.mouseDown (ev (cv, a, a)); cv.mouseDrag (ev (cv, b, a)); cv.mouseUp (ev (cv, b, a));
        if (t.lfo.amp.pts.size() != 1 || ! t.lfo.on) bad.add ("dragging the LFO handle did not make a point and switch the LFO on");
        else if (std::abs (t.lfo.amp.pts[0].y - 0.75f) > 0.04f || t.lfo.amp.pts[0].x < 0.3f) bad.add ("LFO handle: amplitude / start time not taken (" + juce::String (t.lfo.amp.pts[0].x, 2) + ", " + juce::String (t.lfo.amp.pts[0].y, 2) + ")");
        ed->lfoKnob[1]->change (0.7f);
        if (std::abs (t.lfo.speed - 0.7f) > 1.0e-4f) bad.add ("the LFO speed control did not reach the LFO");
    }
    // what the editor holds is what the engine got
    {
        juce::AudioBuffer<float> blk (2, 256); juce::MidiBuffer mb; blk.clear(); proc.processBlock (blk, mb);
        auto d = proc.engine.getPartData (0);
        if (d == nullptr || ! d->tg[TG::Pitch].lfo.on || ! d->tg[TG::PhMix].env.on || ! d->tg[TG::F1Freq].mapOn[MAP::ModX]) bad.add ("edits made in the window did not arrive in the engine");
    }
    // switching a part off with the switch under the graph
    ed->artOn->change (0.0f);
    if (proc.edit (0).tg[TG::Pitch].lfo.on) bad.add ("the on / off switch did not switch the LFO off");
    // what the menus do, called the way the menus call it: tools for the graph on every kind of line
    {
        const int envOps[] = { 2, 3, 5, 6, 7, 8, 10, 30, 31, 32, 33, 34, 35, 4, 41, 42, 40, 1 };
        ed->select (false, TG::F1Res, 0); for (int op : envOps) ed->curveOp (op);
        if (proc.edit (0).tg[TG::F1Res].env.c.pts.size() != 4) bad.add ("reset did not bring the default envelope back");
        if (proc.edit (1).tg[TG::F1Res].env.c.pts.empty()) bad.add ("copy to part B did not copy the envelope");
        const int lineOps[] = { 2, 3, 5, 6, 7, 8, 4, 41, 42, 40, 1 };
        ed->select (false, TG::F1Res, 1); for (int op : lineOps) ed->curveOp (op);
        ed->select (false, TG::F1Res, 2 + MAP::Vel); for (int op : lineOps) ed->curveOp (op);
        if (proc.edit (0).tg[TG::F1Res].map[MAP::Vel].empty()) bad.add ("a reset mapping should be a flat line, not nothing");
        const int shapeOps[] = { 20, 2, 3, 5, 7, 8, 21, 6, 4, 1 };
        ed->select (true, SH::PluckShape, 0); for (int op : shapeOps) ed->curveOp (op);
        ed->select (true, SH::Timbre2, 0); for (int op : shapeOps) ed->curveOp (op);
    }
    // the image tools
    {
        const int imgOps[] = { 20, 21, 22, 23, 24, 25, 26, 27, 28, 14, 15, 12, 13, 16, 29, 4, 11, 5 };
        for (int op : imgOps) ed->imageOp (op);
        if (proc.edit (0).image != nullptr) bad.add ("remove image left an image behind");
        proc.edit (0).tg[TG::ImgTime] = TargetDef(); proc.commit (0);
    }
    // the built-in sounds, each played for a moment; chorus and reverb presets; controllers
    for (int i = 7; i >= 0; --i)
    {
        ed->factory (i);
        juce::AudioBuffer<float> blk (2, 512); float pk = 0; bool fin = true;
        for (int b = 0; b < 60; ++b)
        {
            juce::MidiBuffer mb; if (b == 0) { mb.addEvent (juce::MidiMessage::noteOn (1, 48, 0.8f), 0); mb.addEvent (juce::MidiMessage::noteOn (1, 55, 0.8f), 0); }
            if (b == 40) mb.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            blk.clear(); proc.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) { const float v = blk.getSample (c, k); fin = fin && std::isfinite (v); pk = juce::jmax (pk, std::abs (v)); }
        }
        if (! fin || pk < 0.02f || pk > 1.5f) bad.add ("built-in sound " + proc.presetName + ": peak " + juce::String (pk, 3));
        if (juce::SystemStats::getEnvironmentVariable ("PARTIALS_LEVELS", {}).isNotEmpty()) std::cout << "        " << proc.presetName << ": two notes peak " << juce::String (pk, 3) << std::endl;
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allSoundOff (1), 0); for (int b = 0; b < 400; ++b) { blk.clear(); proc.processBlock (blk, off); off.clear(); }
    }
    if (proc.presetName != "Default") bad.add ("the last built-in sound loaded should be Default");
    for (int i = 0; i < 9; ++i) ed->chorusPreset (i);
    for (int i = 0; i < 6; ++i) ed->reverbPreset (i);
    ed->factory (0);
    ed->modMenuResult (2, 10); if (proc.learnMod.load() != 2) bad.add ("MIDI learn did not arm");
    {
        juce::AudioBuffer<float> blk (2, 256); juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::controllerEvent (1, 21, 100), 0); blk.clear(); proc.processBlock (blk, mb);
        if (proc.modCC[2].load() != 21 || proc.learnMod.load() != -1) bad.add ("MIDI learn did not take controller 21 for Z");
        ed->modMenuResult (2, 11); if (proc.modCC[2].load() != -1) bad.add ("forgetting a controller did not work");
        ed->modMenuResult (0, 12); if (proc.modCC[0].load() != 1 || proc.modCC[1].load() != 74 || proc.modCC[2].load() != 71) bad.add ("default controllers not restored");
    }
    // keyboard hidden and shown, the display wide, narrow and off: the window keeps its proportions
    ed->showKeys = false; ed->applySize (0.75f);
    if (std::abs ((double) ed->getWidth() / ed->getHeight() - (double) PartialsEditor::BW / PartialsEditor::BHN) > 0.01) bad.add ("hiding the keyboard should shorten the window");
    ed->showKeys = true; ed->applySize (0.75f);
    for (int m = 0; m < 3; ++m) { ed->vizMode = m; ed->layoutViz(); ed->timerCallback(); }
    ed->vizMode = 0; ed->layoutViz();
    // all four tabs, both parts, a shape
    for (int t = 0; t < 4; ++t) { ed->tab = t; ed->layoutPages(); }
    ed->setPart (1); ed->select (true, SH::GlobalEQ, 0); ed->select (false, TG::LfoSpeed, 0);
    if (ed->selArt < 2) bad.add ("a mapping target must open on a mapping, it has no envelope or LFO");
    ed->setPart (0); ed->tab = 0; ed->layoutPages();
    return bad;
}
#endif
