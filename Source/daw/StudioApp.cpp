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
        const auto self = juce::SystemStats::getEnvironmentVariable ("SNDSTUDIO_SELFTEST", {});
        if (self.isNotEmpty())
        {
            // the self-check opens one output device, by name (the built-in speakers unless SNDSTUDIO_DEVICE says otherwise),
            // no input, at the rate the device already runs at, and nothing it plays reaches the device: the output is
            // measured and then silenced. It never falls back to another device.
            selfTesting = true;
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup.outputDeviceName = juce::SystemStats::getEnvironmentVariable ("SNDSTUDIO_DEVICE", "MacBook Pro Speakers");
            setup.inputDeviceName = {}; setup.useDefaultInputChannels = false; setup.inputChannels.clear(); setup.useDefaultOutputChannels = true;
            selfDeviceError = deviceManager.initialise (0, kOutChannels, nullptr, false, {}, &setup);
        }
        else deviceManager.initialise (kOutChannels, kOutChannels, saved.get(), true);
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
        // SNDSTUDIO_SELFTEST=<folder>: the app drives itself once (pages, playback, the outputs, recording) and writes
        // pictures and a log there, then quits. For checking a build on a real audio device without hands (and without sound).
        if (self.isNotEmpty()) { selfTest = std::make_unique<SelfTest> (*this, juce::File (self)); }
    }
    void shutdown() override
    {
        selfTest.reset();
        juce::MenuBarModel::setMacMainMenu (nullptr);
        deviceManager.removeAudioCallback (this);
        deviceManager.removeMidiInputDeviceCallback ({}, &collector);
        if (! selfTesting)   // a self-test leaves the user's settings alone
        {
            if (auto x = deviceManager.createStateXml()) settings->setValue ("audio", x.get());
            if (processor->session.projectFile.existsAsFile()) settings->setValue ("lastProject", processor->session.projectFile.getFullPathName());
            settings->saveIfNeeded();
        }
        window.reset();
        processor.reset();
    }
    void systemRequestedQuit() override { if (processor && processor->session.dirty.load() && ! selfTesting) processor->autosave(); quit(); }

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
        if (selfTesting)   // what the device takes and gives, for the self-test's log
        {
            for (int c = 0; c < nOut && c < kOutChannels; ++c) { const auto r = juce::FloatVectorOperations::findMinAndMax (out[c], n); const float pk = std::max (std::abs (r.getStart()), std::abs (r.getEnd())); if (pk > outPeak[(size_t) c].load()) outPeak[(size_t) c].store (pk); }
            for (int c = 0; c < nIn && c < kOutChannels; ++c) { const auto r = juce::FloatVectorOperations::findMinAndMax (in[c], n); const float pk = std::max (std::abs (r.getStart()), std::abs (r.getEnd())); if (pk > inPeak[(size_t) c].load()) inPeak[(size_t) c].store (pk); }
            for (int c = 0; c < nOut; ++c) juce::FloatVectorOperations::clear (out[c], n);   // measured, not played
            callbacks.fetch_add (1);
        }
    }

    // ---- menu -------------------------------------------------------------------------------------
    juce::StringArray getMenuBarNames() override { return { "file", "audio" }; }
    StudioEditor* studioEditor() const { return window ? window->studio() : nullptr; }
    juce::PopupMenu getMenuForIndex (int i, const juce::String&) override
    {
        juce::PopupMenu m;
        if (i == 0) { if (auto* e = studioEditor()) m = e->fileMenu(); }
        else
        {
            m.addItem (10, "audio and midi settings...");
            juce::PopupMenu c; c.addItem (20, "off", true, clockOut == nullptr);
            int k = 21; for (auto& d : juce::MidiOutput::getAvailableDevices()) { c.addItem (k++, d.name, true, clockOut && clockOut->getIdentifier() == d.identifier); }
            m.addSubMenu ("midi clock out", c);
        }
        return m;
    }
    void menuItemSelected (int id, int menu) override
    {
        if (menu == 0) { if (auto* e = studioEditor()) e->fileAction (id); return; }
        if (id >= 21) { int k = 21; for (auto& d : juce::MidiOutput::getAvailableDevices()) { if (k++ == id) setClockOut (d.identifier); } return; }
        if (id == 20) { setClockOut ({}); return; }
        if (id == 10)
        {
            auto* sel = new juce::AudioDeviceSelectorComponent (deviceManager, 0, kOutChannels, 2, kOutChannels, true, false, false, false);
            sel->setSize (560, 640);
            juce::DialogWindow::LaunchOptions lo;
            lo.content.setOwned (sel); lo.dialogTitle = "audio and midi settings"; lo.componentToCentreAround = window.get();
            lo.dialogBackgroundColour = juce::Colour (studioInfo().ground); lo.useNativeTitleBar = true; lo.resizable = true;
            lo.launchAsync();
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
        StudioEditor* studio() const { return dynamic_cast<StudioEditor*> (editor.get()); }
        void closeButtonPressed() override { app.systemRequestedQuit(); }
    private:
        StudioApp& app;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };

    // ---- the self-test ------------------------------------------------------------------------------
    struct SelfTest : juce::Timer
    {
        SelfTest (StudioApp& a, juce::File d) : app (a), dir (std::move (d))
        {
            dir.createDirectory();
            log = dir.getChildFile ("selftest.txt"); log.deleteFile();
            if (app.selfDeviceError.isNotEmpty()) say ("audio device: " + app.selfDeviceError);
            startTimer (700);
        }
        void say (const juce::String& line) { log.appendText (line + "\n"); }
        void shot (const juce::String& name)
        {
            auto* e = app.studioEditor(); if (! e) return;
            const auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
            const auto f = dir.getChildFile (name + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        }
        void clearPeaks() { for (auto& p : app.outPeak) p.store (0.0f); for (auto& p : app.inPeak) p.store (0.0f); }
        juce::String peaks (const std::array<std::atomic<float>, kOutChannels>& a, int n) const
        {
            juce::String s; for (int c = 0; c < n; ++c) s << (c ? " " : "") << juce::String (juce::Decibels::gainToDecibels (a[(size_t) c].load(), -120.0f), 0);
            return s;
        }
        int lit (const std::array<std::atomic<float>, kOutChannels>& a, int from, int to) const { int k = 0; for (int c = from; c < to; ++c) if (a[(size_t) c].load() > 1.0e-4f) ++k; return k; }
        void timerCallback() override
        {
            auto& s = app.processor->session; auto* e = app.studioEditor();
            if (! e) { juce::JUCEApplicationBase::quit(); return; }
            switch (step++)
            {
                case 0:
                    if (auto* d = app.deviceManager.getCurrentAudioDevice())
                        say ("device: " + d->getName() + "  " + juce::String (d->getCurrentSampleRate(), 0) + " hz  buffer " + juce::String (d->getCurrentBufferSizeSamples()) + "  outputs " + juce::String (d->getActiveOutputChannels().countNumberOfSetBits()) + "  inputs " + juce::String (d->getActiveInputChannels().countNumberOfSetBits()));
                    else say ("no audio device");
                    s.newFromTemplate (1); s.launchScene (4);
                    break;
                case 1: e->showPage (0); break;
                case 2: case 3: case 4: case 5: case 6: shot ("app-page-" + juce::String (step - 3)); e->showPage (step - 2); break;
                case 7: shot ("app-page-5"); e->showPage (0); clearPeaks(); s.play(); break;
                case 8: case 9: break;
                case 10:
                    say ("produce, the starter playing: output channels 1-8 (db) " + peaks (app.outPeak, 8) + "   audio callbacks " + juce::String (app.callbacks.load()));
                    shot ("app-session-playing"); e->showPage (3); break;
                case 11: shot ("app-mix-playing"); s.assignOutputs(); clearPeaks(); break;
                case 12: case 13: break;
                case 14:
                    say ("multi-out, assign all: output channels 1-24 (db) " + peaks (app.outPeak, 24) + "   channels with sound in 3..22: " + juce::String (lit (app.outPeak, 2, 22)));
                    shot ("app-mix-multiout");
                    s.stop(); s.newFromTemplate (1); s.launchScene (4);
                    // record what the master plays onto a new audio track, in real time (no device input is opened in a self-check)
                    if (auto* t = s.addTrack ("resampled", -1, TrackKind::Audio)) { t->armed.store (true); t->inputSource.store (0); t->monitor.store ((int) Monitor::Off); t->toMaster.store (false); recUid = t->uid; }
                    s.projectFile = dir.getChildFile ("selftest.fstudio"); s.projectName = "selftest";
                    s.locate (0.0); s.startRecording(); s.play();
                    break;
                case 15: case 16: case 17: case 18: break;
                case 19:
                {
                    s.stopRecording(); s.stop();
                    auto* t = s.trackByUid (recUid);
                    if (t && t->aclips->size() == 1 && (*t->aclips)[0].sample)
                    {
                        const auto& c = (*t->aclips)[0];
                        say ("recorded a clip: " + juce::String (c.sample->seconds(), 2) + " s, " + juce::String (c.sample->data.getNumChannels()) + " channels, peak " + juce::String (juce::Decibels::gainToDecibels (c.sample->data.getMagnitude (0, c.sample->data.getNumSamples()), -120.0f), 1) + " db, at bar " + juce::String (c.startBar + 1.0, 3) + "  file " + juce::File (c.path).getFileName());
                    }
                    else say ("recording: no clip (" + juce::String (t ? (int) t->aclips->size() : -1) + ")");
                    e->showPage (1); break;
                }
                case 20: shot ("app-arrange-recorded"); s.newFromTemplate (7); s.launchScene (0); s.setWidth (*s.master, 24); if (s.tracks.size() > 4) { s.tracks[3]->mixer->setBase (MP_MUTE, 0.0f); s.tracks[4]->mixer->setBase (MP_MUTE, 1.0f); } clearPeaks(); s.play(); e->showPage (3); break;
                case 21: case 22: break;
                case 23:
                    say ("spatial template, master 24 wide: outputs with sound in 1..24: " + juce::String (lit (app.outPeak, 0, 24)) + ", in 25..128: " + juce::String (lit (app.outPeak, 24, kOutChannels)) + "   master meters moving: " + juce::String ([&] { int k = 0; for (int c = 0; c < 24; ++c) if (s.master->meter[(size_t) c].load() > 1.0e-5f) ++k; return k; }()));
                    shot ("app-mix-spatial"); s.stop(); break;
                case 24:
                {
                    juce::String err; const auto f = dir.getChildFile ("selftest saved.fstudio");
                    const bool ok = app.processor->saveProject (f, err);
                    const auto a = s.save()->toString();
                    const bool ok2 = app.processor->loadProject (f, err);
                    const auto b = s.save()->toString();
                    // a device's own state blob carries its live value (automation and modulators move it); the project's
                    // base values sit beside it. Compare everything but those blobs.
                    auto bare = [] (juce::String x) { juce::String o; for (;;) { const int i = x.indexOf ("<state>"); if (i < 0) break; const int j = x.indexOf (i, "</state>"); if (j < 0) break; o << x.substring (0, i); x = x.substring (j + 8); } return o + x; };
                    say ("project saved " + juce::String (ok ? "ok" : "FAILED") + ", reopened " + (ok2 ? "ok" : "FAILED " + err) + ", identical " + (a == b ? "yes" : bare (a) == bare (b) ? "yes, apart from live values inside device states" : "NO (saved / reopened xml written beside the log)"));
                    if (a != b) { dir.getChildFile ("roundtrip-a.xml").replaceWithText (a); dir.getChildFile ("roundtrip-b.xml").replaceWithText (b); }
                    break;
                }
                default: say ("done"); stopTimer(); juce::JUCEApplicationBase::quit(); break;
            }
        }
        StudioApp& app; juce::File dir, log; int step = 0, recUid = 0;
    };
    std::unique_ptr<SelfTest> selfTest; bool selfTesting = false; juce::String selfDeviceError;
    std::array<std::atomic<float>, kOutChannels> outPeak {}, inPeak {}; std::atomic<int> callbacks { 0 };

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
