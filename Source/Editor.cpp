#include "Editor.h"
#include "ValueEntry.h"

namespace fal
{
using juce::Colour;
using juce::Rectangle;

static ui::Theme themeFor (const ProductInfo& pi)
{
    const Colour ground (pi.ground);   // (the faltung deep builds carry the dark counterparts in the product table)
    Colour fill (pi.accent);
    if (std::abs (fill.getPerceivedBrightness() - ground.getPerceivedBrightness()) < 0.12f)
        fill = ui::inkFor (ground).withAlpha (0.55f);
    return { ground, ui::inkFor (ground), fill, ui::edgeFor (ground) };
}

static juce::RangedAudioParameter& P (Processor& p, const juce::String& id)
{
    auto* r = p.apvts.getParameter (id);
    jassert (r != nullptr);
    return *r;
}

#include "MasterView.h"

// pattern clipboard shared by every machine in the session
static Pattern<8> kitClip;
static Pattern<1> loopClip;

// =================================================================================================
//  sw6: sixteen per-step offsets drawn as slanted bars
// =================================================================================================
class StepOffsets : public juce::Component
{
public:
    StepOffsets (Processor& proc, ui::Theme t) : p (proc), theme (t)
    {
        for (int i = 0; i < 16; ++i)
        {
            auto& prm = P (p, "step" + juce::String (i + 1));
            att.push_back (std::make_unique<juce::ParameterAttachment> (prm, [this] (float) { repaint(); }, nullptr));
        }
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = b.getWidth() / 16.0f, mid = b.getCentreY();
        auto* sw = p.swing();
        const int cur = sw ? sw->shownStep.load() : -1;
        g.setColour (theme.ink.withAlpha (0.35f));
        g.fillRect (b.getX(), mid, b.getWidth(), 1.0f);
        for (int i = 0; i < 16; ++i)
        {
            const float v = p.param ("step" + juce::String (i + 1));
            const float h = v * 2.0f * (b.getHeight() * 0.45f);
            auto r = Rectangle<float> (b.getX() + i * w + 4, h < 0 ? mid : mid - h, w - 10, std::abs (h) + 1.0f);
            g.setColour (i == cur && p.playPpq.load() >= 0 ? juce::Colours::white : theme.fill);
            g.fillPath (ui::skewRect (r, 0.12f));
            g.setColour (theme.ink);
            g.setFont (ui::mono (9.0f));
            g.drawText (juce::String (i + 1).paddedLeft ('0', 2), Rectangle<float> (b.getX() + i * w, b.getBottom() - 11, w, 11), juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { last = -1; mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 15, (int) (e.position.x / ((float) getWidth() / 16.0f)));
        const float v = juce::jlimit (-0.5f, 0.5f, ((float) getHeight() * 0.5f - e.position.y) / ((float) getHeight() * 0.9f));
        if (e.mods.isAltDown() || e.getNumberOfClicks() > 1) { att[(size_t) i]->setValueAsCompleteGesture (0.0f); return; }
        if (i != last) { if (last >= 0) att[(size_t) last]->endGesture(); att[(size_t) i]->beginGesture(); last = i; }
        att[(size_t) i]->setValueAsPartOfGesture (v);
    }
    void mouseUp (const juce::MouseEvent&) override { if (last >= 0) att[(size_t) last]->endGesture(); last = -1; }
private:
    Processor& p; ui::Theme theme; int last = -1;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> att;
};

// =================================================================================================
//  modules + effects: picture on top, controls below
// =================================================================================================
class ModuleView : public View
{
public:
    explicit ModuleView (Processor& proc) : p (proc), pi (proc.pinfo), theme (themeFor (pi)), art (proc), presetBar (proc, theme)
    {
        addAndMakeVisible (art);
        addAndMakeVisible (presetBar);
        int slot = 0;
        for (auto* prm : p.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
            if (! r) continue;
            const auto id = r->getParameterID();
            if (id.startsWith ("step")) continue;
            if (dynamic_cast<juce::AudioParameterChoice*> (r)) { auto* c = top.add (new ui::ChoiceBox (*r, theme)); addAndMakeVisible (c); }
            else if (dynamic_cast<juce::AudioParameterBool*> (r)) { auto* t = top.add (new ui::ToggleBox (*r, theme)); addAndMakeVisible (t); }
            else
            {
                auto* s = sliders.add (new ui::ParamSlider (*r, theme));
                s->setLabelWidth (68);
                if (id == "freerate" || id == "lfofree") freeRate = s;
                const int mySlot = pi.kind == Kind::Module && slot < kSlots ? slot : -1;
                s->onTouch = [this, mySlot] { art.focus = mySlot; };
                ++slot;
                addAndMakeVisible (s);
            }
        }
        if (pi.id == Pid::sw6)
        {
            offsets = std::make_unique<StepOffsets> (p, theme);
            addAndMakeVisible (*offsets);
            learn = std::make_unique<ui::TextLink> ("learn groove", theme);
            learn->onClick = [this]
            {
                learning = ! learning;
                p.swingLearn (learning);
                learn->setLit (learning);
                learn->setText (learning ? ui::U ("learning… (click to set)") : "learn groove");
            };
            addAndMakeVisible (*learn);
        }
        setSize (pi.w, pi.h);
    }

    void resized() override
    {
        const int w = getWidth();
        art.setBounds (0, 0, w, 290); presetBar.setBounds (w - 236, 14, 200, 20);
        int x = 36;
        const int rowY = 300;
        for (auto* c : top)
        {
            const bool isToggle = dynamic_cast<ui::ToggleBox*> (c) != nullptr;
            const int cw = isToggle ? 128 : 172;
            c->setBounds (x, rowY, cw, 22);
            x += cw + 8;
        }
        if (learn) learn->setBounds (w - 236, rowY, 200, 22);
        const int cols = 3, colW = (w - 72 - 2 * 28) / cols;
        for (int i = 0; i < sliders.size(); ++i)
            sliders[i]->setBounds (36 + (i % cols) * (colW + 28), 332 + (i / cols) * 24, colW, 22);
        if (offsets) offsets->setBounds (36, 332 + ((sliders.size() + cols - 1) / cols) * 24 + 6, w - 72, getHeight() - 332 - ((sliders.size() + cols - 1) / cols) * 24 - 36);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme.ground); }
    void paintOverChildren (juce::Graphics& g) override
    {
        g.setColour (theme.ink.withAlpha (0.25f));
        g.fillRect (36.0f, 324.0f, (float) getWidth() - 72.0f, 1.0f);
        g.setColour (theme.ink.withAlpha (0.7f));
        g.setFont (ui::mono (10.0f));
        juce::String foot;
        if (pi.kind == Kind::Module)
            foot = pi.vtype == VType::Hat ? ui::U ("midi: any note = closed, a# notes (46, 58 …) = open · click the picture: left closed, right open")
                                          : "midi: any note" + juce::String (p.param ("keytrack") > 0.5f ? " (tracks pitch from c3)" : "") + ui::U (" · click the picture to play");
        else if (pi.id == Pid::sw6)
            foot = "latency " + juce::String (Swing::kMaxMs, 0) + ui::U (" ms (reported to the host) · drag bars: per-step offset · alt-click: reset");
        else
            foot = pi.blurb;
        g.drawText ((kDeep ? "faltung deep " : "faltung ") + juce::String (pi.code) + " " + pi.name + ui::U ("  ·  ") + foot,
                    Rectangle<float> (36.0f, (float) getHeight() - 24.0f, (float) getWidth() - 72.0f, 14.0f), juce::Justification::centredLeft, true);
    }

    void tick() override
    {
        art.tick(); if (offsets) offsets->repaint();
        if (freeRate)   // mc3: the free rate knob only counts while the rate is "free"
        {
            const bool free = pi.id == Pid::fl7 ? (int) p.param ("lfodiv") >= FilterParams::kFree : (int) p.param ("division") >= MicroParams::kFree;
            const float a = free ? 1.0f : 0.35f;
            if (std::abs (freeRate->getAlpha() - a) > 0.01f) freeRate->setAlpha (a);
        }
    }

private:
    Processor& p;
    const ProductInfo& pi;
    ui::Theme theme;
    Art art;
    ui::PresetBar presetBar;
    juce::OwnedArray<juce::Component> top;
    juce::OwnedArray<ui::ParamSlider> sliders;
    ui::ParamSlider* freeRate = nullptr;
    std::unique_ptr<StepOffsets> offsets;
    std::unique_ptr<ui::TextLink> learn;
    bool learning = false;
};

// =================================================================================================
//  kt1 kit
// =================================================================================================
/** the eight voices as folded strips (the selector) */
class Strips : public juce::Component
{
public:
    explicit Strips (Processor& proc) : p (proc) { for (int i = 0; i < 8; ++i) seen[(size_t) i] = p.hitCount[(size_t) i].load(); }
    std::function<void (int)> onSelect;
    int selected = 2;

