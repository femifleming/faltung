// umleitung — the app: the model (saved in ~/Library/Application Support/umleitung), the engine, the window, and the
// menu bar item (sessions on and off, the system eq and its presets).
//   umleitung --scan-plugin <format> <file> <out.xml>    (the plug-in scanner's child process)
//   umleitung --screenshots <dir> [--demo]              (a picture of every page, then quit)
#include "ui/MainView.h"

namespace um
{
class MenuBarItem : public juce::SystemTrayIconComponent
{
public:
    MenuBarItem (Model& m, Engine& e, std::function<void()> showWindow) : model (m), engine (e), show (std::move (showWindow))
    {
        juce::Image img (juce::Image::ARGB, 44, 44, true);
        { juce::Graphics g (img); g.setColour (juce::Colours::black); auto r = juce::Rectangle<float> (6, 10, 32, 24); g.fillPath (fal::ui::skewRect (r, 0.4f)); g.setColour (juce::Colours::transparentBlack); }
        setIconImage (img, img);
        setIconTooltip ("umleitung");
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m; int k = 1; std::vector<juce::ValueTree> ss;
        auto eqs = engine.systemEqSession();
        m.addItem (900, "system eq", true, (bool) eqs[ids::on]);
        juce::PopupMenu pre; int pk = 1000; for (auto& [n, g] : ui::EqPage::presets()) pre.addItem (pk++, n);
        m.addSubMenu ("eq preset", pre);
        m.addSeparator();
        for (auto s : model.root) if (s.hasType (ids::session) && s[ids::kind].toString() != "system eq") { ss.push_back (s); m.addItem (k++, s[ids::name].toString(), true, (bool) s[ids::on]); }
        m.addSeparator(); m.addItem (800, "open umleitung"); m.addItem (801, "quit");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, ss, eqs] (int r) mutable
        {
            if (r >= 1 && r <= (int) ss.size()) ss[(size_t) r - 1].setProperty (ids::on, ! (bool) ss[(size_t) r - 1][ids::on], nullptr);
            else if (r == 900) eqs.setProperty (ids::on, ! (bool) eqs[ids::on], nullptr);
            else if (r >= 1000) { const auto& p = ui::EqPage::presets()[(size_t) (r - 1000)]; for (auto b : eqs) if (b.hasType (ids::block) && typeFromId (b[ids::type]) == BlockType::Eq) b.setProperty (ids::bands, Model::graphicText (p.second), nullptr); eqs.setProperty (ids::on, true, nullptr); }
            else if (r == 800) show();
            else if (r == 801) juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    }
    Model& model; Engine& engine; std::function<void()> show;
};

class App : public juce::JUCEApplication, private juce::ValueTree::Listener, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "umleitung"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    static juce::File dataDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/umleitung"); }

