// faltungHostCheck — loads the BUILT plug-ins the way a DAW does and checks that they make sound.
//   faltungHostCheck file.vst3|file.component ...
//   faltungHostCheck --save-state <plug-in> <file>  /  --load-state <plug-in> <file>   (does a state from one build load in another?)
// For each file: instantiate (VST3 / AU), stereo main bus, 48 kHz / 512, host transport playing at 122 bpm.
// Instruments get MIDI (kit notes 36-43, modules / synth: a note every half beat); effects get a drum-like
// test signal. Prints peak / rms of the main output for: transport playing (no midi), midi notes, and the
// first three programs. FAIL when the main output is silent.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>

struct Head : juce::AudioPlayHead
{
    double ppq = 0, bpm = 122; bool playing = true; juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (playing); p.setPpqPosition (ppq);
        p.setTimeInSamples (samples); p.setTimeSignature (TimeSignature { 4, 4 }); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return p;
    }
};

static int failures = 0;

static void run (juce::AudioPluginInstance& p, bool instrument, bool isKit, bool sendMidi, bool playing, double secs, float& peak, float& rms)
{
    const double sr = 48000.0; const int bs = 512;
    Head head; head.playing = playing; p.setPlayHead (&head);
    p.prepareToPlay (sr, bs);
    const int ch = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> b (std::max (ch, 2), bs);
    double sum = 0; juce::int64 n = 0; peak = 0;
    const int total = (int) (secs * sr), every = (int) (sr * 60.0 / head.bpm / 2.0);
    juce::Random rnd (7);
    int note = 0;
    for (int pos = 0; pos < total; pos += bs)
    {
        b.clear();
        juce::MidiBuffer m;
        if (! instrument)
            for (int k = 0; k < bs; ++k)
            {
                const int t = (pos + k) % every;
                const float env = std::exp (-(float) t / 2400.0f);
                const float s = env * (0.6f * std::sin ((float) t * 0.012f) + 0.2f * (rnd.nextFloat() * 2 - 1));
                b.setSample (0, k, s); b.setSample (1, k, s);
            }
        else if (sendMidi)
            for (int k = 0; k < bs; ++k)
                if ((pos + k) % every == 0)
                {
                    const int key = isKit ? 36 + (note % 8) : 48 + (note % 3) * 5;
                    m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 110), k);
                    m.addEvent (juce::MidiMessage::noteOff (1, key), std::min (bs - 1, k + 400));
                    ++note;
                }
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < bs; ++k) { const float x = b.getSample (c, k); sum += x * x; peak = std::max (peak, std::abs (x)); }
        n += 2 * bs;
        head.samples += bs; head.ppq += bs / sr * head.bpm / 60.0;
        if (pos < 30 * bs) juce::Thread::sleep ((int) (bs * 1000.0 / sr));   // the first blocks at real-time pace, as a host delivers them (background threads catch up)
    }
    p.releaseResources();
    rms = (float) std::sqrt (sum / (double) std::max<juce::int64> (1, n));
}

