#include "MainView.h"

namespace um::ui
{
namespace
{
const juce::Colour kSlate (pal::slate::sidebar);
/** a plug-in's own window; its state goes back into the block when it closes */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* ed, const juce::String& title, std::function<void()> closed)
        : DocumentWindow (title, juce::Colours::black, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true); setContentOwned (ed, true); setResizable (ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight()); setVisible (true);
    }
    void closeButtonPressed() override { if (onClosed) juce::MessageManager::callAsync (onClosed); }
    std::function<void()> onClosed;
};
}

MainView::MainView (Model& m, Engine& e, PluginHost& p)
    : ctx { m, e, p, {}, {}, {}, {} }
{
    ctx.show = [this] (int pg, int sid) { show (pg, sid); };
    ctx.status = [this] (const juce::String& s) { statusText = s; statusAt = juce::Time::getMillisecondCounter(); repaint(); };
    ctx.openPlugin = [this] (int id) { openPlugin (id); };
    ctx.watch = [this] (int id) { show (Meters); meters->watch (id); };
    sessions = std::make_unique<SessionPage> (ctx); eq = std::make_unique<EqPage> (ctx); devices = std::make_unique<DevicesPage> (ctx);
    meters = std::make_unique<MetersPage> (ctx); recordings = std::make_unique<RecordingsPage> (ctx); plugins = std::make_unique<PluginsPage> (ctx); settings = std::make_unique<SettingsPage> (ctx);
    for (int i = 0; i < NumPages; ++i) addChildComponent (pageAt (i));
    settings->onDark = [this] (bool d) { if (onDark) onDark (d); };
    settings->onMenuBar = [this] (bool d) { if (onMenuBar) onMenuBar (d); };
    m.root.addListener (this);
    auto list = sessionList();
    show (Sessions, list.empty() ? 0 : (int) list.front()[ids::id]);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}
MainView::~MainView() { stopTimer(); ctx.model.root.removeListener (this); for (auto& [id, w] : pluginWindows) if (auto* pw = dynamic_cast<PluginWindow*> (w.get())) pw->onClosed = nullptr; pluginWindows.clear(); }

PageBase* MainView::pageAt (int p)
{
    switch (p) { case Sessions: return sessions.get(); case SystemEq: return eq.get(); case Devices: return devices.get(); case Meters: return meters.get(); case Recordings: return recordings.get(); case Plugins: return plugins.get(); default: return settings.get(); }
}
std::vector<juce::ValueTree> MainView::sessionList() const
{
    std::vector<juce::ValueTree> v;
    for (auto s : ctx.model.root) if (s.hasType (ids::session) && s[ids::kind].toString() != "system eq") v.push_back (s);
    return v;
}
void MainView::show (int p, int sid)
{
    page = juce::jlimit (0, NumPages - 1, p);
    if (page == Sessions) { if (sid == 0) { auto l = sessionList(); if (! l.empty()) sid = l.front()[ids::id]; } sessionShown = sid; sessions->setSession (sid); }
    for (int i = 0; i < NumPages; ++i) pageAt (i)->setVisible (i == page);
    pageAt (page)->refresh();
    resized(); repaint();
}
void MainView::newSession()
{
    auto s = ctx.model.addSession ("session " + juce::String ((int) sessionList().size() + 1));
    show (Sessions, s[ids::id]);
}

void MainView::openPlugin (int id)
{
    if (auto it = pluginWindows.find (id); it != pluginWindows.end()) { it->second->toFront (true); return; }
    auto* rt = ctx.engine.runtime (id);
    if (! rt || ! rt->plugin) { ctx.status ("the plug-in isn't running: switch its session on (and choose a plug-in)"); return; }
    auto* ed = rt->plugin->createEditorIfNeeded();
    if (! ed) { ctx.status ("this plug-in has no window of its own"); return; }
    pluginWindows[id] = std::make_unique<PluginWindow> (ed, rt->plugin->getName(), [this, id]
    {
        // keep its state in the session, then close
        if (auto* r = ctx.engine.runtime (id)) if (r->plugin)
            for (auto s : ctx.model.root) if (auto b = Model::blockById (s, id); b.isValid()) { juce::MemoryBlock mb; r->plugin->getStateInformation (mb); b.setProperty (ids::state, mb.toBase64Encoding(), nullptr); }
        pluginWindows.erase (id);
    });
}

void MainView::timerCallback()
{
    if (auto* p = pageAt (page)) p->tick();
    // a plug-in window whose instance went away (its session stopped) closes
    for (auto it = pluginWindows.begin(); it != pluginWindows.end();)
    {
        auto* rt = ctx.engine.runtime (it->first);
        auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (it->second->getContentComponent());
        if (! rt || ! rt->plugin || (ed && ed->getAudioProcessor() != rt->plugin.get())) it = pluginWindows.erase (it); else ++it;
    }
    if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() - statusAt > 6000) { statusText.clear(); }
    repaint (0, 0, kSide, getHeight());
    repaint (kSide, getHeight() - 20, getWidth() - kSide, 20);
}