    void paint (juce::Graphics& g) override
    {
        // geometry follows the mockup: two figures of folded planes
        static const float poly[8][3][8] = {
            { { 20,150, 110,150, 150,100, 60,100 }, { 60,100, 150,100, 150,40, 60,40 }, {} },
            { { 150,100, 240,100, 240,40, 150,40 }, { 150,40, 240,40, 280,0, 190,0 }, {} },
            { { 200,140, 300,140, 300,90, 200,90 }, { 300,140, 340,110, 340,60, 300,90 }, { 200,90, 300,90, 340,60, 240,60 } },
            { { 120,200, 220,200, 220,140, 120,140 }, { 120,200, 220,200, 260,170, 160,170 }, {} },
            { { 300,250, 400,250, 440,210, 340,210 }, {}, {} },
            { { 400,250, 480,250, 480,190, 400,190 }, { 400,190, 480,190, 520,150, 440,150 }, {} },
            { { 440,150, 520,150, 520,90, 440,90 }, { 440,90, 520,90, 540,70, 460,70 }, {} },
            { { 320,310, 440,310, 480,270, 360,270 }, { 320,310, 360,270, 360,250, 320,290 }, {} },
        };
        static const float lab[8][3] = { { 58,135,-38 }, { 170,78,0 }, { 226,122,0 }, { 146,178,0 }, { 350,236,0 }, { 420,228,0 }, { 458,126,0 }, { 380,296,0 } };
        const float sx = (float) getWidth() / 560.0f, sy = (float) getHeight() / 330.0f, s = std::min (sx, sy);
        const auto tf = juce::AffineTransform::scale (s).translated (14.0f, 16.0f);
        hit.clear();
        for (int v = 0; v < 8; ++v)
        {
            const Colour base (kitVoice (v).colour);
            juce::Path all;
            for (int k = 0; k < 3; ++k)
            {
                if (poly[v][k][2] == 0 && poly[v][k][0] == 0) continue;
                juce::Path q;
                q.startNewSubPath (poly[v][k][0], poly[v][k][1]);
                for (int j = 1; j < 4; ++j) q.lineTo (poly[v][k][j * 2], poly[v][k][j * 2 + 1]);
                q.closeSubPath();
                q.applyTransform (tf);
                const Colour c = (k == 0 ? base : k == 1 ? base.darker (0.15f) : base.brighter (0.2f))
                                     .interpolatedWith (juce::Colours::white, flash[(size_t) v] * 0.55f);
                g.setColour (c);
                g.fillPath (q);
                all.addPath (q);
            }
            if (v == selected) { g.setColour (juce::Colours::white); g.strokePath (all, juce::PathStrokeType (2.0f)); }
            hit.push_back (all);
            juce::Graphics::ScopedSaveState ss (g);
            const auto pt = juce::Point<float> (lab[v][0], lab[v][1]).transformedBy (tf);
            g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (lab[v][2]), pt.x, pt.y));
            g.setColour (ui::kDark ? ui::inkFor (base) : v == 3 ? Colour (0xfff4f6ff) : Colour (0xff1e2a20));
            g.setFont (ui::sans (15.0f * s));
            g.drawText (kitVoice (v).id, Rectangle<float> (pt.x, pt.y - 15 * s, 60 * s, 18 * s), juce::Justification::centredLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int v = 7; v >= 0; --v)
            if (v < (int) hit.size() && hit[(size_t) v].contains (e.position))
            {
                selected = v; repaint();
                p.uiTrigger (v, 0.85f);
                if (onSelect) onSelect (v);
                return;
            }
    }
    void tick()
    {
        bool any = false;
        for (int v = 0; v < 8; ++v)
        {
            const int h = p.hitCount[(size_t) v].load();
            if (h != seen[(size_t) v]) { seen[(size_t) v] = h; flash[(size_t) v] = 1.0f; any = true; }
            else if (flash[(size_t) v] > 0.01f) { flash[(size_t) v] *= 0.8f; any = true; }
        }
        if (any) repaint();
    }
private:
    Processor& p;
    std::vector<juce::Path> hit;
    std::array<int, 8> seen {};
    std::array<float, 8> flash {};
};

/** shared step editing for kt1 (8 rows) and kt2 (1 row of slices) */
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepGrid (Processor& proc, ui::Theme t, int rows) : p (proc), theme (t), nRows (rows)
    {
        setTooltip (rows > 1 ? ui::U ("click: step on/off · drag up/down: velocity · drag across: paint · shift-drag: microtiming · alt-click: probability · right-click: ratchet · alt-drag / alt-scroll: ratchet curve · click a step number: length")
                             : ui::U ("click: slice on/off · drag up/down: choose slice · cmd-drag / scroll: pitch · shift-drag / shift-scroll: microtiming · alt-click: reverse · right-click: ratchet · alt-drag / alt-scroll: ratchet curve · click a step number: length"));
    }
    int selected = 2;
    std::function<void (int)> onSelect;

    Step& stepRef (int row, int s)
    {
        if (nRows > 1) return p.kitBank.p[(size_t) p.currentPattern()].t[(size_t) row][(size_t) s];
        return p.loopBank.p[(size_t) p.currentPattern()].t[0][(size_t) s];
    }
    juce::String lenId (int row) const { return nRows > 1 ? kitPrefix (row) + "len" : juce::String ("len"); }
    int trackLen (int row) const { return juce::jlimit (1, 16, (int) std::round (p.param (lenId (row)))); }
    /** kt2: the clock curve, which spaces the cells like the steps' time */
    SeqCurve curve() const { return nRows > 1 ? SeqCurve {} : SeqCurve { (int) p.param ("curve"), p.param ("curveamt") }; }
    int stepPitch (int s) const { return nRows > 1 ? 0 : juce::roundToInt (p.param ("pitch" + juce::String (s + 1))); }
    Colour rowColour (int row) const { return nRows > 1 ? Colour (kitVoice (row).colour) : ui::accent (Colour (0xff2f7fd1)); }

