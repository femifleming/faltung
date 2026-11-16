#include "PluginEditor.h"

namespace ecr
{
namespace
{
    // the faltung look: one flat field, its ink, one accent for lines and fills, white for the highlights
    const juce::Colour ground { 0xff6e80c8 };               // the suite's slate blue
    const juce::Colour accent { 0xfff6c590 };               // peach: wireframe, chips, slider fills
    const juce::Colour white  = juce::Colours::white;       // highlight faces, edge ticks
    const juce::Colour ink    = fal::ui::inkFor (ground);

    struct KnobRow { Knob id; const char* name; const char* shiftName; bool bipolar; };
    const KnobRow rows[8] = {
        { Knob::Width,    "width",    "pitch",       true  },
        { Knob::Xfade,    "crossfade", "slew",       false },
        { Knob::Predelay, "predelay", "hpf",         false },
        { Knob::Pos,      "position", "input level", false },
        { Knob::Dry,      "dry",      nullptr,       false },
        { Knob::Wet,      "wet",      nullptr,       false },
        { Knob::Attack,   "attack",   nullptr,       false },
        { Knob::Release,  "release",  nullptr,       false },
    };

    juce::Font mono (float size, bool medium = false) { return fal::ui::mono (size, medium); }
    juce::Font sans (float size, bool medium = false) { return fal::ui::sans (size, medium); }
    juce::String U (const char* s) { return fal::ui::U (s); }
    float textW (const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }

    // the house shape, filled
    void chip (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float skew = 0.35f)
    {
        g.setColour (c);
        g.fillPath (fal::ui::skewRect (r, skew));
    }

    // a thin ink rule
    void rule (juce::Graphics& g, float x, float y, float w, float alpha = 0.35f)
    {
        g.setColour (ink.withAlpha (alpha));
        g.fillRect (x, y, w, 1.0f);
    }

    // the slider track of a faltung ParamSlider: rule · slanted fill from zero · white edge tick
    void sliderTrack (juce::Graphics& g, juce::Rectangle<float> t, float v, bool bipolar, bool dragging)
    {
        const float cy = t.getCentreY();
        rule (g, t.getX(), cy, t.getWidth());
        const float zero = bipolar ? 0.5f : 0.0f;
        const float a = t.getX() + t.getWidth() * std::min (zero, v), e = t.getX() + t.getWidth() * std::max (zero, v);
        const float h = 9.0f;
        if (e - a > 0.5f)
        {
            g.setColour (accent.withAlpha (dragging ? 1.0f : 0.92f));
            g.fillPath (fal::ui::skewRect ({ a, cy - h * 0.5f, e - a, h }));
        }
        g.setColour (white);
        g.fillRect (t.getX() + t.getWidth() * v - 1.0f, cy - 5.5f, 2.0f, 11.0f);
    }

