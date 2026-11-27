#pragma once
// umleitung — the window: the slate sidebar (sessions, the pages, the engine's state) and the page it shows.
#include "SessionPage.h"
#include "Pages.h"

namespace um::ui
{
class MainView : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    MainView (Model&, Engine&, PluginHost&);
    ~MainView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void show (int page, int sessionId = 0);
    int currentPage() const { return page; }
    void openPlugin (int blockId);
    void newSession();
    std::function<void (bool)> onDark, onMenuBar;
    /** a picture of every page (tests, the manual) */
    void screenshots (const juce::File& dir, int watchBlock = 0);
    Ctx ctx;
    std::unique_ptr<SessionPage> sessions;
    std::unique_ptr<EqPage> eq;
    std::unique_ptr<DevicesPage> devices;
    std::unique_ptr<MetersPage> meters;
    std::unique_ptr<RecordingsPage> recordings;
    std::unique_ptr<PluginsPage> plugins;
    std::unique_ptr<SettingsPage> settings;
private:
    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint (0, 0, kSide, getHeight()); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint (0, 0, kSide, getHeight()); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint (0, 0, kSide, getHeight()); }
    PageBase* pageAt (int p);
    std::vector<juce::ValueTree> sessionList() const;
    int page = Sessions, sessionShown = 0;
    juce::String statusText; juce::uint32 statusAt = 0;
    std::map<int, std::unique_ptr<juce::DocumentWindow>> pluginWindows;
    static constexpr int kSide = 220;
};
} // namespace um::ui
