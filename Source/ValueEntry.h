#pragma once
// Typing a value instead of dragging it: "1.2k", "850", "2k5", "a4", "c#3 +20", "-4.5 db", "85%" → the
// parameter's plain value, and the small text field the ms0 window opens over a value.
#include "Ui.h"

namespace fal::ui
{
/** the note a frequency is, with the cents it is off: "g#4 -12 c" */
inline juce::String noteName (float hz)
{
    if (hz <= 1.0f) return {};
    const double m = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);
    const int n = (int) std::lround (m), cents = (int) std::lround ((m - n) * 100.0);
    static const char* names[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
    juce::String s = juce::String (names[((n % 12) + 12) % 12]) + juce::String ((int) std::floor (n / 12.0) - 1);
    if (cents != 0) s << (cents > 0 ? " +" : " -") << std::abs (cents) << " c";
    return s;
}

/** What somebody typed → the plain value of the parameter (clamped to its range). The parameter's own text
    says what kind of value it is:
      frequency  "850"  "850 hz"  "1.2k"  "1.2 khz"  "2k5"  "a4"  "c#3"  "bb2"  "a4 +20" (cents);
                 a bare number below the range ("2.8" on a 20 hz – 20 khz control) is taken as khz
      percent    "85"  "85%"            time  "20"  "20 ms"  "0.02 s"
      balance    "c"  "l 20"  "r 20"    anything else: the number ("-4.5", "-4.5 db", "2.5:1")
    false when there is nothing to read. */
inline bool parseTyped (const juce::RangedAudioParameter& prm, const juce::String& typed, float& out)
{
    auto t = typed.trim().toLowerCase().replaceCharacter (',', '.').replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-");
    if (t.isEmpty()) return false;
    const auto range = prm.getNormalisableRange();
    const auto shown = prm.getText (prm.getValue(), 64).toLowerCase();
    const bool isHz = shown.contains ("hz"), isPct = shown.endsWithChar ('%'), isMs = shown.endsWith (" ms"), isSec = shown.endsWith (" s");
    const bool isBal = range.start < -0.99f && range.start > -1.01f && range.end > 0.99f && range.end < 1.01f && (shown == "c" || shown.startsWith ("l ") || shown.startsWith ("r "));
    auto finish = [&] (double v) { out = range.snapToLegalValue (juce::jlimit (range.start, range.end, (float) v)); return true; };
    // the number at the front of a text, and what follows it
    auto number = [] (const juce::String& s, double& v, juce::String& rest)
    {
        int i = 0, digits = 0; const int n = s.length();
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        while (i < n && juce::CharacterFunctions::isDigit (s[i])) { ++i; ++digits; }
        if (i < n && s[i] == '.') { ++i; while (i < n && juce::CharacterFunctions::isDigit (s[i])) { ++i; ++digits; } }
        if (digits == 0) return false;
        v = s.substring (0, i).getDoubleValue(); rest = s.substring (i).trim();
        return true;
    };
    double v = 0; juce::String rest;
    if (isHz && t[0] >= 'a' && t[0] <= 'g')
    {
        // a note: letter, # or b, octave, then perhaps cents
        static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        int i = 1, semi = semis[t[0] - 'a'];
        if (t[i] == '#') { ++semi; ++i; } else if (t[i] == 'b') { --semi; ++i; }
        double oct = 0; juce::String after;
        if (! number (t.substring (i), oct, after) || std::abs (oct - std::round (oct)) > 1.0e-9 || t[i] == '+') return false;
        double cents = 0; juce::String unit;
        if (after.isNotEmpty() && ! number (after, cents, unit)) return false;
        return finish (440.0 * std::exp2 ((12.0 * (oct + 1.0) + semi + cents / 100.0 - 69.0) / 12.0));
    }
    if (isBal)
    {
        if (t[0] == 'c') return finish (0.0);
        const double sign = t[0] == 'l' ? -1.0 : 1.0;
        if (t[0] == 'l' || t[0] == 'r') t = t.substring (1).trim();
        return number (t, v, rest) && finish (sign * v / 100.0);
    }
    if (! number (t, v, rest)) return false;
    if (isHz)
    {
        if (rest.startsWithChar ('k'))
        {
            v *= 1000.0;
            const auto frac = rest.substring (1).initialSectionContainingOnly ("0123456789");   // "2k5"
            if (frac.isNotEmpty()) v += frac.getDoubleValue() / std::pow (10.0, frac.length()) * 1000.0;
        }
        else if (rest.isEmpty() && v > 0.0 && v < range.start && v * 1000.0 <= range.end) v *= 1000.0;
    }
    else if (isPct) v /= 100.0;
    else if (isMs && rest.startsWithChar ('s')) v *= 1000.0;
    else if (isSec && rest.startsWith ("ms")) v /= 1000.0;
    return finish (v);
}

/** the text field: opens with the value selected; return or clicking away applies, escape leaves it */
class ValueEntry : public juce::TextEditor
{
public:
    ValueEntry (juce::RangedAudioParameter& p, Theme t) : prm (p)
    {
        setFont (mono (11.0f, true));
        setJustification (juce::Justification::centredLeft);
        setBorder ({}); setIndents (5, 0);
        setColour (backgroundColourId, t.ground.interpolatedWith (t.ink, 0.14f));
        setColour (textColourId, t.ink);
        setColour (highlightColourId, t.fill.withAlpha (0.6f));
        setColour (highlightedTextColourId, t.ink);
        setColour (outlineColourId, t.fill); setColour (focusedOutlineColourId, t.fill);
        setColour (juce::CaretComponent::caretColourId, t.ink);
        setSelectAllWhenFocused (true);
        setInputRestrictions (24);
        setText (p.getCurrentValueAsText(), false);
        selectAll();
        onReturnKey = [this] { finish (true); };
        onEscapeKey = [this] { finish (false); };
        onFocusLost = [this] { finish (true); };
    }
    std::function<void()> onDone;   // the owner takes the field away (not from inside this call)
    /** sets the parameter from a text, as one gesture; false when the text says nothing */
    bool apply (const juce::String& text)
    {
        float v = 0;
        if (! parseTyped (prm, text, v)) return false;
        prm.beginChangeGesture(); prm.setValueNotifyingHost (prm.convertTo0to1 (v)); prm.endChangeGesture();
        return true;
    }
    void finish (bool keep)
    {
        if (done) return;
        done = true;
        if (keep) apply (getText());
        if (onDone) onDone();
    }
    juce::RangedAudioParameter& prm;
private:
    bool done = false;
};
} // namespace fal::ui
