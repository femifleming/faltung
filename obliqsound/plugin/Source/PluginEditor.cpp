#include "PluginEditor.h"
#include "Help.h"
#include "../../../Source/Ui.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using juce::String; using juce::Colour; using juce::Rectangle; using juce::Point;
using fal::ui::skewRect; using fal::ui::sans; using fal::ui::mono;

namespace
{
    enum Dyn { dNone, dWave, dSampLabel, dLcd, dL1, dL2, dFol, dOut, dVLed, dSubLed, dBoid, dLz, dSpd, dLesA, dLesB, dSpMode, dLesAn, dLesBn, dSpec, dRadar };
    const char* kKeys = "awsedftgyhujkolp;'";
    // the Fold palette (Source/Art.cpp): the pastels of the folded paper strips
    const juce::uint32 kPalette[8] = { 0xff5e8f6a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xfff6c590, 0xff97adc4, 0xffcfe6f0 };
    const int kTabColour[7] = { 2, 5, 1, 3, 7, 4, 6 };            // polygon odessa lester lfo matrix swarm space
    const Colour kStripInk (0xff1e2a20), kNegative (0xffee8e6e), kField (0xffee8e6e), kAmber (0xfff6c590);

    String lower (const String& s) { return s.toLowerCase(); }
    String modeName (float m)
    {
        const float k = std::round (m * 2.0f) / 2.0f;
        if (k < 0.25f) return "lp"; if (k < 0.75f) return "lp>bp"; if (k < 1.25f) return "bp"; if (k < 1.75f) return "bp>hp";
        if (k < 2.25f) return "hp"; if (k < 2.75f) return "hp>notch"; if (k < 3.25f) return "notch"; if (k < 3.75f) return "notch>comb";
        return "comb";
    }
    // Ge'ez (Ethiopic) numerals, as on the sampler squares
    String geez (int n)
    {
        String s;
        if (n >= 10) s += String::charToString ((juce::juce_wchar) (0x1371 + n / 10));
        if (n % 10) s += String::charToString ((juce::juce_wchar) (0x1368 + n % 10));
        return s;
    }
    String pageTitle (const String& container)
    {
        static const std::pair<const char*, const char*> names[] = { { "polypanel", "polygon" }, { "odepanel", "odessa" }, { "lestpanel", "lester" },
            { "motpanel", "lfo" }, { "matpanel", "matrix" }, { "swarmpanel", "swarm" }, { "spacepanel", "space" } };
        for (auto& n : names) if (container == n.first) return n.second;
        return lower (container.upToFirstOccurrenceOf ("panel", false, false));
    }
    String valueText (const ParamDef& d, float v) { return d.inc >= 1.0f ? String ((int) std::lrint (v)) : String (v, 3); }
    void chevron (juce::Graphics& g, float x, float y)   // a small "v"
    {
        juce::Path p; p.startNewSubPath (x, y); p.lineTo (x + 2.5f, y + 2.5f); p.lineTo (x + 5.0f, y);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
}

// =================================================================================================== build
ObliqPanel::ObliqPanel (ObliqProcessor& p) : proc (p), params (p.params)
{
   #if OBLIQ_SONIC
    ground = Colour (0xff1f3a2c); accent = Colour (0xffc8d6a0);
   #else
    ground = Colour (0xffc8d6a0); accent = Colour (0xff1f3a2c);
   #endif
    ink = fal::ui::inkFor (ground); edge = fal::ui::edgeFor (ground);
    build();
    geezSheet = juce::ImageFileFormat::loadFrom (ObliqProcessor::resourceDir().getChildFile ("geez.png"));
    if (geezSheet.isValid() && (geezSheet.getWidth() != 64 || geezSheet.getHeight() != 24 * 26)) geezSheet = {};
    setOpaque (true);
    setWantsKeyboardFocus (true);
    lfoH[0].assign (256, 0.0f); lfoH[1].assign (256, 0.0f);
    startTimerHz (30);
}

ObliqPanel::~ObliqPanel() { stopTimer(); for (auto& k : heldKeys) proc.keyboard.noteOff (1, k.second, 0.0f); }

void ObliqPanel::build()
{
    const auto& lay = proc.layout;
    if (auto* sz = lay["size"].getArray()) { baseW = (int) (*sz)[0]; baseH = (int) (*sz)[1]; }
    auto* ws = lay["widgets"].getArray();
    if (ws == nullptr) return;
    for (auto& w : *ws)
    {
        const String type = w["type"].toString(), ch = w["channel"].toString();
        auto* b = w["bounds"].getArray(); if (b == nullptr) continue;
        Rectangle<int> r ((int) (*b)[0], (int) (*b)[1], (int) (*b)[2], (int) (*b)[3]);
        if (ch == "image6") continue;                                              // the old bitmap background
        if ((bool) w["container"])
        {
            pageNames.add (ch); pageBounds.push_back (r); pageStart.push_back ((int) items.size());
            pageBoard.push_back (w["file"].toString().isNotEmpty());              // a board covers the main panel (the matrix)
            continue;
        }
        Item it; it.channel = ch; it.popup = w["popup"].toString(); it.group = w["group"].toString();
        it.page = pageNames.indexOf (w["page"].toString());
        if (it.page >= 0) r = r.translated (pageBounds[(size_t) it.page].getX(), pageBounds[(size_t) it.page].getY());
        it.r = r;
        it.param = params.find (ch);
        if (w.hasProperty ("alpha")) it.alpha = (float) (double) w["alpha"];
        if (w.hasProperty ("visible")) it.visible = (int) w["visible"] != 0;
        if (auto* ink_ = w["ink"].getArray()) it.section = (int) (*ink_)[0] == 30 && (int) (*ink_)[1] == 60;   // the section headings
        if (w["align"].toString() == "left") it.align = 1;
        if (auto* t = w["text"].getArray()) for (auto& s : *t) it.text.add (s.toString());
        if (auto* sg = w["signals"].getArray()) for (auto& s : *sg) it.signals.add (s.toString());
        if (type == "image") it.type = Image;
        else if (type == "rslider") { it.type = Knob; it.pin = it.popup.isNotEmpty() && ch.startsWithChar ('p') && ch.containsChar ('_'); }
        else if (type == "vslider") it.type = VSlider;
        else if (type == "nslider") it.type = NSlider;
        else if (type == "checkbox") { it.type = Check; it.sampler = it.group == "100"; it.tab = it.group == "200"; }
        else if (type == "combobox")
        {
            if ((bool) w["presets"]) it.type = PresetBox;
            else if (w["mode"].toString() == "resize") { it.type = SizeBox; it.text = { "50%", "75%", "100%", "125%", "150%", "200%" }; }
            else it.type = Combo;
        }
        else if (type == "button") it.type = Button;
        else if (type == "filebutton") it.type = w["mode"].toString().contains ("remove") ? PresetRemove : PresetSave;
        else if (type == "label") it.type = Label;
        else if (type == "signaldisplay") it.type = Scope;
        else continue;
        // engine-driven elements
        auto num = [&] (const char* pre) { return ch.startsWith (pre) && ch.substring ((int) std::strlen (pre)).containsOnly ("0123456789") && ch.length() > (int) std::strlen (pre); };
        auto idx = [&] (const char* pre) { return ch.substring ((int) std::strlen (pre)).getIntValue(); };
        if (num ("Swave")) { it.dyn = dWave; it.dynIndex = idx ("Swave"); }
        else if (num ("sl")) { it.dyn = dSampLabel; it.dynIndex = idx ("sl"); }
        else if (ch == "lcdText") it.dyn = dLcd;
        else if (ch == "l1Led") it.dyn = dL1; else if (ch == "l2Led") it.dyn = dL2;
        else if (ch == "fLed") it.dyn = dFol; else if (ch == "oLed") it.dyn = dOut;
        else if (ch == "vLed") it.dyn = dVLed; else if (ch == "subLed") it.dyn = dSubLed;
        else if (num ("bd")) { it.dyn = dBoid; it.dynIndex = idx ("bd") - 1; }
        else if (num ("lz")) { it.dyn = dLz; it.dynIndex = idx ("lz"); }
        else if (num ("spd")) { it.dyn = dSpd; it.dynIndex = idx ("spd"); }
        else if (num ("sp") && it.type == Image) it.dyn = dSpec;
        else if (ch == "radar") it.dyn = dRadar;
        else if (ch == "lesA") it.dyn = dLesA; else if (ch == "lesB") it.dyn = dLesB;
        else if (ch == "lesAn") it.dyn = dLesAn; else if (ch == "lesBn") it.dyn = dLesBn;
        else if (ch == "spModeTxt") it.dyn = dSpMode;
        if (it.tab) { tabParams.push_back (it.param); tabItems.push_back ((int) items.size()); }
        items.push_back (it);
    }
    // the Help button takes the right end of the preset list, so the header stays mirrored
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].type == PresetBox)
        {
            Item h; h.type = Help; h.channel = "help"; h.text = { "help" };
            h.r = items[i].r.removeFromRight (56).withTrimmedLeft (4);
            items.push_back (h);
            break;
        }
    // the page tabs become folded strips: a strip takes the place of the light and its name under it
    const int nTabs = (int) tabItems.size();
    const float len = nTabs > 6 ? 62.0f : 76.0f, depth = nTabs > 6 ? 20.0f : 22.0f;
    for (int i = 0; i < nTabs; ++i)
    {
        auto& t = items[(size_t) tabItems[(size_t) i]];
        const float lean = depth * 0.62f, h = depth * 0.78f, cx = (float) t.r.getCentreX(), bottom = (float) t.r.getY() + 20.0f;
        t.r = Rectangle<float> (cx - (len + lean) * 0.5f, bottom - h, len + lean, h).getSmallestIntegerContainer();
        t.dynIndex = (int) depth;
        for (auto& l : items)
            if (l.type == Label && l.page < 0 && std::abs (l.r.getCentreX() - t.r.getCentreX()) < 40 && l.r.getY() > t.r.getY() && l.r.getY() < t.r.getBottom() + 14)
                l.visible = false;
    }
    // every control's name, from the label under it (for the read-out while dragging)
    for (auto& it : items)
        if ((it.type == Knob || it.type == VSlider) && ! it.pin)
            for (auto& l : items)
                if (l.type == Label && l.page == it.page && l.dyn == dNone && ! l.text.isEmpty()
                    && std::abs (l.r.getCentreX() - it.r.getCentreX()) <= 14 && l.r.getY() >= it.r.getBottom() - 4 && l.r.getY() <= it.r.getBottom() + 12)
                { it.name = lower (l.text[0]); break; }
}