    void text (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c,
               juce::Justification j = juce::Justification::centredLeft, bool ellipsis = true)
    {
        g.setColour (c);
        g.setFont (f);
        g.drawText (t, r, j, ellipsis);
    }
}

EcrEditor::EcrEditor (EcrProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setOpaque (true);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (kW * 3 / 4, kH * 3 / 4, kW * 3, kH * 3);
    setSize (kW, kH);
    startTimerHz (30);
}

EcrEditor::~EcrEditor() { stopTimer(); }

void EcrEditor::timerCallback()
{
    proc.ui->tick();
    for (int l = 0; l < kNumLfos; ++l)
    {
        auto& t = trails[l];
        const int shape = juce::roundToInt (proc.lfoShapeParam (l)->convertFrom0to1 (proc.lfoShapeParam (l)->getValue()));
        if (shape != t.shape) { t.shape = shape; t.path.clear(); t.values.clear(); }
        proc.lfoView (l, t.view);
        // Lorenz is most recognisable from the side (x against z), Roessler from above, boids as a map
        const juce::Point<float> pt = shape == kLorenz ? juce::Point<float> (t.view[0], -t.view[2])
                                    : shape == kRossler ? juce::Point<float> (t.view[0], -t.view[1])
                                                        : juce::Point<float> (t.view[3], -t.view[4]);
        t.path.push_back (pt);
        if (t.path.size() > (shape == kBoid ? 60u : 400u)) t.path.erase (t.path.begin());
        t.values.push_back (proc.lfoValue (l));
        if (t.values.size() > 110u) t.values.erase (t.values.begin());
    }
    if ((active.type == Hit::Push || active.type == Hit::Screen) && ! moved && ! holdFired
        && juce::Time::getMillisecondCounter() - downTime > 700)
    {
        holdFired = true;
        proc.ui->holdSelect();
    }
    repaint();
}

//== drawing =====================================================================
// A text link; lit (or pressed) ones sit on a skewed accent chip. Returns the x after it.
float EcrEditor::link (juce::Graphics& g, const juce::String& t, float x, float y, Target target, bool lit)
{
    const auto font = mono (11.0f);
    const float w = textW (font, t);
    const bool pressed = active.type == target.type && active.index == target.index && target.type != Hit::None;
    const juce::Rectangle<float> box (x - 4.0f, y, w + 8.0f, 18.0f);
    if (lit || pressed) chip (g, box.reduced (0.0f, 2.0f), lit ? accent : accent.withAlpha (0.55f));
    text (g, t, { x, y, w + 2.0f, 18.0f }, font, ink, juce::Justification::centredLeft, false);
    hits.push_back ({ box.expanded (0.0f, 1.0f), target });
    return x + w;
}

// " · " between links
float EcrEditor::separator (juce::Graphics& g, float x, float y)
{
    text (g, U ("\xc2\xb7"), { x + 5.0f, y, 8.0f, 18.0f }, mono (11.0f), ink.withAlpha (0.45f), juce::Justification::centred, false);
    return x + 18.0f;
}

// A response as a thin wire shell: one row per channel, the overview as a polyline on ribs,
// struts between the rows, and the attack-release window as a white highlight face.
void EcrEditor::drawResponse (juce::Graphics& g, juce::Rectangle<float> plot, int slot)
{
    auto src = proc.responseSource (slot);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.toNearestInt());
    if (src == nullptr || src->length <= 0)
    {
        rule (g, plot.getX(), plot.getBottom() - 2.0f, plot.getWidth(), 0.25f);
        return;
    }
    const int chans = src->numChannels >= 4 ? 4 : 2;
    static const char* const chanNames[4] = { "l > l", "r > r", "l > r", "r > l" };
    const float rowH = plot.getHeight() / (float) chans, cw = plot.getWidth() / 128.0f;
    const float small = plot.getHeight() < 100.0f ? 0.5f : 1.0f;   // the thumbnail keeps its ribs sparser
    const int rib = small < 1.0f ? 16 : 8;
    std::vector<juce::Point<float>> prev, cur;
    for (int c = 0; c < chans; ++c)
    {
        const auto& ov = src->overview[(size_t) (src->numChannels == 1 ? 0 : c)];
        const float base = plot.getY() + rowH * (float) (c + 1) - 2.0f;
        const float top = rowH - (small < 1.0f ? 6.0f : 14.0f);
        cur.clear();
        juce::Path line;
        for (int i = 0; i < 128; ++i)
        {
            const juce::Point<float> pt (plot.getX() + cw * (float) i + cw * 0.5f, base - (float) ov[(size_t) i] / 255.0f * top);
            cur.push_back (pt);
            if (i == 0) line.startNewSubPath (pt); else line.lineTo (pt);
        }
        // the shell: baseline, ribs, the outline, struts to the row above
        g.setColour (accent.withAlpha (0.45f));
        g.fillRect (plot.getX(), base, plot.getWidth(), 1.0f);
        for (int i = 0; i < 128; i += rib)
            if (cur[(size_t) i].y < base - 1.0f) g.drawLine (cur[(size_t) i].x, cur[(size_t) i].y, cur[(size_t) i].x, base, 1.0f);
        if (! prev.empty())
        {
            g.setColour (accent.withAlpha (0.3f));
            for (int i = rib; i + rib < 128; i += rib * 2)
                g.drawLine (prev[(size_t) i].x, prev[(size_t) i].y, cur[(size_t) (i + rib)].x, cur[(size_t) (i + rib)].y, 1.0f);
        }
        g.setColour (accent);
        g.strokePath (line, juce::PathStrokeType (1.0f));
        if (small >= 1.0f)
            text (g, chanNames[c], { plot.getX() + 3.0f, base - rowH + 2.0f, 60.0f, 12.0f }, mono (10.0f), ink, juce::Justification::centredLeft, false);
        prev.swap (cur);
    }

    // the window the knobs cut out of it: position -> attack -> release
    const double pos = proc.slotParam (slot, kPos)->getValue();
    const double attack = curve::timeSeconds (proc.slotParam (slot, kAttack)->getValue());
    const double relK = proc.slotParam (slot, kRelease)->getValue();
    const double seconds = juce::jmax (1e-3, src->length / src->sampleRate / std::pow (2.0, curve::pitchOctaves (proc.slotParam (slot, kPitch)->getValue())));
    const float xa = plot.getX() + (float) pos * plot.getWidth(), xb = xa + (float) (attack / seconds) * plot.getWidth();
    const float top = plot.getY() + 1.0f, bot = plot.getBottom() - 1.0f;
    juce::Path face;
    face.startNewSubPath (xa, bot);
    face.lineTo (attack > 0.0 ? xb : xa, top);
    if (curve::releaseIsFull (relK)) { face.lineTo (plot.getRight() + 4.0f, top); face.lineTo (plot.getRight() + 4.0f, bot); }
    else face.lineTo (xb + (float) (curve::timeSeconds (relK) / seconds) * plot.getWidth(), bot);
    face.closeSubPath();
    g.setColour (white.withAlpha (0.3f));
    g.fillPath (face);
    g.setColour (white);
    g.strokePath (face, juce::PathStrokeType (small < 1.0f ? 1.5f : 2.0f));
}

