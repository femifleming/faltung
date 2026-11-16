// faltung sndstudio — the standalone application. Its own audio callback (not JUCE's standalone wrapper) so the
// engine can address up to 128 output channels and 128 input channels of any device (BlackHole 128ch is the way into
// Ableton), with midi inputs, a midi clock output, the audio settings page and the project menu.
#include "StudioEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace fal::daw
{
class StudioApp : public juce::JUCEApplication, private juce::AudioIODeviceCallback, private juce::MenuBarModel
{
public:
    const juce::String getApplicationName() override { return juce::String (kDeep ? "faltung deep " : "faltung ") + studioInfo().code; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "standalone"; o.filenameSuffix = "settings"; o.folderName = Session::dataDir().getFullPathName(); o.storageFormat = juce::PropertiesFile::storeAsXML;
        settings = std::make_unique<juce::PropertiesFile> (Session::dataDir().getChildFile ("standalone.settings"), o);
        processor.reset (dynamic_cast<StudioProcessor*> (createPluginFilter()));
        processor->isStandaloneApp = true;
        processor->midiClockOut = [this] (const juce::MidiMessage& m) { if (clockOut) clockOut->sendMessageNow (m); };
        processor->openAudioSettings = [this] { menuItemSelected (10, 1); };
        std::unique_ptr<juce::XmlElement> saved (settings->getXmlValue ("audio"));
        deviceManager.initialise (kOutChannels, kOutChannels, saved.get(), true);
        for (auto& d : juce::MidiInput::getAvailableDevices()) deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        deviceManager.addMidiInputDeviceCallback ({}, &collector);
        setClockOut (settings->getValue ("clockOut"));
        window = std::make_unique<MainWindow> (*this);
        deviceManager.addAudioCallback (this);
        juce::MenuBarModel::setMacMainMenu (this);
        // the last project, or the starter
        juce::String err;
        const auto last = juce::File (settings->getValue ("lastProject"));
        if (last.existsAsFile() && processor->loadProject (last, err)) {}
        else processor->session.newFromTemplate (1);
    }
    void shutdown() override
    {
        juce::MenuBarModel::setMacMainMenu (nullptr);
        deviceManager.removeAudioCallback (this);
        deviceManager.removeMidiInputDeviceCallback ({}, &collector);
        if (auto x = deviceManager.createStateXml()) settings->setValue ("audio", x.get());
        if (processor->session.projectFile.existsAsFile()) settings->setValue ("lastProject", processor->session.projectFile.getFullPathName());
        settings->saveIfNeeded();
        window.reset();
        processor.reset();
    }
    void systemRequestedQuit() override { if (processor && processor->session.dirty.load()) processor->autosave(); quit(); }

    void setClockOut (const juce::String& identifier)
    {
        clockOut.reset();
        if (identifier.isNotEmpty()) clockOut = juce::MidiOutput::openDevice (identifier);
        settings->setValue ("clockOut", identifier);
    }

    // ---- audio ------------------------------------------------------------------------------------
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        collector.reset (d->getCurrentSampleRate());
        numIn = d->getActiveInputChannels().countNumberOfSetBits();
        processor->standaloneInputs.store (numIn);
        processor->standaloneInputLatency.store (d->getInputLatencyInSamples());
        const int bs = d->getCurrentBufferSizeSamples();
        io.setSize (numIn + kOutChannels, bs, false, false, true);
        const juce::ScopedLock sl (processor->getCallbackLock());
        processor->setRateAndBufferSizeDetails (d->getCurrentSampleRate(), bs);
        processor->prepareToPlay (d->getCurrentSampleRate(), bs);
    }
    void audioDeviceStopped() override { const juce::ScopedLock sl (processor->getCallbackLock()); processor->releaseResources(); }
    void audioDeviceIOCallbackWithContext (const float* const* in, int nIn, float* const* out, int nOut, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        midi.clear();
        collector.removeNextBlockOfMessages (midi, n);
        if (io.getNumSamples() < n || io.getNumChannels() < nIn + nOut) io.setSize (std::max (io.getNumChannels(), nIn + nOut), std::max (n, io.getNumSamples()), false, false, true);
        // inputs first, then the outputs, all in one buffer for the processor
        for (int c = 0; c < nIn; ++c) io.copyFrom (c, 0, in[c], n);
        for (int c = 0; c < nOut; ++c) io.clear (nIn + c, 0, n);
        juce::AudioBuffer<float> b (io.getArrayOfWritePointers(), nIn + nOut, n);
        processor->standaloneInputs.store (nIn);
        {
            const juce::ScopedLock sl (processor->getCallbackLock());
            processor->processBlock (b, midi);
        }
        for (int c = 0; c < nOut; ++c) juce::FloatVectorOperations::copy (out[c], io.getReadPointer (nIn + c), n);
    }

    // ---- menu -------------------------------------------------------------------------------------
    juce::StringArray getMenuBarNames() override { return { "file", "audio" }; }
    juce::PopupMenu getMenuForIndex (int i, const juce::String&) override
    {
        juce::PopupMenu m;
        if (i == 0)
        {
            juce::PopupMenu t; const auto names = Session::templateNames();
            for (int k = 0; k < names.size(); ++k) t.addItem (100 + k, names[k]);
            m.addSubMenu ("new from template", t);
            m.addItem (1, "open..."); m.addItem (2, "save"); m.addItem (3, "save as..."); m.addSeparator();
            m.addItem (4, "export stems..."); m.addItem (7, "export master..."); m.addItem (5, "export midi..."); m.addSeparator();
            m.addItem (6, "open the autosave");
        }
        else
        {
            m.addItem (10, "audio and midi settings...");
            juce::PopupMenu c; c.addItem (20, "off", true, clockOut == nullptr);
            int k = 21; for (auto& d : juce::MidiOutput::getAvailableDevices()) { c.addItem (k++, d.name, true, clockOut && clockOut->getIdentifier() == d.identifier); }
            m.addSubMenu ("midi clock out", c);
        }
        return m;
    }
    void menuItemSelected (int id, int) override
    {
        auto& s = processor->session;
        if (id >= 100 && id < 200) { s.newFromTemplate (id - 100); return; }
        if (id >= 21) { int k = 21; for (auto& d : juce::MidiOutput::getAvailableDevices()) { if (k++ == id) setClockOut (d.identifier); } return; }
        switch (id)
        {
            case 1: chooser = std::make_unique<juce::FileChooser> ("open project", StudioProcessor::projectsDir(), "*.fstudio");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
                    { if (c.getResult().existsAsFile()) { juce::String err; processor->loadProject (c.getResult(), err); report (err); } });
                    break;
            case 2: if (s.projectFile.existsAsFile()) { juce::String err; processor->saveProject (s.projectFile, err); report (err); break; } [[fallthrough]];
            case 3: chooser = std::make_unique<juce::FileChooser> ("save project", StudioProcessor::projectsDir().getChildFile (s.projectName + ".fstudio"), "*.fstudio");
                    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& c)
                    { if (c.getResult() != juce::File()) { juce::String err; processor->saveProject (c.getResult().withFileExtension ("fstudio"), err); report (err); } });
                    break;
            case 4: chooser = std::make_unique<juce::FileChooser> ("export stems into a folder", StudioProcessor::projectsDir());
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& c)
                    {
                        if (c.getResult() == juce::File()) return;
                        auto& ss = processor->session; juce::String err;
                        ss.renderStems (c.getResult().getChildFile (ss.projectName + " stems"), 0.0, ss.projectBars(), nullptr, err);
                        report (err);
                    });
                    break;
            case 7: chooser = std::make_unique<juce::FileChooser> ("export master", StudioProcessor::projectsDir().getChildFile (s.projectName + ".wav"), "*.wav");
                    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
                    {
                        if (c.getResult() == juce::File()) return;
                        auto& ss = processor->session; juce::String err; ExportOptions o; o.limit = true;
                        ss.renderMaster (c.getResult().withFileExtension ("wav"), 0.0, ss.projectBars(), nullptr, err, o); report (err);
                    });
                    break;
            case 5: chooser = std::make_unique<juce::FileChooser> ("export midi", StudioProcessor::projectsDir().getChildFile (s.projectName + ".mid"), "*.mid");
                    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
                    {
                        if (c.getResult() == juce::File()) return;
                        auto& ss = processor->session; juce::String err; ss.exportMidi (c.getResult().withFileExtension ("mid"), 0.0, ss.projectBars(), err); report (err);
                    });
                    break;
            case 6: { juce::String err; if (processor->autosaveFile().existsAsFile()) processor->loadProject (processor->autosaveFile(), err); report (err); break; }
            case 10:
            {
                auto* sel = new juce::AudioDeviceSelectorComponent (deviceManager, 0, kOutChannels, 2, kOutChannels, true, false, false, false);
                sel->setSize (560, 640);
                juce::DialogWindow::LaunchOptions lo;
                lo.content.setOwned (sel); lo.dialogTitle = "audio and midi settings"; lo.componentToCentreAround = window.get();
                lo.dialogBackgroundColour = juce::Colour (studioInfo().ground); lo.useNativeTitleBar = true; lo.resizable = true;
                lo.launchAsync();
                break;
            }
            case 20: setClockOut ({}); break;
            default: break;
        }
    }
    void report (const juce::String& err)
    {
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, getApplicationName(), err);
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (StudioApp& a)
            : DocumentWindow (a.getApplicationName(), juce::Colour (studioInfo().ground), DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);
            editor.reset (a.processor->createEditorIfNeeded());
            setContentNonOwned (editor.get(), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        ~MainWindow() override { clearContentComponent(); app.processor->editorBeingDeleted (editor.get()); editor.reset(); }
        void closeButtonPressed() override { app.systemRequestedQuit(); }
    private:
        StudioApp& app;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };

    std::unique_ptr<StudioProcessor> processor;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::MidiOutput> clockOut;
    juce::AudioDeviceManager deviceManager;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> io;
    int numIn = 0;
};
} // namespace fal::daw

juce::JUCEApplicationBase* juce_CreateApplication() { return new fal::daw::StudioApp(); }