String ObliqPanel::pageName (int i) const { return i >= 0 && i < pageNames.size() ? pageTitle (pageNames[i]) : String(); }

void ObliqPanel::showPage (int p)
{
    if (p < 0 || p >= pageNames.size()) return;
    page = p; pending = -1; fade = 0; hoverItem = -1; dragItem = -1;
    for (size_t i = 0; i < tabParams.size(); ++i) params.set (tabParams[i], (int) i == p ? 1.0f : 0.0f);
    repaint();
}

void ObliqPanel::showHelp (bool on) { helpOpen = on; hoverItem = -1; repaint(); }

Rectangle<float> ObliqPanel::tabFace (int i) const { return items[(size_t) tabItems[(size_t) i]].r.toFloat(); }

bool ObliqPanel::covered (int i) const
{
    const auto& it = items[(size_t) i];
    if (page < 0 || page >= (int) pageBoard.size()) return false;
    return it.page < 0 && pageBoard[(size_t) page] && i < pageStart[(size_t) page] && pageBounds[(size_t) page].intersects (it.r);
}

// =================================================================================================== timer: pages, meters
void ObliqPanel::changePage (int to) { pending = to; }

void ObliqPanel::timerCallback()
{
    ++tickCount;
    // which tab is on (a tab can also be switched from the host or a preset)
    int want = pending >= 0 ? pending : page;
    for (size_t i = 0; i < tabParams.size(); ++i) if ((int) i != want && params.get (tabParams[i]) > 0.5f) { want = (int) i; break; }
    for (size_t i = 0; i < tabParams.size(); ++i) { const float on = (int) i == want ? 1.0f : 0.0f; if (params.get (tabParams[i]) != on) params.set (tabParams[i], on); }
    if (want != page) { page = want; fade = kFade; hoverItem = -1; dragItem = -1; }
    pending = -1;
    if (fade > 0) --fade;

    // histories for the scopes and the Lorenz trail
    auto& v = proc.engine->view;
    for (int l = 0; l < 2; ++l) { lfoH[l].erase (lfoH[l].begin()); lfoH[l].push_back (l == 0 ? v.lfo1.load() : v.lfo2.load()); }
    if (tickCount % 3 == 0) { for (int i = 23; i > 0; --i) { lzx[(size_t) i] = lzx[(size_t) i - 1]; lzy[(size_t) i] = lzy[(size_t) i - 1]; } lzx[0] = v.lorX; lzy[0] = -v.lorZ.load(); }
    if (proc.presetVersion.load() != lastPresetVersion) { lastPresetVersion = proc.presetVersion.load(); layerChange = -1; }
   #if JucePlugin_Build_Standalone
    // the desktop app listens to every MIDI input, including ones plugged in later
    if (tickCount % 60 == 1)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            for (auto& d : juce::MidiInput::getAvailableDevices())
                if (! holder->deviceManager.isMidiInputDeviceEnabled (d.identifier)) holder->deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
   #endif
    repaint();
}

