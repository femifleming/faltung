#include "Ui.h"
#include "PluginProcessor.h"
#include "Pixels.h"

namespace ecr
{
using namespace pix;

// "BOTTLEDUNGEON_48K.wav" -> "BOTTLEDUN..48K", as the module shortens names
static juce::String listName (const juce::String& fileName)
{
    const auto n = fileName.upToLastOccurrenceOf (".", false, false).toUpperCase();
    return n.length() <= 14 ? n : n.substring (0, 9) + ".." + n.getLastCharacters (3);
}

//== logic =======================================================================
Ui::Ui (EcrProcessor& p) : proc (p) { lastActivity = juce::Time::getMillisecondCounter(); }

void Ui::wake() { lastActivity = juce::Time::getMillisecondCounter(); }
void Ui::stateRestored() { screen = Main; message = juce::String ("CONVOLVER ") + (proc.foreground.load() == 0 ? "A" : "B"); }

void Ui::popup (const juce::String& t, int ms)
{
    popupText = t;
    popupUntil = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
}

juce::RangedAudioParameter* Ui::paramFor (Knob k, bool shift) const
{
    const int fg = proc.foreground.load();
    switch (k)
    {
        case Knob::Width:    return proc.slotParam (fg, shift ? kPitch : kWidth);
        case Knob::Xfade:    return proc.globalParam (shift ? kSlew : kXfade);
        case Knob::Predelay: return proc.slotParam (fg, shift ? kHpf : kPredelay);
        case Knob::Pos:      return shift ? proc.globalParam (kInLvl) : proc.slotParam (fg, kPos);
        case Knob::Dry:      return proc.globalParam (kDry);
        case Knob::Wet:      return proc.globalParam (kWet);
        case Knob::Attack:   return proc.slotParam (fg, kAttack);
        case Knob::Release:  return proc.slotParam (fg, kRelease);
    }
    return nullptr;
}

void Ui::knobMoved (Knob k, bool shift)
{
    wake();
    const double v = paramFor (k, shift)->getValue();
    auto secs = [] (double s) { return juce::String (s, 3) + "s"; };
    auto db = [] (double k01) { return juce::String (curve::gainDb (k01), 1) + "dB"; };
    switch (k)
    {
        case Knob::Width:
            if (shift) { const double o = curve::pitchOctaves (v); setMessage ("Pitch " + juce::String (o > 0 ? "+" : "") + juce::String (o, 2) + " oct"); }
            else setMessage ("Width " + juce::String (curve::width (v), 2));
            break;
        case Knob::Xfade:
            if (shift) setMessage ("Slew " + secs (curve::slewSeconds (v)));
            else setMessage ("Xfade " + juce::String (juce::roundToInt ((1.0 - v) * 100.0)) + "/" + juce::String (juce::roundToInt (v * 100.0)));
            break;
        case Knob::Predelay:
            if (shift) setMessage (curve::hpfHz (v) <= 0.0 ? juce::String ("HPF off") : "HPF " + juce::String (juce::roundToInt (curve::hpfHz (v))) + "Hz");
            else setMessage ("Predelay " + secs (curve::predelaySeconds (v)));
            break;
        case Knob::Pos:
            if (shift) setMessage ("In lvl " + db (v));
            else setMessage ("Pos " + juce::String (v, 3));
            break;
        case Knob::Dry:     setMessage ("Dry " + db (v)); break;
        case Knob::Wet:     setMessage ("Wet " + db (v)); break;
        case Knob::Attack:  setMessage ("Attack " + secs (curve::timeSeconds (v))); break;
        case Knob::Release: setMessage (curve::releaseIsFull (v) ? juce::String ("Len full") : "Len " + secs (curve::timeSeconds (v))); break;
    }
}

void Ui::cvMoved (int which)
{
    wake();
    static const char* const names[4] = { "Pos", "X", "Width", "Aux" };
    const float v = proc.globalParam (kCvPos + which)->convertFrom0to1 (proc.globalParam (kCvPos + which)->getValue()) * 5.0f;
    setMessage ("CV " + juce::String (names[which]) + " " + (v > 0 ? "+" : "") + juce::String (v, 2) + "V");
}

void Ui::pressShift() { wake(); shiftLatch = ! shiftLatch; }

void Ui::shiftUsed()
{
    if (! proc.settings.directShift) shiftLatch = false;
}

void Ui::pressRecord()
{
    wake();
    const bool was = proc.isRecording();
    proc.toggleRecording();
    if (was) setMessage ("Recorded");
    else if (proc.isRecording()) { screen = Main; setMessage ("Recording"); }
    else popup ("START AUDIO\nFIRST", 1500);
}

void Ui::pressStore (bool shift)
{
    wake();
    if (shift)
    {
        popup (proc.storePresets() ? "STORE PRESET.." : "STORE FAILED", 1200);
        storeFlashUntil = juce::Time::getMillisecondCounter() + 600;
        return;
    }
    const int fg = proc.foreground.load();
    auto src = proc.responseSource (fg);
    if (src != nullptr && src->unsaved)
    {
        const auto name = src->name;
        popup (proc.storeRecording (fg) ? "STORED\n" + listName (name) : juce::String ("STORE FAILED"), 1500);
        storeFlashUntil = juce::Time::getMillisecondCounter() + 600;
    }
    else
        popup ("NO RECORDING\nTO STORE", 1200);
}

void Ui::pressPlay (bool shift)
{
    wake();
    if (proc.previewActive()) { proc.stopPreview(); setMessage ("Preview off"); }
    else { proc.startPreview (shift); setMessage (shift ? "Preview proc." : "Preview"); }
}

bool Ui::recordLed() const { return proc.isRecording() && (juce::Time::getMillisecondCounter() / 300) % 2 == 0; }
bool Ui::storeLed() const  { return juce::Time::getMillisecondCounter() < storeFlashUntil; }
bool Ui::playLed() const   { return proc.previewActive(); }

void Ui::turnSelect (int steps, bool shift)
{
    wake();
    if (steps == 0) return;
    if (screen == Menu)
    {
        menuSel = juce::jlimit (0, kNumMenuItems - 1, menuSel + steps);
        return;
    }
    const auto& list = proc.library.entries();
    if (list.empty()) { popup ("NO RESPONSES", 1200); return; }

    const int fg = proc.foreground.load();
    const int current = proc.library.indexOf (proc.responseId (fg));
    chooserTouched = juce::Time::getMillisecondCounter();

    if (proc.settings.chooser && screen == Main)
    {
        // the first click only opens the list
        screen = Chooser;
        chooserSel = juce::jmax (0, current);
        steps += steps > 0 ? -1 : 1;
        if (steps == 0) return;
    }

    int target = juce::jmax (0, current);
    if (current < 0) target = steps > 0 ? 0 : (int) list.size() - 1;   // coming from a recording
    else if (shift)
        for (int i = 0; i < std::abs (steps); ++i) target = proc.library.nextFolderStart (target, steps);
    else
        target = juce::jlimit (0, (int) list.size() - 1, target + steps);

    chooserSel = target;
    if (target != current)
    {
        proc.selectResponse (fg, list[(size_t) target].id);
        setMessage (juce::String (fg == 0 ? "A " : "B ") + listName (list[(size_t) target].name));
    }
}

void Ui::pushSelect (bool shift)
{
    wake();
    if (shift)
    {
        screen = screen == Menu ? Main : Menu;
        return;
    }
    if (screen == Menu) { activateMenuItem (menuSel); return; }
    if (screen == Chooser) { screen = Main; return; }
    const int fg = 1 - proc.foreground.load();
    proc.foreground = fg;
    setMessage (juce::String ("CONVOLVER ") + (fg == 0 ? "A" : "B"));
}

void Ui::holdSelect()
{
    wake();
    if (screen == Menu) return;
    if (proc.loadPatch (proc.foreground.load())) popup ("READ PATCH...", 1200);
}

void Ui::tick()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (screen == Chooser && proc.settings.autoExit && now - chooserTouched > 4000) screen = Main;
    if (proc.isRecording()) setMessage ("Rec " + juce::String (proc.recordedSeconds(), 1) + "s");
}