// pop-ups (stored, read patch..., no recording to store): a skewed chip in the middle
void EcrEditor::drawPopup (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto popup = proc.ui->currentPopup();
    if (popup.isEmpty()) return;
    const auto lines = juce::StringArray::fromLines (popup.toLowerCase());
    const auto font = mono (11.0f, true);
    float w = 0.0f;
    for (const auto& l : lines) w = std::max (w, textW (font, l));
    const float lh = 16.0f;
    const juce::Rectangle<float> box (r.getCentreX() - w * 0.5f - 14.0f, r.getCentreY() - lh * (float) lines.size() * 0.5f - 6.0f, w + 28.0f, lh * (float) lines.size() + 12.0f);
    chip (g, box, accent);
    for (int i = 0; i < lines.size(); ++i)
        text (g, lines[i], { box.getX(), box.getY() + 6.0f + lh * (float) i, box.getWidth(), lh }, font, ink, juce::Justification::centred, false);
}

// What the module shows on its display: the response, the list of responses, or the system menu.
void EcrEditor::drawScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.toNearestInt());
    const int fg = proc.foreground.load();
    const juce::String ab (fg == 0 ? "a" : "b");
    const float x0 = r.getX() + 4.0f, w = r.getWidth() - 8.0f;

    if (proc.ui->screen == Ui::Chooser)
    {
        const auto& list = proc.library.entries();
        const int n = (int) list.size(), sel = juce::jlimit (0, juce::jmax (0, n - 1), proc.ui->chooserIndex());
        text (g, n > 0 ? list[(size_t) sel].folder.toLowerCase() : juce::String ("no responses"), { x0, r.getY() + 3.0f, w - 20.0f, 16.0f }, mono (11.0f, true), ink);
        text (g, ab, { x0, r.getY() + 3.0f, w, 16.0f }, mono (11.0f, true), white, juce::Justification::centredRight);
        rule (g, x0, r.getY() + 22.0f, w);
        const int visible = 10, first = juce::jlimit (0, juce::jmax (0, n - visible), sel - 4);
        for (int i = 0; i < visible && first + i < n; ++i)
        {
            const auto& e = list[(size_t) (first + i)];
            const float y = r.getY() + 28.0f + 20.0f * (float) i;
            const auto name = e.name.upToLastOccurrenceOf (".", false, false).toLowerCase();
            const bool here = first + i == sel;
            if (here) chip (g, { x0, y + 1.0f, w - 8.0f, 18.0f }, accent);
            if (e.hasPatch) chip (g, { x0 + 4.0f, y + 7.0f, 8.0f, 6.0f }, here ? ink : accent, 0.55f);   // a patch is saved with it
            text (g, name, { x0 + 18.0f, y, w - 24.0f, 20.0f }, mono (11.0f, here), ink);
        }
        rule (g, x0, r.getBottom() - 20.0f, w);
        text (g, juce::String (sel + 1) + " of " + juce::String (n) + (first + visible < n ? U(" \xc2\xb7 more below") : juce::String()),
              { x0, r.getBottom() - 17.0f, w, 14.0f }, mono (10.0f), ink.withAlpha (0.7f));
    }
    else if (proc.ui->screen == Ui::Menu)
    {
        text (g, "system menu", { x0, r.getY() + 3.0f, w, 16.0f }, mono (11.0f, true), ink);
        text (g, "convosolution " JucePlugin_VersionString, { x0, r.getY() + 3.0f, w, 16.0f }, mono (10.0f), ink.withAlpha (0.7f), juce::Justification::centredRight);
        rule (g, x0, r.getY() + 22.0f, w);
        static const char* const nice[Ui::kNumMenuItems] = { "direct shift", "file chooser", "auto exit", "cv map", "aux cv", "screensaver", "convolver b", "autogain", "exit" };
        const float ty = r.getY() + 30.0f, rh = 22.0f;
        for (int i = 0; i < Ui::kNumMenuItems; ++i)
        {
            juce::String label, value;
            proc.ui->menuParts (i, label, value);
            const float y = ty + rh * (float) i;
            const bool here = i == proc.ui->menuIndex();
            if (here) chip (g, { x0, y + 1.0f, w - 8.0f, rh - 2.0f }, accent);
            text (g, nice[i], { x0 + 6.0f, y, w * 0.62f, rh }, mono (11.0f, here), ink);
            text (g, value.toLowerCase(), { x0, y, w - 8.0f, rh }, mono (11.0f, true), ink, juce::Justification::centredRight);
        }
    }
    else
    {
        auto src = proc.responseSource (fg);
        text (g, proc.ui->currentMessage().toLowerCase(), { x0, r.getY() + 3.0f, w - 60.0f, 16.0f }, mono (11.0f, true), ink);
        text (g, ab, { x0, r.getY() + 3.0f, w, 16.0f }, mono (11.0f, true), white, juce::Justification::centredRight);
        if (proc.clipping.load())
        {
            // the wet sum is being held back
            const juce::Rectangle<float> c (r.getRight() - 50.0f, r.getY() + 5.0f, 30.0f, 12.0f);
            chip (g, c, white);
            text (g, "clip", c, mono (9.0f, true), ink, juce::Justification::centred, false);
        }

        const juce::Rectangle<float> plot (x0, r.getY() + 24.0f, w, r.getHeight() - 46.0f);
        drawResponse (g, plot, fg);
        if (proc.isRecording() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0)
            text (g, "recording", { plot.getX(), plot.getCentreY() - 10.0f, plot.getWidth(), 20.0f }, mono (13.0f, true), white, juce::Justification::centred, false);

        const juce::String name = proc.isLoading (fg) ? juce::String ("loading...") : (src != nullptr ? src->name.toLowerCase() : juce::String ("no response"));
        text (g, name, { x0, r.getBottom() - 18.0f, w, 14.0f }, mono (10.0f), ink);
    }
    drawPopup (g, r);
}

