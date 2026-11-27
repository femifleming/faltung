#pragma once
// umleitung — the model. Everything the user builds is a juce::ValueTree, saved as xml:
//
//   <umleitung>
//     <session id name on>            a signal flow, like Audio Hijack's: blocks wired together; runs while "on"
//       <block id type name x y bypass ...params/>
//       <wire from to gain/>          from block id → to block id (all channels; the receiving block sums)
//     </session>
//     <eq .../>                       the system eq's settings (the eq page; its session is kind="system eq")
//     <settings .../>
//   </umleitung>
//
// Block types (BlockType) are sources (app, system, input, tone, file), processors (volume, eq, peq, plugin,
// matrix, delay, leveler, gate, limiter) and destinations (output, recorder, meter). A virtual device of the
// driver is an input / output block whose device is one of umleitung's own.
#include <juce_data_structures/juce_data_structures.h>
#include <set>
#include <array>

namespace um
{
namespace ids
{
#define UM_ID(x) static const juce::Identifier x { #x };
UM_ID (umleitung) UM_ID (session) UM_ID (block) UM_ID (wire) UM_ID (eq) UM_ID (settings) UM_ID (band)
UM_ID (id) UM_ID (name) UM_ID (on) UM_ID (kind) UM_ID (type) UM_ID (x) UM_ID (y) UM_ID (bypass) UM_ID (from) UM_ID (to) UM_ID (gain)
// sources
UM_ID (pids) UM_ID (bundles) UM_ID (mute) UM_ID (mono) UM_ID (exclude) UM_ID (device) UM_ID (first) UM_ID (count)
UM_ID (freq) UM_ID (level) UM_ID (shape) UM_ID (channels) UM_ID (file) UM_ID (loop)
// processors
UM_ID (db) UM_ID (balance) UM_ID (invert) UM_ID (bands) UM_ID (q) UM_ID (ms) UM_ID (target) UM_ID (threshold) UM_ID (ceiling)
UM_ID (release) UM_ID (matrix) UM_ID (outs) UM_ID (plugin) UM_ID (state) UM_ID (pluginName)
// recorder
UM_ID (folder) UM_ID (pattern) UM_ID (format) UM_ID (bits) UM_ID (splitMin) UM_ID (maxMin) UM_ID (armed) UM_ID (startAt) UM_ID (stopAfter)
UM_ID (pauseSilence) UM_ID (silenceDb)
// eq page
UM_ID (enabled) UM_ID (followDefault) UM_ID (preset) UM_ID (mode) UM_ID (boost) UM_ID (perDevice)
#undef UM_ID
}

enum class BlockType { App, System, Input, Tone, File, Volume, Eq, Peq, Plugin, Matrix, Delay, Leveler, Gate, Limiter, Output, Recorder, Meter, Count };

struct BlockTypeInfo { BlockType type; const char* id; const char* name; const char* group; const char* blurb; };
inline const BlockTypeInfo& typeInfo (BlockType t)
{
    static const BlockTypeInfo infos[] = {
        { BlockType::App,      "app",      "application",   "sources",   "an app's sound (one or several apps), captured as it plays; optionally muted where it plays" },
        { BlockType::System,   "system",   "system audio",  "sources",   "everything the mac plays (all apps, apart from any you leave out)" },
        { BlockType::Input,    "input",    "input device",  "sources",   "a microphone, an interface's inputs, or a virtual device" },
        { BlockType::Tone,     "tone",     "test tone",     "sources",   "a sine, noise or sweep, to check a route" },
        { BlockType::File,     "file",     "file player",   "sources",   "an audio file, played in the session" },
        { BlockType::Volume,   "volume",   "volume",        "effects",   "level, balance, mute, polarity" },
        { BlockType::Eq,       "eq",       "graphic eq",    "effects",   "ten bands, 32 hz to 16 khz" },
        { BlockType::Peq,      "peq",      "parametric eq", "effects",   "bells, shelves and filters, as many as you like" },
        { BlockType::Plugin,   "plugin",   "plug-in",       "effects",   "any VST3 or Audio Unit effect (or instrument) in the path" },
        { BlockType::Matrix,   "matrix",   "channels",      "effects",   "route, split and combine channels: any input channel to any output channel, with a level" },
        { BlockType::Delay,    "delay",     "delay",        "effects",   "delays the sound (to line it up with video or another source)" },
        { BlockType::Leveler,  "leveler",  "leveler",       "effects",   "brings quiet sound up and loud sound down to a steady level (like magic boost)" },
        { BlockType::Gate,     "gate",     "gate",          "effects",   "silences what is below a threshold (room noise between words)" },
        { BlockType::Limiter,  "limiter",  "limiter",       "effects",   "keeps the peaks under a ceiling" },
        { BlockType::Output,   "output",   "output device", "outputs",   "speakers, headphones, an interface's outputs, or a virtual device for other apps" },
        { BlockType::Recorder, "recorder", "recorder",      "outputs",   "writes wav (or aiff, flac) files: any number of channels, split, timed" },
        { BlockType::Meter,    "meter",    "meters",        "outputs",   "vu, peak, loudness, spectrum, spectrogram, stereo field and scope" },
    };
    return infos[juce::jlimit (0, (int) BlockType::Count - 1, (int) t)];
}
inline BlockType typeFromId (const juce::String& s)
{
    for (int i = 0; i < (int) BlockType::Count; ++i) if (s == typeInfo ((BlockType) i).id) return (BlockType) i;
    return BlockType::Volume;
}
inline bool isSource (BlockType t) { return t == BlockType::App || t == BlockType::System || t == BlockType::Input || t == BlockType::Tone || t == BlockType::File; }
inline bool isSink (BlockType t) { return t == BlockType::Output || t == BlockType::Recorder || t == BlockType::Meter; }

/** the graphic eq's centre frequencies */
inline const std::array<float, 10>& graphicFreqs() { static const std::array<float, 10> f { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 }; return f; }

/** a parametric band */
struct PeqBand { int shape = 0; float freq = 1000, db = 0, q = 0.707f; bool on = true; };   // shape: bell, low shelf, high shelf, low cut, high cut, notch, band pass
inline const char* peqShapeName (int s) { static const char* n[] = { "bell", "low shelf", "high shelf", "low cut", "high cut", "notch", "band pass" }; return n[juce::jlimit (0, 6, s)]; }
constexpr int kPeqShapes = 7;

/** helpers to read / write the model */
struct Model
{
    juce::ValueTree root { ids::umleitung };
    juce::UndoManager undo;