juce::String Ui::menuLine (int item) const
{
    const auto& s = proc.settings;
    auto onOff = [] (bool b) { return b ? "ON" : "OFF"; };
    static const char* const maps[3] = { "A", "B", "AB" };
    static const char* const auxs[3] = { "WET", "TUNE", "INL" };
    static const char* const svrs[4] = { "OFF", "30S", "2M", "5M" };
    juce::String label, value;
    switch (item)
    {
        case 0: label = "DIRECT SHIFT"; value = onOff (s.directShift); break;
        case 1: label = "CHOOSER";      value = onOff (s.chooser); break;
        case 2: label = "AUTO EXIT";    value = onOff (s.autoExit); break;
        case 3: label = "CV MAP";       value = maps[s.cvMap]; break;
        case 4: label = "AUX";          value = auxs[s.aux]; break;
        case 5: label = "SVR";          value = svrs[s.svr]; break;
        case 6: label = "CONV B";       value = onOff (s.convB); break;
        case 7: label = "AUTOGAIN";     value = onOff (s.autoGain); break;
        default: label = "EXIT"; break;
    }
    return label + juce::String::repeatedString (" ", juce::jmax (1, 16 - label.length() - value.length())) + value;
}

void Ui::activateMenuItem (int item)
{
    auto& s = proc.settings;
    switch (item)
    {
        case 0: s.directShift = ! s.directShift; if (! s.directShift) shiftLatch = false; break;
        case 1: s.chooser = ! s.chooser; break;
        case 2: s.autoExit = ! s.autoExit; break;
        case 3: s.cvMap = (s.cvMap + 1) % 3; break;
        case 4: s.aux = (s.aux + 1) % 3; break;
        case 5: s.svr = (s.svr + 1) % 4; break;
        case 6: s.convB = ! s.convB; break;
        case 7: s.autoGain = ! s.autoGain; break;
        default: screen = Main; break;
    }
    proc.settingsChanged();
}