// =================================================================================================== the controls
void ObliqPanel::drawSlider (juce::Graphics& g, Rectangle<float> r, int param, bool hot)
{
    if (param < 0) return;
    const auto& d = params.def (param);
    const float n = juce::jlimit (0.0f, 1.0f, params.toNorm (param, params.get (param)));
    const bool bipolar = d.lo < 0.0f && d.hi > 0.0f;
    const float zero = bipolar ? juce::jlimit (0.0f, 1.0f, params.toNorm (param, 0.0f)) : 0.0f;
    const float tw = r.getWidth() >= 56.0f ? r.getWidth() * 0.7f : r.getWidth() - 6.0f;
    const float tx = r.getCentreX() - tw * 0.5f, cy = r.getCentreY();
    g.setColour (ink.withAlpha (0.35f));
    g.fillRect (tx, cy, tw, 1.0f);
    const float a = tx + tw * std::min (zero, n), e = tx + tw * std::max (zero, n);
    if (e - a > 0.5f) { g.setColour (accent.withAlpha (hot ? 1.0f : 0.92f)); g.fillPath (skewRect ({ a, cy - 4.5f, e - a, 9.0f })); }
    g.setColour (edge);
    g.fillRect (tx + tw * n - 1.0f, cy - 5.5f, 2.0f, 11.0f);
}

void ObliqPanel::drawVSlider (juce::Graphics& g, Rectangle<float> r, int param, bool hot)
{
    if (param < 0) return;
    const auto& d = params.def (param);
    const float n = juce::jlimit (0.0f, 1.0f, params.toNorm (param, params.get (param)));
    const bool bipolar = d.lo < 0.0f && d.hi > 0.0f;
    const float zero = bipolar ? juce::jlimit (0.0f, 1.0f, params.toNorm (param, 0.0f)) : 0.0f;
    const float cx = r.getCentreX(), top = r.getY() + 5.0f, bot = r.getBottom() - 5.0f, th = bot - top;
    g.setColour (ink.withAlpha (0.35f));
    g.fillRect (cx - 0.5f, top, 1.0f, th);
    const float y0 = bot - th * std::min (zero, n), y1 = bot - th * std::max (zero, n);
    if (y0 - y1 > 0.5f)
    {
        const float w = 9.0f, lean = 5.0f;         // a parallelogram leaning right, the house shape stood up
        juce::Path p;
        p.startNewSubPath (cx - w * 0.5f - lean * 0.5f, y0); p.lineTo (cx + w * 0.5f - lean * 0.5f, y0);
        p.lineTo (cx + w * 0.5f + lean * 0.5f, y1); p.lineTo (cx - w * 0.5f + lean * 0.5f, y1); p.closeSubPath();
        g.setColour (accent.withAlpha (hot ? 1.0f : 0.92f)); g.fillPath (p);
    }
    g.setColour (edge);
    g.fillRect (cx - 5.5f, bot - th * n - 1.0f, 11.0f, 2.0f);
}

// one Ge'ez numeral (1..26) from the strip of masks, in the current colour, h high and centred on (cx, cy)
bool ObliqPanel::drawGeez (juce::Graphics& g, int n, float cx, float cy, float h) const
{
    if (! geezSheet.isValid() || n < 1 || n > 26) return false;
    const float w = h * 64.0f / 24.0f;
    g.drawImage (geezSheet, juce::roundToInt (cx - w * 0.5f), juce::roundToInt (cy - h * 0.5f), juce::roundToInt (w), juce::roundToInt (h),
                 0, 24 * (n - 1), 64, 24, true);
    return true;
}

void ObliqPanel::drawPin (juce::Graphics& g, Rectangle<float> r, float amount)
{
    const Rectangle<float> c (r.getX() + 1.5f, r.getY() + 1.0f, r.getWidth() - 3.0f, r.getHeight() - 2.0f);
    const float skew = 0.25f, d = c.getHeight() * skew;
    if (std::abs (amount) >= 0.005f)
    {   // the filled part of the chip: up from the centre for a positive pin, down for an inverted one
        const float cy = c.getCentreY(), half = c.getHeight() * 0.5f;
        const float len = std::max (2.0f, half * std::abs (amount));          // (a small amount still shows as a band)
        const float ya = amount > 0 ? cy - len : cy, yb = amount > 0 ? cy : cy + len;
        auto off = [&] (float y) { return (c.getBottom() - y) / c.getHeight() * d; };
        juce::Path p;
        p.startNewSubPath (c.getX() + off (yb), yb); p.lineTo (c.getRight() + off (yb), yb);
        p.lineTo (c.getRight() + off (ya), ya); p.lineTo (c.getX() + off (ya), ya); p.closeSubPath();
        g.setColour (amount > 0 ? accent : kNegative); g.fillPath (p);
    }
    g.setColour (ink.withAlpha (std::abs (amount) >= 0.005f ? 0.5f : 0.3f));
    g.strokePath (skewRect (c, skew), juce::PathStrokeType (1.0f));
}