    float labelW() const { return nRows > 1 ? 40.0f : 0.0f; }
    float rightW() const { return 52.0f; }
    float rowH() const { return nRows > 1 ? 19.0f : (float) getHeight() - 16.0f; }
    float cellW() const { return ((float) getWidth() - labelW() - rightW()) / 16.0f; }
    /** the left edge of step s (s = 16: the right end); inside the loop the clock curve stretches the cells */
    float cellX (int row, int s) const
    {
        const int len = trackLen (row);
        const SeqCurve c = curve();
        if (s >= len || ! c.active (len)) return labelW() + (float) s * cellW();
        float start, dur; curvedStep (c, s, len, start, dur);
        return labelW() + start * cellW();
    }
    Rectangle<float> cell (int row, int s) const
    {
        const float x0 = cellX (row, s), x1 = cellX (row, s + 1);
        return { x0 + 2.0f, 14.0f + (float) row * (rowH() + 2.0f), std::max (6.0f, x1 - x0 - 4.0f), rowH() - 4.0f };
    }
    /** the ratchet's hits inside a cell, as x positions */
    static void ratchetX (const Step& st, Rectangle<float> c, std::vector<float>& xs)
    {
        xs.clear();
        float at[4]; ratchetTimes (st.ratchet, st.curve, at);
        for (int k = 1; k < std::min (4, st.ratchet); ++k) xs.push_back (c.getX() + c.getWidth() * at[k]);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (ui::mono (9.0f));
        g.setColour (theme.ink.withAlpha (0.75f));
        for (int s = 0; s < 16; ++s)
            g.drawText (juce::String (s + 1).paddedLeft ('0', 2), Rectangle<float> (cellX (nRows > 1 ? selected : 0, s) + 2.0f, 0, cellW(), 12), juce::Justification::centredLeft);
        g.drawText (nRows > 1 ? ui::U ("len · prob") : "len", Rectangle<float> ((float) getWidth() - rightW(), 0, rightW(), 12), juce::Justification::centredRight);
        const float ppq = p.playPpq.load();
        std::array<std::array<Step, 16>, 8> snap;
        {
            const juce::SpinLock::ScopedLockType sl (p.bankLock);   // copy, then paint without the lock
            for (int r = 0; r < nRows; ++r) for (int s = 0; s < 16; ++s) snap[(size_t) r][(size_t) s] = stepRef (r, s);
        }
        std::vector<float> rx;
        for (int r = 0; r < nRows; ++r)
        {
            const int len = trackLen (r);
            const Colour col = rowColour (r);
            if (nRows > 1)
            {
                g.setColour (theme.ink);
                g.setFont (ui::mono (10.0f, r == selected));
                g.drawText (kitVoice (r).id, Rectangle<float> (0, cell (r, 0).getY() - 2, labelW(), rowH()), juce::Justification::centredLeft);
            }
            const int playing = ppq >= 0 ? stepAtPpq (ppq, len, curve()) : -1;
            // the end of the loop: a thin mark after the last step
            {
                const auto c0 = cell (r, 0);
                g.setColour (theme.ink.withAlpha (0.55f));
                g.fillRect (cellX (r, len) - 1.0f, c0.getY(), 1.0f, c0.getHeight());
            }
            for (int s = 0; s < 16; ++s)
            {
                auto c = cell (r, s);
                const Step& st = snap[(size_t) r][(size_t) s];
                const float alphaLen = s < len ? 1.0f : 0.25f;
                const int pitch = stepPitch (s);
                if (st.on)
                {
                    c = c.translated (st.micro * cellW() * 0.35f, 0.0f);
                    const float a = (0.35f + 0.65f * st.vel) * alphaLen;
                    auto shape = ui::skewRect (c, nRows > 1 ? 0.55f : 0.2f);
                    if (st.prob < 0.999f)
                    {
                        g.setColour (col.withAlpha (a));
                        g.strokePath (shape, juce::PathStrokeType (1.2f));
                        juce::Graphics::ScopedSaveState ss (g);
                        g.reduceClipRegion (c.withTrimmedTop (c.getHeight() * (1.0f - st.prob)).expanded (8, 0).toNearestInt());
                        g.fillPath (shape);
                    }
                    else { g.setColour (col.withAlpha (a)); g.fillPath (shape); }
                    if (r == selected && nRows > 1) { g.setColour (juce::Colours::white.withAlpha (alphaLen)); g.strokePath (shape, juce::PathStrokeType (1.0f)); }
                    if (st.ratchet > 1)
                    {
                        g.setColour (theme.ink.withAlpha (0.8f));
                        ratchetX (st, c, rx);
                        for (float x : rx) g.fillRect (x + c.getHeight() * 0.25f, c.getY() + 2, 1.0f, c.getHeight() - 4);
                    }
                    if (nRows == 1)
                    {
                        g.setColour (ui::inkFor (col));
                        g.setFont (ui::mono (13.0f, true));
                        g.drawText (juce::String::toHexString (st.slice) + (st.rev ? "<" : ""), pitch != 0 ? c.withTrimmedRight (c.getWidth() * 0.45f) : c, juce::Justification::centred);
                        g.setFont (ui::mono (9.0f, true));
                        if (pitch != 0) g.drawText ((pitch > 0 ? "+" : "") + juce::String (pitch), c.withTrimmedLeft (c.getWidth() * 0.5f).reduced (4.0f, 2.0f), juce::Justification::topRight);
                        g.setFont (ui::mono (8.0f));
                        if (st.curve != 0) g.drawText ((st.curve > 0 ? "in " : "out ") + juce::String (std::abs (st.curve)), c.reduced (6.0f, 2.0f), juce::Justification::bottomLeft);
                    }
                }
                else
                {
                    g.setColour (theme.ink.withAlpha (0.22f * alphaLen));
                    g.strokePath (ui::skewRect (c, nRows > 1 ? 0.55f : 0.2f), juce::PathStrokeType (1.0f));
                }
                if (s == playing) { g.setColour (juce::Colours::white); g.fillRect (c.getX(), c.getBottom() + 1, c.getWidth(), 2.0f); }
            }
            g.setColour (theme.ink);
            g.setFont (ui::mono (10.0f));
            const juce::String right = nRows > 1 ? juce::String (len) + ui::U (" · ") + juce::String (juce::roundToInt (p.param (kitPrefix (r) + "prob") * 100)) + "%"
                                                 : juce::String (len);
            g.drawText (right, Rectangle<float> ((float) getWidth() - rightW(), cell (r, 0).getY() - 2, rightW(), rowH()), juce::Justification::centredRight);
        }
    }