// One LFO as a figure: the attractor's orbit, the flock, or a strip chart, in thin accent lines.
void EcrEditor::drawLfoFigure (juce::Graphics& g, juce::Rectangle<float> r, int l)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.toNearestInt());
    const auto& t = trails[l];
    text (g, "lfo " + juce::String (l + 1) + U (" \xc2\xb7 ") + juce::String (lfoShapeName (std::max (0, t.shape))).toLowerCase(),
          { r.getX() + 4.0f, r.getY() + 2.0f, r.getWidth() - 8.0f, 12.0f }, mono (10.0f), ink, juce::Justification::centredLeft, false);

    const auto plot = r.reduced (6.0f).withTrimmedTop (14.0f).withTrimmedBottom (10.0f);
    auto map = [&] (juce::Point<float> p) { return juce::Point<float> (plot.getCentreX() + p.x * plot.getWidth() * 0.48f, plot.getCentreY() + p.y * plot.getHeight() * 0.48f); };
    auto polyline = [&] (const std::vector<juce::Point<float>>& pts, juce::Colour c)
    {
        if (pts.size() < 2) return;
        juce::Path p;
        p.startNewSubPath (map (pts.front()));
        for (size_t i = 1; i < pts.size(); ++i) p.lineTo (map (pts[i]));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    };

    if (t.shape == kLorenz || t.shape == kRossler)
    {
        polyline (t.path, accent);
        if (! t.path.empty()) { const auto now = map (t.path.back()); chip (g, { now.x - 3.0f, now.y - 2.0f, 6.0f, 4.0f }, white, 0.5f); }
    }
    else if (t.shape == kBoid)
    {
        polyline (t.path, accent.withAlpha (0.5f));
        for (int i = (Lfo::kViewSize - 3) / 2 - 1; i >= 0; --i)
        {
            const auto b = map ({ t.view[3 + 2 * i], -t.view[4 + 2 * i] });
            if (i == 0) chip (g, { b.x - 4.0f, b.y - 2.5f, 8.0f, 5.0f }, white, 0.5f);
            else chip (g, { b.x - 3.0f, b.y - 2.0f, 6.0f, 4.0f }, accent, 0.5f);
        }
    }
    else if (t.values.size() > 1)
    {
        rule (g, plot.getX(), plot.getCentreY(), plot.getWidth(), 0.25f);
        juce::Path p;
        const float dx = plot.getWidth() / 109.0f, x0 = plot.getRight() - dx * (float) (t.values.size() - 1);
        for (size_t i = 0; i < t.values.size(); ++i)
        {
            const juce::Point<float> pt (x0 + dx * (float) i, plot.getCentreY() - t.values[i] * plot.getHeight() * 0.48f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // where the output is right now, -1 .. +1
    const float v = t.values.empty() ? 0.0f : t.values.back();
    const float mx = r.getX() + 8.0f, mw = r.getWidth() - 16.0f, my = r.getBottom() - 7.0f;
    rule (g, mx, my, mw);
    chip (g, { mx + mw * 0.5f + v * mw * 0.5f - 3.0f, my - 2.5f, 6.0f, 5.0f }, white, 0.5f);
}

void EcrEditor::drawPage (juce::Graphics& g)
{
    hits.clear();
    g.fillAll (ground);
    const bool shift = shiftNow (juce::ModifierKeys::getCurrentModifiers());
    const int fg = proc.foreground.load();
    const juce::String ab (fg == 0 ? "a" : "b");

    // title
    text (g, "convosolution", { 12.0f, 6.0f, 260.0f, 26.0f }, sans (22.0f, true), white, juce::Justification::centredLeft, false);
    text (g, U ("convolution reverb \xc2\xb7 convolver ") + ab, { 12.0f, 31.0f, 260.0f, 13.0f }, mono (10.0f), ink, juce::Justification::centredLeft, false);

    // the picture: the module's display
    const juce::Rectangle<float> img (12.0f, 50.0f, 256.0f, 256.0f);
    const bool figures = tab == 1 && proc.ui->screen == Ui::Main;
    if (figures)
    {
        // modulation page: the LFOs get the picture, the response shrinks to a thumbnail
        const float half = img.getWidth() * 0.5f;
        rule (g, img.getX(), img.getCentreY(), img.getWidth(), 0.2f);
        g.setColour (ink.withAlpha (0.2f)); g.fillRect (img.getCentreX(), img.getY(), 1.0f, img.getHeight());
        for (int l = 0; l < kNumLfos; ++l)
            drawLfoFigure (g, { img.getX() + (float) (l % 2) * half, img.getY() + (float) (l / 2) * half, half, half }, l);
        const juce::Rectangle<float> thumb (img.getX() + half, img.getY() + half, half, half);
        auto src = proc.responseSource (fg);
        text (g, ab + U (" \xc2\xb7 ") + (src != nullptr ? src->name.upToLastOccurrenceOf (".", false, false).toLowerCase() : juce::String ("no response")),
              { thumb.getX() + 4.0f, thumb.getY() + 2.0f, thumb.getWidth() - 8.0f, 12.0f }, mono (10.0f), ink);
        drawResponse (g, thumb.reduced (6.0f).withTrimmedTop (14.0f).withTrimmedBottom (8.0f), fg);
        drawPopup (g, img);
    }
    else
        drawScreen (g, img);
    hits.push_back ({ img, { Hit::Screen } });
    text (g, figures ? U ("the lfos at work \xc2\xb7 the response: drag to browse") : U ("drag: browse \xc2\xb7 click: a / b \xc2\xb7 hold: read the patch"),
          { 12.0f, 310.0f, 262.0f, 14.0f }, mono (10.0f), ink.withAlpha (0.7f), juce::Justification::centredLeft, false);

    // links: the four buttons, the select encoder, the folder
    const float cx = 286.0f;
    float x = cx;
    const bool blink = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    const char* const names[4] = { "shift", "record", "store", "play" };
    const bool lit[4] = { proc.ui->shiftLed(), proc.isRecording() && blink, proc.ui->storeLed(), proc.ui->playLed() };
    for (int i = 0; i < 4; ++i)
    {
        x = link (g, names[i], x, 44.0f, { Hit::Button, i }, lit[i]);
        if (i < 3) x = separator (g, x, 44.0f);
    }
    x = link (g, "< prev", cx, 66.0f, { Hit::Prev }, false);
    x = separator (g, x, 66.0f);
    x = link (g, "next >", x, 66.0f, { Hit::Next }, false);
    x = separator (g, x, 66.0f);
    x = link (g, "select " + ab, x, 66.0f, { Hit::Push }, false);
    x = separator (g, x, 66.0f);
    link (g, "folder...", x, 66.0f, { Hit::Folder }, false);

    // tabs: the current page on a chip, the other plain
    x = cx;
    for (int i = 0; i < 2; ++i)
    {
        x = link (g, i == 0 ? "knobs" : "modulation", x, 88.0f, { Hit::Tab, i }, i == tab);
        if (i == 0) x = separator (g, x, 88.0f);
    }

    const float tx = cx, tw = 262.0f, ty = 112.0f;
    if (tab == 0)
    {
        // the knobs as slider rows: label · rule · slanted fill · white edge · value
        const float rh = 23.0f, lw = 86.0f, vw = 66.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto& r = rows[i];
            const bool alt = shift && r.shiftName != nullptr;
            const float y = ty + rh * (float) i;
            auto* prm = proc.ui->paramFor (r.id, shift);
            text (g, alt ? r.shiftName : r.name, { tx, y, lw - 4.0f, rh }, mono (11.0f, alt), alt ? white : (shift ? ink.withAlpha (0.45f) : ink));
            const juce::Rectangle<float> track (tx + lw + 4.0f, y, tw - lw - vw - 8.0f, rh);
            sliderTrack (g, track, prm->getValue(), r.bipolar, active.type == Hit::Knob && active.index == i);
            text (g, prm->getCurrentValueAsText().toLowerCase(), { tx + tw - vw, y, vw, rh }, mono (11.0f), ink, juce::Justification::centredRight);
            Target t { Hit::Knob, i }; t.width = track.getWidth();
            hits.push_back ({ juce::Rectangle<float> (tx, y, tw, rh), t });
        }
    }
    else
    {
        // LFOs: shape (click for the list), rate, and what it is doing right now
        const float rh = 22.0f;
        for (int l = 0; l < kNumLfos; ++l)
        {
            const float y = ty + rh * (float) l, cy = y + rh * 0.5f;
            text (g, "lfo " + juce::String (l + 1), { tx, y, 36.0f, rh }, mono (11.0f), ink);
            const int shape = juce::roundToInt (proc.lfoShapeParam (l)->convertFrom0to1 (proc.lfoShapeParam (l)->getValue()));
            const juce::Rectangle<float> shapeBox (tx + 36.0f, y, 64.0f, rh);
            text (g, juce::String (lfoShapeName (shape)).toLowerCase(), shapeBox.withTrimmedLeft (2.0f), mono (11.0f, true), ink);
            rule (g, shapeBox.getX() + 2.0f, shapeBox.getBottom() - 3.0f, shapeBox.getWidth() - 8.0f);
            hits.push_back ({ shapeBox, { Hit::LfoShape, l } });

            const juce::Rectangle<float> track (tx + 106.0f, y, 76.0f, rh);
            sliderTrack (g, track, proc.lfoRateParam (l)->getValue(), false, active.type == Hit::LfoRate && active.index == l);
            Target t { Hit::LfoRate, l }; t.width = track.getWidth();
            hits.push_back ({ track.expanded (4.0f, 0.0f), t });
            text (g, proc.lfoRateParam (l)->getCurrentValueAsText().toLowerCase(), { tx + 184.0f, y, 44.0f, rh }, mono (10.0f), ink, juce::Justification::centredRight);

            // the output now: a white tick on a short rule
            const float mx = tx + 234.0f, mw = 28.0f;
            rule (g, mx, cy, mw);
            g.setColour (white);
            g.fillRect (mx + mw * 0.5f + proc.lfoValue (l) * mw * 0.5f - 1.0f, cy - 4.5f, 2.0f, 9.0f);
        }

        // the matrix: a chip per cell, filled up for positive amounts, down for negative
        const float my = ty + rh * 3.0f + 12.0f, hh = 14.0f, mh = 22.0f, lw = 24.0f, cw = (tw - lw) / (float) kNumModDests;
        const float skewD = (mh - 2.0f) * 0.35f;
        for (int d = 0; d < kNumModDests; ++d)
            text (g, juce::String (EcrProcessor::modDestShort (d)).toLowerCase(), { tx + lw + cw * (float) d + skewD * 0.5f, my, cw, hh }, mono (10.0f),
                  active.type == Hit::Cell && active.sub == d ? white : ink, juce::Justification::centred, false);
        for (int l = 0; l < kNumLfos; ++l)
        {
            const float y = my + hh + mh * (float) l;
            text (g, "l" + juce::String (l + 1), { tx, y, lw, mh }, mono (11.0f), ink);
            for (int d = 0; d < kNumModDests; ++d)
            {
                const juce::Rectangle<float> r (tx + lw + cw * (float) d, y, cw, mh);
                const auto cell = fal::ui::skewRect (r.reduced (1.0f), 0.35f);
                const auto* prm = proc.modAmountParam (l, d);
                const float v = prm->convertFrom0to1 (prm->getValue());
                if (std::abs (v) > 0.004f)
                {
                    juce::Graphics::ScopedSaveState clip (g);
                    g.reduceClipRegion (cell);
                    const float half = r.getHeight() * 0.5f - 1.0f, len = std::max (1.5f, std::abs (v) * half);
                    g.setColour (v > 0 ? accent : accent.darker (0.5f));
                    g.fillRect (r.getX() - 4.0f, v > 0 ? r.getCentreY() - len : r.getCentreY(), r.getWidth() + skewD + 8.0f, len);
                }
                g.setColour (ink.withAlpha (0.35f));
                g.strokePath (cell, juce::PathStrokeType (1.0f));
                hits.push_back ({ r, { Hit::Cell, l, d } });
            }
        }
        text (g, U ("drag a cell up or down \xc2\xb7 double-click to clear"), { tx, my + hh + mh * 3.0f + 6.0f, tw, 14.0f }, mono (10.0f), ink.withAlpha (0.7f), juce::Justification::centredLeft, false);
    }

    // footer: what sits in a and b
    rule (g, 12.0f, 328.0f, (float) kW - 24.0f);
    x = 12.0f;
    for (int s = 0; s < 2; ++s)
    {
        auto src = proc.responseSource (s);
        const juce::String name = (src != nullptr ? src->name.upToLastOccurrenceOf (".", false, false) : juce::String ("(none)")).toLowerCase().substring (0, 30);
        text (g, s == 0 ? "a" : "b", { x, 334.0f, 12.0f, 16.0f }, mono (11.0f, true), s == fg ? white : ink.withAlpha (0.7f), juce::Justification::centredLeft, false);
        x += 14.0f;
        text (g, name, { x, 334.0f, 300.0f, 16.0f }, mono (11.0f), s == fg ? white : ink.withAlpha (0.7f), juce::Justification::centredLeft, false);
        x += textW (mono (11.0f), name) + 20.0f;
    }
    text (g, U ("convosolution \xc2\xb7 part of faltung"), { (float) kW - 262.0f, 352.0f, 250.0f, 14.0f }, mono (10.0f), ink.withAlpha (0.6f), juce::Justification::centredRight, false);
}

void EcrEditor::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
    drawPage (g);
}

