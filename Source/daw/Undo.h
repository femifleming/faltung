#pragma once
// faltung sndstudio — undo / redo for everything that is saved in a project. Every edit counts itself (Session::dirty);
// when the count has moved and no mouse button is held (a drag is one step), the history keeps the project as it was
// before. Undo puts that project back (the transport keeps going from where it was); redo goes forward again.
// Message thread only. Opening a project or starting a new one begins a new history.
#include "Session.h"
#include <map>

namespace fal::daw
{
class UndoHistory
{
public:
    explicit UndoHistory (Session& s) : session (s) {}
    static constexpr int kDepth = 200;

    /** forget everything: the project as it is now is where undo stops */
    void reset()
    {
        undos.clear(); redos.clear(); undoLabels.clear(); redoLabels.clear(); currentLabel = "the project opened  " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        current = snapshot();
        seenEdits = session.dirty.edits.load(); seenEpoch = session.epoch.load();
    }
    /** call often (the app does, ten times a second): takes a step when something was edited. True when it did */
    bool poll (bool mouseDown = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        if (session.epoch.load() != seenEpoch) { reset(); return false; }   // a project was opened / a template started
        const int e = session.dirty.edits.load();
        if (e == seenEdits || mouseDown || session.isRecording()) return false;
        seenEdits = e;
        auto now = snapshot();
        if (now == current) return false;
        const auto what = describe (current, now);
        undos.push_back (std::move (current)); undoLabels.push_back (currentLabel);
        if ((int) undos.size() > kDepth) { undos.erase (undos.begin()); undoLabels.erase (undoLabels.begin()); }
        redos.clear(); redoLabels.clear();
        current = std::move (now); currentLabel = what + "  " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        return true;
    }
    bool canUndo() const { return ! undos.empty(); }
    bool canRedo() const { return ! redos.empty(); }
    int undoSteps() const { return (int) undos.size(); }
    int redoSteps() const { return (int) redos.size(); }

    bool undo()
    {
        poll (false);   // an edit not yet taken in is a step of its own
        if (undos.empty() || session.isRecording()) return false;
        redos.push_back (current); redoLabels.push_back (currentLabel);
        current = undos.back(); undos.pop_back(); currentLabel = undoLabels.back(); undoLabels.pop_back();
        restore (current);
        return true;
    }
    bool redo()
    {
        poll (false);
        if (redos.empty() || session.isRecording()) return false;
        undos.push_back (current); undoLabels.push_back (currentLabel);
        current = redos.back(); redos.pop_back(); currentLabel = redoLabels.back(); redoLabels.pop_back();
        restore (current);
        return true;
    }

    /** every step, oldest first: the steps that can be undone, the project as it is (marked), the ones that can be redone */
    struct Entry { juce::String label; int index; bool now; };
    std::vector<Entry> entries() const
    {
        std::vector<Entry> v; int i = 0;
        for (auto& l : undoLabels) v.push_back ({ l, i++, false });
        v.push_back ({ currentLabel, i++, true });
        for (int k = (int) redoLabels.size() - 1; k >= 0; --k) v.push_back ({ redoLabels[(size_t) k], i++, false });
        return v;
    }
    /** to any step: undo or redo as many times as it takes */
    void jumpTo (int index)
    {
        const int now = (int) undos.size();
        if (index < now) for (int k = 0; k < now - index; ++k) undo();
        else for (int k = 0; k < index - now; ++k) redo();
    }
    /** what changed between two snapshots: the first part of the project that differs, named */
    static juce::String describe (const juce::String& a, const juce::String& b)
    {
        auto xa = juce::XmlDocument::parse (a), xb = juce::XmlDocument::parse (b);
        if (! xa || ! xb) return "a change";
        static const std::map<juce::String, juce::String> names { { "clip", "clips" }, { "aclip", "audio clips" }, { "pattern", "a pattern" }, { "mixer", "the mixer" }, { "insert", "a device" },
            { "instrument", "the instrument" }, { "mod", "modulators" }, { "route", "routes" }, { "groove", "the groove" }, { "midifx", "midi effects" }, { "follow", "follow actions" }, { "scale", "the scale" } };
        auto nameOf = [] (const juce::XmlElement& e) { return e.getStringAttribute ("name").isNotEmpty() ? e.getStringAttribute ("name") : e.getTagName(); };
        int ia = 0;
        for (auto* ca = xa->getFirstChildElement(), * cb = xb->getFirstChildElement(); ca || cb; ca = ca ? ca->getNextElement() : nullptr, cb = cb ? cb->getNextElement() : nullptr, ++ia)
        {
            if (! ca || ! cb) return cb ? "added " + cb->getTagName() + " " + nameOf (*cb) : "removed " + ca->getTagName() + " " + nameOf (*ca);
            if (ca->isEquivalentTo (cb, false)) continue;
            const auto tag = ca->getTagName();
            if (tag == "track" || tag == "return" || tag == "master")
            {
                if (ca->getStringAttribute ("name") != cb->getStringAttribute ("name")) return "renamed " + ca->getStringAttribute ("name") + " to " + cb->getStringAttribute ("name");
                for (auto* sa = ca->getFirstChildElement(), * sb = cb->getFirstChildElement(); sa || sb; sa = sa ? sa->getNextElement() : nullptr, sb = sb ? sb->getNextElement() : nullptr)
                {
                    if (sa && sb && sa->isEquivalentTo (sb, false)) continue;
                    const auto st = (sa ? sa : sb)->getTagName();
                    return nameOf (*cb) + ": " + (names.count (st) ? names.at (st) : st);
                }
                return nameOf (*cb) + ": its settings";
            }
            if (tag == "lane") return "automation";
            if (tag == "marker") return "markers";
            if (tag == "meter") return "time signatures";
            if (tag == "scene") return "scenes";
            if (tag == "song") return "the song";
            if (tag == "global") return "tempo, swing, macros or the master";
            if (tag == "group") return "groups";
            if (tag == "rule") return "rules";
            return tag;
        }
        for (auto* att : { "loop", "loopStart", "loopEnd", "mode", "grid", "timebase", "ruler" }) if (xa->getStringAttribute (att) != xb->getStringAttribute (att)) return juce::String ("the ") + att;
        return "settings";
    }

private:
    std::vector<juce::String> undoLabels, redoLabels; juce::String currentLabel { "the project opened" };
    juce::String snapshot() const { return session.save()->toString(); }
    void restore (const juce::String& xml)
    {
        auto x = juce::XmlDocument::parse (xml);
        if (! x) return;
        const bool wasRunning = session.running.load();
        const double bar = session.currentBar();
        const auto file = session.projectFile; const auto name = session.projectName;
        juce::String err;
        session.load (*x, &err);
        session.projectFile = file; session.projectName = name;
        session.dirty = true;
        seenEdits = session.dirty.edits.load(); seenEpoch = session.epoch.load();
        session.locate (bar);
        if (wasRunning) session.play();
    }
    Session& session;
    std::vector<juce::String> undos, redos;
    juce::String current;
    int seenEdits = -1, seenEpoch = -1;
};
} // namespace fal::daw
