#include "PluginEditor.h"
#include "BinaryData.h"

namespace ecr
{
namespace
{
    // the colours every browser shipped with
    const juce::Colour pageGrey { 0xffc0c0c0 }, ink { 0xff000000 }, linkBlue { 0xff0000ee }, visited { 0xff551a8b }, activeRed { 0xffff0000 };
    const juce::Colour light { 0xffffffff }, mid { 0xff808080 }, navy { 0xff000080 }, maroon { 0xff800000 };

    struct KnobRow { Knob id; const char* name; const char* shiftName; bool bipolar; };
    const KnobRow rows[8] = {
        { Knob::Width,    "Stereo width", "Pitch",       true  },
        { Knob::Xfade,    "Crossfade",    "Slew time",   false },
        { Knob::Predelay, "Predelay",     "HPF cutoff",  false },
        { Knob::Pos,      "Position",     "Input level", false },
        { Knob::Dry,      "Dry",          nullptr,       false },
        { Knob::Wet,      "Wet",          nullptr,       false },
        { Knob::Attack,   "Attack",       nullptr,       false },
        { Knob::Release,  "Release",      nullptr,       false },
    };

    // Liberation Serif/Sans are built in, so the page looks the same on every system
    juce::Font times (float size, int style = juce::Font::plain)
    {
        static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::LiberationSerifRegular_ttf, BinaryData::LiberationSerifRegular_ttfSize);
        static const juce::Typeface::Ptr bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::LiberationSerifBold_ttf, BinaryData::LiberationSerifBold_ttfSize);
        static const juce::Typeface::Ptr italic  = juce::Typeface::createSystemTypefaceFor (BinaryData::LiberationSerifItalic_ttf, BinaryData::LiberationSerifItalic_ttfSize);
        return juce::Font (juce::FontOptions (style == juce::Font::bold ? bold : (style == juce::Font::italic ? italic : regular)).withHeight (size));
    }
    juce::Font formFont()
    {
        static const juce::Typeface::Ptr sans = juce::Typeface::createSystemTypefaceFor (BinaryData::LiberationSansRegular_ttf, BinaryData::LiberationSansRegular_ttfSize);
        return juce::Font (juce::FontOptions (sans).withHeight (11.0f));
    }

    // <hr>
    void rule (juce::Graphics& g, float x, float y, float w)
    {
        g.setColour (mid);   g.fillRect (x, y, w, 1.0f);
        g.setColour (light); g.fillRect (x, y + 1.0f, w, 1.0f);
    }

    // <table border=N>: raised outer edge, each cell sunken
    void tableFrame (juce::Graphics& g, juce::Rectangle<float> r, float border)
    {
        g.setColour (light); g.fillRect (r.getX(), r.getY(), r.getWidth(), border); g.fillRect (r.getX(), r.getY(), border, r.getHeight());
        g.setColour (mid);   g.fillRect (r.getX(), r.getBottom() - border, r.getWidth(), border); g.fillRect (r.getRight() - border, r.getY(), border, r.getHeight());
    }

    void cell (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (mid);   g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f); g.fillRect (r.getX(), r.getY(), 1.0f, r.getHeight());
        g.setColour (light); g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f); g.fillRect (r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
    }

    // a value shown the old way: a table cell with a background colour
    void valueBar (juce::Graphics& g, juce::Rectangle<float> r, float v, bool bipolar, juce::Colour c = navy)
    {
        r = r.reduced (3.0f, 4.0f);
        if (bipolar)
        {
            const float m = r.getCentreX(), x = r.getX() + v * r.getWidth();
            g.setColour (c); g.fillRect (std::min (m, x), r.getY(), std::abs (x - m), r.getHeight());
            g.setColour (ink); g.fillRect (m, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
        }
        else
        {
            g.setColour (c); g.fillRect (r.withWidth (std::max (1.0f, v * r.getWidth())));
        }
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
// An underlined link; returns the x after it.
float EcrEditor::link (juce::Graphics& g, const juce::String& text, float x, float y, Target t, bool activeLink)
{
    const auto font = times (15.0f, activeLink ? juce::Font::bold : juce::Font::plain);
    const float w = juce::GlyphArrangement::getStringWidth (font, text);
    const bool pressed = active.type == t.type && active.index == t.index && t.type != Hit::None;
    g.setColour (activeLink || pressed ? activeRed : linkBlue);
    g.setFont (font);
    g.drawText (text, juce::Rectangle<float> (x, y, w + 2.0f, 18.0f), juce::Justification::centredLeft, false);
    g.fillRect (x, y + 15.0f, w, 1.0f);
    hits.push_back ({ juce::Rectangle<float> (x - 2.0f, y - 1.0f, w + 4.0f, 20.0f), t });
    return x + w;
}

// What the module shows on its display, set as a little page of its own:
// a chart for the response, a directory listing for the file chooser, a table for the menu.
void EcrEditor::drawScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (light); g.fillRect (r);
    const int fg = proc.foreground.load();
    const auto fgCol = fg == 0 ? linkBlue : visited;
    const float x0 = r.getX() + 6.0f, w = r.getWidth() - 12.0f;
    auto put = [&] (const juce::String& t, float x, float y, float width, float size, int style, juce::Colour c, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (c); g.setFont (times (size, style));
        g.drawText (t, juce::Rectangle<float> (x, y, width, size + 4.0f), j, true);
    };

    if (proc.ui->screen == Ui::Chooser)
    {
        const auto& list = proc.library.entries();
        const int n = (int) list.size(), sel = juce::jlimit (0, juce::jmax (0, n - 1), proc.ui->chooserIndex());
        put ("Index of /" + (n > 0 ? list[(size_t) sel].folder : juce::String()), x0, r.getY() + 4.0f, w - 20.0f, 16.0f, juce::Font::bold, ink);
        put (fg == 0 ? "A" : "B", x0, r.getY() + 4.0f, w, 16.0f, juce::Font::bold, fgCol, juce::Justification::centredRight);
        rule (g, x0, r.getY() + 26.0f, w);
        const int visible = 10, first = juce::jlimit (0, juce::jmax (0, n - visible), sel - 4);
        for (int i = 0; i < visible && first + i < n; ++i)
        {
            const auto& e = list[(size_t) (first + i)];
            const float y = r.getY() + 32.0f + 20.0f * (float) i;
            const auto name = e.name.upToLastOccurrenceOf (".", false, false);
            const auto font = times (14.0f, e.hasPatch ? juce::Font::bold : juce::Font::plain);
            const float tw = std::min (w - 4.0f, juce::GlyphArrangement::getStringWidth (font, name));
            const bool here = first + i == sel;
            if (here) { g.setColour (navy); g.fillRect (x0, y, w, 19.0f); }
            g.setColour (here ? light : linkBlue); g.setFont (font);
            g.drawText (name, juce::Rectangle<float> (x0 + 3.0f, y, w - 6.0f, 18.0f), juce::Justification::centredLeft, true);
            g.fillRect (x0 + 3.0f, y + 15.5f, tw, 1.0f);
        }
        rule (g, x0, r.getBottom() - 22.0f, w);
        put (juce::String (sel + 1) + " of " + juce::String (n) + (first + visible < n ? "  (more below)" : ""), x0, r.getBottom() - 19.0f, w, 12.0f, juce::Font::italic, ink);
    }
    else if (proc.ui->screen == Ui::Menu)
    {
        put ("System menu", x0, r.getY() + 4.0f, w, 16.0f, juce::Font::bold, ink);
        rule (g, x0, r.getY() + 26.0f, w);
        const float ty = r.getY() + 32.0f, rh = 22.0f;
        tableFrame (g, { x0, ty, w, rh * (float) Ui::kNumMenuItems + 4.0f }, 2.0f);
        for (int i = 0; i < Ui::kNumMenuItems; ++i)
        {
            juce::String label, value;
            proc.ui->menuParts (i, label, value);
            const juce::Rectangle<float> a (x0 + 3.0f, ty + 3.0f + rh * (float) i, w * 0.62f, rh - 2.0f), b (a.getRight() + 2.0f, a.getY(), w - 8.0f - a.getWidth(), rh - 2.0f);
            if (i == proc.ui->menuIndex()) { g.setColour (juce::Colour (0xffffff00)); g.fillRect (a); g.fillRect (b); }   // bgcolor=yellow
            cell (g, a); cell (g, b);
            static const char* const nice[Ui::kNumMenuItems] = { "Direct shift", "File chooser", "Auto exit", "CV map", "Aux CV", "Screensaver", "Convolver B", "Autogain", "Exit" };
            label = nice[i];
            put (label, a.getX() + 4.0f, a.getY() + 1.0f, a.getWidth() - 6.0f, 14.0f, juce::Font::plain, ink);
            put (value.toLowerCase(), b.getX(), b.getY() + 1.0f, b.getWidth() - 5.0f, 14.0f, juce::Font::bold, ink, juce::Justification::centredRight);
        }
    }
    else
    {
        auto src = proc.responseSource (fg);
        put (proc.ui->currentMessage(), x0, r.getY() + 3.0f, w - 50.0f, 15.0f, juce::Font::bold, ink);
        put (fg == 0 ? "A" : "B", x0, r.getY() + 3.0f, w, 15.0f, juce::Font::bold, fgCol, juce::Justification::centredRight);
        if (proc.clipping.load()) put ("CLIP", x0, r.getY() + 3.0f, w - 16.0f, 12.0f, juce::Font::bold, activeRed, juce::Justification::centredRight);

        // the chart: one grey silhouette per channel on a ruled white field
        const juce::Rectangle<float> plot (x0, r.getY() + 26.0f, w, r.getHeight() - 52.0f);
        g.setColour (mid); g.drawRect (plot, 1.0f);
        if (src != nullptr && src->length > 0)
        {
            const int chans = src->numChannels >= 4 ? 4 : 2;
            static const char* const chanNames[4] = { "L > L", "R > R", "L > R", "R > L" };
            const float rowH = plot.getHeight() / (float) chans, cw = plot.getWidth() / 128.0f;
            for (int c = 0; c < chans; ++c)
            {
                const auto& ov = src->overview[(size_t) (src->numChannels == 1 ? 0 : c)];
                const float base = plot.getY() + rowH * (float) (c + 1) - 1.0f;
                g.setColour (juce::Colour (0xff808080));
                for (int i = 0; i < 128; ++i)
                {
                    const float h = (float) ov[(size_t) i] / 255.0f * (rowH - 14.0f);
                    if (h > 0.0f) g.fillRect (plot.getX() + cw * (float) i, base - h, cw + 0.3f, h);
                }
                g.setColour (juce::Colour (0xffc0c0c0)); g.fillRect (plot.getX(), base, plot.getWidth(), 1.0f);
                put (chanNames[c], plot.getX() + 3.0f, base - rowH + 2.0f, 60.0f, 10.5f, juce::Font::italic, ink);
            }

            // attack-release envelope
            const double pos = proc.slotParam (fg, kPos)->getValue();
            const double attack = curve::timeSeconds (proc.slotParam (fg, kAttack)->getValue());
            const double relK = proc.slotParam (fg, kRelease)->getValue();
            const double seconds = juce::jmax (1e-3, src->length / src->sampleRate / std::pow (2.0, curve::pitchOctaves (proc.slotParam (fg, kPitch)->getValue())));
            const float xa = plot.getX() + (float) pos * plot.getWidth(), xb = xa + (float) (attack / seconds) * plot.getWidth();
            const float top = plot.getY() + 1.0f, bot = plot.getBottom() - 1.0f;
            juce::Path env;
            env.startNewSubPath (xa, bot); env.lineTo (attack > 0.0 ? xb : xa, top);
            if (curve::releaseIsFull (relK)) env.lineTo (plot.getRight(), top);
            else env.lineTo (xb + (float) (curve::timeSeconds (relK) / seconds) * plot.getWidth(), bot);
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (linkBlue);
            g.strokePath (env, juce::PathStrokeType (1.5f));
            g.fillRect (xa - 0.5f, top, 1.0f, bot - top);
        }
        if (proc.isRecording() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0)
            put ("RECORDING", plot.getX(), plot.getCentreY() - 10.0f, plot.getWidth(), 18.0f, juce::Font::bold, activeRed, juce::Justification::centred);

        const juce::String name = proc.isLoading (fg) ? juce::String ("Loading...") : (src != nullptr ? src->name : juce::String ("No response"));
        put (name, x0, r.getBottom() - 22.0f, w, 13.0f, juce::Font::italic, ink);
    }

    // messages arrive the way they did on the web: in a grey alert box
    const auto popup = proc.ui->currentPopup();
    if (popup.isNotEmpty())
    {
        const auto lines = juce::StringArray::fromLines (popup);
        const juce::Rectangle<float> box (r.getCentreX() - 90.0f, r.getCentreY() - 12.0f - 9.0f * (float) lines.size(), 180.0f, 24.0f + 18.0f * (float) lines.size());
        g.setColour (pageGrey); g.fillRect (box);
        tableFrame (g, box, 2.0f);
        g.setColour (ink); g.drawRect (box.expanded (1.0f), 1.0f);
        for (int i = 0; i < lines.size(); ++i)
            put (lines[i].substring (0, 1) + lines[i].substring (1).toLowerCase(), box.getX(), box.getY() + 10.0f + 18.0f * (float) i, box.getWidth(), 14.0f, juce::Font::bold, ink, juce::Justification::centred);
    }
}

// One LFO as a figure: the attractor's orbit, the flock, or a strip chart.
void EcrEditor::drawLfoFigure (juce::Graphics& g, juce::Rectangle<float> r, int l)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.toNearestInt());
    const auto& t = trails[l];
    g.setColour (light); g.fillRect (r);
    g.setColour (mid); g.drawRect (r, 1.0f);
    g.setColour (ink); g.setFont (times (11.5f, juce::Font::bold));
    g.drawText ("LFO " + juce::String (l + 1), r.reduced (4.0f, 2.0f), juce::Justification::topLeft, false);
    g.setFont (times (11.5f, juce::Font::italic));
    g.drawText (juce::String (lfoShapeName (std::max (0, t.shape))).toLowerCase(), r.reduced (4.0f, 2.0f), juce::Justification::topRight, false);

    const auto plot = r.reduced (6.0f).withTrimmedTop (12.0f).withTrimmedBottom (8.0f);
    auto map = [&] (juce::Point<float> p) { return juce::Point<float> (plot.getCentreX() + p.x * plot.getWidth() * 0.48f, plot.getCentreY() + p.y * plot.getHeight() * 0.48f); };

    if (t.shape == kLorenz || t.shape == kRossler)
    {
        if (t.path.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath (map (t.path.front()));
            for (size_t i = 1; i < t.path.size(); ++i) p.lineTo (map (t.path[i]));
            g.setColour (linkBlue);
            g.strokePath (p, juce::PathStrokeType (1.0f));
            const auto now = map (t.path.back());
            g.setColour (activeRed); g.fillRect (now.x - 2.0f, now.y - 2.0f, 4.0f, 4.0f);
        }
    }
    else if (t.shape == kBoid)
    {
        if (t.path.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath (map (t.path.front()));
            for (size_t i = 1; i < t.path.size(); ++i) p.lineTo (map (t.path[i]));
            g.setColour (juce::Colour (0xffc0c0c0));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        for (int i = (Lfo::kViewSize - 3) / 2 - 1; i >= 0; --i)
        {
            const auto b = map ({ t.view[3 + 2 * i], -t.view[4 + 2 * i] });
            g.setColour (i == 0 ? activeRed : navy);
            g.fillRect (b.x - (i == 0 ? 2.5f : 2.0f), b.y - (i == 0 ? 2.5f : 2.0f), i == 0 ? 5.0f : 4.0f, i == 0 ? 5.0f : 4.0f);
        }
    }
    else if (t.values.size() > 1)
    {
        g.setColour (juce::Colour (0xffc0c0c0)); g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
        juce::Path p;
        const float dx = plot.getWidth() / 109.0f, x0 = plot.getRight() - dx * (float) (t.values.size() - 1);
        for (size_t i = 0; i < t.values.size(); ++i)
        {
            const juce::Point<float> pt (x0 + dx * (float) i, plot.getCentreY() - t.values[i] * plot.getHeight() * 0.48f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (linkBlue);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    // where the output is right now, -1 .. +1
    const float v = t.values.empty() ? 0.0f : t.values.back();
    const auto meter = juce::Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 7.0f, r.getWidth() - 12.0f, 3.0f);
    g.setColour (juce::Colour (0xffc0c0c0)); g.fillRect (meter);
    g.setColour (activeRed); g.fillRect (meter.getCentreX() + v * meter.getWidth() * 0.5f - 1.5f, meter.getY() - 1.0f, 3.0f, 5.0f);
}

void EcrEditor::drawPage (juce::Graphics& g)
{
    hits.clear();
    g.fillAll (pageGrey);
    const bool shift = shiftNow (juce::ModifierKeys::getCurrentModifiers());
    const int fg = proc.foreground.load();
    auto plain = [&] (const juce::String& t, float x, float y, float size = 15.0f, int style = juce::Font::plain, juce::Colour c = ink)
    {
        const auto font = times (size, style);
        g.setColour (c); g.setFont (font);
        g.drawText (t, juce::Rectangle<float> (x, y, 400.0f, size + 3.0f), juce::Justification::centredLeft, false);
        return x + juce::GlyphArrangement::getStringWidth (font, t);
    };

    // <h1>
    g.setColour (ink);
    g.setFont (times (27.0f, juce::Font::bold));
    g.drawText ("ConvoSolution", juce::Rectangle<float> (0.0f, 4.0f, (float) kW, 30.0f), juce::Justification::centred, false);
    rule (g, 8.0f, 38.0f, (float) kW - 16.0f);

    // <img border=2>: the module's screen
    const juce::Rectangle<float> img (12.0f, 50.0f, 256.0f, 256.0f);
    tableFrame (g, img.expanded (3.0f), 3.0f);
    const bool figures = tab == 1 && proc.ui->screen == Ui::Main;
    if (figures)
    {
        // modulation page: the LFOs get the picture, the response shrinks to a thumbnail
        g.setColour (pageGrey); g.fillRect (img);
        const float half = (img.getWidth() - 4.0f) * 0.5f;
        for (int l = 0; l < kNumLfos; ++l)
            drawLfoFigure (g, { img.getX() + (float) (l % 2) * (half + 4.0f), img.getY() + (float) (l / 2) * (half + 4.0f), half, half }, l);
        const juce::Rectangle<float> thumb (img.getX() + half + 4.0f, img.getY() + half + 4.0f, half, half);
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (half / img.getWidth()).translated (thumb.getX(), thumb.getY()));
        drawScreen (g, img.withPosition (0.0f, 0.0f));
    }
    else
        drawScreen (g, img);
    hits.push_back ({ img, { Hit::Screen } });
    plain (figures ? "Figs. 1-4: the LFOs at work, and the response (drag to browse)"
                   : "Fig. 1: drag or scroll to browse, click to switch A/B", 12.0f, 312.0f, 11.5f, juce::Font::italic);

    // links
    const float cx = 286.0f;
    float x = cx;
    const bool blink = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    const char* const names[4] = { "Shift", "Record", "Store", "Play" };
    const bool lit[4] = { proc.ui->shiftLed(), proc.isRecording(), proc.ui->storeLed(), proc.ui->playLed() };
    x = plain ("[ ", x, 46.0f);
    for (int i = 0; i < 4; ++i)
    {
        if (i == 1 && lit[1] && ! blink) x += juce::GlyphArrangement::getStringWidth (times (15.0f, juce::Font::bold), names[i]);   // <blink>
        else x = link (g, names[i], x, 46.0f, { Hit::Button, i }, lit[i]);
        x = plain (i < 3 ? " | " : " ]", x, 46.0f);
    }
    x = link (g, "<< Prev", cx, 66.0f, { Hit::Prev }, false);
    x = plain (" | ", x, 66.0f);
    x = link (g, "Next >>", x, 66.0f, { Hit::Next }, false);
    x = plain (" | ", x, 66.0f);
    x = link (g, juce::String ("Select ") + (fg == 0 ? "A" : "B"), x, 66.0f, { Hit::Push }, false);
    x = plain (" | ", x, 66.0f);
    link (g, "Folder...", x, 66.0f, { Hit::Folder }, false);

    // "tabs": the current page is plain bold text, the other one a link
    x = cx;
    for (int i = 0; i < 2; ++i)
    {
        const juce::String name (i == 0 ? "Knobs" : "Modulation");
        if (i == tab) x = plain (name, x, 88.0f, 15.0f, juce::Font::bold);
        else x = link (g, name, x, 88.0f, { Hit::Tab, i }, false);
        if (i == 0) x = plain (" | ", x, 88.0f);
    }

    const float tx = cx, tw = 262.0f, ty = 110.0f;
    if (tab == 0)
    {
        const float rh = 23.0f, c1 = 86.0f, c2 = 104.0f;
        const juce::Rectangle<float> table (tx, ty, tw, rh * 8.0f + 4.0f);
        tableFrame (g, table, 2.0f);
        for (int i = 0; i < 8; ++i)
        {
            const auto& r = rows[i];
            const bool alt = shift && r.shiftName != nullptr;
            const float y = ty + 2.0f + rh * (float) i;
            const juce::Rectangle<float> a (tx + 3.0f, y + 1.0f, c1 - 2.0f, rh - 2.0f), b (tx + 2.0f + c1 + 1.0f, y + 1.0f, c2 - 2.0f, rh - 2.0f),
                c (tx + 2.0f + c1 + c2 + 1.0f, y + 1.0f, tw - 4.0f - c1 - c2 - 2.0f, rh - 2.0f);
            cell (g, a); cell (g, b); cell (g, c);
            auto* prm = proc.ui->paramFor (r.id, shift);
            plain (alt ? r.shiftName : r.name, a.getX() + 4.0f, a.getY() + 1.0f, 14.0f, alt ? juce::Font::italic : juce::Font::plain,
                   shift && ! alt ? mid : (alt ? visited : ink));
            valueBar (g, b, prm->getValue(), r.bipolar);
            g.setColour (ink); g.setFont (times (13.0f));
            g.drawText (prm->getCurrentValueAsText(), c.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
            Target t { Hit::Knob, i }; t.width = b.getWidth() - 6.0f;
            hits.push_back ({ juce::Rectangle<float> (tx, y, tw, rh), t });
        }
    }
    else
    {
        // LFOs: <select> for the shape, rate, and what it is doing right now
        const float rh = 22.0f;
        const juce::Rectangle<float> table (tx, ty, tw, rh * 3.0f + 4.0f);
        tableFrame (g, table, 2.0f);
        for (int l = 0; l < kNumLfos; ++l)
        {
            const float y = ty + 2.0f + rh * (float) l;
            const juce::Rectangle<float> a (tx + 3.0f, y + 1.0f, 40.0f, rh - 2.0f), b (tx + 45.0f, y + 1.0f, 76.0f, rh - 2.0f),
                c (tx + 123.0f, y + 1.0f, 70.0f, rh - 2.0f), d (tx + 195.0f, y + 1.0f, tw - 198.0f, rh - 2.0f);
            for (auto r : { a, b, c, d }) cell (g, r);
            plain ("LFO " + juce::String (l + 1), a.getX() + 3.0f, a.getY() + 1.0f, 14.0f);

            // the drop-down
            const auto sel = b.reduced (3.0f, 2.0f);
            g.setColour (light); g.fillRect (sel);
            g.setColour (mid); g.drawRect (sel, 1.0f);
            const auto arrowBox = sel.withLeft (sel.getRight() - 14.0f).reduced (1.0f);
            g.setColour (pageGrey); g.fillRect (arrowBox);
            g.setColour (light); g.fillRect (arrowBox.getX(), arrowBox.getY(), arrowBox.getWidth(), 1.0f); g.fillRect (arrowBox.getX(), arrowBox.getY(), 1.0f, arrowBox.getHeight());
            g.setColour (mid); g.fillRect (arrowBox.getX(), arrowBox.getBottom() - 1.0f, arrowBox.getWidth(), 1.0f); g.fillRect (arrowBox.getRight() - 1.0f, arrowBox.getY(), 1.0f, arrowBox.getHeight());
            juce::Path tri; tri.addTriangle (arrowBox.getCentreX() - 3.5f, arrowBox.getCentreY() - 2.0f, arrowBox.getCentreX() + 3.5f, arrowBox.getCentreY() - 2.0f, arrowBox.getCentreX(), arrowBox.getCentreY() + 2.5f);
            g.setColour (ink); g.fillPath (tri);
            g.setFont (formFont());
            const int shape = juce::roundToInt (proc.lfoShapeParam (l)->convertFrom0to1 (proc.lfoShapeParam (l)->getValue()));
            g.drawText (juce::String (lfoShapeName (shape)).toLowerCase(), sel.withTrimmedLeft (4.0f).withTrimmedRight (14.0f), juce::Justification::centredLeft, false);
            hits.push_back ({ b, { Hit::LfoShape, l } });

            valueBar (g, c, proc.lfoRateParam (l)->getValue(), false);
            Target t { Hit::LfoRate, l }; t.width = c.getWidth() - 6.0f;
            hits.push_back ({ c, t });
            valueBar (g, d, proc.lfoValue (l) * 0.5f + 0.5f, true, activeRed);
        }

        // the matrix
        const float my = ty + rh * 3.0f + 12.0f, hh = 16.0f, mh = 23.0f, lw = 26.0f, cw = (tw - 4.0f - lw) / (float) kNumModDests;
        tableFrame (g, { tx, my, tw, hh + mh * 3.0f + 4.0f }, 2.0f);
        for (int d = 0; d < kNumModDests; ++d)
        {
            const juce::Rectangle<float> h (tx + 2.0f + lw + cw * (float) d, my + 2.0f, cw, hh);
            cell (g, h.reduced (0.5f));
            g.setColour (active.type == Hit::Cell && active.sub == d ? activeRed : ink);
            g.setFont (times (10.5f, juce::Font::bold));
            g.drawText (juce::String (EcrProcessor::modDestShort (d)).toLowerCase(), h, juce::Justification::centred, false);
        }
        for (int l = 0; l < kNumLfos; ++l)
        {
            const float y = my + 2.0f + hh + mh * (float) l;
            const juce::Rectangle<float> lab (tx + 2.0f, y, lw, mh);
            cell (g, lab.reduced (0.5f));
            g.setColour (ink); g.setFont (times (12.0f, juce::Font::bold));
            g.drawText ("L" + juce::String (l + 1), lab, juce::Justification::centred, false);
            for (int d = 0; d < kNumModDests; ++d)
            {
                const juce::Rectangle<float> r (tx + 2.0f + lw + cw * (float) d, y, cw, mh);
                cell (g, r.reduced (0.5f));
                const auto* prm = proc.modAmountParam (l, d);
                const float v = prm->convertFrom0to1 (prm->getValue());
                if (std::abs (v) > 0.004f)
                {
                    // amount as a coloured cell: grows up for positive, down for negative
                    const auto in = r.reduced (3.0f);
                    const float half = in.getHeight() * 0.5f, len = std::max (1.5f, std::abs (v) * half);
                    g.setColour (v > 0 ? navy : maroon);
                    g.fillRect (in.getX(), v > 0 ? in.getCentreY() - len : in.getCentreY(), in.getWidth(), len);
                }
                else
                {
                    g.setColour (mid); g.setFont (times (11.0f));
                    g.drawText ("-", r, juce::Justification::centred, false);
                }
                hits.push_back ({ r, { Hit::Cell, l, d } });
            }
        }
        plain ("Drag a cell up or down. Double-click to clear.", tx, my + hh + mh * 3.0f + 7.0f, 11.5f, juce::Font::italic);
    }

    // footer
    rule (g, 8.0f, 330.0f, (float) kW - 16.0f);
    x = 12.0f;
    for (int s = 0; s < 2; ++s)
    {
        auto src = proc.responseSource (s);
        const juce::String name = src != nullptr ? src->name.upToLastOccurrenceOf (".", false, false) : juce::String ("(none)");
        x = plain (juce::String (s == 0 ? "A: " : "B: "), x, 335.0f, 13.0f, juce::Font::bold);
        x = plain (name.substring (0, 30), x, 335.0f, 13.0f, juce::Font::plain, s == fg ? linkBlue : visited) + 16.0f;
    }
    g.setColour (ink); g.setFont (times (11.0f, juce::Font::italic));
    g.drawText ("This page is best viewed at 800x600.", juce::Rectangle<float> (0.0f, 352.0f, (float) kW, 14.0f), juce::Justification::centred, false);
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
            // a real drop-down list
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
        // bars follow the mouse sideways, matrix cells are dragged up and down; cmd/ctrl = fine
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
    m.addSectionHeader ("Response folder");
    m.addItem (1, root.getFullPathName(), false);
    m.addItem (2, "Show folder");
    m.addItem (3, "Choose folder...");
    m.addItem (4, "Rescan");
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