void ObliqPanel::drawItem (juce::Graphics& g, const Item& it)
{
    const auto r = it.r.toFloat();
    auto& v = proc.engine->view;
    switch (it.type)
    {
        case Image:
        {
            if (it.dyn == dWave)
            {   // the waveform picture beside the WAVE knob: a thin line drawing of the chosen wave
                const int wv = (int) std::lrint (params.get ("waveform"));
                const bool vis = (it.dynIndex == 1 && wv == 4) || (it.dynIndex == 2 && wv == 3) || (it.dynIndex == 3 && wv == 2) || (it.dynIndex == 4 && wv == 1);
                if (! vis) break;
                const float x = r.getX() + 1.0f, w = r.getWidth() - 2.0f, top = r.getY() + 1.0f, bot = r.getBottom() - 1.0f, cy = r.getCentreY();
                juce::Path p;
                if (it.dynIndex == 1) { for (int i = 0; i <= 24; ++i) { const float t = (float) i / 24.0f; const float px = x + w * t, py = cy - std::sin (t * juce::MathConstants<float>::twoPi) * (bot - top) * 0.5f; if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py); } }
                else if (it.dynIndex == 2) { p.startNewSubPath (x, cy); p.lineTo (x + w * 0.5f, top); p.lineTo (x + w * 0.5f, bot); p.lineTo (x + w, cy); }
                else if (it.dynIndex == 3) { p.startNewSubPath (x, bot); p.lineTo (x, top); p.lineTo (x + w * 0.5f, top); p.lineTo (x + w * 0.5f, bot); p.lineTo (x + w, bot); p.lineTo (x + w, top); }
                else { p.startNewSubPath (x, bot); p.lineTo (x + w * 0.5f, top); p.lineTo (x + w, bot); }
                g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            else if (it.dyn == dSampLabel)
            {   // the Ge'ez number of a sampler square: the instrument's own numeral art (geez.png), the same on every system
                g.setColour (ink.withAlpha (v.sampler.load() == it.dynIndex ? 1.0f : 0.55f));
                if (! drawGeez (g, it.dynIndex, r.getCentreX(), r.getCentreY(), 12.0f))
                {   // (without the art: a system font with Ethiopic glyphs, or a plain number)
                    static const bool haveGeez = mono (11.0f).findSuitableFontForText (geez (10)).getTypefaceName() != mono (11.0f).getTypefaceName();
                    g.setFont (mono (haveGeez ? 11.0f : 9.0f));
                    g.drawText (haveGeez ? geez (it.dynIndex) : String (it.dynIndex), r.expanded (4.0f, 2.0f), juce::Justification::centred, false);
                }
            }
            else if (it.dyn == dLcd)
            {   // the sampler display: a chip with the chosen square
                // it reads as the old display did, "obliqsound : <numeral>", with the plain number after it
                const int n = juce::jlimit (1, 26, v.sampler.load());
                const auto font = mono (10.0f, true);
                const String a = lower (JucePlugin_Name) + " : ", b = fal::ui::U ("  \xc2\xb7  ") + String (n);
                const float wa = juce::GlyphArrangement::getStringWidth (font, a), wb = juce::GlyphArrangement::getStringWidth (font, b);
                const float gw = geezSheet.isValid() ? 30.0f : juce::GlyphArrangement::getStringWidth (font, geez (n));
                const float w = wa + gw + wb + 18.0f;
                const Rectangle<float> box (r.getCentreX() - w * 0.5f, r.getCentreY() - 7.0f, w, 14.0f);
                g.setColour (accent); g.fillPath (skewRect (box, 0.35f));
                g.setColour (ground); g.setFont (font);
                float x = box.getX() + 9.0f + 2.0f;
                g.drawText (a, Rectangle<float> (x, box.getY(), wa + 2.0f, box.getHeight()), juce::Justification::centredLeft, false); x += wa;
                if (! drawGeez (g, n, x + gw * 0.5f, box.getCentreY(), 11.0f))
                    g.drawText (geez (n), Rectangle<float> (x, box.getY(), gw + 2.0f, box.getHeight()), juce::Justification::centredLeft, false);
                x += gw;
                g.setColour (ground); g.drawText (b, Rectangle<float> (x, box.getY(), wb + 2.0f, box.getHeight()), juce::Justification::centredLeft, false);
            }
            else if (it.dyn == dSpec)
            {   // the spectrum strip over PARTIALS: a quiet arc of small frames
                g.setColour (ink.withAlpha (0.18f));
                g.strokePath (skewRect ({ r.getCentreX() - 4.0f, r.getCentreY() - 2.5f, 8.0f, 5.0f }), juce::PathStrokeType (1.0f));
            }
            break;
        }
        case Knob:
            if (it.pin) drawPin (g, r, it.param >= 0 ? params.get (it.param) : 0.0f);
            else drawSlider (g, r, it.param, false);
            break;
        case VSlider: drawVSlider (g, r, it.param, false); break;
        case NSlider:
        {
            g.setColour (ink); g.setFont (mono (10.0f, true));
            g.drawText (String ((int) std::lrint (params.get (it.param))), r, juce::Justification::centred);
            g.setColour (ink.withAlpha (0.35f)); g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
            break;
        }
        case Check:
        {
            const bool on = it.param >= 0 && params.get (it.param) > 0.5f;
            if (it.sampler)
            {   // a sampler square: a larger chip, filled when chosen
                auto p = skewRect ({ r.getCentreX() - 8.0f, r.getCentreY() - 5.5f, 16.0f, 11.0f });
                if (on) { g.setColour (accent); g.fillPath (p); }
                g.setColour (on ? ink : ink.withAlpha (0.45f)); g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            else
            {   // the toggle: a small parallelogram, filled when on (16 wide with its lean: rows of them sit 20 apart)
                auto p = skewRect ({ r.getCentreX() - 8.0f, r.getCentreY() - 4.5f, 11.0f, 9.0f });
                if (on) { g.setColour (accent); g.fillPath (p); }
                g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            break;
        }
        case Combo: case PresetBox: case SizeBox:
        {
            String t;
            if (it.type == PresetBox) t = lower (proc.currentPreset);
            else if (it.type == SizeBox) t = String ((int) std::lrint (scale * 100.0f)) + "%";
            else if (it.param >= 0) t = lower (it.text[juce::jlimit (0, it.text.size() - 1, (int) std::lrint (params.get (it.param)) - 1)]);
            const bool small = r.getHeight() <= 15.0f;
            g.setColour (ink);
            g.setFont (mono (small ? 9.5f : 10.5f, it.type == PresetBox));
            if (it.type == PresetBox)
            {
                g.drawText ("<", r.withWidth (16.0f), juce::Justification::centred);
                g.drawText (">", r.withLeft (r.getRight() - 16.0f), juce::Justification::centred);
                g.drawFittedText (t, it.r.reduced (18, 0), juce::Justification::centred, 1, 0.7f);
            }
            else
            {
                g.drawFittedText (t, it.r.withTrimmedLeft (2).withTrimmedRight (9), juce::Justification::centredLeft, 1, 0.65f);
                chevron (g, r.getRight() - 7.0f, r.getCentreY() - 1.5f);
            }
            g.setColour (ink.withAlpha (0.35f)); g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
            break;
        }
        case Button: case PresetSave: case PresetRemove: case Help:
        {
            g.setColour (ink); g.setFont (mono (11.0f));
            g.drawText (it.text.isEmpty() ? String() : lower (it.text[0]), r, juce::Justification::centred);
            break;
        }
        case Label:
        {
            String t = it.text.isEmpty() ? String() : lower (it.text[0]);
            if (it.dyn == dLesA) t = String ((int) v.cutA.load()) + " hz   res " + String ((int) std::lrint (v.resA.load() * 100.0f)) + "   " + modeName (v.modeA);
            else if (it.dyn == dLesB) t = String ((int) v.cutB.load()) + " hz   res " + String ((int) std::lrint (v.resB.load() * 100.0f)) + "   " + modeName (v.modeB);
            else if (it.dyn == dSpMode) { const int m = (int) std::lrint (v.spMode.load() * 2.0f); t = m <= 0 ? "flock" : (m == 1 ? "flock  +  lorenz" : (m == 2 ? "lorenz" : (m == 3 ? "lorenz  +  gravity" : "gravity"))); }
            if (t.isEmpty()) break;
            if (it.dyn == dLesAn || it.dyn == dLesBn)
            {   // the filter's letter on a chip
                g.setColour (accent); g.fillPath (skewRect (r.reduced (0.0f, 1.0f), 0.35f));
                g.setColour (ground); g.setFont (mono (11.0f, true)); g.drawText (t, r, juce::Justification::centred);
                break;
            }
            const float h = (float) it.r.getHeight();
            if (it.dyn == dLesA || it.dyn == dLesB) { g.setColour (accent); g.setFont (mono (10.0f, true)); }
            else if (it.section) { g.setColour (ink); g.setFont (mono (h >= 10 ? 10.0f : 9.0f, true)); }
            else { g.setColour (ink.withAlpha (0.9f)); g.setFont (mono (h >= 10 ? 10.0f : 9.0f)); }
            if (it.align == 1) g.drawText (t, r.expanded (0.0f, 2.0f), juce::Justification::centredLeft, false);
            else g.drawText (t, r.expanded (20.0f, 2.0f), juce::Justification::centred, false);
            break;
        }
        default: break;
    }
}

void ObliqPanel::drawTabs (juce::Graphics& g)
{
    // the pages as a row of folded paper strips: a flat face, a darker sliver where the paper turns, the name along it
    for (size_t i = 0; i < tabItems.size(); ++i)
    {
        const auto& it = items[(size_t) tabItems[i]];
        const float depth = (float) it.dynIndex, lean = depth * 0.62f, h = depth * 0.78f;
        const auto r = it.r.toFloat();
        const Point<float> s (r.getX(), r.getBottom()), D (r.getWidth() - lean, 0.0f), W (lean, -h), F (0.0f, depth * 0.42f);
        juce::Path face;
        face.startNewSubPath (s); face.lineTo (s + D); face.lineTo (s + D + W); face.lineTo (s + W); face.closeSubPath();
        Colour c (kPalette[kTabColour[i % 7]]);
        if (std::abs (c.getPerceivedBrightness() - ground.getPerceivedBrightness()) < 0.06f && std::abs (c.getHue() - ground.getHue()) < 0.08f) c = Colour (kPalette[(kTabColour[i % 7] + 3) % 8]);
        g.setColour (c); g.fillPath (face);
        juce::Path fold;
        fold.startNewSubPath (s + D); fold.lineTo (s + D + F); fold.lineTo (s + D + W); fold.closeSubPath();
        g.setColour (c.darker (0.28f)); g.fillPath (fold);
        if ((int) i == page) { g.setColour (juce::Colours::white); g.strokePath (face, juce::PathStrokeType (2.0f)); }
        g.setColour (kStripInk); g.setFont (sans (12.0f, (int) i == page));
        const auto mid = s + D * 0.5f + W * 0.5f;
        g.drawText (pageTitle (pageNames[(int) i]), Rectangle<float> (mid.x - D.x * 0.5f, mid.y - 8.0f, D.x, 16.0f), juce::Justification::centred, false);
    }
}

void ObliqPanel::drawStatic (juce::Graphics& g, bool pageItems)
{
    for (size_t n = 0; n < items.size(); ++n)
    {
        const auto& it = items[n];
        if (pageItems != (it.page >= 0)) continue;
        if (it.page >= 0 && it.page != page) continue;
        if (! pageItems && covered ((int) n)) continue;
        if (it.type == Scope || it.tab || (! it.visible && it.dyn != dWave)) continue;   // (the WAVE knob chooses which waveform picture shows)
        if (it.dyn == dL1 || it.dyn == dL2 || it.dyn == dFol || it.dyn == dOut || it.dyn == dVLed || it.dyn == dSubLed || it.dyn == dBoid || it.dyn == dLz
            || it.dyn == dSpd || it.dyn == dRadar || it.dyn == dLesA || it.dyn == dLesB || it.dyn == dSpMode) continue;      // drawn every frame
        drawItem (g, it);
    }
    if (! pageItems) drawTabs (g);
}

void ObliqPanel::drawReadout (juce::Graphics& g, const Item& it)
{
    const String name = it.popup.isNotEmpty() ? lower (it.popup) : (it.name.isNotEmpty() ? it.name : lower (it.channel));
    const String t = name + ":  " + valueText (params.def (it.param), params.get (it.param));
    const auto font = mono (10.0f, true);
    const float w = juce::GlyphArrangement::getStringWidth (font, t) + 18.0f;
    const float y = (float) it.r.getY() - 22.0f < 2.0f ? (float) it.r.getBottom() + 4.0f : (float) it.r.getY() - 22.0f;
    const Rectangle<float> box (juce::jlimit (2.0f, (float) baseW - w - 2.0f, (float) it.r.getCentreX() - w * 0.5f), y, w, 16.0f);
    g.setColour (accent); g.fillPath (skewRect (box, 0.35f));
    g.setColour (ground); g.setFont (font); g.drawText (t, box, juce::Justification::centred, false);
}

void ObliqPanel::drawDynamic (juce::Graphics& g)
{
    auto& v = proc.engine->view;
    const float pageA = fade > 0 ? 1.0f - (float) fade / (float) kFade : 1.0f;
    auto led = [&] (Rectangle<float> r, float a)
    {
        g.setColour (accent.withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        g.fillPath (skewRect ({ r.getCentreX() - 6.0f, r.getCentreY() - 3.5f, 12.0f, 7.0f }));
    };
    auto frame = [&] (Rectangle<float> r) { g.setColour (ink.withAlpha (0.15f)); g.drawRect (r, 1.0f); };
    const bool swarmPage = pageNames[page] == "swarmpanel";
    if (swarmPage)
    {   // the two screens of the swarm page
        const auto po = pageBounds[(size_t) page].getPosition().toFloat();
        frame ({ po.x + 252.0f, po.y + 21.0f, 176.0f, 144.0f });
        frame ({ po.x + 474.0f, po.y + 14.0f, 172.0f, 144.0f });
    }
    for (size_t n = 0; n < items.size(); ++n)
    {
        const auto& it = items[n];
        if (it.page >= 0 && it.page != page) continue;
        if (covered ((int) n)) continue;
        const auto r = it.r.toFloat();
        const auto po = it.page >= 0 ? pageBounds[(size_t) it.page].getPosition().toFloat() : Point<float>();
        if (it.page >= 0) g.setOpacity (pageA);
        switch (it.dyn)
        {
            case dL1: led (r, 0.15f + 0.85f * (v.lfo1 * 0.5f + 0.5f)); break;
            case dL2: led (r, 0.15f + 0.85f * (v.lfo2 * 0.5f + 0.5f)); break;
            case dFol: led (r, 0.1f + 0.9f * v.follow); break;
            case dOut: led (r, 0.1f + 0.9f * v.outEnv); break;
            case dVLed: led (r, 0.15f + 0.85f * params.get ("uniLock")); break;
            case dSubLed: led (r, 0.15f + 0.85f * params.get ("sub2")); break;
            case dBoid:
                g.setColour (edge);
                g.fillRect (po.x + 255.0f + (v.boidX[(size_t) it.dynIndex] + 1.0f) * 82.0f, po.y + 24.0f + (v.boidY[(size_t) it.dynIndex] + 1.0f) * 67.0f, 6.0f, 6.0f);
                break;
            case dLz:
                g.setColour (accent.withAlpha (it.alpha));
                g.fillRect (po.x + 557.0f + juce::jlimit (-1.0f, 1.0f, lzx[(size_t) it.dynIndex]) * 80.0f, po.y + 82.0f + juce::jlimit (-1.0f, 1.0f, lzy[(size_t) it.dynIndex]) * 64.0f, 5.0f, 5.0f);
                break;
            case dRadar:
            {   // the radar as a thin wireframe sphere: the outline, the equator and two tilted meridians
                const float cx = po.x + 450.0f, cy = po.y + 104.0f, R = 92.0f;
                juce::Path outer, eq, mer;
                outer.addEllipse (cx - R, cy - R, 2.0f * R, 2.0f * R);
                eq.addEllipse (cx - R, cy - R * 0.32f, 2.0f * R, 2.0f * R * 0.32f);
                mer.addEllipse (cx - R * 0.32f, cy - R, 2.0f * R * 0.32f, 2.0f * R);
                g.setColour (accent); g.strokePath (outer, juce::PathStrokeType (1.0f));
                g.setColour (accent.withAlpha (0.6f));
                g.strokePath (eq, juce::PathStrokeType (1.0f));
                g.strokePath (mer, juce::PathStrokeType (1.0f), juce::AffineTransform::rotation (0.6f, cx, cy));
                g.strokePath (mer, juce::PathStrokeType (1.0f), juce::AffineTransform::rotation (-0.6f, cx, cy));
                g.setColour (accent); g.fillRect (cx - 1.0f, cy - R - 5.0f, 2.0f, 6.0f);       // front
                break;
            }
            case dSpd:
            {   // a point in the scene: a chip whose size follows its height
                const float rm = juce::jmax (1.0f, params.get ("spDist")) * 1.2f;
                float qx = v.px[(size_t) it.dynIndex] / rm, qy = v.py[(size_t) it.dynIndex] / rm; const float ql = std::sqrt (qx * qx + qy * qy);
                if (ql > 1.0f) { qx /= ql; qy /= ql; }
                const float sz = juce::jlimit (4.0f, 12.0f, 7.0f + v.pz[(size_t) it.dynIndex] / rm * 6.0f);
                const float x = po.x + 450.0f - qy * 92.0f, y = po.y + 104.0f - qx * 92.0f;
                g.setColour (it.dynIndex < 12 ? edge : (it.dynIndex < 18 ? kField : kAmber));
                g.fillPath (skewRect ({ x - sz * 0.5f, y - sz * 0.35f, sz, sz * 0.7f }, 0.5f));
                break;
            }
            case dLesA: case dLesB: case dSpMode: drawItem (g, it); break;
            default: break;
        }
        if (it.type == Scope)
        {   // the screens: a faint frame, an axis, a thin accent trace
            frame (r);
            g.setColour (ink.withAlpha (0.2f)); g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
            if (it.signals.size() == 2) g.fillRect (r.getCentreX(), r.getY(), 1.0f, r.getHeight());
            juce::Path p;
            if (it.signals.size() == 2 && it.signals[0] == "adispX")
            {   // the polygon as an XY figure
                const float order = juce::jlimit (2.05f, 32.0f, params.get ("pRegular") > 0.5f ? std::round (params.get ("pOrder")) : params.get ("pOrder"));
                const float teeth = params.get ("pTeeth"), amt = params.get ("pAmt"), skew = params.get ("pPhase");
                float ratio = params.get ("pRatio"); if (params.get ("pSnap") > 0.5f) ratio = ratio < 0.75f ? 0.5f : std::round (ratio);
                const float roll = params.get ("pRoll") + (float) tickCount / 30.0f * params.get ("pSpin");
                const double pin = juce::MathConstants<double>::pi / order; double tmax = juce::MathConstants<double>::halfPi - pin - 0.07; if (tmax < 0) tmax = 0;
                const double T = teeth * tmax; const int n = 400;
                for (int i = 0; i <= n; ++i)
                {
                    const double ph = (double) i / n, th = ph + amt * std::sin (juce::MathConstants<double>::twoPi * ph * ratio) * 0.3;
                    double fr = th * order; fr -= std::floor (fr);
                    double den = juce::jlimit (0.04, 1.0, std::cos (2 * pin * fr - pin + T));
                    const double P = std::cos (pin + T) / den, ang = juce::MathConstants<double>::twoPi * (th + roll);
                    const float x = r.getCentreX() + (float) (P * std::cos (ang)) * r.getWidth() * 0.46f, y = r.getCentreY() - (float) (P * std::sin (ang + skew * juce::MathConstants<double>::halfPi)) * r.getHeight() * 0.46f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
            }
            else if (it.signals.size() == 1)
            {
                const auto& h = lfoH[it.signals[0] == "alfo1" ? 0 : 1];
                for (size_t i = 0; i < h.size(); ++i)
                {
                    const float x = r.getX() + r.getWidth() * (float) i / (float) (h.size() - 1), y = r.getCentreY() - h[i] * r.getHeight() * 0.45f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
            }
            g.setColour (accent);
            g.saveState(); g.reduceClipRegion (it.r); g.strokePath (p, juce::PathStrokeType (1.0f)); g.restoreState();
        }
        g.setOpacity (1.0f);
    }
    // a lit button under the pointer, and the value of the control being hovered or dragged
    const int ti = dragItem >= 0 ? dragItem : hoverItem;
    if (ti >= 0 && ! helpOpen)
    {
        const auto& it = items[(size_t) ti];
        if (it.type == Button || it.type == PresetSave || it.type == PresetRemove || it.type == Help)
        {
            const auto r = it.r.toFloat();
            g.setColour (accent); g.fillPath (skewRect (r.reduced (2.0f, 3.0f), 0.35f));
            g.setColour (ground); g.setFont (mono (11.0f));
            g.drawText (it.text.isEmpty() ? String() : lower (it.text[0]), r, juce::Justification::centred);
        }
        else if (it.param >= 0 && (it.type == Knob || it.type == VSlider || it.type == NSlider)) drawReadout (g, it);
    }
}

void ObliqPanel::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::scale (scale));
    g.setColour (ground); g.fillRect (0, 0, baseW, baseH);

    // the title where the logo was, and the rule between the instrument and the page
    g.setColour (ink); g.setFont (sans (22.0f, true));
    g.drawText (lower (JucePlugin_Name), Rectangle<float> (300.0f, 4.0f, 360.0f, 26.0f), juce::Justification::centred, false);
    g.setColour (ink.withAlpha (0.8f)); g.setFont (mono (10.0f));
    g.drawText (OBLIQ_SONIC ? "third-order ambisonics" : fal::ui::U ("polyphonic synth \xc2\xb7 field recordings"), Rectangle<float> (300.0f, 30.0f, 360.0f, 12.0f), juce::Justification::centred, false);
    g.setFont (mono (9.0f)); g.drawText ("faltung", Rectangle<float> (858.0f, 36.0f, 62.0f, 10.0f), juce::Justification::centredRight, false);
    if (page >= 0 && page < (int) pageBoard.size() && ! pageBoard[(size_t) page]) { g.setColour (ink.withAlpha (0.25f)); g.fillRect (30.0f, 394.0f, 900.0f, 1.0f); }

    // the controls are cached at device resolution and only redrawn when something changes
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int change = params.changeCount.load() * 4 + proc.engine->view.sampler.load();
    if (! layerMain.isValid() || layerChange != change || layerPageNo != page || layerScale != ps)
    {
        layerChange = change; layerPageNo = page; layerScale = ps;
        const int w = (int) std::ceil (baseW * ps), h = (int) std::ceil (baseH * ps);
        for (auto* layer : { &layerMain, &layerPage })
        {
            if (! layer->isValid() || layer->getWidth() != w || layer->getHeight() != h) *layer = juce::Image (juce::Image::ARGB, w, h, true);
            else layer->clear (layer->getBounds());
            juce::Graphics lg (*layer);
            lg.addTransform (juce::AffineTransform::scale (ps));
            drawStatic (lg, layer == &layerPage);
        }
    }
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.setOpacity (1.0f);                                   // (an image is drawn with the current colour's alpha)
    g.drawImage (layerMain, Rectangle<float> (0, 0, (float) baseW, (float) baseH));
    if (fade > 0) g.setOpacity (1.0f - (float) fade / (float) kFade);
    g.drawImage (layerPage, Rectangle<float> (0, 0, (float) baseW, (float) baseH));
    g.setOpacity (1.0f);
    drawDynamic (g);
    if (helpOpen) drawHelp (g);
}

// =================================================================================================== interaction
int ObliqPanel::hit (juce::Point<float> p) const
{
    for (int i = (int) items.size() - 1; i >= 0; --i)
    {
        const auto& it = items[(size_t) i];
        if (it.type == Image || it.type == Label || it.type == Scope) continue;
        if (it.page >= 0 && it.page != page) continue;
        if (covered (i)) continue;                 // controls under a page's board can't be reached
        auto area = it.r.toFloat();
        if (it.type == Check && ! it.sampler && ! it.tab)                    // the drawn chip is wider than the old light: all of it takes the click
            area = area.withSizeKeepingCentre (std::max (area.getWidth(), 17.0f), std::max (area.getHeight(), 11.0f));
        if (area.contains (p)) return i;
    }
    return -1;
}

void ObliqPanel::setParam (int item, float norm)
{
    const auto& it = items[(size_t) item];
    if (it.param >= 0) params.set (it.param, params.fromNorm (it.param, norm));
}

void ObliqPanel::showMenu (int item)
{
    auto& it = items[(size_t) item];
    juce::PopupMenu m;
    auto area = localAreaToGlobal ((it.r.toFloat() * scale).toNearestInt());
    juce::Component::SafePointer<ObliqPanel> safe (this);
    if (it.type == PresetBox)
    {
        const auto names = proc.presetNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, names[i] == proc.currentPreset);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area).withMaximumNumColumns (4), [safe, names] (int r) { if (safe != nullptr && r > 0) safe->proc.loadPreset (names[r - 1]); });
    }
    else if (it.type == SizeBox)
    {
        const float sizes[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
        for (int i = 0; i < 6; ++i) m.addItem (i + 1, it.text[i], true, std::abs (sizes[i] - scale) < 0.01f);
        if (OBLIQ_SONIC)
        {
            m.addSeparator();
            m.addItem (100, "output: stereo (decoded from the ambisonics)", true, proc.stereoMonitor.load());
            m.addItem (101, "output: 16-channel ambisonics (acn / sn3d)", true, ! proc.stereoMonitor.load());
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area), [safe] (int r)
        {
            const float sz[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
            if (safe == nullptr || r <= 0) return;
            if (r >= 100) safe->proc.stereoMonitor = r == 100;
            else if (safe->onScale) safe->onScale (sz[r - 1]);
        });
    }
    else
    {
        const int cur = (int) std::lrint (params.get (it.param));
        for (int i = 0; i < it.text.size(); ++i) m.addItem (i + 1, lower (it.text[i]), true, cur == i + 1);
        const int pi = it.param;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area), [safe, pi] (int r) { if (safe != nullptr && r > 0) safe->params.set (pi, (float) r); });
    }
}

void ObliqPanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = toPanel (e.position);
    if (helpOpen) { if (! helpClick (p)) helpOpen = false; return; }
    const int i = hit (p);
    if (i < 0) return;
    auto& it = items[(size_t) i];
    switch (it.type)
    {
        case Knob: case VSlider: case NSlider:
            dragItem = i; dragStart = params.toNorm (it.param, params.get (it.param)); dragPos = p; break;
        case Check:
            if (it.group.isNotEmpty())
            {
                for (auto& o : items) if (o.type == Check && o.group == it.group && o.param >= 0) params.set (o.param, 0.0f);
                params.set (it.param, 1.0f);
            }
            else if (it.channel == "lRnd")
            {   // randomize both filters
                auto roll = [this] (const char* n) { const int k = params.find (n); if (k >= 0) params.set (k, params.fromNorm (k, random.nextFloat())); };
                for (auto n : { "aMode", "aGain", "LPFreq", "Res", "bCut", "bRes", "bMode", "bGain", "lMix" }) roll (n);
                params.set ("link", (float) (1 + random.nextInt (5)));
            }
            else params.set (it.param, params.get (it.param) > 0.5f ? 0.0f : 1.0f);
            break;
        case PresetBox:
        {   // < and > step through the presets, the name opens the list
            const auto names = proc.presetNames();
            const int cur = names.indexOf (proc.currentPreset), n = names.size();
            if (n > 0 && p.x < (float) it.r.getX() + 16.0f) proc.loadPreset (names[(cur - 1 + n) % n]);
            else if (n > 0 && p.x > (float) it.r.getRight() - 16.0f) proc.loadPreset (names[(cur + 1) % n]);
            else showMenu (i);
            break;
        }
        case Combo: case SizeBox: showMenu (i); break;
        case Button: params.set (it.param, params.get (it.param) + 1.0f); break;
        case PresetSave:
        {
            auto* aw = new juce::AlertWindow ("save preset", "name:", juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("name", proc.currentPreset == "Default" ? String ("my sound") : proc.currentPreset);
            aw->addButton ("save", 1, juce::KeyPress (juce::KeyPress::returnKey)); aw->addButton ("cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<ObliqPanel> safe (this);
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int r) { if (safe != nullptr && r == 1) { auto n = aw->getTextEditorContents ("name").trim(); if (n.isNotEmpty()) safe->proc.savePreset (n); } }), true);
            break;
        }
        case PresetRemove: proc.removePreset (proc.currentPreset); break;
        case Help: helpOpen = true; dragItem = -1; break;
        default: break;
    }
}

void ObliqPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragItem < 0) return;
    const auto p = toPanel (e.position);
    const float range = e.mods.isShiftDown() ? 800.0f : 180.0f;
    // up or right raises; the bars read horizontally, the old vertical drag still works
    const float d = items[(size_t) dragItem].type == Knob ? (dragPos.y - p.y) + (p.x - dragPos.x) : (dragPos.y - p.y);
    setParam (dragItem, juce::jlimit (0.0f, 1.0f, dragStart + d / range));
}

void ObliqPanel::mouseUp (const juce::MouseEvent&) { dragItem = -1; }
void ObliqPanel::mouseMove (const juce::MouseEvent& e) { mousePos = toPanel (e.position); hoverItem = helpOpen ? -1 : hit (mousePos); }
void ObliqPanel::mouseExit (const juce::MouseEvent&) { hoverItem = -1; }

void ObliqPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (helpOpen) return;
    const int i = hit (toPanel (e.position));
    if (i >= 0 && items[(size_t) i].param >= 0 && (items[(size_t) i].type == Knob || items[(size_t) i].type == VSlider || items[(size_t) i].type == NSlider))
        params.set (items[(size_t) i].param, params.def (items[(size_t) i].param).def);
}

void ObliqPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (helpOpen) return;
    const int i = hit (toPanel (e.position));
    if (i >= 0 && items[(size_t) i].param >= 0 && (items[(size_t) i].type == Knob || items[(size_t) i].type == VSlider || items[(size_t) i].type == NSlider))
        setParam (i, juce::jlimit (0.0f, 1.0f, params.toNorm (items[(size_t) i].param, params.get (items[(size_t) i].param)) + w.deltaY * 0.15f));
}