//== mouse =======================================================================
EcrEditor::Target EcrEditor::hitTest (juce::Point<float> p) const
{
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->first.contains (p)) return it->second;
    return {};
}

juce::RangedAudioParameter* EcrEditor::paramOf (Target t, bool shift) const
{
    switch (t.type)
    {
        case Hit::Knob:    return proc.ui->paramFor (rows[t.index].id, shift);
        case Hit::LfoRate: return proc.lfoRateParam (t.index);
        case Hit::Cell:    return proc.modAmountParam (t.index, t.sub);
        default:           return nullptr;
    }
}

void EcrEditor::announce (Target t, bool shift)
{
    if (t.type == Hit::Knob) proc.ui->knobMoved (rows[t.index].id, shift);
    else if (t.type == Hit::LfoRate)
        proc.ui->setMessage ("LFO" + juce::String (t.index + 1) + " " + juce::String (lfoRateHz (proc.lfoRateParam (t.index)->getValue()), 2) + "Hz");
    else if (t.type == Hit::LfoShape)
        proc.ui->setMessage ("LFO" + juce::String (t.index + 1) + " " + proc.lfoShapeParam (t.index)->getCurrentValueAsText().toUpperCase());
    else if (t.type == Hit::Cell)
    {
        auto* prm = proc.modAmountParam (t.index, t.sub);
        const int pct = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()) * 100.0f);
        proc.ui->setMessage ("L" + juce::String (t.index + 1) + ">" + EcrProcessor::modDestName (t.sub) + (pct > 0 ? " +" : " ") + juce::String (pct) + "%");
    }
    proc.ui->wake();
}