    bool find (juce::Point<float> pt, int& row, int& s) const
    {
        for (int r = 0; r < nRows; ++r)
            for (int k = 0; k < 16; ++k)
                if (cell (r, k).expanded (2.0f, 1.0f).contains (pt)) { row = r; s = k; return true; }
        return false;
    }
    /** the step number row above the grid: which step is under x (-1: none) */
    int headerStep (juce::Point<float> pt) const
    {
        if (pt.y >= 13.0f || pt.x < labelW() || pt.x >= (float) getWidth() - rightW()) return -1;
        const int row = nRows > 1 ? selected : 0;
        for (int s = 0; s < 16; ++s) if (pt.x < cellX (row, s + 1)) return s;
        return 15;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        mode = None;
        int r, s;
        if (nRows > 1 && e.position.x < labelW() && e.position.y >= 13.0f)
        {
            const int row = juce::jlimit (0, nRows - 1, (int) ((e.position.y - 14.0f) / (rowH() + 2.0f)));
            selected = row; p.uiTrigger (row, 0.85f);
            if (onSelect) onSelect (row);
            repaint(); return;
        }
        if (const int hs = headerStep (e.position); hs >= 0)
        {
            // click a step number: the loop ends there (the track's length)
            p.setParam (lenId (nRows > 1 ? selected : 0), (float) (hs + 1));
            repaint(); return;
        }
        if (! find (e.position, r, s)) return;
        const juce::SpinLock::ScopedLockType sl (p.bankLock);
        Step& st = stepRef (r, s);
        downRow = r; downStep = s; startVel = st.vel; startMicro = st.micro; startSlice = st.slice; startCurve = st.curve; startPitch = stepPitch (s);
        if (e.mods.isPopupMenu()) { if (! st.on) st.on = true; st.ratchet = st.ratchet % 4 + 1; repaint(); return; }
        if (e.mods.isAltDown()) { if (! st.on) st.on = true; mode = AltPending; repaint(); return; }   // click: reverse / probability; drag: the ratchet's curve
        if (e.mods.isShiftDown()) { if (! st.on) st.on = true; mode = Micro; repaint(); return; }
        if (e.mods.isCommandDown() && nRows == 1) { if (! st.on) st.on = true; mode = Pitch; repaint(); return; }
        if (! st.on)
        {
            st.on = true; st.vel = 0.8f; st.prob = 1.0f; st.ratchet = 1; st.micro = 0; st.curve = 0;
            if (nRows == 1) { st.slice = s % 16; startSlice = st.slice; }
            paintOn = true; mode = Pending; turnedOn = true;
        }
        else { paintOn = false; mode = Pending; turnedOn = false; }
        if (nRows > 1 && r != selected) { selected = r; if (onSelect) onSelect (r); }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == None) return;
        const int dx = e.getDistanceFromDragStartX(), dy = e.getDistanceFromDragStartY();
        if (mode == Pitch)
        {
            p.setParam ("pitch" + juce::String (downStep + 1), (float) juce::jlimit (-24, 24, startPitch - dy / 6));
            repaint(); return;
        }
        const juce::SpinLock::ScopedLockType sl (p.bankLock);
        Step& st = stepRef (downRow, downStep);
        if (mode == Pending)
        {
            if (std::abs (dy) > 4 && std::abs (dy) > std::abs (dx)) { mode = Vertical; st.on = true; }
            else if (std::abs (dx) > 6) mode = Paint;
        }
        if (mode == AltPending && std::abs (dy) > 4) mode = Curve;
        if (mode == Vertical)
        {
            if (nRows > 1) st.vel = juce::jlimit (0.05f, 1.0f, startVel - dy / 80.0f);
            else st.slice = juce::jlimit (0, 31, startSlice - dy / 8);
        }
        else if (mode == Micro) st.micro = juce::jlimit (-1.0f, 1.0f, startMicro + dx / (cellW() * 1.4f));
        else if (mode == Curve) st.curve = juce::jlimit (-7, 7, startCurve - dy / 8);
        else if (mode == Paint)
        {
            int r, s;
            if (find ({ e.position.x, cell (downRow, 0).getCentreY() }, r, s))
            {
                Step& t = stepRef (downRow, s);
                if (t.on != paintOn) { t.on = paintOn; if (paintOn) { t.vel = 0.8f; t.prob = 1; t.ratchet = 1; t.micro = 0; t.curve = 0; if (nRows == 1) t.slice = s % 16; } }
            }
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == Pending && ! turnedOn)
        {
            const juce::SpinLock::ScopedLockType sl (p.bankLock);
            stepRef (downRow, downStep).on = false;
        }
        if (mode == AltPending)
        {
            const juce::SpinLock::ScopedLockType sl (p.bankLock);
            Step& st = stepRef (downRow, downStep);
            if (nRows == 1) st.rev = ! st.rev;
            else st.prob = st.prob > 0.99f ? 0.75f : st.prob > 0.7f ? 0.5f : st.prob > 0.45f ? 0.25f : 1.0f;
        }
        mode = None; repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int r, s;
        if (! find (e.position, r, s)) return;
        const int dir = (w.deltaY + w.deltaX) > 0 ? 1 : (w.deltaY + w.deltaX) < 0 ? -1 : 0;
        if (dir == 0) return;
        if (e.mods.isShiftDown() || e.mods.isAltDown())
        {
            const juce::SpinLock::ScopedLockType sl (p.bankLock);
            Step& st = stepRef (r, s);
            if (e.mods.isAltDown()) st.curve = juce::jlimit (-7, 7, st.curve + dir);          // the ratchet's curve
            else st.micro = juce::jlimit (-1.0f, 1.0f, st.micro + 0.1f * (float) dir);        // microtiming
        }
        else if (nRows == 1) p.setParam ("pitch" + juce::String (s + 1), (float) juce::jlimit (-24, 24, stepPitch (s) + dir));   // pitch
        repaint();
    }

private:
    Processor& p; ui::Theme theme; int nRows;
    enum Mode { None, Pending, Vertical, Paint, Micro, AltPending, Curve, Pitch } mode = None;
    int downRow = 0, downStep = 0, startSlice = 0, startCurve = 0, startPitch = 0;
    float startVel = 0, startMicro = 0;
    bool paintOn = true, turnedOn = false;
};

/** pattern tabs + copy / paste / clear */
class PatternBar : public juce::Component
{
public:
    PatternBar (Processor& proc, ui::Theme t) : p (proc), tabs (P (proc, "pattern"), t, " "),
        copy ("copy", t), paste ("paste", t), clear ("clear", t)
    {
        tabs.asTabs = true;
        for (auto* c : std::initializer_list<juce::Component*> { &tabs, &copy, &paste, &clear }) addAndMakeVisible (c);
        copy.onClick = [this]
        {
            const juce::SpinLock::ScopedLockType sl (p.bankLock);
            if (p.pid == Pid::kt1) kitClip = p.kitBank.p[(size_t) p.currentPattern()]; else loopClip = p.loopBank.p[(size_t) p.currentPattern()];
        };
        paste.onClick = [this]
        {
            { const juce::SpinLock::ScopedLockType sl (p.bankLock);
              if (p.pid == Pid::kt1) p.kitBank.p[(size_t) p.currentPattern()] = kitClip; else p.loopBank.p[(size_t) p.currentPattern()] = loopClip; }
            if (onChange) onChange();
        };
        clear.onClick = [this]
        {
            { const juce::SpinLock::ScopedLockType sl (p.bankLock);
              if (p.pid == Pid::kt1) p.kitBank.p[(size_t) p.currentPattern()] = Pattern<8>(); else p.loopBank.p[(size_t) p.currentPattern()] = Pattern<1>(); }
            if (onChange) onChange();
        };
    }
    std::function<void()> onChange;
    void resized() override
    {
        auto b = getLocalBounds();
        clear.setBounds (b.removeFromRight (44)); paste.setBounds (b.removeFromRight (48)); copy.setBounds (b.removeFromRight (44));
        tabs.setBounds (b.withTrimmedRight (8));
    }
private:
    Processor& p;
    ui::ChoiceBox tabs;
    ui::TextLink copy, paste, clear;
};