/** ms0: compare (bypass) on → the output must be the input, delayed by the reported latency. Returns the residual in dB. */
static float bypassResidual (juce::AudioPluginInstance& p)
{
    auto set = [&] (const juce::String& name, float v)
    {
        for (auto* prm : p.getParameters())
            if (prm->getName (64) == name) { prm->setValueNotifyingHost (v); return true; }
        return false;
    };
    set ("compare (bypass)", 1.0f); set ("level-matched compare", 0.0f); set ("dc filter", 0.0f); set ("dither", 0.0f);
    const double sr = 48000.0; const int bs = 512;
    Head head; p.setPlayHead (&head);
    p.prepareToPlay (sr, bs);
    const int total = (int) (4.0 * sr), every = (int) (sr * 60.0 / head.bpm / 2.0);
    juce::AudioBuffer<float> in (2, total), out (2, total), b (std::max (2, p.getTotalNumOutputChannels()), bs);
    juce::Random rnd (7);
    for (int pos = 0; pos < total; pos += bs)
    {
        b.clear();
        for (int k = 0; k < bs; ++k)
        {
            const int t = (pos + k) % every;
            const float s = std::exp (-(float) t / 2400.0f) * (0.6f * std::sin ((float) t * 0.012f) + 0.2f * (rnd.nextFloat() * 2 - 1));
            b.setSample (0, k, s); b.setSample (1, k, s); in.setSample (0, pos + k, s); in.setSample (1, pos + k, s);
        }
        juce::MidiBuffer m; p.processBlock (b, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, bs);
        head.samples += bs; head.ppq += bs / sr * head.bpm / 60.0;
        if (pos < 30 * bs) juce::Thread::sleep ((int) (bs * 1000.0 / sr));
    }
    p.releaseResources();
    const int lat = p.getLatencySamples();
    double err = 0, ref = 0;
    for (int c = 0; c < 2; ++c)
        for (int k = lat + 24000; k < total; ++k)
        {
            const float x = in.getSample (c, k - lat), y = out.getSample (c, k);
            err += (double) (y - x) * (y - x); ref += (double) x * x;
        }
    set ("compare (bypass)", 0.0f); set ("level-matched compare", 1.0f);
    return (float) (10.0 * std::log10 ((err + 1e-30) / (ref + 1e-30)));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    auto db = [] (float x) { return juce::String (juce::Decibels::gainToDecibels (x, -150.0f), 1); };

    // faltungHostCheck --save-state <plug-in> <file> : load the plug-in, move its parameters, write its state as a host
    //                                                    would save it (and the parameter values beside it, <file>.params)
    // faltungHostCheck --load-state <plug-in> <file> : load the plug-in (e.g. a newer build), restore that state, and
    //                                                    check every parameter is where it was: old sessions keep loading
    if (argc == 4 && (juce::String (argv[1]) == "--save-state" || juce::String (argv[1]) == "--load-state"))
    {
        const bool saving = juce::String (argv[1]) == "--save-state";
        const juce::String path (argv[2]);
        const juce::File stateFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[3])), valueFile (stateFile.getFullPathName() + ".params");
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : fm.getFormats())
            if (f->fileMightContainThisPluginType (path)) { f->findAllTypesForFile (found, path); if (! found.isEmpty()) break; }
        if (found.isEmpty()) { std::cout << "FAIL " << path << ": not loadable\n"; return 1; }
        juce::String err;
        auto inst = fm.createPluginInstance (*found[0], 48000.0, 512, err);
        if (! inst) { std::cout << "FAIL " << path << ": " << err << "\n"; return 1; }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        inst->prepareToPlay (48000.0, 512);
        auto wanted = [&] (int i) { const auto n = inst->getParameters()[i]->getName (64); return ! n.startsWith ("MIDI CC") && n != "Bypass"; };
        auto values = [&]
        {
            juce::StringArray v;
            for (int i = 0; i < inst->getParameters().size(); ++i) if (wanted (i)) v.add (juce::String (inst->getParameters()[i]->getValue(), 5));
            return v;
        };
        std::cout << found[0]->name << " " << found[0]->version << " (" << found[0]->pluginFormatName << ", " << path << ")\n";
        if (saving)
        {
            int moved = 0;
            for (int i = 0; i < inst->getParameters().size(); ++i)
                if (wanted (i)) { const double x = (i + 1) * 0.37; inst->getParameters()[i]->setValueNotifyingHost ((float) (0.2 + 0.6 * (x - std::floor (x)))); ++moved; }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
            juce::MemoryBlock mb; inst->getStateInformation (mb);
            stateFile.replaceWithData (mb.getData(), mb.getSize());
            valueFile.replaceWithText (values().joinIntoString ("\n"));
            std::cout << "  moved " << moved << " parameters, state " << (int) mb.getSize() << " bytes -> " << stateFile.getFullPathName() << "\n";
            return 0;
        }
        juce::MemoryBlock mb; if (! stateFile.loadFileAsData (mb)) { std::cout << "FAIL no state file\n"; return 1; }
        const auto before = values();
        inst->setStateInformation (mb.getData(), (int) mb.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);     // (past the moment a host's late program change would be ignored)
        const auto now = values();
        juce::StringArray saved; saved.addLines (valueFile.loadFileAsString());
        if (saved.size() != now.size()) { std::cout << "  FAIL " << now.size() << " parameters, the state was saved with " << saved.size() << "\n"; return 1; }
        float worst = 0, changed = 0; int at = -1;
        for (int i = 0; i < now.size(); ++i)
        {
            const float d = std::abs (now[i].getFloatValue() - saved[i].getFloatValue());
            if (d > worst) { worst = d; at = i; }
            changed = std::max (changed, std::abs (now[i].getFloatValue() - before[i].getFloatValue()));
        }
        std::cout << "  " << now.size() << " parameters restored, largest difference from the saved values " << juce::String (worst, 5)
                  << (at >= 0 && worst > 0.002f ? " (parameter " + juce::String (at) + ")" : juce::String()) << "; the state moved them by up to " << juce::String (changed, 3) << " from this build's defaults\n";
        const bool ok = worst <= 0.002f && changed > 0.05f;
        std::cout << (ok ? "  ok\n" : "  FAIL the state did not restore\n");
        return ok ? 0 : 1;
    }

    for (int a = 1; a < argc; ++a)
    {
        const juce::String path (argv[a]);
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : fm.getFormats())
            if (f->fileMightContainThisPluginType (path)) { f->findAllTypesForFile (found, path); if (! found.isEmpty()) break; }
        if (found.isEmpty()) { std::cout << "FAIL " << path << ": not loadable\n"; ++failures; continue; }
        juce::String err;
        auto inst = fm.createPluginInstance (*found[0], 48000.0, 512, err);
        if (! inst) { std::cout << "FAIL " << path << ": " << err << "\n"; ++failures; continue; }
        // let it settle as it would in a DAW: plug-ins that load things on the message thread (timers) get to run
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        // stereo main in/out, aux buses off (a plain Live track)
        auto layout = inst->getBusesLayout();
        for (int i = 0; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        for (int i = 0; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        inst->setBusesLayout (layout);
        const bool instrument = found[0]->isInstrument;
        const bool isKit = found[0]->name.endsWith ("kt1");
        std::cout << found[0]->name << " (" << found[0]->pluginFormatName << ", " << inst->getTotalNumOutputChannels() << " out ch, "
                  << inst->getNumPrograms() << " programs, program " << inst->getCurrentProgram() << " \"" << inst->getProgramName (inst->getCurrentProgram()) << "\")\n";
        float pk, rm; bool any = false;
        if (instrument)
        {
            run (*inst, true, isKit, false, true, 4.0, pk, rm);
            std::cout << "  transport, no midi:  peak " << db (pk) << " dB  rms " << db (rm) << " dB\n";
            if (isKit || found[0]->name.endsWith ("kt2")) any |= pk > 1.0e-3f;
            run (*inst, true, isKit, true, false, 4.0, pk, rm);
            std::cout << "  midi, stopped:       peak " << db (pk) << " dB  rms " << db (rm) << " dB\n";
            any |= pk > 1.0e-3f;
            if (pk <= 1.0e-3f) { std::cout << "  FAIL midi notes are silent\n"; ++failures; }
        }
        else
        {
            run (*inst, false, false, false, true, 4.0, pk, rm);
            std::cout << "  effect, test signal: peak " << db (pk) << " dB  rms " << db (rm) << " dB\n";
            any = pk > 1.0e-3f;
        }
        for (int pr = 0; pr < std::min (3, inst->getNumPrograms()); ++pr)
        {
            inst->setCurrentProgram (pr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            run (*inst, instrument, isKit, true, true, 2.0, pk, rm);
            std::cout << "  program " << pr << " \"" << inst->getProgramName (pr) << "\": peak " << db (pk) << " dB\n";
            if (pk <= 1.0e-3f && inst->getProgramName (pr) != "empty") { std::cout << "  FAIL program silent\n"; ++failures; }
        }
        if (! any) { std::cout << "  FAIL silent\n"; ++failures; }
        if (found[0]->name.endsWith ("ms0"))
        {
            inst->setCurrentProgram (2);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            const float res = bypassResidual (*inst);
            std::cout << "  compare (bypass) on program 2: output vs input delayed by " << inst->getLatencySamples() << " samples: " << juce::String (res, 1) << " dB\n";
            if (res > -60.0f) { std::cout << "  FAIL compare / bypass does not pass the input\n"; ++failures; }
        }
    }
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failures\n");
    return failures == 0 ? 0 : 1;
}