static void stepShape (juce::RangedAudioParameter* prm, int delta)
{
    const int n = kNumLfoShapes;
    const int now = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (((now + delta) % n + n) % n)));
    prm->endChangeGesture();
}

void EcrEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto t = hitTest (toPage (e.position)).type;
    const bool isLink = t == Hit::Button || t == Hit::Prev || t == Hit::Next || t == Hit::Push || t == Hit::Folder || t == Hit::Tab || t == Hit::LfoShape;
    setMouseCursor (isLink ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void EcrEditor::mouseDown (const juce::MouseEvent& e)
{
    proc.ui->wake();
    active = hitTest (toPage (e.position));
    dragShift = shiftNow (e.mods);
    moved = holdFired = false;
    selectAccum = 0.0f;
    lastDrag = e.position;
    downTime = juce::Time::getMillisecondCounter();

    if (auto* prm = paramOf (active, dragShift))
    {
        dragStartValue = prm->getValue();
        prm->beginChangeGesture();
        announce (active, dragShift);
        return;
    }
    switch (active.type)
    {
        case Hit::Button:
            if (active.index == 0) proc.ui->pressShift();
            else if (active.index == 1) proc.ui->pressRecord();
            else if (active.index == 2) { proc.ui->pressStore (dragShift); proc.ui->shiftUsed(); }
            else { proc.ui->pressPlay (dragShift); proc.ui->shiftUsed(); }
            break;
        case Hit::Prev:     proc.ui->turnSelect (-1, dragShift); break;
        case Hit::Next:     proc.ui->turnSelect (1, dragShift); break;
        case Hit::Folder:   showFolderMenu(); break;
        case Hit::Tab:      tab = active.index; break;
        case Hit::LfoShape:
        {
            // the list of shapes
            juce::PopupMenu m;
            auto* prm = proc.lfoShapeParam (active.index);
            const int now = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));
            for (int i = 0; i < kNumLfoShapes; ++i) m.addItem (i + 1, juce::String (lfoShapeName (i)).toLowerCase(), true, i == now);
            juce::Component::SafePointer<EcrEditor> safe (this);
            const Target t = active;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, t, prm] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (r - 1)));
                prm->endChangeGesture();
                safe->announce (t, false);
            });
            break;
        }
        default: break;
    }
}

