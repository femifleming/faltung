// Offline checks of the whole plug-in, and panel screenshots.
//   ECRTest                      run the checks
//   ECRTest --screenshots <dir>  also write PNGs of the panel (main, chooser, menu)
//   ECRTest --render <in.wav> <out.wav> [response-id]   run a file through the reverb
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Ui.h"

using namespace ecr;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static void setP (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }

static juce::AudioBuffer<float> run (EcrProcessor& p, const juce::AudioBuffer<float>& in, int block, bool randomBlocks = false)
{
    juce::AudioBuffer<float> out (2, in.getNumSamples());
    juce::MidiBuffer midi;
    juce::Random rnd (3);
    for (int pos = 0; pos < in.getNumSamples();)
    {
        const int n = std::min (in.getNumSamples() - pos, randomBlocks ? 1 + rnd.nextInt (block) : block);
        juce::AudioBuffer<float> buf (2, n);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, n);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
        pos += n;
    }
    return out;
}

static bool finite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static void runChecks()
{
    // 1. dry path is bit-transparent and latency-free
    {
        EcrProcessor p;
        p.setNonRealtime (true);
        setP (p.globalParam (kWet), 0.0f); setP (p.globalParam (kDry), 1.0f);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> in (2, 48000);
        juce::Random r (1);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (c, i, r.nextFloat() - 0.5f);
        auto out = run (p, in, 512);
        float err = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < in.getNumSamples(); ++i) err = std::max (err, std::abs (out.getSample (c, i) - in.getSample (c, i)));
        check (err < 1e-6f, "dry path transparent, zero latency (max diff " + juce::String (err, 9) + ")");
        check (p.getLatencySamples() == 0, "reports zero latency");
    }

    // 2. an impulse returns the response itself, starting on the same sample
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        EcrProcessor p;
        p.setNonRealtime (true);
        setP (p.globalParam (kWet), 1.0f); setP (p.globalParam (kDry), 0.0f);
        p.settings.autoGain = false; p.settingsChanged();
        p.prepareToPlay (rate, 256);
        auto src = p.responseSource (0);
        check (src != nullptr, "built-in response loads at " + juce::String (rate));
        if (src == nullptr) continue;

        IrRenderer renderer;
        auto ir = renderer.render (src, IrSettings(), rate);
        const int n = std::min (ir.length, (int) rate * 2);
        juce::AudioBuffer<float> in (2, n + 1000);
        in.clear();
        in.setSample (0, 100, 1.0f); in.setSample (1, 100, 1.0f);
        auto out = run (p, in, 333, true);
        // the 2 Hz DC blocker tilts the tail very slightly: allow for it
        double err = 0.0, mag = 0.0;
        for (int i = 0; i < n; ++i)
        {
            err = std::max (err, (double) std::abs (out.getSample (0, 100 + i) - ir.h[0][(size_t) i]));
            mag = std::max (mag, (double) std::abs (ir.h[0][(size_t) i]));
        }
        float before = 0.0f;
        for (int i = 0; i < 100; ++i) before = std::max (before, std::abs (out.getSample (0, i)));
        check (before == 0.0f && err < 0.02 * mag, "impulse -> response at " + juce::String (rate) + " Hz (err " + juce::String (err / mag, 5) + " of peak, " + juce::String (ir.length / rate, 2) + " s)");
        check (finite (out), "output finite");
    }

    // 3. changing responses and knobs while audio runs: no blow-ups, no clicks louder than the signal
    {
        EcrProcessor p;
        setP (p.globalParam (kWet), 0.8f); setP (p.globalParam (kDry), 0.0f);
        p.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> in (2, 128);
        juce::MidiBuffer midi;
        juce::Random r (5);
        float peak = 0.0f;
        bool ok = true;
        const auto& lib = p.library.entries();
        double phase = 0.0;
        for (int b = 0; b < 3000; ++b)
        {
            for (int i = 0; i < 128; ++i) { const float s = 0.3f * (float) std::sin (phase); phase += 0.05; in.setSample (0, i, s); in.setSample (1, i, s); }
            if (b % 40 == 0) setP (p.slotParam (0, kPos), r.nextFloat() * 0.5f);
            if (b % 55 == 0) setP (p.slotParam (0, kPitch), r.nextFloat());
            if (b % 70 == 0) setP (p.slotParam (0, kRelease), 0.3f + 0.7f * r.nextFloat());
            if (b % 90 == 0) setP (p.slotParam (0, kPredelay), r.nextFloat() * 0.5f);
            if (b % 200 == 0 && ! lib.empty()) p.selectResponse (0, lib[(size_t) r.nextInt ((int) lib.size())].id);
            if (b % 33 == 0) setP (p.globalParam (kXfade), r.nextFloat());
            juce::AudioBuffer<float> buf (in);
            p.processBlock (buf, midi);
            ok = ok && finite (buf);
            peak = std::max (peak, buf.getMagnitude (0, 128));
            juce::Thread::sleep (1);
        }
        check (ok && peak <= 1.0001f, "live response/knob changes stay finite and inside full scale (peak " + juce::String (peak, 3) + ")");
    }

    // 4. session state round trip
    {
        juce::MemoryBlock state;
        juce::String idB;
        {
            EcrProcessor p;
            const auto& lib = p.library.entries();
            setP (p.slotParam (1, kAttack), 0.37f);
            setP (p.globalParam (kXfade), 0.81f);
            p.settings.aux = 1; p.settings.cvMap = 0; p.foreground = 1;
            if (! lib.empty()) p.selectResponse (1, lib.back().id);
            idB = p.responseId (1);
            p.getStateInformation (state);
        }
        EcrProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q.slotParam (1, kAttack)->getValue() - 0.37f) < 1e-4f && std::abs (q.globalParam (kXfade)->getValue() - 0.81f) < 1e-4f
               && q.settings.aux == 1 && q.settings.cvMap == 0 && q.foreground.load() == 1 && q.responseId (1) == idB,
               "state round trip (response " + idB + ")");
    }

    // 5. record -> the recording becomes the response
    {
        EcrProcessor p;
        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> in (2, 24000);
        juce::Random r (9);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 24000; ++i) in.setSample (c, i, (r.nextFloat() - 0.5f) * std::exp (-(float) i / 6000.0f));
        p.toggleRecording();
        check (p.isRecording(), "recording starts");
        run (p, in, 256);
        p.toggleRecording();
        for (int i = 0; i < 300 && (p.responseSource (0) == nullptr || ! p.responseSource (0)->unsaved); ++i) juce::Thread::sleep (10);
        auto src = p.responseSource (0);
        check (src != nullptr && src->unsaved && std::abs (src->length - 24000) <= 256, "recording becomes the foreground response");
        juce::MemoryBlock state;
        p.getStateInformation (state);
        EcrProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        auto again = q.responseSource (0);
        check (again != nullptr && again->unsaved && again->length == (src != nullptr ? src->length : -1), "unsaved recording survives in the session");
    }
}

