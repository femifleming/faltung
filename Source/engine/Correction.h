#pragma once
// umleitung — reading filters measured elsewhere: Room EQ Wizard's exported filter settings, Equalizer APO configs
// and AutoEQ's ParametricEQ.txt (they share one line format):
//   Preamp: -6.2 dB
//   Channel: L                                    (APO: the filters after it are for the left channel; L R or all)
//   Filter 1: ON PK Fc 63.50 Hz Gain -4.30 dB Q 2.500
//   Filter: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.71   (LS / LSC / LS 6dB / LS 12dB, HS …, LP, HP, LPQ, HPQ, NO, BP, AP)
//   Filter 3: ON PK Fc 1000 Hz Gain 2 dB BW Oct 0.5
#include "Model.h"

namespace um
{
struct Correction { std::vector<PeqBand> bands; double preamp = 0; int read = 0, skipped = 0; };

inline Correction parseCorrection (const juce::String& text)
{
    Correction c; int channel = 0;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWith ("#")) continue;
        auto lower = line.toLowerCase();
        if (lower.startsWith ("preamp:")) { c.preamp += lower.fromFirstOccurrenceOf ("preamp:", false, false).getDoubleValue(); continue; }
        if (lower.startsWith ("channel:"))
        {
            const auto ch = lower.fromFirstOccurrenceOf ("channel:", false, false).trim();
            channel = ch == "l" || ch == "1" ? 1 : ch == "r" || ch == "2" ? 2 : 0;
            continue;
        }
        if (! lower.startsWith ("filter") || ! lower.contains (":")) continue;   // ("Filter Settings file", REW's title line)
        auto t = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (":", false, false), " \t", ""); t.removeEmptyStrings();
        if (t.size() < 2 || t[0].toLowerCase() != "on") { if (t.size() >= 1 && t[0].toLowerCase() == "off") continue; ++c.skipped; continue; }
        auto value = [&] (const juce::String& key) { for (int i = 0; i + 1 < t.size(); ++i) if (t[i].toLowerCase() == key) return t[i + 1].getDoubleValue(); return std::numeric_limits<double>::quiet_NaN(); };
        const auto type = t[1].toUpperCase();
        PeqBand b; b.ch = channel;
        b.freq = value ("fc"); b.db = value ("gain"); if (std::isnan (b.db)) b.db = 0;
        double q = value ("q");
        // bandwidth in octaves instead of q
        for (int i = 0; i + 2 < t.size(); ++i) if (t[i].toLowerCase() == "bw" && t[i + 1].toLowerCase() == "oct") { const double n = t[i + 2].getDoubleValue(); if (n > 0) q = std::sqrt (std::pow (2.0, n)) / (std::pow (2.0, n) - 1.0); }
        if (type == "PK" || type == "PEQ" || type == "MODAL") b.shape = 0;
        else if (type.startsWith ("LS")) { b.shape = 1; if (std::isnan (q)) q = type.contains ("6") ? 0.5 : 0.7071; }
        else if (type.startsWith ("HS")) { b.shape = 2; if (std::isnan (q)) q = type.contains ("6") ? 0.5 : 0.7071; }
        else if (type.startsWith ("HP")) { b.shape = 3; b.slope = 12; }
        else if (type.startsWith ("LP")) { b.shape = 4; b.slope = 12; }
        else if (type == "NO") b.shape = 5;
        else if (type == "BP") b.shape = 6;
        else if (type == "AP") b.shape = 7;
        else { ++c.skipped; continue; }
        b.q = std::isnan (q) ? 0.7071 : q;
        if (std::isnan (b.freq) || b.freq <= 0) { ++c.skipped; continue; }
        c.bands.push_back (b); ++c.read;
    }
    return c;
}
} // namespace um