//== rendering ===================================================================
void Ui::renderMain (juce::Image& img)
{
    juce::Image::BitmapData bm (img, juce::Image::BitmapData::readWrite);
    const int fg = proc.foreground.load();
    auto src = proc.responseSource (fg);

    if (src != nullptr && src->length > 0)
    {
        const int rows = src->numChannels >= 4 ? 4 : 2;
        for (int r = 0; r < rows; ++r)
        {
            const auto& ov = src->overview[(size_t) (src->numChannels == 1 ? 0 : r)];
            const int base = r * 32 + 31;
            for (int x = 0; x < 128; ++x)
            {
                const int h = ov[(size_t) x] > 0 ? 1 + ov[(size_t) x] * 23 / 255 : 0;
                for (int y = 0; y < h; ++y) px (bm, x, base - y, col::wave);
            }
        }

        // attack-release envelope over the (pitched) response
        const double pos = proc.slotParam (fg, kPos)->getValue();
        const double attack = curve::timeSeconds (proc.slotParam (fg, kAttack)->getValue());
        const double relK = proc.slotParam (fg, kRelease)->getValue();
        const double octaves = curve::pitchOctaves (proc.slotParam (fg, kPitch)->getValue());
        const double seconds = juce::jmax (1e-3, src->length / src->sampleRate / std::pow (2.0, octaves));
        const int x0 = juce::roundToInt (pos * 127.0);
        const int x1 = x0 + juce::roundToInt (attack / seconds * 128.0);
        line (bm, x0, 127, x0, attack > 0.0 ? 127 : 0, col::blue);
        if (attack > 0.0) line (bm, x0, 127, juce::jmin (x1, 2000), 0, col::blue);
        if (curve::releaseIsFull (relK)) line (bm, x1, 0, 127, 0, col::blue);
        else line (bm, x1, 0, juce::jmin (2000, x1 + juce::roundToInt (curve::timeSeconds (relK) / seconds * 128.0)), 127, col::blue);
    }

    text (bm, 0, 0, message.substring (0, 18), col::white, 7);
    text (bm, 112, 9, fg == 0 ? "A" : "B", fg == 0 ? col::blue : col::orange);
    if (proc.clipping.load()) text (bm, 120, 9, "C", col::red);
    if (proc.isRecording()) { fill (bm, 58, 58, 12, 12, col::red); }

    juce::String name = proc.isLoading (fg) ? juce::String ("Loading..") : (src != nullptr ? src->name : juce::String ("No response"));
    if (name.length() > 18) name = name.upToLastOccurrenceOf (".", false, false);
    text (bm, 0, 120, name.substring (0, 18), col::white, 7);
}