static void modChecks()
{
    // every shape stays in range and actually moves
    bool shapesOk = true;
    for (int shape = 0; shape < kNumLfoShapes; ++shape)
    {
        Lfo lfo ((uint32_t) shape + 1);
        float lo = 1.0f, hi = -1.0f;
        for (int i = 0; i < 4000; ++i)
        {
            const float v = lfo.advance (shape, 2.0, 0.005);
            shapesOk = shapesOk && std::isfinite (v) && v >= -1.0f && v <= 1.0f;
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        std::printf ("      %-7s range %+.2f .. %+.2f\n", lfoShapeName (shape), lo, hi);
        shapesOk = shapesOk && hi - lo > 0.6f;
    }
    check (shapesOk, "all LFO shapes stay within -1..+1 and move");

    // LFO 1 (square, 5 Hz) -> dry level: the output must swing between loud and quiet
    EcrProcessor p;
    setP (p.globalParam (kWet), 0.0f); setP (p.globalParam (kDry), 0.5f);
    setP (p.lfoShapeParam (0), (float) kSquare);
    p.lfoRateParam (0)->setValueNotifyingHost (0.82f);
    setP (p.modAmountParam (0, dDry), 0.5f);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> in (2, 48000);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) in.setSample (c, i, 0.5f);
    auto out = run (p, in, 256);
    float lo = 10.0f, hi = 0.0f;
    for (int i = 4800; i < 48000; ++i) { lo = std::min (lo, out.getSample (0, i)); hi = std::max (hi, out.getSample (0, i)); }
    check (lo < 0.01f && hi > 0.45f, "matrix: LFO 1 > Dry modulates the dry level (" + juce::String (lo, 3) + " .. " + juce::String (hi, 3) + ")");
}