void EcrEditor::mouseDrag (const juce::MouseEvent& e)
{
    const float scale = (float) kW / (float) getWidth();
    if (auto* prm = paramOf (active, dragShift))
    {
        // sliders follow the mouse sideways, matrix cells are dragged up and down; cmd/ctrl = fine
        const float dx = (float) e.getDistanceFromDragStartX() * scale, dy = (float) -e.getDistanceFromDragStartY() * scale;
        const bool fine = e.mods.isCommandDown() || e.mods.isCtrlDown();
        const float delta = active.type == Hit::Cell ? (dy + dx) / (fine ? 600.0f : 120.0f)
                                                     : (dx + dy) / (fine ? 1200.0f : active.width);
        prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartValue + delta));
        announce (active, dragShift);
        moved = true;
    }
    else if (active.type == Hit::Screen || active.type == Hit::Push)
    {
        selectAccum += (e.position.y - lastDrag.y) * scale;      // drag down = next
        lastDrag = e.position;
        const int steps = (int) (selectAccum / 16.0f);
        if (steps != 0)
        {
            selectAccum -= (float) steps * 16.0f;
            moved = true;
            proc.ui->turnSelect (steps, dragShift);
        }
    }
}

void EcrEditor::mouseUp (const juce::MouseEvent&)
{
    if (auto* prm = paramOf (active, dragShift))
    {
        prm->endChangeGesture();
        if (moved && active.type == Hit::Knob) proc.ui->shiftUsed();
    }
    else if (active.type == Hit::Screen || active.type == Hit::Push)
    {
        if (! moved && ! holdFired) proc.ui->pushSelect (dragShift);
        proc.ui->shiftUsed();
    }
    else if (active.type == Hit::Prev || active.type == Hit::Next)
        proc.ui->shiftUsed();
    active = {};
}

void EcrEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = hitTest (toPage (e.position));
    const bool shift = shiftNow (e.mods);
    if (auto* prm = paramOf (t, shift))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
        announce (t, shift);
    }
}

void EcrEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto t = hitTest (toPage (e.position));
    const bool shift = shiftNow (e.mods);
    const float delta = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
    if (auto* prm = paramOf (t, shift))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, prm->getValue() + delta * 0.15f));
        prm->endChangeGesture();
        announce (t, shift);
    }
    else if (t.type == Hit::LfoShape)
    {
        wheelAccum += delta * 6.0f;
        const int steps = (int) wheelAccum;
        if (steps != 0) { wheelAccum -= (float) steps; stepShape (proc.lfoShapeParam (t.index), -steps); announce (t, false); }
    }
    else if (t.type == Hit::Screen || t.type == Hit::Push || t.type == Hit::Prev || t.type == Hit::Next)
    {
        wheelAccum += delta * 6.0f;
        const int steps = (int) wheelAccum;
        if (steps != 0) { wheelAccum -= (float) steps; proc.ui->turnSelect (-steps, shift); }
    }
}

void EcrEditor::showFolderMenu()
{
    juce::PopupMenu m;
    const auto root = proc.library.userRoot();
    m.addSectionHeader ("response folder");
    m.addItem (1, root.getFullPathName(), false);
    m.addItem (2, "show folder");
    m.addItem (3, "choose folder...");
    m.addItem (4, "rescan");
    juce::Component::SafePointer<EcrEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, root] (int r)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        if (r == 2) { root.createDirectory(); root.revealToUser(); }
        else if (r == 4) self.proc.library.scan();
        else if (r == 3)
        {
            self.chooser = std::make_unique<juce::FileChooser> ("Folder with impulse responses", root);
            self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                       [safe] (const juce::FileChooser& fc)
                                       {
                                           if (safe == nullptr || fc.getResult() == juce::File()) return;
                                           safe->proc.library.setUserRoot (fc.getResult());
                                           safe->proc.library.scan();
                                       });
        }
    });
}
} // namespace ecr
