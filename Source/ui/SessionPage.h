#pragma once
// umleitung — a session: blocks on a canvas, wired together (drag from a block's output dot onto another block),
// the selected block's controls on the right, and the session's own switch, timer and templates on top.
#include "Widgets.h"

namespace um::ui
{
/** the controls of one block */
class Inspector : public juce::Component, private juce::Timer
{
public:
    Inspector (Ctx& c, Theme t) : ctx (c), theme (t) { startTimerHz (20); }
    void show (juce::ValueTree b);
    juce::ValueTree block;
    void paint (juce::Graphics& g) override;
    void resized() override;
    int contentHeight = 0;
private:
    void timerCallback() override;
    void add (std::unique_ptr<juce::Component> c, int h) { rows.push_back ({ c.get(), h }); addAndMakeVisible (*c); owned.push_back (std::move (c)); }
    void addLabel (const juce::String& text);
    void addLink (const juce::String& text, std::function<void()> fn, bool lit = false);
    Ctx& ctx; Theme theme;
    struct Row { juce::Component* c; int h; };
    std::vector<Row> rows; std::vector<std::unique_ptr<juce::Component>> owned;
    MeterBridge* meters = nullptr; TextLink* recLink = nullptr; CurveEditor* curve = nullptr; juce::Label* recInfo = nullptr;
};

/** the canvas */
class BlockCanvas : public juce::Component, private juce::Timer
{
public:
    BlockCanvas (Ctx& c, Theme t) : ctx (c), theme (t) { setWantsKeyboardFocus (true); startTimerHz (30); }
    juce::ValueTree session;
    std::function<void (juce::ValueTree)> onSelect;
    int selected = 0; juce::Point<int> selectedWire { -1, -1 };
    static constexpr float kW = 176, kH = 78;

    juce::Rectangle<float> boxOf (const juce::ValueTree& b) const { return { (float) b[ids::x] + origin.x, (float) b[ids::y] + origin.y, kW, kH }; }
    juce::Point<float> outPort (const juce::ValueTree& b) const { auto r = boxOf (b); return { r.getRight() - 4.0f, r.getCentreY() }; }
    juce::Point<float> inPort (const juce::ValueTree& b) const { auto r = boxOf (b); return { r.getX() + 8.0f, r.getCentreY() }; }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void addMenu (juce::Point<float> at);
    void blockMenu (juce::ValueTree b);
    void select (int id) { selected = id; selectedWire = { -1, -1 }; if (onSelect) onSelect (Model::blockById (session, id)); repaint(); }
    juce::ValueTree addBlockAt (BlockType t, juce::Point<float> at);
private:
    void timerCallback() override;
    juce::ValueTree blockAt (juce::Point<float> p) const;
    bool hitOut (const juce::ValueTree& b, juce::Point<float> p) const { return ! isSink (typeFromId (b[ids::type])) && outPort (b).getDistanceFrom (p) < 11.0f; }
    juce::Point<int> wireAt (juce::Point<float> p) const;
    static juce::Path wirePath (juce::Point<float> a, juce::Point<float> b);
    Ctx& ctx; Theme theme;
    juce::Point<float> origin { 20, 20 };
    int dragBlock = 0, wireFrom = 0; juce::Point<float> dragOff, wireEnd; bool panning = false; juce::Point<float> panStart;
    std::map<int, std::vector<float>> levels;   // per block: smoothed peak per channel (for the tiles and the wires)
};

class SessionPage : public PageBase, private juce::ValueTree::Listener
{
public:
    explicit SessionPage (Ctx& c);
    ~SessionPage() override { ctx.model.root.removeListener (this); }
    void setSession (int id);
    int sessionId() const { return canvas.session.isValid() ? (int) canvas.session[ids::id] : 0; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void refresh() override;
    void templates (juce::Component* near);
    void timerDialog();
    BlockCanvas canvas;
    juce::Viewport inspectorView;
    Inspector inspector;
private:
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override { if (t == canvas.session && (p == ids::on || p == ids::name)) repaint(); }
    TextLink run, add, tmpl, timer, rename, remove;
};
} // namespace um::ui