class KitView : public View
{
public:
    explicit KitView (Processor& proc)
        : p (proc), pi (proc.pinfo), theme (themeFor (pi)), strips (proc), grid (proc, theme, 8),
          presetBar (proc, theme), patterns (proc, theme),
          run (P (proc, "run"), theme, "run"), bpm (P (proc, "bpm"), theme, "bpm"),
          swing (P (proc, "swing"), theme, "swing"), micro (P (proc, "micro"), theme, "micro"),
          master (P (proc, "master"), theme, "master"), outs (P (proc, "outs"), theme, "separate outs")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &strips, &grid, &presetBar, &patterns, &run, &bpm, &swing, &micro, &master, &outs })
            addAndMakeVisible (c);
        for (auto* s : { &bpm, &swing, &micro, &master }) s->setLabelWidth (44);
        strips.onSelect = [this] (int v) { grid.selected = v; grid.repaint(); buildPanel (v); };
        grid.onSelect = [this] (int v) { strips.selected = v; strips.repaint(); buildPanel (v); };
        patterns.onChange = [this] { grid.repaint(); };
        buildPanel (2);
        setSize (pi.w, pi.h);
    }

    void buildPanel (int v)
    {
        voice = v;
        panel.clear();
        const auto pre = kitPrefix (v);
        const auto& vi = vtypeInfo (kitVoice (v).type);
        ui::Theme t = theme; t.fill = Colour (kitVoice (v).colour);
        if (t.fill.getPerceivedBrightness() > 0.8f) t.fill = t.fill.darker (0.2f);
        panel.add (new ui::ChoiceBox (P (p, pre + "model"), t, "model"));
        for (int s = 0; s < kSlots; ++s) panel.add (new ui::ParamSlider (P (p, pre + vi.slot[(size_t) s]), t, vi.slot[(size_t) s]));
        panel.add (new ui::ParamSlider (P (p, pre + "level"), t, "level"));
        panel.add (new ui::ParamSlider (P (p, pre + "pan"), t, "pan"));
        panel.add (new ui::ParamSlider (P (p, pre + "vel"), t, "velocity"));
        panel.add (new ui::ParamSlider (P (p, pre + "len"), t, "length"));
        panel.add (new ui::ParamSlider (P (p, pre + "prob"), t, "prob"));
        panel.add (new ui::ParamSlider (P (p, pre + "shift"), t, "shift"));
        panel.add (new ui::ToggleBox (P (p, pre + "mute"), t, "mute"));
        for (auto* c : panel)
        {
            if (auto* s = dynamic_cast<ui::ParamSlider*> (c)) s->setLabelWidth (60);
            addAndMakeVisible (c);
        }
        resized(); repaint();
    }

    void resized() override
    {
        const int w = getWidth();
        strips.setBounds (0, 20, 590, 360);
        presetBar.setBounds (604, 30, 190, 20);
        master.setBounds (604, 58, 190, 22);
        outs.setBounds (604, 84, 190, 20);
        const int px = 604, pw = w - 36 - px;
        int y = 150;
        for (int i = 0; i < panel.size(); ++i)
        {
            auto* c = panel[i];
            if (i >= 12) // track controls: two columns
            {
                const int k = i - 12;
                c->setBounds (px + (k % 2) * (pw / 2 + 6), y + (k / 2) * 19, pw / 2 - 6, 18);
            }
            else { c->setBounds (px, y, pw, i == 0 ? 20 : 18); y += i == 0 ? 24 : 19; }
        }
        grid.setBounds (36, 424, w - 72, 14 + 8 * 21);
        const int gy = getHeight() - 42;
        run.setBounds (36, gy, 60, 22);
        bpm.setBounds (100, gy, 156, 22);
        swing.setBounds (266, gy, 140, 22);
        micro.setBounds (416, gy, 156, 22);
        patterns.setBounds (586, gy, w - 36 - 586, 22);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
        // the header box, as on the sleeve
        const Rectangle<float> box ((float) getWidth() - 186.0f, 30.0f, 150.0f, 100.0f);
        g.setColour (Colour (pi.accent));
        g.fillRect (box);
        g.setColour (ui::kDark ? ui::inkFor (Colour (pi.accent)) : theme.ink);
        g.setFont (ui::sans (17.0f, true)); g.drawText ("kt1", box.reduced (16, 12).removeFromTop (22), juce::Justification::topLeft);
        g.setFont (ui::sans (15.0f)); g.drawText ("kit", box.reduced (16, 12).withTrimmedTop (22).removeFromTop (20), juce::Justification::topLeft);
        g.setFont (ui::mono (10.0f)); g.drawText (kDeep ? "faltung deep" : "faltung", box.reduced (16, 10), juce::Justification::bottomLeft);
        // panel title
        const auto& kv = kitVoice (voice);
        g.setColour (theme.ink);
        g.setFont (ui::mono (11.0f, true));
        g.drawText (juce::String (kv.id) + ui::U (" · ") + kitVoiceName (voice), Rectangle<float> (604.0f, 128.0f, 200.0f, 16.0f), juce::Justification::centredLeft);
        g.setFont (ui::mono (11.0f));
        g.drawText ("midi " + juce::MidiMessage::getMidiNoteName (kv.defaultNote, true, true, 3) + ui::U (" · out ") + juce::String (voice + 2),
                    Rectangle<float> (604.0f, 128.0f, (float) getWidth() - 640.0f, 16.0f), juce::Justification::centredRight);
        g.fillRect (604.0f, 145.0f, (float) getWidth() - 640.0f, 1.0f);
        g.setFont (ui::mono (10.0f));
        g.setColour (theme.ink.withAlpha (0.75f));
        // (two lines in the left column: one long line ran under the voice panel's last row)
        g.drawText (ui::U ("click a strip or row name to select + play · midi c1–g1 plays ") + kitVoice (0).id + ui::U ("–") + kitVoice (7).id,
                    Rectangle<float> (36.0f, 388.0f, 556.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText (ui::U ("multi-out: enable outputs 2–9 in your host (au / vst2: also switch on separate outs)"),
                    Rectangle<float> (36.0f, 402.0f, 556.0f, 14.0f), juce::Justification::centredLeft);
    }

    void tick() override { strips.tick(); grid.repaint(); }

private:
    Processor& p;
    const ProductInfo& pi;
    ui::Theme theme;
    Strips strips;
    StepGrid grid;
    ui::PresetBar presetBar;
    PatternBar patterns;
    ui::ToggleBox run, outs;
    ui::ParamSlider bpm, swing, micro, master;
    juce::OwnedArray<juce::Component> panel;
    int voice = 2;
};

// =================================================================================================
//  kt2 loop
// =================================================================================================
class Wave : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit Wave (Processor& proc) : p (proc) { for (int i = 0; i < 16; ++i) seen[(size_t) i] = p.hitCount[(size_t) i].load(); }
    bool isInterestedInFileDrag (const juce::StringArray& f) override
    {
        for (auto& s : f) if (s.endsWithIgnoreCase (".wav") || s.endsWithIgnoreCase (".aif") || s.endsWithIgnoreCase (".aiff") || s.endsWithIgnoreCase (".flac") || s.endsWithIgnoreCase (".mp3") || s.endsWithIgnoreCase (".ogg")) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& f, int, int) override { if (! f.isEmpty()) { p.loadSample (juce::File (f[0])); cached = nullptr; repaint(); } dropping = false; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const Colour ink = ui::paperInk();
        g.fillAll (Colour (p.pinfo.ground));
        auto s = p.getSample();
        if (! s) return;
        if (s != cached) { cached = s; buildPeaks (*s); }
        const float y0 = b.getY() + 70.0f, h = b.getHeight() - 130.0f, mid = y0 + h * 0.5f;
        static const juce::uint32 cols[4] = { 0xff2f7fd1, 0xffe06a2e, 0xffe9a0a8, 0xff8f6bb8 };
        const int n = (int) peaks.size();
        // the waveform as four coloured lines, offset like the oval sleeve
        for (int k = 0; k < 4; ++k)
        {
            juce::Path line;
            for (int i = 0; i < n; ++i)
            {
                const float x = b.getX() + 36.0f + (b.getWidth() - 72.0f) * (float) i / (float) std::max (1, n - 1);
                const float y = mid + (k - 1.5f) * 9.0f - peaks[(size_t) i] * h * 0.45f * (k % 2 ? -1.0f : 1.0f) * (1.0f - 0.18f * k);
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            g.setColour (ui::accent (Colour (cols[k])).withAlpha (k == 0 ? 0.95f : 0.7f));
            g.strokePath (line, juce::PathStrokeType (k == 0 ? 2.5f : 1.3f));
        }
        // slices: black dots on the base line, ticks down, numbers
        const auto sl = p.currentSlices (*s);
        const float total = (float) s->buf.getNumSamples();
        slicesX.clear();
        for (size_t i = 0; i < sl.size(); ++i)
        {
            const float x0 = b.getX() + 36.0f + (b.getWidth() - 72.0f) * (float) sl[i].first / total;
            const float x1 = b.getX() + 36.0f + (b.getWidth() - 72.0f) * (float) sl[i].second / total;
            slicesX.push_back ({ x0, x1 });
            const float fl = flash[i & 15];
            if (fl > 0.02f)
            {
                g.setColour (Colour (0xff8fbf6a).withAlpha (0.55f * fl));
                g.fillPath (ui::skewRect ({ x0, y0, x1 - x0, h }, 0.08f));
            }
            g.setColour (ink);
            g.fillEllipse (x0 - 3.5f, b.getBottom() - 50.0f, 7.0f, 7.0f);
            g.drawLine (x0, b.getBottom() - 46.0f, x0, b.getBottom() - 24.0f - (i % 3) * 6.0f, 1.0f);
            g.setFont (ui::mono (9.0f));
            g.drawText (juce::String::toHexString ((int) i), Rectangle<float> (x0 + 3, b.getBottom() - 40.0f, 20, 10), juce::Justification::centredLeft);
        }
        g.setColour (ink);
        g.fillRect (b.getX() + 36.0f, b.getBottom() - 47.0f, b.getWidth() - 72.0f, 1.0f);
        g.setFont (ui::sans (16.0f));
        g.fillRect (36.0f, 26.0f, 1.0f, 30.0f);
        g.drawText ("kt2loop", Rectangle<float> (44.0f, 24.0f, 200.0f, 20.0f), juce::Justification::centredLeft);
        g.setFont (ui::mono (10.0f));
        g.drawText (s->name + ui::U ("  ·  ") + juce::String (s->buf.getNumSamples() / s->rate, 2) + ui::U (" s  ·  ") + juce::String ((int) s->onsets.size()) + ui::U (" onsets  ·  drop a file here"),
                    Rectangle<float> (44.0f, 42.0f, 600.0f, 14.0f), juce::Justification::centredLeft);
        if (dropping) { g.setColour (ui::accent (Colour (0xff2f7fd1))); g.drawRect (b.reduced (6.0f), 2.0f); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (size_t i = 0; i < slicesX.size(); ++i)
            if (e.position.x >= slicesX[i].first && e.position.x < slicesX[i].second && e.position.y > 60) { p.uiTrigger ((int) i, 0.9f); return; }
    }
    void tick()
    {
        for (int i = 0; i < 16; ++i)
        {
            const int h = p.hitCount[(size_t) i].load();
            if (h != seen[(size_t) i]) { seen[(size_t) i] = h; flash[(size_t) i] = 1.0f; }
            else flash[(size_t) i] *= 0.85f;
        }
        repaint();
    }
private:
    Processor& p;
    std::shared_ptr<const LoopSample> cached;
    std::vector<float> peaks;
    std::vector<std::pair<float, float>> slicesX;
    std::array<int, 16> seen {}; std::array<float, 16> flash {};
    bool dropping = false;
    void buildPeaks (const LoopSample& s)
    {
        const int n = 600, len = s.buf.getNumSamples();
        peaks.assign ((size_t) n, 0.0f);
        float mx = 1.0e-6f;
        for (int i = 0; i < n; ++i)
        {
            const int a = (int) ((juce::int64) len * i / n), b = (int) ((juce::int64) len * (i + 1) / n);
            float pk = 0;
            for (int c = 0; c < s.buf.getNumChannels(); ++c) pk = std::max (pk, s.buf.getMagnitude (c, a, std::max (1, b - a)));
            peaks[(size_t) i] = pk; mx = std::max (mx, pk);
        }
        for (auto& v : peaks) v /= mx;
    }
};

class LoopView : public View
{
public:
    explicit LoopView (Processor& proc) : p (proc), pi (proc.pinfo), theme (themeFor (pi)), wave (proc),
        steps (proc, theme, 1), presetBar (proc, theme), patterns (proc, theme), load (ui::U ("load sample…"), theme)
    {
        theme.fill = ui::accent (Colour (0xff2f7fd1));
        addAndMakeVisible (wave); addAndMakeVisible (steps); addAndMakeVisible (presetBar); addAndMakeVisible (patterns); addAndMakeVisible (load);
        load.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("load a loop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) p.loadSample (fc.getResult()); });
        };
        patterns.onChange = [this] { steps.repaint(); };
        for (auto id : { "slices", "slicemode" }) { auto* c = top.add (new ui::ChoiceBox (P (p, id), theme)); addAndMakeVisible (c); }
        for (auto id : { "fit", "run" }) { auto* c = top.add (new ui::ToggleBox (P (p, id), theme)); addAndMakeVisible (c); }
        for (auto id : { "pitch", "gate", "attack", "release", "tone", "revprob", "start", "spread", "vel", "level", "loopbeats", "len",
                         "prob", "shift", "curve", "curveamt", "swing", "micro", "bpm", "master" })
        {
            if (juce::String (id) == "curve")
            {
                auto* c = new ui::ChoiceBox (P (p, id), theme); c->labelW = 78; sliders.add (c);
            }
            else { auto* s = new ui::ParamSlider (P (p, id), theme); s->setLabelWidth (78); sliders.add (s); }
            addAndMakeVisible (sliders.getLast());
        }
        setSize (pi.w, pi.h);
    }
    void resized() override
    {
        const int w = getWidth();
        wave.setBounds (0, 0, w, 300);
        presetBar.setBounds (w - 236, 24, 200, 20);
        load.setBounds (w - 236, 48, 200, 20);
        steps.setBounds (36, 310, w - 72, 56);
        int x = 36;
        for (auto* c : top) { const int cw = dynamic_cast<ui::ToggleBox*> (c) ? 150 : 150; c->setBounds (x, 380, cw, 22); x += cw + 6; }
        patterns.setBounds (x + 10, 380, w - 36 - x - 10, 22);
        const int cols = 3, colW = (w - 72 - 56) / cols;
        for (int i = 0; i < sliders.size(); ++i)
            sliders[i]->setBounds (36 + (i % cols) * (colW + 28), 414 + (i / cols) * 24, colW, 22);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
        g.setColour (theme.ink.withAlpha (0.7f));
        g.setFont (ui::mono (10.0f));
        g.drawText (ui::U ("midi c1 and up plays slices 0, 1, 2 … · click a slice to hear it · steps: cmd-drag or scroll = pitch · alt-drag = ratchet curve · click a step number = length"),
                    Rectangle<float> (36.0f, (float) getHeight() - 22.0f, (float) getWidth() - 72.0f, 14.0f), juce::Justification::centredLeft);
    }
    void tick() override { wave.tick(); steps.repaint(); }
private:
    Processor& p;
    const ProductInfo& pi;
    ui::Theme theme;
    Wave wave;
    StepGrid steps;
    ui::PresetBar presetBar;
    PatternBar patterns;
    ui::TextLink load;
    juce::OwnedArray<juce::Component> top;
    juce::OwnedArray<juce::Component> sliders;
    std::unique_ptr<juce::FileChooser> chooser;
};

// =================================================================================================
//  wb1 wub
// =================================================================================================
class Keys : public juce::Component
{
public:
    Keys (Processor& proc, ui::Theme t) : p (proc), theme (t) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    int base = 24;   // C0
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (theme.ink); g.setFont (ui::mono (11.0f));
        g.drawText ("<", b.withWidth (20.0f), juce::Justification::centred);
        g.drawText (">", b.withLeft (b.getRight() - 20.0f), juce::Justification::centred);
        const float w = (b.getWidth() - 48.0f) / 36.0f;
        for (int k = 0; k < 36; ++k)
        {
            const int nn = base + k;
            const bool black = juce::MidiMessage::isMidiNoteBlack (nn);
            auto r = juce::Rectangle<float> (24.0f + k * w, black ? 2.0f : 8.0f, w - 3.0f, b.getHeight() - (black ? 14.0f : 10.0f));
            auto shape = ui::skewRect (r, 0.25f);
            g.setColour (nn == down ? juce::Colours::white : black ? theme.ink.withAlpha (0.55f) : theme.fill.withAlpha (0.8f));
            g.fillPath (shape);
            if (nn % 12 == 0)
            {
                g.setColour (ui::inkFor (theme.fill)); g.setFont (ui::mono (9.0f));
                g.drawText ("c" + juce::String (nn / 12 - 2), r.withTrimmedTop (r.getHeight() - 12.0f), juce::Justification::centred);
            }
        }
    }
    int noteAt (juce::Point<float> pt) const
    {
        const float w = ((float) getWidth() - 48.0f) / 36.0f;
        const int k = (int) ((pt.x - 24.0f) / w);
        return k >= 0 && k < 36 ? base + k : -1;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < 22) { base = std::max (0, base - 12); repaint(); return; }
        if (e.x > getWidth() - 22) { base = std::min (84, base + 12); repaint(); return; }
        down = noteAt (e.position);
        if (down >= 0) p.uiNote (down, 0.6f + 0.4f * (float) e.y / (float) getHeight());
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int n = noteAt (e.position);
        if (n >= 0 && n != down)
        {
            const int old = down; down = n;
            p.uiNote (n, 0.85f);        // new note first: mono legato glides
            if (old >= 0) p.uiNote (old, 0.0f);
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent&) override { if (down >= 0) p.uiNote (down, 0.0f); down = -1; repaint(); }
private:
    Processor& p; ui::Theme theme; int down = -1;
};

// ---- wb1 modulation page ----------------------------------------------------------------------------
class Trace : public juce::Component
{
public:
    Trace (std::function<float()> f, juce::Colour c, ui::Theme t) : get (std::move (f)), col (c), theme (t) {}
    void tick() { hist[(size_t) pos] = get(); pos = (pos + 1) % kN; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (b);
        juce::Path p;
        for (int i = 0; i < kN; ++i)
        {
            const float v = hist[(size_t) ((pos + i) % kN)];
            const float x = b.getX() + b.getWidth() * (float) i / (float) (kN - 1), y = b.getBottom() - 2 - v * (b.getHeight() - 4);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col); g.strokePath (p, juce::PathStrokeType (1.6f));
    }
private:
    static constexpr int kN = 220;
    std::function<float()> get; juce::Colour col; ui::Theme theme;
    std::array<float, kN> hist {}; int pos = 0;
};

class StepBars : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepBars (Processor& proc, ui::Theme t) : p (proc), theme (t)
    {
        for (int i = 0; i < 16; ++i) att.push_back (std::make_unique<juce::ParameterAttachment> (P (p, "stp" + juce::String (i + 1)), [this] (float) { repaint(); }, nullptr));
        setTooltip ("draw the steps · alt-click: zero");
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = b.getWidth() / 16.0f;
        const int len = (int) std::round (p.param ("steplen"));
        const float ph = p.bass() ? p.bass()->showStepPh.load() : 0.0f;
        const int cur = std::min (len - 1, (int) (ph * (float) len));
        g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (b);
        for (int i = 0; i < 16; ++i)
        {
            const float v = p.param ("stp" + juce::String (i + 1));
            auto r = juce::Rectangle<float> (b.getX() + i * w + 2, b.getBottom() - v * (b.getHeight() - 4) - 2, w - 6, v * (b.getHeight() - 4));
            g.setColour ((i == cur && p.bass() && p.bass()->activeVoices() > 0 ? juce::Colours::white : theme.fill).withAlpha (i < len ? 1.0f : 0.25f));
            g.fillPath (ui::skewRect (r, 0.06f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { last = -1; mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 15, (int) (e.position.x / ((float) getWidth() / 16.0f)));
        const float v = e.mods.isAltDown() ? 0.0f : juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / (float) getHeight());
        if (i != last) { if (last >= 0) att[(size_t) last]->endGesture(); att[(size_t) i]->beginGesture(); last = i; }
        att[(size_t) i]->setValueAsPartOfGesture (v);
    }
    void mouseUp (const juce::MouseEvent&) override { if (last >= 0) att[(size_t) last]->endGesture(); last = -1; }
private:
    Processor& p; ui::Theme theme; int last = -1;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> att;
};

class ModPage : public juce::Component
{
public:
    ModPage (Processor& proc, ui::Theme t) : p (proc), theme (t),
        tr2 ([this] { return p.bass() ? p.bass()->showL2.load() : 0.0f; }, juce::Colour (0xffef7a3c), t),
        tr3 ([this] { return p.bass() ? p.bass()->showL3.load() : 0.0f; }, juce::Colour (0xffeba3ab), t),
        trW ([this] { return p.bass() ? p.bass()->showWob.load() : 0.0f; }, juce::Colours::white, t),
        trS ([this] { return p.bass() ? p.bass()->showSteps.load() : 0.0f; }, t.fill, t),
        bars (proc, t)
    {
        auto add = [this] (Stage& s, const juce::String& id, const juce::String& label)
        {
            auto& prm = P (p, id);
            juce::Component* c;
            if (dynamic_cast<juce::AudioParameterChoice*> (&prm)) c = new ui::ChoiceBox (prm, theme, label);
            else if (dynamic_cast<juce::AudioParameterBool*> (&prm)) c = new ui::ToggleBox (prm, theme, label);
            else { auto* sl = new ui::ParamSlider (prm, theme, label); sl->setLabelWidth (58); c = sl; }
            s.add (c);
        };
        for (int k = 2; k <= 3; ++k)
        {
            auto& st = k == 2 ? s2 : s3;
            st = std::make_unique<Stage> ("lfo " + juce::String (k), theme);
            st->add (k == 2 ? (juce::Component*) &tr2 : (juce::Component*) &tr3, 46, false);
            const juce::String x = "l" + juce::String (k);
            for (auto [id, lb] : { std::pair<const char*, const char*> { "shape", "shape" }, { "div", "rate" }, { "sync", "sync" }, { "hz", "free rate" }, { "retrig", "retrigger" },
                                   { "mode", "mode" }, { "phase", "phase" }, { "skew", "skew" }, { "smooth", "smooth" } })
                add (*st, x + id, lb);
            addAndMakeVisible (*st);
        }
        ss = std::make_unique<Stage> ("steps", theme);
        ss->add (&trS, 30, false);
        ss->add (&bars, 96, false);
        add (*ss, "steplen", "length"); add (*ss, "stepdiv", "rate"); add (*ss, "stepfree", "free time"); add (*ss, "stepglide", "glide"); add (*ss, "stepretrig", "retrigger");
        addAndMakeVisible (*ss);
        sm = std::make_unique<Stage> ("matrix  (source > destination)", theme);
        sm->add (&trW, 30, false);
        for (int k = 1; k <= 8; ++k)
        {
            const juce::String x = "m" + juce::String (k);
            auto* row = new Row (p, theme, x, k);
            sm->add (row, 22);
        }
        addAndMakeVisible (*sm);
    }
    void resized() override
    {
        const int w = getWidth(), gap = 22;
        const int cw = (w - 3 * gap) / 4;
        s2->setBounds (0, 0, cw - 20, s2->wanted());
        s3->setBounds (cw - 20 + gap, 0, cw - 20, s3->wanted());
        ss->setBounds (2 * (cw - 20) + 2 * gap, 0, cw - 20, ss->wanted());
        const int mx = 3 * (cw - 20) + 3 * gap;
        sm->setBounds (mx, 0, w - mx, sm->wanted());
    }
    void tick() { tr2.tick(); tr3.tick(); trW.tick(); trS.tick(); bars.repaint(); }
private:
    struct Row : juce::Component
    {
        Row (Processor& p, ui::Theme t, const juce::String& x, int k) : theme (t), index (k),
            src (P (p, x + "src"), t, " "), dst (P (p, x + "dst"), t, " "), amt (P (p, x + "amt"), t, " ")
        {
            src.labelW = 0; dst.labelW = 0; amt.setLabelWidth (0);
            addAndMakeVisible (src); addAndMakeVisible (dst); addAndMakeVisible (amt);
        }
        void resized() override
        {
            const int w = getWidth() - 16;
            src.setBounds (16, 0, (int) (w * 0.30f), getHeight());
            dst.setBounds (16 + (int) (w * 0.32f), 0, (int) (w * 0.33f), getHeight());
            amt.setBounds (16 + (int) (w * 0.66f), 0, w - (int) (w * 0.66f), getHeight());
        }
        void paint (juce::Graphics& g) override { g.setColour (theme.ink.withAlpha (0.6f)); g.setFont (ui::mono (9.5f)); g.drawText (juce::String (index), 0, 0, 14, getHeight(), juce::Justification::centredLeft); }
        ui::Theme theme; int index; ui::ChoiceBox src, dst; ui::ParamSlider amt;
    };
    Processor& p; ui::Theme theme;
    Trace tr2, tr3, trW, trS;
    StepBars bars;
    std::unique_ptr<Stage> s2, s3, ss, sm;
};

class SynthView : public View
{
public:
    explicit SynthView (Processor& proc) : p (proc), pi (proc.pinfo), theme (themeFor (pi)), art (proc), presetBar (proc, theme), keys (proc, theme),
        modPage (proc, theme), tabSynth ("synth", theme), tabMod ("modulation", theme)
    {
        addAndMakeVisible (art); addAndMakeVisible (presetBar); addAndMakeVisible (keys);
        addChildComponent (modPage); addAndMakeVisible (tabSynth); addAndMakeVisible (tabMod);
        tabSynth.onClick = [this] { setPage (0); };
        tabMod.onClick = [this] { setPage (1); };
        const std::vector<std::vector<std::pair<juce::String, std::vector<juce::String>>>> cols = {
            { { "osc a", { "a_shape", "a_pw", "a_oct", "a_semi", "a_fine", "a_level", "a_uni", "a_detune", "a_spread" } },
              { "osc b", { "b_shape", "b_pw", "b_oct", "b_semi", "b_fine", "b_level", "b_uni", "b_detune", "b_spread", "fm" } } },
            { { "chord", { "chord", "voicing" } },
              { ui::U ("sub · noise"), { "sub", "subwave", "suboct", "noise" } },
              { "filter", { "ftype", "cutoff", "res", "fdrive", "keytrack" } },
              { "formant", { "vowel", "fmtq", "fmtshift", "fmtmix" } },
              { "comb", { "combtune", "combfb", "combmix" } } },
            { { "distortion", { "dmode", "ddrive", "dtone", "dmix", "order" } },
              { "ott", { "ott", "otttime" } },
              { "wobble", { "wdiv", "wfree", "wshape", "wretrig", "wphase", "wcut", "wvowel", "wfm", "wdrive", "wpitch", "wamp", "wcomb" } } },
            { { "mod env", { "ma", "md", "ms", "mr", "mcut", "mpitch", "mfm", "mvowel" } },
              { "amp env", { "aa", "ad", "as", "ar" } },
              { "play", { "voices", "legato", "glide", "bend", "wheel", "vel", "width", "level" } } } };
        for (size_t c = 0; c < cols.size(); ++c)
            for (auto& [title, ids] : cols[c])
            {
                items.push_back ({ (int) c, title, nullptr });
                for (auto& id : ids)
                {
                    auto& prm = P (p, id);
                    juce::String label = prm.getName (40);
                    for (auto pre : { "a ", "b ", "wob > ", "env > ", "amp ", "env ", "wobble ", "formant ", "comb ", "dist ", "sub " })
                        if (label.startsWith (pre) && label.length() > juce::String (pre).length() + 1) { label = label.substring (juce::String (pre).length()); break; }
                    if (title == "wobble" && id.startsWith ("w") && id != "wdiv" && id != "wfree" && id != "wshape" && id != "wretrig" && id != "wphase") label = "> " + label;
                    if (title == "mod env" && (id == "mcut" || id == "mpitch" || id == "mfm" || id == "mvowel")) label = "> " + label;
                    juce::Component* comp;
                    if (dynamic_cast<juce::AudioParameterChoice*> (&prm)) comp = new ui::ChoiceBox (prm, theme, label);
                    else if (dynamic_cast<juce::AudioParameterBool*> (&prm)) comp = new ui::ToggleBox (prm, theme, label);
                    else { auto* s = new ui::ParamSlider (prm, theme, label); s->setLabelWidth (64); comp = s; }
                    owned.add (comp); addAndMakeVisible (comp);
                    items.push_back ({ (int) c, {}, comp });
                }
            }
        setSize (pi.w, pi.h);
        setPage (0);
    }
    void setPage (int pg) override
    {
        page = pg;
        for (auto& it : items) if (it.comp) it.comp->setVisible (pg == 0);
        modPage.setVisible (pg == 1);
        tabSynth.setLit (pg == 0); tabMod.setLit (pg == 1);
        repaint();
    }
    void resized() override
    {
        const int w = getWidth();
        art.setBounds (0, 0, w, 212);
        presetBar.setBounds (w - 236, 14, 200, 20);
        tabSynth.setBounds (w - 236, 40, 80, 20); tabMod.setBounds (w - 150, 40, 114, 20);
        modPage.setBounds (36, 226, w - 72, getHeight() - 226 - 60);
        const int colW = (w - 72 - 3 * 28) / 4;
        std::array<int, 4> y { 226, 226, 226, 226 };
        headers.clear();
        for (auto& it : items)
        {
            const int x = 36 + it.col * (colW + 28);
            if (it.comp == nullptr) { headers.push_back ({ juce::Rectangle<int> (x, y[(size_t) it.col] + 4, colW, 16), it.title }); y[(size_t) it.col] += 24; }
            else { it.comp->setBounds (x, y[(size_t) it.col], colW, 18); y[(size_t) it.col] += 19; }
        }
        keys.setBounds (36, getHeight() - 48, w - 72, 34);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (page == 0) for (auto& [r, t] : headers)
        {
            g.setColour (theme.ink);
            g.setFont (ui::mono (11.0f, true));
            g.drawText (t, r, juce::Justification::centredLeft);
            g.setColour (Colour (pi.accent).withAlpha (0.6f));
            g.fillRect ((float) r.getX(), (float) r.getBottom(), (float) r.getWidth(), 1.0f);
        }
    }
    void tick() override { art.tick(); if (page == 1) modPage.tick(); }
private:
    struct Item { int col; juce::String title; juce::Component* comp; };
    Processor& p;
    const ProductInfo& pi;
    ui::Theme theme;
    Art art;
    ui::PresetBar presetBar;
    Keys keys;
    std::vector<Item> items;
    juce::OwnedArray<juce::Component> owned;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> headers;
    ModPage modPage;
    ui::TextLink tabSynth, tabMod;
    int page = 0;
};

// =================================================================================================
Editor::Editor (Processor& p) : AudioProcessorEditor (p), proc (p)
{
    if (p.pid == Pid::kt1) view = std::make_unique<KitView> (p);
    else if (p.pid == Pid::kt2) view = std::make_unique<LoopView> (p);
    else if (p.pid == Pid::wb1) view = std::make_unique<SynthView> (p);
    else if (p.pid == Pid::ms0) view = std::make_unique<MasterView> (p);
    else view = std::make_unique<ModuleView> (p);
    nominalW = p.pinfo.w; nominalH = p.pinfo.h;
    view->setBounds (0, 0, nominalW, nominalH);
    addAndMakeVisible (*view);
    setResizable (true, true);
    setResizeLimits (nominalW / 2, nominalH / 2, nominalW * 2, nominalH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) nominalW / nominalH);
    setSize (nominalW, nominalH);
    startTimerHz (30);
}
Editor::~Editor() { stopTimer(); }
void Editor::paint (juce::Graphics& g) { g.fillAll (Colour (proc.pinfo.ground)); }
void Editor::resized()
{
    const float s = (float) getWidth() / (float) nominalW;
    view->setTransform (juce::AffineTransform::scale (s));
}
void Editor::timerCallback() { view->tick(); }
} // namespace fal