    juce::ValueTree settings() { auto s = root.getChildWithName (ids::settings); if (! s.isValid()) { s = juce::ValueTree (ids::settings); root.appendChild (s, nullptr); } return s; }
    juce::ValueTree eqSettings() { auto s = root.getChildWithName (ids::eq); if (! s.isValid()) { s = juce::ValueTree (ids::eq); root.appendChild (s, nullptr); } return s; }

    int nextId()
    {
        int m = 0;
        std::function<void (const juce::ValueTree&)> scan = [&] (const juce::ValueTree& t) { m = std::max (m, (int) t.getProperty (ids::id, 0)); for (auto c : t) scan (c); };
        scan (root);
        return m + 1;
    }
    juce::ValueTree addSession (const juce::String& name)
    {
        juce::ValueTree s (ids::session);
        s.setProperty (ids::id, nextId(), nullptr); s.setProperty (ids::name, name, nullptr); s.setProperty (ids::on, false, nullptr);
        root.appendChild (s, &undo);
        return s;
    }
    juce::ValueTree addBlock (juce::ValueTree session, BlockType t, float x, float y)
    {
        juce::ValueTree b (ids::block);
        b.setProperty (ids::id, nextId(), nullptr); b.setProperty (ids::type, typeInfo (t).id, nullptr);
        b.setProperty (ids::name, typeInfo (t).name, nullptr); b.setProperty (ids::x, x, nullptr); b.setProperty (ids::y, y, nullptr);
        defaults (b, t);
        session.appendChild (b, &undo);
        return b;
    }
    static void defaults (juce::ValueTree& b, BlockType t)
    {
        auto set = [&] (const juce::Identifier& k, const juce::var& v) { if (! b.hasProperty (k)) b.setProperty (k, v, nullptr); };
        switch (t)
        {
            case BlockType::App: set (ids::mute, false); set (ids::mono, false); break;
            case BlockType::System: set (ids::mute, false); break;
            case BlockType::Input: set (ids::first, 0); set (ids::count, 2); break;
            case BlockType::Tone: set (ids::freq, 440.0f); set (ids::level, -18.0f); set (ids::shape, 0); set (ids::channels, 2); break;
            case BlockType::File: set (ids::loop, true); set (ids::level, 0.0f); break;
            case BlockType::Volume: set (ids::db, 0.0f); set (ids::balance, 0.0f); set (ids::mute, false); set (ids::invert, false); break;
            case BlockType::Eq: set (ids::bands, "0 0 0 0 0 0 0 0 0 0"); set (ids::db, 0.0f); break;
            case BlockType::Peq: set (ids::bands, "1 80 0 0.7 1|0 1000 0 1 1|2 8000 0 0.7 1"); set (ids::db, 0.0f); break;
            case BlockType::Matrix: set (ids::outs, 2); set (ids::matrix, ""); break;
            case BlockType::Delay: set (ids::ms, 0.0f); break;
            case BlockType::Leveler: set (ids::target, -18.0f); set (ids::db, 12.0f); break;
            case BlockType::Gate: set (ids::threshold, -50.0f); set (ids::release, 150.0f); break;
            case BlockType::Limiter: set (ids::ceiling, -1.0f); set (ids::release, 80.0f); break;
            case BlockType::Output: set (ids::first, 0); set (ids::level, 0.0f); break;
            case BlockType::Recorder:
                set (ids::folder, juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("umleitung").getFullPathName());
                set (ids::pattern, "{session} {date} {time}"); set (ids::format, "wav"); set (ids::bits, 24);
                set (ids::splitMin, 0); set (ids::maxMin, 0); set (ids::armed, true); set (ids::pauseSilence, false); set (ids::silenceDb, -60.0f);
                break;
            default: break;
        }
    }
    void connect (juce::ValueTree session, int from, int to)
    {
        for (auto w : session) if (w.hasType (ids::wire) && (int) w[ids::from] == from && (int) w[ids::to] == to) return;
        juce::ValueTree w (ids::wire); w.setProperty (ids::from, from, nullptr); w.setProperty (ids::to, to, nullptr); w.setProperty (ids::gain, 1.0f, nullptr);
        session.appendChild (w, &undo);
    }
    static juce::ValueTree blockById (const juce::ValueTree& session, int id) { for (auto b : session) if (b.hasType (ids::block) && (int) b[ids::id] == id) return b; return {}; }
    static juce::ValueTree sessionById (const juce::ValueTree& root, int id) { for (auto s : root) if (s.hasType (ids::session) && (int) s[ids::id] == id) return s; return {}; }
    /** true when wiring from → to would make a loop */
    static bool wouldLoop (const juce::ValueTree& session, int from, int to)
    {
        if (from == to) return true;
        std::vector<int> stack { to }; std::set<int> seen;
        while (! stack.empty())
        {
            const int n = stack.back(); stack.pop_back();
            if (n == from) return true;
            if (! seen.insert (n).second) continue;
            for (auto w : session) if (w.hasType (ids::wire) && (int) w[ids::from] == n) stack.push_back ((int) w[ids::to]);
        }
        return false;
    }
    void removeBlock (juce::ValueTree session, int id)
    {
        for (int i = session.getNumChildren(); --i >= 0;)
        {
            auto c = session.getChild (i);
            if ((c.hasType (ids::wire) && ((int) c[ids::from] == id || (int) c[ids::to] == id)) || (c.hasType (ids::block) && (int) c[ids::id] == id)) session.removeChild (i, &undo);
        }
    }