void Ui::renderChooser (juce::Image& img)
{
    juce::Image::BitmapData bm (img, juce::Image::BitmapData::readWrite);
    const auto& list = proc.library.entries();
    const int n = (int) list.size(), fg = proc.foreground.load();
    const int sel = juce::jlimit (0, juce::jmax (0, n - 1), chooserSel);
    const int pitch = 12, visible = 8;

    text (bm, 0, 2, fg == 0 ? "A" : "B", fg == 0 ? col::blue : col::orange);
    if (n > 0) text (bm, 8, 2, list[(size_t) sel].folder.substring (0, 13), col::white);
    text (bm, 0, 2 + pitch, "---------------", col::white);

    const int first = juce::jlimit (0, juce::jmax (0, n - visible), sel - 3);
    for (int r = 0; r < visible && first + r < n; ++r)
    {
        const auto& e = list[(size_t) (first + r)];
        const int y = 2 + pitch * (r + 2);
        const bool selected = first + r == sel;
        if (selected) fill (bm, 0, y - 2, 120, pitch, col::white);
        text (bm, 0, y, listName (e.name), selected ? col::black : (e.hasPatch ? col::blue : col::white));
    }
    if (first > 0) arrow (bm, 120, 2, true, col::white);
    if (first + visible < n) arrow (bm, 120, 2 + pitch * 9, false, col::white);
}

void Ui::renderMenu (juce::Image& img)
{
    juce::Image::BitmapData bm (img, juce::Image::BitmapData::readWrite);
    text (bm, 0, 2, "CONVOSOL " JucePlugin_VersionString, col::blue);
    for (int i = 0; i < kNumMenuItems; ++i)
    {
        const int y = 2 + 12 * (i + 1) + 4;
        if (i == menuSel) fill (bm, 0, y - 2, 128, 12, col::white);
        text (bm, 0, y, menuLine (i), i == menuSel ? col::black : col::white);
    }
}

void Ui::render (juce::Image& img)
{
    jassert (img.getWidth() == 128 && img.getHeight() == 128);
    img.clear (img.getBounds(), col::black);
    if (screen == Chooser) renderChooser (img);
    else if (screen == Menu) renderMenu (img);
    else renderMain (img);

    const auto now = juce::Time::getMillisecondCounter();
    if (now < popupUntil && popupText.isNotEmpty())
    {
        juce::Image::BitmapData bm (img, juce::Image::BitmapData::readWrite);
        const auto lines = juce::StringArray::fromLines (popupText);
        const int h = lines.size() * 10 + 10, y0 = 64 - h / 2;
        fill (bm, 4, y0, 120, h, col::white);
        fill (bm, 5, y0 + 1, 118, h - 2, col::black);
        for (int i = 0; i < lines.size(); ++i)
            text (bm, 64 - lines[i].length() * 4, y0 + 6 + i * 10, lines[i], col::white);
    }

    // screensaver: half brightness after the set time, black 5 s later
    static const int times[4] = { 0, 30, 120, 300 };
    const int t = times[juce::jlimit (0, 3, proc.settings.svr)];
    if (t > 0)
    {
        const double idle = (now - lastActivity) / 1000.0;
        if (idle > t)
        {
            juce::Graphics g (img);
            g.fillAll (juce::Colours::black.withAlpha (idle > t + 5 ? 1.0f : 0.5f));
        }
    }
}
} // namespace ecr
