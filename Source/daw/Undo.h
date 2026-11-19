#pragma once
// faltung sndstudio — undo / redo for everything that is saved in a project. Every edit counts itself (Session::dirty);
// when the count has moved and no mouse button is held (a drag is one step), the history keeps the project as it was
// before. Undo puts that project back (the transport keeps going from where it was); redo goes forward again.
// Message thread only. Opening a project or starting a new one begins a new history.
#include "Session.h"

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
        undos.clear(); redos.clear();
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
        undos.push_back (std::move (current));
        if ((int) undos.size() > kDepth) undos.erase (undos.begin());
        redos.clear();
        current = std::move (now);
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
        redos.push_back (current);
        current = undos.back(); undos.pop_back();
        restore (current);
        return true;
    }
    bool redo()
    {
        poll (false);
        if (redos.empty() || session.isRecording()) return false;
        undos.push_back (current);
        current = redos.back(); redos.pop_back();
        restore (current);
        return true;
    }

private:
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