// the computer keyboard plays notes: A W S E D F T G Y H U J K O L P ; '   (Z / X octave, C / V velocity)
bool ObliqPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::F1Key) { helpOpen = ! helpOpen; return true; }
    if (helpOpen && k == juce::KeyPress::escapeKey) { helpOpen = false; return true; }
    if (helpOpen && (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey))
    {
        const int n = (int) helpTopics().size();
        helpTopic = (helpTopic + (k == juce::KeyPress::downKey ? 1 : n - 1)) % n; return true;
    }
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c == 'z') { kbOctave = juce::jmax (-3, kbOctave - 1); return true; }
    if (c == 'x') { kbOctave = juce::jmin (4, kbOctave + 1); return true; }
    if (c == 'c') { kbVelocity = juce::jmax (10, kbVelocity - 20); return true; }
    if (c == 'v') { kbVelocity = juce::jmin (127, kbVelocity + 20); return true; }
    return String (kKeys).containsChar (c);
}

bool ObliqPanel::keyStateChanged (bool)
{
    bool used = false;
    for (int i = 0; kKeys[i] != 0; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (kKeys[i]) && ! juce::ModifierKeys::currentModifiers.isCommandDown();
        const bool was = heldKeys.count (i) > 0;
        if (down && ! was) { const int n = juce::jlimit (0, 127, 60 + 12 * kbOctave + i); heldKeys[i] = n; proc.keyboard.noteOn (1, n, (float) kbVelocity / 127.0f); used = true; }
        else if (! down && was) { proc.keyboard.noteOff (1, heldKeys[i], 0.0f); heldKeys.erase (i); used = true; }
    }
    return used;
}