    void initialise (const juce::String& cl) override
    {
        const auto args = juce::StringArray::fromTokens (cl, true);
        if (args.size() >= 4 && args[0] == "--scan-plugin") { setApplicationReturnValue (PluginHost::scanChild (args[1].unquoted(), args[2].unquoted(), juce::File (args[3].unquoted()))); quit(); return; }
        const int shotsAt = args.indexOf ("--screenshots");
        const bool shots = shotsAt >= 0 && shotsAt + 1 < args.size();
        dataDir().createDirectory();
        file = shots ? juce::File::createTempFile (".xml") : dataDir().getChildFile ("umleitung.xml");
        if (auto x = juce::XmlDocument::parse (file)) { auto t = juce::ValueTree::fromXml (*x); if (t.hasType (ids::umleitung)) model.root = t; }
        fal::ui::setDark ((bool) model.settings()["dark"]);
        plugins = std::make_unique<PluginHost> (dataDir());
        engine = std::make_unique<Engine> (model, plugins.get());
        if (shots && (args.contains ("--demo") || args.contains ("--demo-live"))) demo (args.contains ("--demo-live"));
        window = std::make_unique<Window> (*this);
        model.root.addListener (this);
        if (! shots && (bool) model.settings().getProperty ("menubar", true)) menuBar = std::make_unique<MenuBarItem> (model, *engine, [this] { window->setVisible (true); window->toFront (true); });
        if (shots) { auto* v = window->view(); v->screenshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[shotsAt + 1].unquoted()), watchBlock); quit(); }
    }
    void shutdown() override
    {
        save();
        menuBar.reset(); window.reset();
        engine.reset(); plugins.reset();
    }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (window) { window->setVisible (true); window->toFront (true); } }

    void save() { if (auto x = model.root.createXml()) x->writeTo (file); }
    /** sessions to look at in the screenshots */
    int watchBlock = 0;
    void demo (bool live)
    {
        auto s = model.addSession ("call to virtual device");
        auto app = model.addBlock (s, BlockType::App, 20, 40); app.setProperty (ids::name, "music", nullptr); app.setProperty (ids::bundles, "com.apple.Music", nullptr);
        auto mic = model.addBlock (s, BlockType::Tone, 20, 160); mic.setProperty (ids::name, "test tone", nullptr);
        auto pl = model.addBlock (s, BlockType::Peq, 250, 100);
        auto lev = model.addBlock (s, BlockType::Leveler, 480, 100);
        auto out = model.addBlock (s, BlockType::Output, 710, 40); out.setProperty (ids::name, "virtual device", nullptr);
        auto rec = model.addBlock (s, BlockType::Recorder, 710, 160);
        auto met = model.addBlock (s, BlockType::Meter, 710, 280);
        model.connect (s, app[ids::id], pl[ids::id]); model.connect (s, mic[ids::id], pl[ids::id]); model.connect (s, pl[ids::id], lev[ids::id]);
        for (auto b : { out, rec, met }) model.connect (s, lev[ids::id], b[ids::id]);
        model.addSession ("podcast");
        if (live)
        {
            // a sweep and a tone through the parametric eq, out through BlackHole 16ch (silent), metered
            model.settings().setProperty (ids::device, "BlackHole16ch_UID", nullptr);
            app.setProperty (ids::bypass, true, nullptr);
            mic.setProperty (ids::shape, 3, nullptr); mic.setProperty (ids::level, -14.0f, nullptr);
            auto t2 = model.addBlock (s, BlockType::Tone, 20, 280); t2.setProperty (ids::shape, 1, nullptr); t2.setProperty (ids::level, -30.0f, nullptr); t2.setProperty (ids::name, "pink noise", nullptr);
            model.connect (s, t2[ids::id], pl[ids::id]);
            pl.setProperty (ids::bands, Model::peqText ({ { 3, 60, 0, 0.7f, true }, { 0, 2500, 6, 1.5f, true }, { 2, 9000, -4, 0.7f, true } }), nullptr);
            out.setProperty (ids::device, "BlackHole16ch_UID", nullptr);
            rec.setProperty (ids::armed, false, nullptr);
            s.setProperty (ids::on, true, nullptr);
            engine->rebuild();
            juce::Thread::sleep (1500);
            watchBlock = met[ids::id];
        }
    }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        explicit Window (App& a) : DocumentWindow ("umleitung", juce::Colour (fal::ui::palette::slate::sidebar), DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);
            auto* v = new ui::MainView (a.model, *a.engine, *a.plugins);
            v->onDark = [this] (bool d) { fal::ui::setDark (d); app.save(); juce::MessageManager::callAsync ([this] { rebuild(); }); };
            v->onMenuBar = [this] (bool on) { if (on && ! app.menuBar) app.menuBar = std::make_unique<MenuBarItem> (app.model, *app.engine, [this] { setVisible (true); toFront (true); }); else if (! on) app.menuBar.reset(); };
            v->setSize (1440, 900);
            setContentOwned (v, true);
            setResizable (true, false); setResizeLimits (1100, 700, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        ui::MainView* view() { return dynamic_cast<ui::MainView*> (getContentComponent()); }
        void rebuild() { const auto b = getBounds(); auto* v = new ui::MainView (app.model, *app.engine, *app.plugins); v->onDark = view()->onDark; v->onMenuBar = view()->onMenuBar; setContentOwned (v, false); setBounds (b); }
        void closeButtonPressed() override
        {
            // with the menu bar item, closing the window leaves umleitung running (the sessions go on)
            if (app.menuBar) setVisible (false); else juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
        App& app;
    };
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { startTimer (1500); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { startTimer (1500); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { startTimer (1500); }
    void timerCallback() override { stopTimer(); save(); }

    Model model; juce::File file;
    std::unique_ptr<PluginHost> plugins;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Window> window;
    std::unique_ptr<MenuBarItem> menuBar;
};
} // namespace um

START_JUCE_APPLICATION (um::App)
