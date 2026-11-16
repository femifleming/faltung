// ObliqSoundTest / ObliqSonicTest: the real processor and editor without a window.
//   ObliqSoundTest [--screenshots dir]
// Checks: the resources are found (layout, presets, SynthDefs, server plug-ins), the server starts, notes
// make sound on every output, presets load, the state survives a round trip, the editor paints. With
// --screenshots it writes a PNG of every page (for the manual).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

// a few blocks with a held note, so the meters and screens have something to show
static void blocks (ObliqProcessor& p, int n, int chans, bool noteOn)
{
    juce::AudioBuffer<float> b (chans, 512);
    for (int i = 0; i < n; ++i)
    {
        b.clear();
        juce::MidiBuffer m;
        if (noteOn && i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), 0);
        p.processBlock (b, m);
    }
}

static float play (ObliqProcessor& p, int seconds, bool notes, float* perChannelPeak, int chans)
{
    const double sr = 48000.0; const int bs = 512;
    juce::AudioBuffer<float> b (chans, bs);
    float peak = 0.0f;
    for (int c = 0; c < chans; ++c) perChannelPeak[c] = 0.0f;
    const int total = (int) (seconds * sr);
    int note = 0;
    for (int pos = 0; pos < total; pos += bs)
    {
        b.clear();
        juce::MidiBuffer m;
        if (notes && pos % (int) (sr / 2) == 0)
        {
            const int key = 48 + (note % 4) * 5;
            m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
            if (note > 0) m.addEvent (juce::MidiMessage::noteOff (1, 48 + ((note - 1) % 4) * 5), 10);
            ++note;
        }
        p.processBlock (b, m);
        for (int c = 0; c < chans; ++c)
        {
            const float pk = b.getMagnitude (c, 0, bs);
            perChannelPeak[c] = std::max (perChannelPeak[c], pk);
            peak = std::max (peak, pk);
        }
    }
    return peak;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File shotDir;
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--screenshots" && i + 1 < argc) shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);

    const auto res = ObliqProcessor::resourceDir();
    std::printf ("resources: %s\n", res.getFullPathName().toRawUTF8());
    check (res.getChildFile ("layout.json").existsAsFile(), "layout.json found");
    check (res.getChildFile ("presets.json").existsAsFile(), "presets.json found");
    check (res.getChildFile ("defs").getNumberOfChildFiles (juce::File::findFiles, "*.scsyndef") > 20, "SynthDefs found");
    check (res.getChildFile ("plugins").getNumberOfChildFiles (juce::File::findFiles, "*") >= 18, "server plug-ins found");
    check (res.getChildFile ("samples").getNumberOfChildFiles (juce::File::findFiles, "*.wav") == 18, "18 recordings found");

    ObliqProcessor p;
    check (p.params.size() > 900, "parameter store built from the layout (" + juce::String (p.params.size()) + " channels)");
    check (p.getParameters().size() > 100, juce::String (p.getParameters().size()) + " host parameters");
    const auto names = p.presetNames();
    check (names.size() > 100, juce::String (names.size()) + " factory presets");

    const int chans = OBLIQ_SONIC ? 16 : 2;
    p.setPlayConfigDetails (0, chans, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    check (p.engine->ready(), "server started: " + p.engine->lastError);
    float pk[16] {};
    const float silent = play (p, 1, false, pk, chans);
    check (silent < 0.2f, "quiet without notes (peak " + juce::String (silent, 3) + ")");
    const float loud = play (p, 3, true, pk, chans);
    check (loud > 0.01f && loud < 2.0f, "notes make sound (peak " + juce::String (loud, 3) + ")");
    if (OBLIQ_SONIC)
    {
        int moving = 0;
        for (int c = 0; c < 16; ++c) if (pk[c] > 1.0e-4f) ++moving;
        check (moving >= 12, juce::String (moving) + " of 16 Ambisonics channels carry signal");
    }

    // a few presets
    int sounding = 0;
    for (int i = 1; i < std::min (6, names.size()); ++i)
    {
        p.loadPreset (names[i]);
        const float v = play (p, 2, true, pk, chans);
        std::printf ("      preset \"%s\": peak %.3f\n", names[i].toRawUTF8(), v);
        if (v > 0.001f) ++sounding;
    }
    check (sounding >= 3, "presets make sound");

    // state round trip
    {
        p.loadPreset (names[0]);
        p.params.set ("VCO", 7.0f);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        p.params.set ("VCO", 0.0f);
        p.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (p.params.get ("VCO") - 7.0f) < 1.0e-4f, "state round trip (" + juce::String ((int) state.getSize()) + " bytes)");
    }

    // the editor paints, and every page can be shown
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        check (ed != nullptr && ed->getWidth() == 960 && ed->getHeight() == 630, "editor is 960 x 630");
        auto* panel = dynamic_cast<ObliqEditor*> (ed.get());
        const int pages = panel != nullptr ? panel->numPages() : 0;
        check (pages >= 6, juce::String (pages) + " pages");
        for (int pg = 0; pg < pages; ++pg)
        {
            if (panel != nullptr) panel->showPage (pg);
            for (int i = 0; i < 12; ++i) { blocks (p, 4, chans, i == 0); if (panel != nullptr) panel->tickForTest(); }
            juce::Image im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            check (im.isValid() && im.getWidth() == 1920, "page " + juce::String (pg) + (panel != nullptr ? " (" + panel->pageName (pg) + ")" : juce::String()) + " painted");
            if (shotDir != juce::File())
            {
                shotDir.createDirectory();
                auto f = shotDir.getChildFile (juce::String (JucePlugin_Name).toLowerCase() + "_" + (panel != nullptr ? panel->pageName (pg) : juce::String (pg)) + ".png");
                f.deleteFile();
                juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
                std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
            }
        }
        if (panel != nullptr && shotDir != juce::File())
        {
            panel->showPage (0);
            panel->showHelp (true);
            blocks (p, 4, chans, true); panel->tickForTest();
            juce::Image im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto f = shotDir.getChildFile (juce::String (JucePlugin_Name).toLowerCase() + "_help.png");
            f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
            panel->showHelp (false);
        }
    }
    p.releaseResources();
    std::printf (failures ? "FAILED: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