// =================================================================================================== help
std::vector<int> ObliqPanel::helpTopics() const
{
    std::vector<int> t;
    for (int i = 0; i < (int) (sizeof (kHelp) / sizeof (kHelp[0])); ++i) if (OBLIQ_SONIC || ! kHelp[i].sonicOnly) t.push_back (i);
    return t;
}

namespace
{
    Rectangle<float> helpTopicRect (Rectangle<float> w, int i) { return { w.getX() + 12.0f, w.getY() + 46.0f + (float) i * 26.0f, 118.0f, 22.0f }; }
    Rectangle<float> helpCloseRect (Rectangle<float> w) { return { w.getRight() - 32.0f, w.getY() + 8.0f, 22.0f, 22.0f }; }
    Rectangle<float> helpManualRect (Rectangle<float> w) { return { w.getX() + 12.0f, w.getBottom() - 36.0f, 118.0f, 22.0f }; }
}

void ObliqPanel::drawHelp (juce::Graphics& g)
{
    const auto w = helpWindow();
    const auto topics = helpTopics();
    helpTopic = juce::jlimit (0, (int) topics.size() - 1, helpTopic);

    // a flat panel in the field colour over a dimmed panel
    g.setColour (ink.withAlpha (0.28f)); g.fillRect (0, 0, baseW, baseH);
    const Colour bg = ground.getPerceivedBrightness() > 0.5f ? ground.brighter (0.22f) : ground.brighter (0.35f);
    g.setColour (bg); g.fillRect (w);
    g.setColour (ink); g.drawRect (w, 1.0f);

    g.setFont (sans (16.0f, true));
    g.drawText (lower (JucePlugin_Name) + " help", Rectangle<float> (w.getX() + 14.0f, w.getY() + 9.0f, 300.0f, 22.0f), juce::Justification::centredLeft, false);
    g.setFont (mono (10.0f)); g.setColour (ink.withAlpha (0.7f));
    g.drawText (fal::ui::U ("f1 \xc2\xb7 esc"), Rectangle<float> (w.getRight() - 140.0f, w.getY() + 12.0f, 100.0f, 16.0f), juce::Justification::centredRight, false);
    g.setColour (ink); g.setFont (sans (16.0f));
    g.drawText (fal::ui::U ("\xc3\x97"), helpCloseRect (w), juce::Justification::centred, false);
    g.setColour (ink.withAlpha (0.25f)); g.fillRect (w.getX() + 12.0f, w.getY() + 38.0f, w.getWidth() - 24.0f, 1.0f);

    // topics as text links; the open one on a chip
    for (int i = 0; i < (int) topics.size(); ++i)
    {
        const auto r = helpTopicRect (w, i); const bool on = i == helpTopic;
        if (on) { g.setColour (accent); g.fillPath (skewRect (r.reduced (2.0f, 3.0f), 0.35f)); }
        g.setColour (on ? ground : ink); g.setFont (mono (11.0f, on));
        g.drawText (lower (kHelp[topics[(size_t) i]].title), r, juce::Justification::centred, false);
    }
    const auto mr = helpManualRect (w);
    g.setColour (ink); g.setFont (mono (11.0f));
    g.drawText ("manual (pdf)", mr, juce::Justification::centred, false);
    g.setColour (ink.withAlpha (0.35f)); g.fillRect (mr.getX() + 14.0f, mr.getBottom() - 3.0f, mr.getWidth() - 28.0f, 1.0f);

    // the text
    const Rectangle<float> box (w.getX() + 146.0f, w.getY() + 48.0f, w.getWidth() - 162.0f, w.getHeight() - 60.0f);
    juce::AttributedString as; as.setWordWrap (juce::AttributedString::byWord); as.setLineSpacing (2.0f);
    juce::StringArray lines; lines.addLines (kHelp[topics[(size_t) helpTopic]].body);
    bool first = true;
    for (auto& l : lines)
    {
        if (l.startsWith ("# ")) { if (! first) as.append ("\n", sans (6.0f), ink); as.append (lower (l.substring (2)) + "\n", sans (12.5f, true), accent); }
        else as.append (l + "\n", sans (12.0f), ink);
        first = false;
    }
    as.draw (g, box);
}

bool ObliqPanel::helpClick (juce::Point<float> p)
{
    const auto w = helpWindow();
    if (! w.contains (p) || helpCloseRect (w).contains (p)) return false;
    const int n = (int) helpTopics().size();
    for (int i = 0; i < n; ++i) if (helpTopicRect (w, i).contains (p)) helpTopic = i;
    if (helpManualRect (w).contains (p))
    {
        auto pdf = ObliqProcessor::resourceDir().getChildFile ("obliqsound Manual.pdf");
        if (pdf.existsAsFile()) pdf.startAsProcess(); else juce::URL ("https://github.com/femifleming/faltung").launchInDefaultBrowser();
    }
    return true;
}

// =================================================================================================== editor
ObliqEditor::ObliqEditor (ObliqProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onScale = [this] (float s) { panel.scale = s; setSize ((int) std::lrint (panel.baseW * s), (int) std::lrint (panel.baseH * s)); };
    setResizable (false, false);
    setSize (panel.baseW, panel.baseH);
}

void ObliqEditor::resized() { panel.setBounds (getLocalBounds()); }
