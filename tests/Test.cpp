// faltungTest — renders every product offline and checks it.
//   faltungTest [--wav dir] [--screenshots dir] [--only kt1]
#include "Processor.h"
#include "Editor.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace fal;

struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 124; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setIsPlaying (playing); p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::cout << "  FAIL " << what << "\n"; }
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static juce::AudioBuffer<float> render (Processor& p, const juce::AudioBuffer<float>* input, double seconds, double& realtimeFactor)
{
    const double sr = 48000.0; const int bs = 512;
    FakeHead head; p.setPlayHead (&head);
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    const int ch = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> block (ch, bs);
    const auto t0 = juce::Time::getHighResolutionTicks();
    int noteCount = 0;
    for (int pos = 0; pos < total; pos += bs)
    {
        const int n = std::min (bs, total - pos);
        block.setSize (ch, n, false, false, true);
        block.clear();
        if (input)
            for (int c = 0; c < 2; ++c) block.copyFrom (c, 0, *input, c, pos % input->getNumSamples(), std::min (n, input->getNumSamples() - pos % input->getNumSamples()));
        juce::MidiBuffer midi;
        if (p.pinfo.kind == Kind::Module)
        {
            // eighth notes at 124 bpm, varying velocity and pitch
            const int every = (int) (sr * 60.0 / 124.0 / 2.0);
            for (int i = 0; i < n; ++i)
                if ((pos + i) % every == 0)
                {
                    const int notes[4] = { 60, 70, 62, 55 };
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[noteCount % 4], (juce::uint8) (60 + (noteCount * 37) % 67)), i);
                    ++noteCount;
                }
        }
        if (p.pinfo.kind == Kind::Synth)
        {
            // a dubstep line: 2 notes per beat-pair, held, with note-offs
            const int len = (int) (sr * 0.45), every = (int) (sr * 0.5);
            const int line[6] = { 31, 31, 34, 29, 31, 38 };
            for (int i = 0; i < n; ++i)
            {
                const int t = pos + i;
                if (t % every == 0) midi.addEvent (juce::MidiMessage::noteOn (1, line[(t / every) % 6], (juce::uint8) 100), i);
                if (t % every == len) midi.addEvent (juce::MidiMessage::noteOff (1, line[(t / every) % 6]), i);
            }
        }
        p.processBlock (block, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, n);
        head.ppq += n / sr * head.bpm / 60.0;
    }
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    realtimeFactor = seconds / std::max (1.0e-6, secs);
    p.setPlayHead (nullptr);
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File wavDir, shotDir; juce::String only;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--wav" && i + 1 < argc) wavDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--screenshots" && i + 1 < argc) shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--only" && i + 1 < argc) only = argv[++i];
    }
    if (wavDir != juce::File()) wavDir.createDirectory();
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--fircheck")
        {
            const int N = 8192;
            FirDesigner d (N); std::vector<float> h ((size_t) N);
            d.design (48000.0, [] (double) { return 1.0; }, h.data());
            int mi = 0; double sum = 0; for (int k = 0; k < N; ++k) { sum += h[(size_t) k]; if (std::abs (h[(size_t) k]) > std::abs (h[(size_t) mi])) mi = k; }
            std::cout << "flat fir: peak " << h[(size_t) mi] << " at " << mi << ", sum " << sum << ", h[mi+1] " << h[(size_t) mi + 1] << "\n";
            PartConv c; c.prepare (N); c.loadNow (h.data(), N);
            std::vector<float> x (20000, 0.0f); x[100] = 1.0f;
            c.process (x.data(), (int) x.size());
            int pi2 = 0; for (int k = 0; k < (int) x.size(); ++k) if (std::abs (x[(size_t) k]) > std::abs (x[(size_t) pi2])) pi2 = k;
            std::cout << "conv impulse: peak " << x[(size_t) pi2] << " at " << pi2 << " (expected " << 100 + PartConv::B + N / 2 << ")\n";
            // random signal vs direct
            juce::Random r (1); std::vector<float> y (20000); for (auto& v : y) v = r.nextFloat() * 2 - 1;
            auto z = y; PartConv c2; c2.prepare (N); c2.loadNow (h.data(), N); c2.process (z.data(), (int) z.size());
            double e = 0, ref = 0; const int lat = PartConv::B + N / 2;
            for (int k = lat + 100; k < 20000; ++k) { const double dd = z[(size_t) k] - y[(size_t) (k - lat)]; e += dd * dd; ref += (double) y[(size_t) (k - lat)] * y[(size_t) (k - lat)]; }
            std::cout << "conv random residual " << 10 * std::log10 (e / ref + 1e-30) << " dB\n";
            return 0;
        }
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--dump-params" && i + 1 < argc)
        {
            // every product's parameters as JSON, for the manual
            juce::Array<juce::var> prods;
            for (int k = 0; k < kNumProducts; ++k)
            {
                Processor proc ((Pid) k);
                auto* o = new juce::DynamicObject();
                o->setProperty ("code", info (k).code); o->setProperty ("name", info (k).name); o->setProperty ("blurb", info (k).blurb);
                juce::Array<juce::var> ps;
                for (auto* prm : proc.getParameters())
                {
                    auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
                    if (! r) continue;
                    auto* q = new juce::DynamicObject();
                    q->setProperty ("id", r->getParameterID()); q->setProperty ("name", r->getName (64));
                    juce::String type = "float";
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r)) { type = "choice"; q->setProperty ("choices", c->choices.joinIntoString (", ")); }
                    if (dynamic_cast<juce::AudioParameterBool*> (r)) type = "bool";
                    q->setProperty ("type", type);
                    q->setProperty ("min", r->getText (0.0f, 64)); q->setProperty ("max", r->getText (1.0f, 64));
                    q->setProperty ("def", r->getText (r->getDefaultValue(), 64));
                    ps.add (juce::var (q));
                }
                o->setProperty ("params", ps);
                juce::Array<juce::var> pr;
                for (int j = 0; j < proc.getNumPrograms(); ++j) pr.add (proc.getProgramName (j));
                o->setProperty ("presets", pr);
                prods.add (juce::var (o));
            }
            juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1])).replaceWithText (juce::JSON::toString (juce::var (prods)));
            return 0;
        }
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--calibrate")
        {
            // median raw peak per type/model over random settings (velocity 1, unity level, no drive)
            for (int t = 0; t < kNumVTypes; ++t)
            {
                std::cout << vtypeInfo ((VType) t).name << ":";
                for (int m = 0; m < 4; ++m)
                {
                    std::vector<float> pk;
                    juce::Random r (42);
                    for (int k = 0; k < 41; ++k)
                    {
                        VoiceParams vp = defaultVoice ((VType) t);
                        vp.model = m; vp.level = 48.0f / 54.0f; vp.pan = 0; vp.vel = 0;
                        if (k > 0) for (auto& x : vp.s) x = r.nextFloat();
                        vp.s[7] = 0;
                        Voice v; v.prepare (48000.0f, 7u + (uint32_t) k);
                        v.trigger ((VType) t, vp, 1.0f, 0.0f);
                        std::vector<float> L (96000, 0.0f), R (96000, 0.0f);
                        v.render (L.data(), R.data(), 96000);
                        float m2 = 0; for (float x : L) m2 = std::max (m2, std::abs (x));
                        pk.push_back (m2 / 0.7071f);
                    }
                    std::sort (pk.begin(), pk.end());
                    std::cout << "  " << vtypeInfo ((VType) t).models[(size_t) m] << " " << juce::String (pk[20], 3) << " (max " << juce::String (pk.back(), 2) << ")";
                }
                std::cout << "\n";
            }
            return 0;
        }
    if (shotDir != juce::File()) shotDir.createDirectory();

    // a drum loop to feed the effects: kt1 playing its first pattern
    juce::AudioBuffer<float> loop;
    {
        Processor kit (Pid::kt1);
        double rt; loop = render (kit, nullptr, 4.0, rt);
    }

    for (int i = 0; i < kNumProducts; ++i)
    {
        const auto& pi = info (i);
        if (only.isNotEmpty() && only != pi.code) continue;
        std::cout << pi.code << " " << pi.name << "\n";
        auto proc = std::make_unique<Processor> ((Pid) i);

        // parameters: unique ids, readable names, sane text
        juce::StringArray ids;
        for (auto* prm : proc->getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
            check (r != nullptr, "parameter is ranged");
            if (! r) continue;
            check (! ids.contains (r->getParameterID()), "duplicate id " + r->getParameterID());
            ids.add (r->getParameterID());
            check (r->getName (64).isNotEmpty(), "empty name");
            check (r->getCurrentValueAsText().isNotEmpty(), "empty text for " + r->getParameterID());
        }
        std::cout << "  " << ids.size() << " automatable parameters, " << proc->getNumPrograms() << " presets\n";

        double rt = 0;
        auto out = render (*proc, pi.kind == Kind::Effect ? &loop : nullptr, 6.0, rt);
        float peak = out.getMagnitude (0, out.getNumSamples());
        float rms = 0.5f * (out.getRMSLevel (0, 0, out.getNumSamples()) + out.getRMSLevel (1, 0, out.getNumSamples()));
        bool finite = true;
        for (int c = 0; c < 2; ++c) for (int k = 0; k < out.getNumSamples(); ++k) finite &= std::isfinite (out.getSample (c, k));
        std::cout << "  peak " << juce::Decibels::gainToDecibels (peak) << " dB, rms " << juce::Decibels::gainToDecibels (rms)
                  << " dB, " << (int) rt << "x realtime\n";
        check (finite, "non-finite output");
        check (peak < 4.0f, "peak too high");
        check (rms > 1.0e-4f, "silent");
        if (wavDir != juce::File()) writeWav (wavDir.getChildFile (juce::String (pi.code) + "-" + pi.name + ".wav"), out, 48000.0);

        // every preset renders cleanly
        for (int pr = 0; pr < proc->getNumPrograms(); ++pr)
        {
            proc->setCurrentProgram (pr);
            auto o = render (*proc, pi.kind == Kind::Effect ? &loop : nullptr, 2.0, rt);
            bool ok = true; float pk = o.getMagnitude (0, o.getNumSamples());
            for (int c = 0; c < 2; ++c) for (int k = 0; k < o.getNumSamples(); ++k) ok &= std::isfinite (o.getSample (c, k));
            check (ok && pk < 4.0f && (pk > 1.0e-4f || proc->getProgramName (pr) == "empty"), "preset " + proc->getProgramName (pr) + " peak " + juce::String (pk));
            if (wavDir != juce::File() && pi.kind != Kind::Effect)
                writeWav (wavDir.getChildFile (juce::String (pi.code) + "-preset-" + proc->getProgramName (pr).replaceCharacter (' ', '_') + ".wav"), o, 48000.0);
        }

        if (pi.id == Pid::ms0)
        {
            // null test: everything neutral → output must equal the input delayed by the reported latency
            Processor m (Pid::ms0);
            for (auto id : { "mbon", "dynon", "glueon", "limon", "saton", "ambon" }) m.setParam (id, 0.0f);
            for (int b = 1; b <= 4; ++b) m.setParam ("b" + juce::String (b) + "width", 1.0f);
            m.setParam ("match", 0.0f);
            double rt2; auto o = render (m, &loop, 4.0, rt2);
            const int lat = m.getLatencySamples();
            double err = 0, ref = 0;
            for (int c = 0; c < 2; ++c)
                for (int k = lat + 2000; k < o.getNumSamples(); ++k)
                {
                    const float x = loop.getSample (c, (k - lat) % loop.getNumSamples()), y = o.getSample (c, k);
                    err += (double) (y - x) * (y - x); ref += (double) x * x;
                }
            const double nullDb = 10.0 * std::log10 ((err + 1e-30) / (ref + 1e-30));
            std::cout << "  null test: latency " << lat << " samples, residual " << nullDb << " dB\n";
            check (nullDb < -90.0, "ms0 null test");
            // limiter: a hot signal must stay under the true-peak ceiling
            Processor h (Pid::ms0);
            h.setParam ("limgain", 18.0f); h.setParam ("ceiling", -1.0f);
            auto hot = render (h, &loop, 4.0, rt2);
            float pk = 0;
            for (int c = 0; c < 2; ++c) for (int k = h.getLatencySamples() + 4800; k < hot.getNumSamples(); ++k) pk = std::max (pk, std::abs (hot.getSample (c, k)));
            std::cout << "  limiter: +18 db into -1 dbtp ceiling, sample peak " << juce::Decibels::gainToDecibels (pk) << " dB\n";
            check (pk <= juce::Decibels::decibelsToGain (-0.99f), "ms0 limiter ceiling");
        }

        // state round trip
        proc->setCurrentProgram (proc->getNumPrograms() > 1 ? 1 : 0);
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        Processor other ((Pid) i);
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int k = 0; k < proc->getParameters().size(); ++k)
            same &= std::abs (proc->getParameters()[k]->getValue() - other.getParameters()[k]->getValue()) < 1.0e-5f;
        check (same, "state round trip");
        if (pi.id == Pid::kt1) check (proc->kitBank.toString() == other.kitBank.toString(), "kit bank round trip");
        if (pi.id == Pid::kt2) check (other.getSample() != nullptr && other.getSample()->buf.getNumSamples() > 1000, "sample round trip");

        if (shotDir != juce::File())
        {
            proc->setCurrentProgram (0);
            if (pi.id == Pid::ms0) { proc->setCurrentProgram (2); double r4; render (*proc, &loop, 3.0, r4); }   // "dense experimental", with audio in the scopes
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
            ed->setSize (pi.w, pi.h);
            if (auto* fe = dynamic_cast<Editor*> (ed.get())) for (int t = 0; t < 12; ++t) fe->tickForTest();
            juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png;
            const auto f = shotDir.getChildFile (juce::String (pi.code) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
        }
    }
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