    // ---- band lists ----------------------------------------------------------------------------------------
    static std::array<float, 10> graphicBands (const juce::ValueTree& b)
    {
        std::array<float, 10> g {}; auto t = juce::StringArray::fromTokens (b[ids::bands].toString(), " ", "");
        for (int i = 0; i < 10 && i < t.size(); ++i) g[(size_t) i] = juce::jlimit (-24.0f, 24.0f, t[i].getFloatValue());
        return g;
    }
    static juce::String graphicText (const std::array<float, 10>& g) { juce::StringArray a; for (float v : g) a.add (juce::String (v, 1)); return a.joinIntoString (" "); }
    static std::vector<PeqBand> peqBands (const juce::ValueTree& b)
    {
        std::vector<PeqBand> v;
        for (auto& s : juce::StringArray::fromTokens (b[ids::bands].toString(), "|", ""))
        {
            auto t = juce::StringArray::fromTokens (s, " ", ""); if (t.size() < 4) continue;
            PeqBand p; p.shape = juce::jlimit (0, kPeqShapes - 1, t[0].getIntValue()); p.freq = juce::jlimit (10.0f, 24000.0f, t[1].getFloatValue());
            p.db = juce::jlimit (-30.0f, 30.0f, t[2].getFloatValue()); p.q = juce::jlimit (0.1f, 30.0f, t[3].getFloatValue()); p.on = t.size() < 5 || t[4].getIntValue() != 0;
            v.push_back (p);
        }
        return v;
    }
    static juce::String peqText (const std::vector<PeqBand>& v)
    {
        juce::StringArray a; for (auto& p : v) a.add (juce::String (p.shape) + " " + juce::String (p.freq, 1) + " " + juce::String (p.db, 2) + " " + juce::String (p.q, 3) + " " + (p.on ? "1" : "0"));
        return a.joinIntoString ("|");
    }
    /** the channel matrix: "in:out:gain" entries */
    struct Cell { int in = 0, out = 0; float gain = 1; };
    static std::vector<Cell> matrixCells (const juce::ValueTree& b)
    {
        std::vector<Cell> v;
        for (auto& s : juce::StringArray::fromTokens (b[ids::matrix].toString(), " ", ""))
        { auto t = juce::StringArray::fromTokens (s, ":", ""); if (t.size() >= 2) v.push_back ({ t[0].getIntValue(), t[1].getIntValue(), t.size() > 2 ? t[2].getFloatValue() : 1.0f }); }
        return v;
    }
    static juce::String matrixText (const std::vector<Cell>& v) { juce::StringArray a; for (auto& c : v) a.add (juce::String (c.in) + ":" + juce::String (c.out) + ":" + juce::String (c.gain, 3)); return a.joinIntoString (" "); }
};
} // namespace um