static void screenshots (const juce::File& dir)
{
    dir.createDirectory();
    EcrProcessor p;
    p.setNonRealtime (true);
    p.prepareToPlay (48000.0, 512);
    setP (p.slotParam (0, kPos), 0.3f);
    setP (p.slotParam (0, kRelease), 0.5f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (EcrEditor::kW * 2, EcrEditor::kH * 2);
    auto shot = [&] (const juce::String& name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
        juce::PNGImageFormat png;
        auto f = dir.getChildFile (name + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    p.ui->knobMoved (Knob::Pos, false);
    shot ("main");
    p.ui->turnSelect (3, false);
    p.waitForEngines (5000);
    shot ("chooser");
    p.ui->pushSelect (false);
    p.ui->pushSelect (true);
    shot ("menu");
    p.ui->pushSelect (true);
    p.lfoRateParam (0)->setValueNotifyingHost (0.72f); p.lfoRateParam (1)->setValueNotifyingHost (0.75f); p.lfoRateParam (2)->setValueNotifyingHost (0.7f);
    setP (p.modAmountParam (0, dPos), 0.6f); setP (p.modAmountParam (1, dPitch), -0.4f);
    setP (p.modAmountParam (2, dXfade), 1.0f); setP (p.modAmountParam (1, dWet), 0.15f);
    dynamic_cast<EcrEditor*> (ed.get())->tab = 1;
    // let the LFOs run for a while so the figures have something to show
    {
        juce::AudioBuffer<float> buf (2, 1024);
        juce::MidiBuffer midi;
        for (int i = 0; i < 200; ++i)
        {
            buf.clear();
            p.processBlock (buf, midi);
            // no modal loops in this build: run the editor's timer by hand
            juce::Thread::sleep (35);
            juce::Timer::callPendingTimersSynchronously();
        }
    }
    shot ("mod");
}

static int render (const juce::StringArray& args)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (args[1])));
    if (reader == nullptr) { std::printf ("cannot read %s\n", args[1].toRawUTF8()); return 1; }
    EcrProcessor p;
    p.setNonRealtime (true);
    if (args.size() > 3) p.selectResponse (0, args[3]);
    setP (p.globalParam (kWet), 0.6f);
    p.prepareToPlay (reader->sampleRate, 512);
    auto src = p.responseSource (0);
    const int tail = src != nullptr ? (int) (src->length / src->sampleRate * reader->sampleRate) : 0;
    juce::AudioBuffer<float> in (2, (int) reader->lengthInSamples + tail);
    in.clear();
    reader->read (&in, 0, (int) reader->lengthInSamples, 0, true, true);
    if (reader->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, (int) reader->lengthInSamples);
    auto out = run (p, in, 512);
    juce::MemoryOutputStream o;
    const int n = out.getNumSamples();
    o.write ("RIFF", 4); o.writeInt (36 + n * 8); o.write ("WAVEfmt ", 8); o.writeInt (16); o.writeShort (3); o.writeShort (2);
    o.writeInt ((int) reader->sampleRate); o.writeInt ((int) reader->sampleRate * 8); o.writeShort (8); o.writeShort (32);
    o.write ("data", 4); o.writeInt (n * 8);
    for (int i = 0; i < n; ++i) { o.writeFloat (out.getSample (0, i)); o.writeFloat (out.getSample (1, i)); }
    juce::File::getCurrentWorkingDirectory().getChildFile (args[2]).replaceWithData (o.getData(), o.getDataSize());
    std::printf ("rendered %d samples with %s, peak %.3f\n", n, p.responseId (0).toRawUTF8(), out.getMagnitude (0, n));
    return 0;
}

// Loads a built plug-in binary the way a DAW would and pushes audio through it.
static int hostCheck (const juce::String& path)
{
    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : fm.getFormats())
        if (format->fileMightContainThisPluginType (path))
            format->findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::printf ("FAIL  no plug-in found in %s\n", path.toRawUTF8()); return 1; }
    juce::String error;
    auto inst = fm.createPluginInstance (*found[0], 48000.0, 256, error);
    if (inst == nullptr) { std::printf ("FAIL  %s\n", error.toRawUTF8()); return 1; }
    std::printf ("loaded %s (%s), %d in / %d out, %d parameters, latency %d\n", found[0]->name.toRawUTF8(), found[0]->pluginFormatName.toRawUTF8(),
                 inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(), inst->getParameters().size(), inst->getLatencySamples());
    inst->setRateAndBufferSizeDetails (48000.0, 256);
    inst->prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buf (juce::jmax (2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()), 256);
    juce::MidiBuffer midi;
    juce::Random r (2);
    float peak = 0.0f, tail = 0.0f;
    bool ok = true;
    for (int b = 0; b < 400; ++b)
    {
        buf.clear();
        if (b < 20) for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buf.setSample (c, i, 0.25f * (r.nextFloat() - 0.5f));
        inst->processBlock (buf, midi);
        ok = ok && finite (buf);
        peak = std::max (peak, buf.getMagnitude (0, 256));
        if (b > 40 && b < 120) tail = std::max (tail, buf.getMagnitude (0, 256));
    }
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    inst->setStateInformation (state.getData(), (int) state.getSize());
    inst->releaseResources();
    check (ok && peak > 0.01f && tail > 1e-5f, "hosted: audio passes and a reverb tail follows (peak " + juce::String (peak, 3) + ", tail " + juce::String (tail, 5) + "), state " + juce::String ((int) state.getSize()) + " bytes");
    return failures;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args (argv + 1, argc - 1);
    if (args.size() >= 3 && args[0] == "--render") return render (args);
    if (args.size() >= 2 && args[0] == "--host") return hostCheck (args[1]);
    runChecks();
    modChecks();
    if (args.size() >= 2 && args[0] == "--screenshots") screenshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    std::printf (failures ? "FAILED: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