void MainView::paint (juce::Graphics& g)
{
    // the sidebar
    g.setColour (kSlate); g.fillRect (0, 0, kSide, getHeight());
    const auto ink = inkFor (kSlate);
    g.setColour (ink.withAlpha (0.62f)); g.setFont (mono (10.0f)); g.drawText ("ul1", 20, 16, 60, 12, juce::Justification::centredLeft);
    g.setColour (ink); g.setFont (sans (24.0f, true)); g.drawText ("umleitung", 20, 28, kSide - 30, 30, juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.62f)); g.setFont (sans (11.0f)); g.drawText ("route, process, record the mac's sound", 20, 58, kSide - 24, 14, juce::Justification::centredLeft, true);
    int y = 92;
    g.setFont (mono (10.0f)); g.setColour (ink.withAlpha (0.5f)); g.drawText ("sessions", 20, y, 100, 14, juce::Justification::centredLeft);
    y += 18;
    for (auto s : sessionList())
    {
        auto r = juce::Rectangle<float> (12.0f, (float) y, (float) kSide - 24.0f, 24.0f);
        const bool sel = page == Sessions && (int) s[ids::id] == sessionShown;
        if (sel) { g.setColour (juce::Colour (pageField (Sessions).ground())); g.fillPath (skewRect (r, 0.25f)); }
        const auto c = sel ? inkFor (juce::Colour (pageField (Sessions).ground())) : ink;
        const bool on = s[ids::on];
        g.setColour (on ? juce::Colour (0xff79e04a) : c.withAlpha (0.25f)); g.fillEllipse (r.getX() + 10.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        bool rec = false; for (auto b : s) if (b.hasType (ids::block) && typeFromId (b[ids::type]) == BlockType::Recorder && ctx.engine.recording (b[ids::id])) rec = true;
        g.setColour (c); g.setFont (mono (12.0f)); g.drawText (s[ids::name].toString(), (int) r.getX() + 24, (int) r.getY(), (int) r.getWidth() - 50, 24, juce::Justification::centredLeft, true);
        if (rec) { g.setColour (juce::Colour (0xffd2452c)); g.fillEllipse (r.getRight() - 22.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f); }
        y += 26;
    }
    g.setColour (ink.withAlpha (0.7f)); g.setFont (mono (11.0f)); g.drawText ("+ session", 36, y + 2, 120, 20, juce::Justification::centredLeft);
    y += 36;
    for (int p = 1; p < NumPages; ++p)
    {
        auto r = juce::Rectangle<float> (12.0f, (float) y, (float) kSide - 24.0f, 26.0f);
        const auto fieldCol = juce::Colour (pageField (p).ground());
        if (page == p) { g.setColour (fieldCol); g.fillPath (skewRect (r, 0.25f)); }
        else { g.setColour (fieldCol); g.fillPath (skewRect (juce::Rectangle<float> (r.getX() + 8.0f, r.getCentreY() - 5.0f, 14.0f, 10.0f), 0.5f)); }
        const auto c = page == p ? inkFor (fieldCol) : ink;
        g.setColour (c.withAlpha (0.6f)); g.setFont (mono (9.0f)); g.drawText (pageCode (p), (int) r.getRight() - 40, (int) r.getY(), 32, 26, juce::Justification::centredRight);
        g.setColour (c); g.setFont (mono (12.0f)); g.drawText (pageName (p), (int) r.getX() + 30, (int) r.getY(), 140, 26, juce::Justification::centredLeft);
        if (p == SystemEq) { const bool on = ctx.engine.systemEqSession()[ids::on]; g.setColour (on ? juce::Colour (0xff79e04a) : c.withAlpha (0.25f)); g.fillEllipse (r.getRight() - 52.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f); }
        y += 30;
    }
    // the engine
    auto& h = ctx.engine.host();
    g.setFont (mono (10.0f)); g.setColour (ink.withAlpha (0.62f));
    juce::String st = h.running() ? juce::String (h.layout().sampleRate / 1000.0, 1) + juce::String (juce::CharPointer_UTF8 (" khz · ")) + juce::String (h.layout().bufferSize) + " samples\n"
                                    + juce::String (h.layout().totalIn) + juce::String (juce::CharPointer_UTF8 (" in · ")) + juce::String (h.layout().totalOut) + " out\nload " + juce::String (juce::roundToInt (h.load() * 100)) + juce::String (juce::CharPointer_UTF8 (" % · ")) + juce::String (h.overloads()) + " dropouts"
                                  : juce::String ("nothing running");
    if (ctx.engine.lastError().isNotEmpty()) st << "\n" << ctx.engine.lastError();
    g.drawFittedText (st, 20, getHeight() - 78, kSide - 30, 70, juce::Justification::bottomLeft, 6);
    // the status line
    if (statusText.isNotEmpty()) { g.setColour (juce::Colour (pageField (page).ground()).contrasting (0.8f)); g.setFont (mono (10.0f)); g.drawText (statusText, kSide + 24, getHeight() - 18, getWidth() - kSide - 48, 14, juce::Justification::centredLeft, true); }
}
void MainView::resized()
{
    for (int i = 0; i < NumPages; ++i) pageAt (i)->setBounds (kSide, 0, getWidth() - kSide, getHeight());
}
void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (e.x >= kSide) return;
    int y = 110;
    for (auto s : sessionList())
    {
        if (e.y >= y && e.y < y + 26)
        {
            if (e.mods.isPopupMenu() || e.x < 34) { s.setProperty (ids::on, ! (bool) s[ids::on], nullptr); return; }   // the dot switches it
            show (Sessions, s[ids::id]); return;
        }
        y += 26;
    }
    if (e.y >= y && e.y < y + 24) { newSession(); return; }
    y += 36;
    for (int p = 1; p < NumPages; ++p) { if (e.y >= y && e.y < y + 30) { show (p); return; } y += 30; }
}
bool MainView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { ctx.model.undo.undo(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { ctx.model.undo.redo(); return true; }
    if (k == juce::KeyPress ('n', juce::ModifierKeys::commandModifier, 0)) { newSession(); return true; }
    for (int p = 0; p < NumPages; ++p) if (k == juce::KeyPress ('1' + p, juce::ModifierKeys::commandModifier, 0)) { show (p); return true; }
    return false;
}

void MainView::screenshots (const juce::File& dir, int watchBlock)
{
    dir.createDirectory();
    if (watchBlock) meters->watch (watchBlock);
    for (int p = 0; p < NumPages; ++p)
    {
        show (p, p == Sessions ? sessionShown : 0);
        if (watchBlock && p == Meters) meters->watch (watchBlock);
        for (int k = 0; k < (watchBlock ? 40 : 6); ++k) { if (watchBlock) juce::Thread::sleep (40); timerCallback(); }
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile ("umleitung-" + juce::String (p) + "-" + juce::String (pageName (p)).replace (" ", "-") + ".png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
    }
    // the system eq in each of its modes (with something set, to see it)
    {
        auto es = ctx.model.eqSettings();
        auto st = [&] (const char* r) { return eq->stage (r); };
        PeqBand a; a.shape = 3; a.freq = 22; a.slope = 24; PeqBand b2; b2.freq = 3150; b2.db = -2.5; b2.q = 2.2; PeqBand c2; c2.shape = 2; c2.freq = 9000; c2.db = 1.5; c2.q = 0.7;
        st ("tone").setProperty (ids::bands, Model::peqText ({ a, b2, c2 }), nullptr);
        st ("correction").setProperty (ids::bands, "0 42.5 -6.3 4.1 1 12 0 1|0 42.5 -4.8 3.6 1 12 0 2|0 118 -3.2 6 1 12 0 0|0 260 2.1 2 1 12 0 1", nullptr); st ("correction").setProperty (ids::bypass, false, nullptr);
        st ("speakers").setProperty (ids::align, "-0.35 0.271 0 0|0 0 0 0", nullptr);
        for (int m = 0; m < EqPage::NumModes; ++m)
        {
            es.setProperty (ids::mode, m, nullptr);
            if (m == EqPage::ThirtyOne) { st ("graphic").setProperty ("count", 31, nullptr); std::vector<float> g31 (31, 0.0f); g31[3] = 2.5f; g31[4] = 3; g31[5] = 2; g31[22] = -1.75f; g31[29] = 1.25f; st ("graphic").setProperty (ids::bands, Model::graphicText (g31), nullptr); }
            if (m == EqPage::Ten || m == EqPage::Basic) { st ("graphic").setProperty ("count", 10, nullptr); st ("graphic").setProperty (ids::bands, "3 2 1 0 -0.5 0 0 0.75 1.5 2", nullptr); }
            show (SystemEq); eq->refresh();
            for (int k = 0; k < 8; ++k) { if (watchBlock) juce::Thread::sleep (30); timerCallback(); }
            auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("umleitung-eq-" + juce::String (m) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        }
    }
    // every block type's controls, in a scratch session
    auto scratch = ctx.model.addSession ("every block");
    show (Sessions, scratch[ids::id]);
    for (int t = 0; t < (int) BlockType::Count; ++t)
    {
        auto b = sessions->canvas.addBlockAt ((BlockType) t, { 40.0f + 30.0f * t, 40.0f + 10.0f * t });
        for (int k = 0; k < 4; ++k) timerCallback();
        sessions->resized();
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile ("umleitung-block-" + juce::String (typeInfo ((BlockType) t).id) + ".png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
    }
    scratch.getParent().removeChild (scratch, nullptr);
}
} // namespace um::ui
