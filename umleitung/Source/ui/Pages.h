#pragma once
// umleitung — the other pages: the system eq (like eqMac), virtual devices (like Loopback), meters, recordings,
// plug-ins, settings.
#include "EqWidgets.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace um::ui
{
// ---- the system eq ----------------------------------------------------------------------------------------------
class EqPage : public PageBase
{
public:
    explicit EqPage (Ctx&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void refresh() override { build(); }
    static const std::vector<std::pair<juce::String, std::array<float, 10>>>& presets();
    void applyPreset (const std::array<float, 10>& g);
    enum Mode { Basic = 0, Ten, ThirtyOne, Parametric, Correction, Speakers, NumModes };
    juce::ValueTree stage (const char* role);
private:
    void build();
    void importCorrection (const juce::String& text, const juce::String& from);
    juce::ValueTree session;
    TextLink onLink, savePreset, presetLink, flat, importLink, pasteLink, clearLink;
    std::unique_ptr<ValueChoice> device, mode, phase, scale;
    std::unique_ptr<ValueToggle> perDevice, stageOn, protect;
    std::unique_ptr<PreciseSlider> volume, balance, preamp, bass, mid, treble, corrGain;
    std::unique_ptr<FaderBank> faders;
    std::unique_ptr<BandTable> table;
    std::unique_ptr<SpeakerTable> speakers;
    std::unique_ptr<ResponsePlot> plot;
    juce::Viewport tableView;
    MeterBridge levels;
    juce::String lastOutput;
    int currentMode = 1;
};

// ---- virtual devices --------------------------------------------------------------------------------------------
class DevicesPage : public PageBase
{
public:
    explicit DevicesPage (Ctx&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void refresh() override { reload(); }
    void mouseDown (const juce::MouseEvent&) override;
private:
    void reload();
    void apply();
    juce::ValueTree routingSession (const VirtualDevice&, bool make);
    std::vector<VirtualDevice> devs; int sel = -1; bool installed = false; juce::String version, message;
    TextLink install, addDev, removeDev, editRouting, applyLink;
    std::unique_ptr<juce::TextEditor> name; std::unique_ptr<ValueSlider> channels; std::unique_ptr<ValueToggle> hidden;
    int tickCount = 0;
};

// ---- meters ------------------------------------------------------------------------------------------------------
class MetersPage : public PageBase
{
public:
    explicit MetersPage (Ctx&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void watch (int blockId);
    int watched() const { return bridge.src.blockId; }
    void visibilityChanged() override;
private:
    TextLink choose;
    MeterBridge bridge; VuMeter vuL, vuR; LoudnessView loud; Spectrum spectrum; Spectrogram gram; Goniometer gonio; Scope scope;
};

// ---- recordings ---------------------------------------------------------------------------------------------------
class RecordingsPage : public PageBase, private juce::ChangeListener
{
public:
    explicit RecordingsPage (Ctx&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override { scan(); }
    void visibilityChanged() override { if (isVisible()) scan(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jmax (0, scroll - (int) (w.deltaY * 200)); repaint(); }
    void scan();
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    struct Item { juce::File file; juce::Time when; double seconds = 0, rate = 0; int channels = 0, bits = 0; };
    std::vector<Item> items; int sel = -1, scroll = 0;
    juce::AudioFormatManager formats; juce::AudioThumbnailCache cache { 8 }; juce::AudioThumbnail thumb { 512, formats, cache };
    TextLink reveal, open, trash, rescan;
};

// ---- plug-ins ----------------------------------------------------------------------------------------------------
class PluginsPage : public PageBase, private juce::Thread
{
public:
    explicit PluginsPage (Ctx&);
    ~PluginsPage() override { stopThread (4000); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override { if (isThreadRunning() || done) { done = false; repaint(); } }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jmax (0, scroll - (int) (w.deltaY * 200)); repaint(); }
private:
    void run() override;
    TextLink scan, forget;
    std::atomic<float> progress { 0 }; juce::String current; juce::CriticalSection lock; std::atomic<bool> done { false }; int scroll = 0, found = 0;
};

// ---- settings -----------------------------------------------------------------------------------------------------
class SettingsPage : public PageBase
{
public:
    explicit SettingsPage (Ctx&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override { build(); }
    std::function<void (bool)> onDark, onMenuBar;
private:
    void build();
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::pair<juce::Component*, int>> rows;
};
} // namespace um::ui
