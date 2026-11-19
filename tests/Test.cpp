// faltungTest — renders every product offline and checks it.
//   faltungTest [--wav dir] [--screenshots dir] [--only kt1]
#include "Processor.h"
#include "Editor.h"
#include "ValueEntry.h"
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
        if (juce::String (argv[i]) == "--pack" && i + 1 < argc)
        {
            // faltungTest --pack <dir> : one-shot sample pack rendered from the bundle's own engines (24-bit / 44.1 kHz)
            const double sr = 44100.0;
            juce::File root (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]));
            root.createDirectory();
            auto finish = [&] (juce::AudioBuffer<float>& b, bool mono)
            {
                // trim the tail below -66 dB (after the last loud sample), fade 8 ms, normalise to -1 dBFS
                const int n = b.getNumSamples();
                int last = 0; const float thr = 0.0005f * std::max (1.0e-6f, b.getMagnitude (0, n));
                for (int c = 0; c < b.getNumChannels(); ++c) for (int k = n - 1; k > last; --k) if (std::abs (b.getSample (c, k)) > thr) { last = k; break; }
                const int len = std::min (n, last + (int) (0.01 * sr));
                juce::AudioBuffer<float> o (mono ? 1 : 2, std::max (64, len));
                for (int c = 0; c < o.getNumChannels(); ++c) o.copyFrom (c, 0, b, mono ? 0 : c, 0, o.getNumSamples());
                const int fade = std::min (o.getNumSamples() / 4, (int) (0.008 * sr));
                for (int c = 0; c < o.getNumChannels(); ++c) o.applyGainRamp (c, o.getNumSamples() - fade, fade, 1.0f, 0.0f);
                const float pk = o.getMagnitude (0, o.getNumSamples());
                if (pk > 1.0e-6f) o.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
                return o;
            };
            auto save = [&] (const juce::File& f, const juce::AudioBuffer<float>& b)
            {
                f.getParentDirectory().createDirectory(); f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
                if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            };
            auto noteName = [] (float hz)
            {
                const int m = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
                return juce::MidiMessage::getMidiNoteName (m, true, true, 3);
            };
            auto hit = [&] (VType t, const VoiceParams& vp, float vel, double secs)
            {
                Voice v; v.prepare ((float) sr, 4242u);
                juce::AudioBuffer<float> b (2, (int) (secs * sr)); b.clear();
                v.trigger (t, vp, vel, 0.0f);
                v.render (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
                return b;
            };
            struct Folder { VType t; Pid pid; const char* dir; const char* word; };
            const Folder folders[] = { { VType::Kick, Pid::bd1, "01 kicks", "kick" }, { VType::Snare, Pid::sd2, "02 snares", "snare" }, { VType::Rim, Pid::rm3, "03 rims", "rim" },
                                       { VType::Hat, Pid::hh4, "04 hats", "hat" }, { VType::Clap, Pid::cp6, "05 claps", "clap" }, { VType::Perc, Pid::pc7, "06 perc", "perc" },
                                       { VType::Click, Pid::cl8, "07 clicks", "click" }, { VType::Tom, Pid::tm9, "08 toms", "tom" } };
            juce::Random rnd (2026);
            int count = 0;
            std::vector<std::pair<VType, VoiceParams>> forProcessing;
            for (const auto& fo : folders)
            {
                const auto& vi = vtypeInfo (fo.t);
                const auto& pr = presets (fo.pid);
                // faltung: one folder per engine model, starting from the preset that uses it;
                // faltung deep: one folder per deep preset
                std::vector<std::pair<juce::String, VoiceParams>> bases;
                if (kDeep)
                    for (auto& p : pr)
                    {
                        VoiceParams b = defaultVoice (fo.t);
                        for (auto& [id, v] : p.values)
                        {
                            if (id == "model") b.model = (int) v;
                            for (int sl = 0; sl < kSlots; ++sl) if (id == vi.slot[(size_t) sl]) b.s[(size_t) sl] = v;
                        }
                        bases.push_back ({ p.name, b });
                    }
                else
                    for (int m = 0; m < 4; ++m)
                    {
                        VoiceParams b = defaultVoice (fo.t); b.model = m;
                        for (auto& p : pr)
                        {
                            bool isModel = false; for (auto& [id, v] : p.values) if (id == "model" && (int) v == m) isModel = true;
                            if (! isModel) continue;
                            for (auto& [id, v] : p.values) for (int sl = 0; sl < kSlots; ++sl) if (id == vi.slot[(size_t) sl]) b.s[(size_t) sl] = v;
                            break;
                        }
                        bases.push_back ({ vi.models[(size_t) m], b });
                    }
                for (auto& [label, base0] : bases)
                {
                    VoiceParams base = base0;
                    base.level = 48.0f / 54.0f; base.pan = 0; base.vel = 0;
                    const int variations = fo.t == VType::Hat ? 8 : 10;
                    for (int k = 0; k < variations; ++k)
                    {
                        VoiceParams vp = base;
                        if (k > 0)
                        {
                            const float spread = 0.08f + 0.02f * (float) k;
                            for (int sl = 0; sl < kSlots; ++sl)
                            {
                                const float amount = sl == 7 ? 0.5f : (sl == 0 ? 1.2f : 1.0f);
                                vp.s[(size_t) sl] = juce::jlimit (0.0f, sl == 7 ? 0.6f : 1.0f, vp.s[(size_t) sl] + (rnd.nextFloat() * 2 - 1) * spread * amount);
                            }
                        }
                        const bool tonal = fo.t == VType::Kick || fo.t == VType::Tom || fo.t == VType::Perc || fo.t == VType::Rim;
                        juce::String name = juce::String (kDeep ? "faltung deep " : "faltung ") + vi.name + " " + label + " " + juce::String (k + 1).paddedLeft ('0', 2);
                        if (tonal) name << " (" << noteName (tuneHz (fo.t, vp.s[0])) << ")";
                        auto run = [&] (const VoiceParams& v2, const juce::String& nm, const char* sub)
                        {
                            auto b = hit (fo.t, v2, 1.0f, std::min (4.0, decaySec (fo.t, v2.s[1]) * 2.5 + 0.3));
                            auto o = finish (b, true);
                            save (root.getChildFile (fo.dir).getChildFile (sub).getChildFile (nm + ".wav"), o); ++count;
                        };
                        if (fo.t == VType::Hat)
                        {
                            VoiceParams cl = vp; cl.s[1] = juce::jlimit (0.0f, 0.45f, cl.s[1]);
                            VoiceParams op = vp; op.s[1] = juce::jlimit (0.55f, 0.85f, 0.6f + (vp.s[1] - 0.3f) * 0.5f);
                            run (cl, name.replace ("hat ", "hat closed "), kDeep ? label.toRawUTF8() : "closed");
                            run (op, name.replace ("hat ", "hat open "), kDeep ? label.toRawUTF8() : "open");
                        }
                        else run (vp, name, label.toRawUTF8());
                        if (k < 2 && (fo.t == VType::Rim || fo.t == VType::Clap || fo.t == VType::Snare || fo.t == VType::Perc || fo.t == VType::Click)) forProcessing.push_back ({ fo.t, vp });
                    }
                }
            }
            // dub throws and rooms: hits through dl4 and rv5
            {
                int k = 0;
                for (auto& [t, vp] : forProcessing)
                {
                    for (int fx = 0; fx < 2; ++fx)
                    {
                        auto b = hit (t, vp, 1.0f, fx == 0 ? 5.0 : 3.0);
                        Transport tr; tr.bpm = 124; tr.playing = false;
                        if (fx == 0)
                        {
                            DubDelay d; d.prepare ((float) sr);
                            DubParams dp; dp.division = (k % 3 == 0) ? 6 : (k % 3 == 1 ? 9 : 8); dp.feedback = 0.55f + 0.1f * (k % 3); dp.age = 0.4f; dp.wow = 0.25f;
                            dp.hp = 300; dp.lp = 2800; dp.spread = 0.8f; dp.mix = 0.55f;
                            for (int pos = 0; pos < b.getNumSamples(); pos += 512) d.process (b.getWritePointer (0) + pos, b.getWritePointer (1) + pos, std::min (512, b.getNumSamples() - pos), dp, tr);
                        }
                        else
                        {
                            Room r; r.prepare ((float) sr);
                            RoomParams rp; rp.model = k % 4; rp.size = 0.3f + 0.1f * (k % 3); rp.decay = 0.6f + 0.4f * (k % 3); rp.gate = (k % 4 == 2) ? 0.5f : 0.0f; rp.mix = 0.4f;
                            for (int pos = 0; pos < b.getNumSamples(); pos += 512) r.process (b.getWritePointer (0) + pos, b.getWritePointer (1) + pos, std::min (512, b.getNumSamples() - pos), rp);
                        }
                        auto o = finish (b, false);
                        const juce::String nm = juce::String (kDeep ? "faltung deep " : "faltung ") + vtypeInfo (t).name + (fx == 0 ? " dub " : " room ") + juce::String (k + 1).paddedLeft ('0', 2);
                        save (root.getChildFile ("09 dub and room").getChildFile (fx == 0 ? "dub (dl4)" : "room (rv5)").getChildFile (nm + ".wav"), o); ++count;
                    }
                    ++k;
                }
            }
            // wb1 bass: every preset as a short stab and a one-bar note, at C1 (140 bpm, host playing)
            {
                Processor probe (Pid::wb1);
                for (int pr = 0; pr < probe.getNumPrograms(); ++pr)
                    for (int len = 0; len < 2; ++len)
                    {
                        Processor w (Pid::wb1);
                        w.loadProgram (pr);
                        FakeHead head; head.bpm = kDeep ? 122 : 140; w.setPlayHead (&head);
                        w.setRateAndBufferSizeDetails (sr, 512); w.prepareToPlay (sr, 512);
                        const double gate = len == 0 ? (kDeep ? 0.5 : 0.35) : 60.0 / (kDeep ? 122.0 : 140.0) * 4.0;
                        const int total = (int) ((gate + 1.5) * sr);
                        juce::AudioBuffer<float> out (2, total), blk (2, 512);
                        for (int pos = 0; pos < total; pos += 512)
                        {
                            const int n = std::min (512, total - pos);
                            blk.setSize (2, n, false, false, true); blk.clear();
                            juce::MidiBuffer mb;
                            const int key = kDeep ? (len == 0 ? 60 : 48) : 36;
                            if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 110), 0);
                            const int off = (int) (gate * sr);
                            if (off >= pos && off < pos + n) mb.addEvent (juce::MidiMessage::noteOff (1, key), off - pos);
                            w.processBlock (blk, mb);
                            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
                            head.ppq += n / sr * head.bpm / 60.0;
                        }
                        w.setPlayHead (nullptr);
                        auto o = finish (out, false);
                        const juce::String nm = kDeep ? "faltung deep " + probe.getProgramName (pr) + (len == 0 ? " short (C3)" : " 1 bar 122 (C2)")
                                                      : "faltung bass " + probe.getProgramName (pr) + (len == 0 ? " stab" : " 1 bar 140") + " (C1)";
                        save (root.getChildFile (kDeep ? "10 stabs and bass (sb1)" : "10 bass (wb1)").getChildFile (nm + ".wav"), o); ++count;
                    }
            }
            std::cout << count << " one-shots written to " << root.getFullPathName() << "\n";
            return 0;
        }
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--master" && i + 3 < argc)
        {
            // faltungTest --master in.wav <preset index> out.wav : the file through ms0, latency removed
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (argv[i + 1])));
            if (! rd) return 1;
            const int len = (int) rd->lengthInSamples; const double sr = rd->sampleRate;
            juce::AudioBuffer<float> in (2, len); rd->read (&in, 0, len, 0, true, true);
            Processor m (Pid::ms0);
            m.loadProgram (juce::String (argv[i + 2]).getIntValue());
            m.setRateAndBufferSizeDetails (sr, 512); m.prepareToPlay (sr, 512);
            const int lat = m.getLatencySamples(), total = len + lat;
            juce::AudioBuffer<float> out (2, len), blk (2, 512);
            for (int pos = 0; pos < total; pos += 512)
            {
                const int n = std::min (512, total - pos);
                blk.setSize (2, n, false, false, true); blk.clear();
                for (int c = 0; c < 2; ++c) if (pos < len) blk.copyFrom (c, 0, in, c, pos, std::min (n, len - pos));
                juce::MidiBuffer mb; m.processBlock (blk, mb);
                for (int k = 0; k < n; ++k) { const int o = pos + k - lat; if (o >= 0 && o < len) { out.setSample (0, o, blk.getSample (0, k)); out.setSample (1, o, blk.getSample (1, k)); } }
            }
            auto stats = [&] (const juce::AudioBuffer<float>& b, const char* name)
            {
                Loudness lu; lu.prepare (sr); TruePeak a1, a2; float tp = 0;
                for (int pos = 0; pos < len; pos += 4800) lu.process (b.getReadPointer (0) + pos, b.getReadPointer (1) + pos, std::min (4800, len - pos));
                for (int k = 0; k < len; ++k) tp = std::max (tp, std::max (a1.push (b.getSample (0, k)), a2.push (b.getSample (1, k))));
                std::cout << name << ": integrated " << lu.integrated.load() << " lufs, lra " << lu.range.load() << " lu, true peak " << gainToDb (tp) << " dbtp\n";
            };
            std::cout << "preset: " << m.getProgramName (m.getCurrentProgram()) << ", latency " << lat << " samples\n";
            stats (in, "in "); stats (out, "out");
            writeWav (juce::File (argv[i + 3]), out, sr);
            return 0;
        }
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--auto")
        {
            // faltungTest --auto a.wav b.wav ... : what ms0's auto hears in each (20 s from a third of
            // the way in, as you'd play it the busy part), and what it would set
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            for (int f = i + 1; f < argc; ++f)
            {
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (argv[f])));
                if (! rd) continue;
                const double sr = rd->sampleRate; const int len = (int) rd->lengthInSamples;
                juce::AudioBuffer<float> buf (2, len); rd->read (&buf, 0, len, 0, true, true);
                if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);
                AutoListen al; al.prepare (sr); al.start (20.0);
                for (int pos = len / 3; pos < len && al.listening(); pos += 512)
                    al.push (buf.getReadPointer (0) + pos, buf.getReadPointer (1) + pos, std::min (512, len - pos), 1.0f);
                if (al.listening()) al.stop();
                if (! al.ready()) { std::cout << juce::File (argv[f]).getFileName() << ": not enough sound\n"; continue; }
                const auto m = al.measures();
                std::cout << "== " << juce::File (argv[f]).getFileName() << "\n   heard " << m.seconds << " s, " << m.integrated << " lufs, lra " << m.lra
                          << ", tp " << m.truePeak << ", crest " << (m.peak - m.rms) << ", corr " << m.corr << "\n   per octave (mid):";
                for (float v : m.midPerOct) std::cout << " " << juce::String (v, 1);
                const auto d = decideAuto (m);
                std::cout << "\n   " << d.summary << "\n   ";
                for (auto& [id, v] : d.params) std::cout << id << "=" << juce::String (v, 2) << " ";
                std::cout << "\n";
            }
            return 0;
        }
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--analyze")
        {
            // measure mixes the way ms0 sees them: loudness, peaks, spectrum by region (mid / side), width by band
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            const float regions[11] = { 20, 40, 80, 160, 350, 700, 1500, 4000, 8000, 16000, 24000 };
            const char* rn[10] = { "subsonic", "sub", "body", "low-mid", "box", "mid", "presence", "detail", "air", "ext" };
            std::cout << "file,len_s,sr,lufs_i,lra,tp_dbtp,peak_db,rms_db,crest_db,plr,corr,dc_db,below30_db,above16k_db,w_sub,w_lowmid,w_midhigh,w_high";
            for (auto* r : rn) std::cout << ",M_" << r; for (auto* r : rn) std::cout << ",S_" << r; std::cout << "\n";
            for (int f = i + 1; f < argc; ++f)
            {
                juce::File file (argv[f]);
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
                if (! rd) continue;
                const double sr = rd->sampleRate; const int len = (int) rd->lengthInSamples;
                juce::AudioBuffer<float> buf (2, len);
                rd->read (&buf, 0, len, 0, true, true);
                if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);
                Loudness lu; lu.prepare (sr);
                TruePeak tpl, tpr; float tp = 0, pk = 0; double ss = 0, dcl = 0, dcr = 0, lr = 0, ll = 0, rr = 0;
                std::vector<float> st;
                const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
                const int blk = (int) (sr * 0.1);
                for (int pos = 0; pos < len; pos += blk)
                {
                    const int n = std::min (blk, len - pos);
                    lu.process (L + pos, R + pos, n);
                    if (pos > (int) (3 * sr)) st.push_back (lu.shortTerm.load());
                }
                for (int k = 0; k < len; ++k)
                {
                    tp = std::max (tp, std::max (tpl.push (L[k]), tpr.push (R[k])));
                    pk = std::max (pk, std::max (std::abs (L[k]), std::abs (R[k])));
                    ss += (double) L[k] * L[k] + (double) R[k] * R[k]; dcl += L[k]; dcr += R[k];
                    lr += (double) L[k] * R[k]; ll += (double) L[k] * L[k]; rr += (double) R[k] * R[k];
                }
                // loudness range (EBU: 10th to 95th percentile of gated short-term values)
                std::vector<float> g; for (float v : st) if (v > -70) g.push_back (v);
                std::sort (g.begin(), g.end());
                float lra = 0;
                if (g.size() > 10)
                {
                    double e = 0; for (float v : g) e += std::pow (10.0, v / 10.0); const float rel = (float) (10 * std::log10 (e / g.size())) - 20.0f;
                    std::vector<float> h; for (float v : g) if (v > rel) h.push_back (v);
                    if (h.size() > 10) lra = h[(size_t) (h.size() * 0.95)] - h[(size_t) (h.size() * 0.10)];
                }
                // averaged mid / side power spectra
                const int order = 15, N = 1 << order; juce::dsp::FFT fft (order);
                std::vector<float> win ((size_t) N), bm ((size_t) 2 * N), bs ((size_t) 2 * N);
                for (int k = 0; k < N; ++k) win[(size_t) k] = 0.5f - 0.5f * std::cos (kTwoPi * k / (N - 1));
                std::vector<double> pm ((size_t) N / 2 + 1, 0.0), ps ((size_t) N / 2 + 1, 0.0); int frames = 0;
                for (int pos = 0; pos + N <= len; pos += N / 2)
                {
                    std::fill (bm.begin(), bm.end(), 0.0f); std::fill (bs.begin(), bs.end(), 0.0f);
                    for (int k = 0; k < N; ++k) { bm[(size_t) k] = 0.5f * (L[pos + k] + R[pos + k]) * win[(size_t) k]; bs[(size_t) k] = 0.5f * (L[pos + k] - R[pos + k]) * win[(size_t) k]; }
                    fft.performFrequencyOnlyForwardTransform (bm.data(), true); fft.performFrequencyOnlyForwardTransform (bs.data(), true);
                    for (int k = 0; k <= N / 2; ++k) { pm[(size_t) k] += (double) bm[(size_t) k] * bm[(size_t) k]; ps[(size_t) k] += (double) bs[(size_t) k] * bs[(size_t) k]; }
                    ++frames;
                }
                auto band = [&] (const std::vector<double>& v, float lo, float hi) { double e = 0; for (int k = 1; k <= N / 2; ++k) { const double fr = k * sr / N; if (fr >= lo && fr < hi) e += v[(size_t) k]; } return e; };
                double tot = 0; for (int k = 1; k <= N / 2; ++k) tot += pm[(size_t) k] + ps[(size_t) k];
                auto rel = [&] (double e) { return 10.0 * std::log10 (e / tot + 1e-20); };
                auto width = [&] (float lo, float hi) { const double m = band (pm, lo, hi), sd = band (ps, lo, hi); return std::sqrt (sd / (m + sd + 1e-30)); };
                const double rms = std::sqrt (ss / (2.0 * len));
                std::cout << file.getFileName() << "," << len / sr << "," << sr << "," << lu.integrated.load() << "," << lra << "," << gainToDb (tp) << "," << gainToDb (pk)
                          << "," << gainToDb ((float) rms) << "," << gainToDb (pk) - gainToDb ((float) rms) << "," << gainToDb (tp) - lu.integrated.load()
                          << "," << lr / std::sqrt (ll * rr + 1e-30) << "," << gainToDb ((float) std::abs ((dcl + dcr) / (2.0 * len)))
                          << "," << rel (band (pm, 1, 30) + band (ps, 1, 30)) << "," << rel (band (pm, 16000, 24000) + band (ps, 16000, 24000))
                          << "," << width (20, 80) << "," << width (80, 350) << "," << width (350, 4000) << "," << width (4000, 20000);
                for (int r = 0; r < 10; ++r) std::cout << "," << rel (band (pm, regions[r], regions[r + 1]));
                for (int r = 0; r < 10; ++r) std::cout << "," << rel (band (ps, regions[r], regions[r + 1]));
                std::cout << "\n" << std::flush;
            }
            return 0;
        }
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
            for (int f : { 2, 4, 8 })
                for (float hz : { 100.0f, 1000.0f, 10000.0f, 18000.0f })
                {
                    OverN o; o.prepare (4096);
                    std::vector<float> in (4096), up ((size_t) 4096 * 8), out (4096);
                    double e2 = 0, r2 = 0; const int lat2 = OverN::latencyFor (f);
                    for (int blk = 0; blk < 8; ++blk)
                    {
                        for (int k = 0; k < 4096; ++k) in[(size_t) k] = std::sin (kTwoPi * hz * (blk * 4096 + k) / 48000.0f);
                        o.up (in.data(), 4096, f, up.data()); o.down (up.data(), 4096 * f, f, out.data());
                        if (blk > 1) for (int k = 0; k < 4096; ++k) { const float ref2 = std::sin (kTwoPi * hz * (blk * 4096 + k - lat2) / 48000.0f); e2 += (out[(size_t) k] - ref2) * (out[(size_t) k] - ref2); r2 += ref2 * ref2; }
                    }
                    std::cout << "oversampling " << f << "x round trip at " << hz << " hz: residual " << 10 * std::log10 (e2 / r2 + 1e-30) << " dB\n";
                }
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
        if (kDeep && (Pid) i == Pid::ms0) continue;   // not part of faltung deep
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
            proc->loadProgram (pr);
            auto o = render (*proc, pi.kind == Kind::Effect ? &loop : nullptr, 2.0, rt);
            bool ok = true; float pk = o.getMagnitude (0, o.getNumSamples());
            for (int c = 0; c < 2; ++c) for (int k = 0; k < o.getNumSamples(); ++k) ok &= std::isfinite (o.getSample (c, k));
            check (ok && pk < 4.0f && (pk > 1.0e-4f || proc->getProgramName (pr) == "empty"), "preset " + proc->getProgramName (pr) + " peak " + juce::String (pk));
            if (wavDir != juce::File() && pi.kind != Kind::Effect)
                writeWav (wavDir.getChildFile (juce::String (pi.code) + "-preset-" + proc->getProgramName (pr).replaceCharacter (' ', '_') + ".wav"), o, 48000.0);
        }

        if (pi.id == Pid::wb1)
        {
            // one key, a whole chord: C3 on "minor 9" must sound C Eb G Bb D
            Processor c (Pid::wb1);
            c.setParam ("chord", 9.0f); c.setParam ("voices", 1.0f); c.setParam ("wcut", 0.0f); c.setParam ("cutoff", 8000.0f);
            c.setParam ("a_shape", 0.0f); c.setParam ("b_level", 0.0f); c.setParam ("sub", 0.0f); c.setParam ("fmtmix", 0.0f); c.setParam ("ddrive", 0.0f);
            c.setRateAndBufferSizeDetails (48000.0, 512); c.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> o (2, 48000), blk (2, 512);
            for (int pos = 0; pos < 48000; pos += 512)
            {
                blk.clear(); juce::MidiBuffer mb; if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                c.processBlock (blk, mb); for (int ch = 0; ch < 2; ++ch) o.copyFrom (ch, pos, blk, ch, 0, std::min (512, 48000 - pos));
            }
            const int N = 16384; juce::dsp::FFT fft (14); std::vector<float> x ((size_t) N * 2, 0.0f);
            for (int k = 0; k < N; ++k) x[(size_t) k] = o.getSample (0, 20000 + k) * (0.5f - 0.5f * std::cos (kTwoPi * k / (N - 1)));
            fft.performFrequencyOnlyForwardTransform (x.data(), true);
            int found = 0;
            for (int semi : { 0, 3, 7, 10, 14 })
            {
                const float f = 261.63f * std::exp2 (semi / 12.0f); const int bin = (int) std::round (f * N / 48000.0f);
                float pk = 0, floor_ = 0; for (int k = bin - 2; k <= bin + 2; ++k) pk = std::max (pk, x[(size_t) k]);
                for (int k = bin + 6; k <= bin + 12; ++k) floor_ += x[(size_t) k] / 7.0f;
                if (pk > 8.0f * floor_ + 1.0e-3f) ++found;
            }
            std::cout << "  chord: one key on minor 9 sounds " << found << " of 5 chord tones\n";
            check (found == 5, "wb1 chord");
        }
        if (pi.id == Pid::mc3)
        {
            // the grid: a tempo division follows the host tempo; "free" runs from the free rate knob and ignores it
            auto ticksAt = [&] (float division, float freeHz, double bpm)
            {
                Processor q (Pid::mc3);
                q.setParam ("mode", 0.0f); q.setParam ("density", 1.0f); q.setParam ("division", division); q.setParam ("freerate", freeHz);
                const double sr = 48000.0; const int bs = 512, total = (int) (sr * 4.0);
                FakeHead head; head.bpm = bpm; q.setPlayHead (&head); q.setRateAndBufferSizeDetails (sr, bs); q.prepareToPlay (sr, bs);
                juce::AudioBuffer<float> blk (2, bs);
                for (int pos = 0; pos < total; pos += bs)
                {
                    const int n = std::min (bs, total - pos);
                    blk.setSize (2, n, false, false, true);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) blk.setSample (c, i, loop.getSample (c, (pos + i) % loop.getNumSamples()));
                    juce::MidiBuffer mb; q.processBlock (blk, mb);
                    head.ppq += n / sr * head.bpm / 60.0;
                }
                q.setPlayHead (nullptr);
                return q.micro()->ticks.load();
            };
            const int s124 = ticksAt (4.0f, 8.0f, 124.0), s90 = ticksAt (4.0f, 8.0f, 90.0);     // 1/16
            const int f124 = ticksAt ((float) MicroParams::kFree, 20.0f, 124.0), f90 = ticksAt ((float) MicroParams::kFree, 20.0f, 90.0);
            std::cout << "  grid ticks in 4 s: 1/16 at 124 bpm " << s124 << ", at 90 bpm " << s90 << " · free 20 hz at 124 bpm " << f124 << ", at 90 bpm " << f90 << "\n";
            check (std::abs (s124 - 33) <= 2 && std::abs (s90 - 24) <= 2, "mc3 division follows the tempo");
            check (std::abs (f124 - 80) <= 2 && f90 == f124, "mc3 free rate ignores the tempo");
        }
        if (pi.id == Pid::fl7)
        {
            // the lfo: a division follows the tempo; "free" (the last entry of the rate) runs at the free rate knob
            auto flips = [&] (float division, float freeHz, double bpm)
            {
                Processor q (Pid::fl7);
                q.setParam ("sweep", -0.55f); q.setParam ("lfodepth", 0.4f); q.setParam ("lfoshape", 2.0f); q.setParam ("res", 0.0f); q.setParam ("lfodiv", division); q.setParam ("lfofree", freeHz);
                const double sr = 48000.0; const int bs = 256, total = (int) (sr * 4.0);
                FakeHead head; head.bpm = bpm; q.setPlayHead (&head); q.setRateAndBufferSizeDetails (sr, bs); q.prepareToPlay (sr, bs);
                juce::AudioBuffer<float> blk (2, bs); Rng nz; nz.seed (11); std::vector<float> level;
                for (int pos = 0; pos < total; pos += bs)
                {
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) blk.setSample (c, i, 0.3f * nz.bi());
                    juce::MidiBuffer mb; q.processBlock (blk, mb);
                    level.push_back (blk.getRMSLevel (0, 0, bs));
                    head.ppq += bs / sr * head.bpm / 60.0;
                }
                q.setPlayHead (nullptr);
                // the level in db, smoothed over a few blocks, read with hysteresis (noise through a 60 hz low-pass is itself restless)
                std::vector<float> sm; for (size_t i = 3; i < level.size(); ++i) sm.push_back (10.0f * std::log10 ((level[i] * level[i] + level[i - 1] * level[i - 1] + level[i - 2] * level[i - 2] + level[i - 3] * level[i - 3]) * 0.25f + 1.0e-12f));
                float lo = 1e9f, hi = -1e9f; for (size_t i = 40; i < sm.size(); ++i) { lo = std::min (lo, sm[i]); hi = std::max (hi, sm[i]); }
                const float up = lo + (hi - lo) * 0.7f, down = lo + (hi - lo) * 0.45f; int n = 0; bool high = sm[40] > (up + down) * 0.5f;
                for (size_t i = 41; i < sm.size(); ++i) { if (! high && sm[i] > up) { high = true; ++n; } else if (high && sm[i] < down) { high = false; ++n; } }
                return n;
            };
            const int s124 = flips (11.0f, 5.0f, 124.0), s90 = flips (11.0f, 5.0f, 90.0);   // 1/4
            const int f124 = flips ((float) FilterParams::kFree, 5.0f, 124.0), f90 = flips ((float) FilterParams::kFree, 5.0f, 90.0);
            std::cout << "  lfo flips in 4 s: 1/4 at 124 bpm " << s124 << ", at 90 bpm " << s90 << " · free 5 hz at 124 bpm " << f124 << ", at 90 bpm " << f90 << "\n";
            check (std::abs (s124 - 16) <= 2 && std::abs (s90 - 12) <= 2, "fl7 lfo division follows the tempo");
            check (std::abs (f124 - 38) <= 3 && std::abs (f90 - f124) <= 1, "fl7 free lfo rate ignores the tempo");
        }
        if (pi.id == Pid::wb1)
        {
            // the wobble and the steps: "free" in their rate lists takes them off the grid
            auto count = [&] (float wdiv, float wfree, float stepdiv, float stepMs, double bpm, int& stepWraps)
            {
                Processor q (Pid::wb1);
                q.setParam ("wshape", 4.0f); q.setParam ("wretrig", 0.0f); q.setParam ("wdiv", wdiv); q.setParam ("wfree", wfree);
                q.setParam ("stepdiv", stepdiv); q.setParam ("stepfree", stepMs); q.setParam ("steplen", 4.0f); q.setParam ("stepretrig", 0.0f); q.setParam ("as", 1.0f);
                const double sr = 48000.0; const int bs = 128, total = (int) (sr * 4.0);
                FakeHead head; head.bpm = bpm; q.setPlayHead (&head); q.setRateAndBufferSizeDetails (sr, bs); q.prepareToPlay (sr, bs);
                juce::AudioBuffer<float> blk (2, bs); int n = 0; float lastW = -1, lastPh = -1; stepWraps = 0;
                for (int pos = 0; pos < total; pos += bs)
                {
                    blk.clear(); juce::MidiBuffer mb; if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
                    q.processBlock (blk, mb);
                    const float w = q.bass()->showWob.load(), ph = q.bass()->showStepPh.load();
                    if (lastW >= 0 && ((w > 0.5f) != (lastW > 0.5f))) ++n;
                    if (lastPh >= 0 && ph < lastPh - 0.5f) ++stepWraps;
                    lastW = w; lastPh = ph;
                    head.ppq += bs / sr * head.bpm / 60.0;
                }
                q.setPlayHead (nullptr);
                return n;
            };
            int w1, w2, w3, w4;
            const int s124 = count (8.0f, 4.0f, 4.0f, 100.0f, 124.0, w1), s90 = count (8.0f, 4.0f, 4.0f, 100.0f, 90.0, w2);   // wobble 1/8, steps 1/16
            const int f124 = count ((float) BassParams::kFree, 4.0f, (float) BassParams::kFree, 100.0f, 124.0, w3), f90 = count ((float) BassParams::kFree, 4.0f, (float) BassParams::kFree, 100.0f, 90.0, w4);
            std::cout << "  wobble flips in 4 s: 1/8 at 124 bpm " << s124 << ", at 90 bpm " << s90 << " · free 4 hz " << f124 << " / " << f90 << "; step loops (4 steps): 1/16 " << w1 << " / " << w2 << " · free 100 ms " << w3 << " / " << w4 << "\n";
            check (std::abs (s124 - 33) <= 2 && std::abs (s90 - 24) <= 2, "wobble division follows the tempo");
            check (std::abs (f124 - 32) <= 2 && std::abs (f90 - f124) <= 1, "wobble free rate ignores the tempo");
            check (std::abs (w1 - 8) <= 1 && std::abs (w2 - 6) <= 1, "steps division follows the tempo");
            check (std::abs (w3 - 10) <= 1 && w3 == w4, "steps free time ignores the tempo");
        }

        if (pi.id == Pid::ms0)
        {
            // null test: everything neutral → output must equal the input delayed by the reported latency
            Processor m (Pid::ms0);
            for (auto id : { "mbon", "dynon", "glueon", "limon", "saton", "ambon", "limos", "dcon" }) m.setParam (id, 0.0f);
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
            // compare (bypass): with a preset that changes the sound, bypass on → the input, delayed by the latency
            {
                Processor b (Pid::ms0);
                b.loadProgram (2);
                b.setParam ("dcon", 0.0f); b.setParam ("dither", 0.0f); b.setParam ("trim", 0.0f);
                b.setParam ("bypass", 1.0f); b.setParam ("match", 0.0f);
                auto ob = render (b, &loop, 4.0, rt2);
                const int lb = b.getLatencySamples();
                double e2 = 0, r2 = 0;
                for (int c = 0; c < 2; ++c)
                    for (int k = lb + 4800; k < ob.getNumSamples(); ++k)
                    {
                        const float x = loop.getSample (c, (k - lb) % loop.getNumSamples()), y = ob.getSample (c, k);
                        e2 += (double) (y - x) * (y - x); r2 += (double) x * x;
                    }
                const double bypDb = 10.0 * std::log10 ((e2 + 1e-30) / (r2 + 1e-30));
                std::cout << "  compare (bypass) on preset " << b.getProgramName (2) << ": residual vs input " << bypDb << " dB\n";
                check (bypDb < -80.0, "ms0 compare / bypass passes the input");
            }
            // level-matched compare: process for a while (louder than the input), then switch compare on in the
            // middle of the run, as a host automation or the button would: the output becomes the input, at the
            // loudness of the processed signal
            {
                Processor b (Pid::ms0);
                b.loadProgram (2);
                b.setParam ("dcon", 0.0f); b.setParam ("dither", 0.0f); b.setParam ("trim", 0.0f);
                b.setParam ("limgain", 8.0f); b.setParam ("ceiling", -1.0f);
                b.setParam ("bypass", 0.0f); b.setParam ("match", 1.0f);
                const double sr = 48000.0; const int bs = 512, secs = 5;
                FakeHead head; b.setPlayHead (&head); b.setRateAndBufferSizeDetails (sr, bs); b.prepareToPlay (sr, bs);
                const int total = (int) (sr * secs);
                juce::AudioBuffer<float> out (2, total * 2), blk (2, bs);
                auto runFor = [&] (int from)
                {
                    for (int pos = 0; pos < total; pos += bs)
                    {
                        const int n = std::min (bs, total - pos);
                        blk.setSize (2, n, false, false, true);
                        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) blk.setSample (c, i, loop.getSample (c, (from + pos + i) % loop.getNumSamples()));
                        juce::MidiBuffer mb; b.processBlock (blk, mb);
                        for (int c = 0; c < 2; ++c) out.copyFrom (c, from + pos, blk, c, 0, n);
                        head.ppq += n / sr * head.bpm / 60.0;
                    }
                };
                runFor (0);
                b.setParam ("bypass", 1.0f);
                runFor (total);
                const int lb = b.getLatencySamples(), win = (int) (2.0 * sr);
                auto rmsDb = [&] (const juce::AudioBuffer<float>& bf, int from) { return juce::Decibels::gainToDecibels (0.5f * (bf.getRMSLevel (0, from, win) + bf.getRMSLevel (1, from, win))); };
                const double procDb = rmsDb (out, total - win), matchedDb = rmsDb (out, 2 * total - win);
                juce::AudioBuffer<float> inp (2, win);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < win; ++i) inp.setSample (c, i, loop.getSample (c, (2 * total - win - lb + i) % loop.getNumSamples()));
                const double inDb = rmsDb (inp, 0);
                // shape: the matched output is the input times one gain
                double xy = 0, xx = 0, yy = 0;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < win; ++i) { const double x = inp.getSample (c, i), y = out.getSample (c, 2 * total - win + i); xy += x * y; xx += x * x; yy += y * y; }
                const double corr = xy / std::sqrt (xx * yy + 1e-30);
                std::cout << "  level-matched compare: input " << inDb << " dB, processed " << procDb << " dB, compare " << matchedDb << " dB, shape correlation " << corr << "\n";
                check (procDb - inDb > 2.0, "ms0 test preset is louder than the input");
                check (corr > 0.995, "ms0 compare output is the input");
                check (std::abs (matchedDb - procDb) < 1.5, "ms0 level-matched compare sits at the processed loudness");
                b.setPlayHead (nullptr);
            }
            // limiter: a hot signal must stay under the true-peak ceiling
            Processor h (Pid::ms0);
            h.setParam ("limgain", 18.0f); h.setParam ("ceiling", -1.0f);
            auto hot = render (h, &loop, 4.0, rt2);
            float pk = 0;
            for (int c = 0; c < 2; ++c) for (int k = h.getLatencySamples() + 4800; k < hot.getNumSamples(); ++k) pk = std::max (pk, std::abs (hot.getSample (c, k)));
            std::cout << "  limiter: +18 db into -1 dbtp ceiling, sample peak " << juce::Decibels::gainToDecibels (pk) << " dB\n";
            check (pk <= juce::Decibels::decibelsToGain (-0.99f), "ms0 limiter ceiling");
            // true-peak compliance at every oversampling setting (measured with 4x interpolation)
            for (int os = 0; os < 4; ++os)
            {
                Processor t (Pid::ms0);
                t.setParam ("limgain", 18.0f); t.setParam ("ceiling", -1.0f); t.setParam ("limos", (float) os);
                t.setParam ("clipon", os == 3 ? 1.0f : 0.0f);
                auto o2 = render (t, &loop, 4.0, rt2);
                TruePeak a1, a2; float tpk = 0;
                for (int k = 0; k < o2.getNumSamples(); ++k) tpk = std::max (tpk, std::max (a1.push (o2.getSample (0, k)), a2.push (o2.getSample (1, k))));
                const char* nm[4] = { "off", "2x", "4x", "8x + clipper" };
                std::cout << "  limiter os " << nm[os] << ": true peak " << juce::Decibels::gainToDecibels (tpk) << " dBTP, " << (int) rt2 << "x realtime\n";
                check (tpk <= juce::Decibels::decibelsToGain (-0.95f), juce::String ("ms0 true peak, os ") + nm[os]);
            }
        }

        if (pi.id == Pid::ms0)
        {
            // ---- the limiter's meter -----------------------------------------------------------------------
            // the VU movement alone: a sine reads its own level; 99 % of it 300 ms after it starts (+-10 %), 1 - 1.5 %
            // overshoot, the same on the way back (IEC 60268-17); at three sample rates
            const double pi2 = juce::MathConstants<double>::twoPi;
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                auto tone = [&] (LevelMeter& lm, double hzf, double amp, double seconds, double& phase, std::vector<float>* trace)
                {
                    const int chunk = (int) std::round (rate / 1000.0), total = (int) (seconds * rate);
                    std::vector<float> a ((size_t) chunk), b ((size_t) chunk);
                    for (int pos = 0; pos < total; pos += chunk)
                    {
                        for (int i = 0; i < chunk; ++i) { a[(size_t) i] = (float) (amp * std::sin (phase)); b[(size_t) i] = a[(size_t) i]; phase += pi2 * hzf / rate; }
                        lm.process (a.data(), b.data(), chunk);
                        if (trace) trace->push_back (lm.vu[0].load());
                    }
                };
                juce::String line = "  vu at " + juce::String (rate / 1000.0, 1) + " khz:";
                for (auto [hzf, db] : { std::pair<double, double> { 1000.0, -18.0 }, { 1000.0, 0.0 }, { 1000.0, -6.0 }, { 1000.0, -30.0 }, { 100.0, -18.0 }, { 10000.0, -18.0 } })
                {
                    LevelMeter lm; lm.prepare (rate); double ph = 0.3;
                    tone (lm, hzf, std::pow (10.0, db / 20.0), 2.0, ph, nullptr);
                    const float got = lm.vuDb (0);
                    line << "  " << (hzf >= 1000 ? juce::String (hzf / 1000.0, 0) + "k" : juce::String (hzf, 0)) << " hz " << juce::String (db, 0) << " dbfs reads " << juce::String (got, 2);
                    check (std::abs (got - db) < 0.1 && std::abs (lm.vuDb (1) - db) < 0.1, "vu calibration: a " + juce::String (db, 0) + " dbfs sine at " + juce::String (hzf, 0) + " hz reads its level");
                }
                std::cout << line << "\n";
                LevelMeter lm; lm.prepare (rate); double ph = 0;
                std::vector<float> up, down;
                const double amp = std::pow (10.0, -18.0 / 20.0);
                tone (lm, 1000.0, amp, 1.5, ph, &up);
                const float steady = up.back();
                tone (lm, 1000.0, 0.0, 1.5, ph, &down);
                double rise = 0, fallT = 0; float top = 0;
                for (size_t i = 0; i < up.size(); ++i) { top = std::max (top, up[i]); if (rise == 0 && up[i] >= 0.99f * steady) rise = (double) (i + 1) * std::round (rate / 1000.0) / rate * 1000.0; }
                for (size_t i = 0; i < down.size(); ++i) if (down[i] <= 0.01f * steady) { fallT = (double) (i + 1) * std::round (rate / 1000.0) / rate * 1000.0; break; }
                const double over = (top / steady - 1.0) * 100.0;
                std::cout << "    0 vu tone on: 99 % after " << juce::String (rise, 1) << " ms, overshoot " << juce::String (over, 2) << " %; tone off: 1 % after " << juce::String (fallT, 1) << " ms\n";
                check (rise > 270.0 && rise < 330.0, "vu rise: 99 % in 300 ms +-10 %");
                check (over >= 1.0 && over <= 1.5, "vu overshoot 1 - 1.5 %");
                check (fallT > 270.0 && fallT < 330.0, "vu fall: back in 300 ms +-10 %");
            }
            {
                // true peak: a full-scale sine at fs / 4 sampled 45 degrees off its crests has samples at -3 dB; the
                // meter must see the 0 dB between them. Then the hold: there for 2 s, gone after
                LevelMeter lm; lm.prepare (48000.0);
                std::vector<float> a (4800), z (4800, 0.0f);
                for (int i = 0; i < 4800; ++i) a[(size_t) i] = (float) std::sin (pi2 * 0.25 * i + pi2 / 8.0);
                lm.process (a.data(), a.data(), 4800);
                const float tp = lm.peakDb (0);
                lm.process (z.data(), z.data(), 480);                                          // (the cut itself peaks a little higher)
                const float top = lm.holdDb (0);
                for (int k = 0; k < 15; ++k) lm.process (z.data(), z.data(), 4800);           // 1.5 s of silence
                const float held = lm.holdDb (0), fallen = lm.peakDb (0);
                for (int k = 0; k < 20; ++k) lm.process (z.data(), z.data(), 4800);           // 2 s more
                std::cout << "  true peak of a 12 khz sine with -3.01 db samples: " << juce::String (tp, 2) << " dbtp; held after 1.5 s " << juce::String (held, 2)
                          << ", peak mark by then " << juce::String (fallen, 1) << "; hold after 3.5 s " << juce::String (lm.holdDb (0), 1) << "\n";
                check (tp > -0.3f && tp < 0.2f, "meter true peak sees between the samples");
                check (std::abs (held - top) < 0.01f && top < 0.5f, "peak hold stays for 2 s");
                check (std::abs (fallen - (top - 20.0f * 1.5f / 1.7f)) < 0.5f, "peak mark falls 20 db in 1.7 s");
                check (lm.holdDb (0) < held - 10.0f, "peak hold lets go");
            }
            {
                // in the plug-in: the meter sits around the limiter. Everything else neutral, limiter gain +6: a
                // -18 dbfs sine on the left only reads -12 going in and coming out, nothing on the right, no reduction;
                // gain +18 into a -1 dbtp ceiling with a -6 dbfs sine: +12 going in, held under the ceiling coming out,
                // and the reduction the meter shows is the difference
                auto run = [&] (float limGain, double ampL, double ampR, float& inL, float& inR, float& outL, float& outR, float& outPk, float& gr, float& grHold)
                {
                    Processor m (Pid::ms0);
                    for (auto id : { "mbon", "dynon", "glueon", "saton", "ambon", "dcon" }) m.setParam (id, 0.0f);
                    for (int b = 1; b <= 4; ++b) m.setParam ("b" + juce::String (b) + "width", 1.0f);
                    m.setParam ("match", 0.0f); m.setParam ("limgain", limGain); m.setParam ("ceiling", -1.0f);
                    const double sr = 48000.0; const int bs = 256;
                    m.setRateAndBufferSizeDetails (sr, bs); m.prepareToPlay (sr, bs);
                    juce::AudioBuffer<float> blk (2, bs); juce::MidiBuffer mb; double ph = 0;
                    for (int pos = 0; pos < (int) (3.0 * sr); pos += bs)
                    {
                        for (int i = 0; i < bs; ++i) { blk.setSample (0, i, (float) (ampL * std::sin (ph))); blk.setSample (1, i, (float) (ampR * std::sin (ph))); ph += pi2 * 1000.0 / sr; }
                        m.processBlock (blk, mb);
                    }
                    auto* mc = m.master();
                    inL = mc->limIn.vuDb (0); inR = mc->limIn.vuDb (1); outL = mc->limOut.vuDb (0); outR = mc->limOut.vuDb (1);
                    outPk = mc->limOut.holdDb (0); gr = mc->limGrMeter.load(); grHold = mc->limGrHold.load();
                };
                float inL, inR, outL, outR, outPk, gr, grHold;
                run (6.0f, std::pow (10.0, -18.0 / 20.0), 0.0, inL, inR, outL, outR, outPk, gr, grHold);
                std::cout << "  limiter meter, -18 dbfs sine left only, gain +6: in l " << juce::String (inL, 2) << " r " << juce::String (inR, 1) << ", out l " << juce::String (outL, 2)
                          << " r " << juce::String (outR, 1) << ", out peak " << juce::String (outPk, 2) << " dbtp, gr " << juce::String (gr, 2) << "\n";
                check (std::abs (inL + 12.0f) < 0.1f && std::abs (outL + 12.0f) < 0.1f, "limiter meter reads the level going in and coming out");
                check (inR < -90.0f && outR < -90.0f, "limiter meter is stereo: a silent right channel reads nothing");
                check (std::abs (outPk + 12.0f) < 0.1f && gr < 0.01f, "limiter meter: peak of an untouched sine, no reduction");
                run (18.0f, 0.5, 0.5, inL, inR, outL, outR, outPk, gr, grHold);
                std::cout << "  limiter meter, -6 dbfs sine, gain +18, ceiling -1: in " << juce::String (inL, 2) << " / " << juce::String (inR, 2) << ", out " << juce::String (outL, 2) << " / " << juce::String (outR, 2)
                          << ", out peak " << juce::String (outPk, 2) << " dbtp, gr " << juce::String (gr, 2) << " (hold " << juce::String (grHold, 2) << ")\n";
                check (std::abs (inL - 12.0f) < 0.15f && std::abs (inR - 12.0f) < 0.15f, "limiter meter: level going in after the gain");
                check (outPk <= -0.95f && outPk > -1.3f, "limiter meter: output peak sits at the ceiling");
                check (std::abs (gr - (inL - outL)) < 0.5f && grHold >= gr - 0.01f, "limiter meter: reduction = in - out");
            }

            // ---- the dynamic eq's display ------------------------------------------------------------------
            // the curve the editor draws for a dynamic band (DynResponse) against the plug-in: a band pinned at its
            // range (threshold far below the tone), a sine at each frequency, level out / level in
            {
                auto measure = [&] (float freq, float q, float range, float eqGain, std::initializer_list<double> probes, const juce::String& what)
                {
                    DynResponse R; R.update (48000.0, freq, q);
                    MasterParams mp; mp.eq[0].on = eqGain != 0.0f; mp.eq[0].type = 0; mp.eq[0].freq = freq; mp.eq[0].gain = eqGain; mp.eq[0].q = 0.7f;
                    juce::String line = "  dynamic band " + what + " (" + juce::String (R.loHz, 1) + " - " + juce::String (R.hiHz, 1) + " hz, " + juce::String (R.octaves(), 2) + " oct, reach " + juce::String (R.peak * 100.0f, 0) + "%):";
                    double worst = 0;
                    for (double f : probes)
                    {
                        Processor m (Pid::ms0);
                        for (auto id : { "mbon", "glueon", "limon", "saton", "ambon", "limos", "dcon" }) m.setParam (id, 0.0f);
                        for (int b = 1; b <= 4; ++b) m.setParam ("b" + juce::String (b) + "width", 1.0f);
                        m.setParam ("match", 0.0f); m.setParam ("dynon", 1.0f); m.setParam ("d1on", 1.0f); m.setParam ("d1ch", 0.0f);
                        m.setParam ("d1freq", freq); m.setParam ("d1q", q); m.setParam ("d1thr", -60.0f); m.setParam ("d1range", range);
                        if (eqGain != 0.0f) { m.setParam ("e1on", 1.0f); m.setParam ("e1type", 0.0f); m.setParam ("e1freq", freq); m.setParam ("e1gain", eqGain); m.setParam ("e1q", 0.7f); }
                        const double sr = 48000.0; const int bs = 512, total = (int) (1.5 * sr);
                        m.setRateAndBufferSizeDetails (sr, bs); m.prepareToPlay (sr, bs);
                        juce::AudioBuffer<float> blk (2, bs); juce::MidiBuffer mb; double ph = 0, sum = 0; int cnt = 0;
                        for (int pos = 0; pos < total; pos += bs)
                        {
                            for (int i = 0; i < bs; ++i) { const float v = (float) (0.25 * std::sin (ph)); blk.setSample (0, i, v); blk.setSample (1, i, v); ph += pi2 * f / sr; }
                            m.processBlock (blk, mb);
                            if (pos > (int) sr) for (int i = 0; i < bs; ++i) { sum += (double) blk.getSample (0, i) * blk.getSample (0, i); ++cnt; }
                        }
                        const double got = 10.0 * std::log10 (sum / cnt / (0.25 * 0.25 * 0.5));
                        const double eq = MasterChain::eqMag (mp, f, 1), drawn = 20.0 * std::log10 (eq) + R.changeDb (f, range, eq);
                        worst = std::max (worst, std::abs (got - drawn));
                        line << "  " << juce::String (f, 0) << " hz " << juce::String (got, 2) << " (drawn " << juce::String (drawn, 2) << ")";
                    }
                    std::cout << line << "\n";
                    check (worst < 0.15, "dynamic eq display matches the plug-in: " + what);
                    return R.peak;
                };
                measure (1000.0f, 2.0f, -6.0f, 0.0f, { 1000.0, 800.0, 1250.0, 500.0, 2000.0, 4000.0 }, "1 khz q 2 range -6");
                measure (3000.0f, 8.0f, 6.0f, 0.0f, { 3000.0, 2800.0, 3200.0, 2000.0 }, "3 khz q 8 range +6");
                const float reach = measure (50.0f, 8.0f, -6.0f, 0.0f, { 50.0, 45.0, 56.0, 80.0 }, "50 hz q 8 range -6");
                check (reach < 0.9f, "a narrow band low down is shown lower than its range (the filter has 8192 taps)");
                measure (1000.0f, 2.0f, -6.0f, -4.0f, { 1000.0, 700.0, 1500.0 }, "1 khz q 2 range -6 on a -4 db eq bell");
                // the width a q gives, on the analog prototype: q 1.41 = 1 octave
                check (std::abs (DynResponse::octavesForQ (1.4142f) - 1.0f) < 0.01f, "q 1.41 is one octave");
                DynResponse R; R.update (48000.0, 1000.0f, 1.4142f);
                std::cout << "  1 khz q 1.41: edges " << R.loHz << " - " << R.hiHz << " hz, " << R.octaves() << " oct\n";
                check (std::abs (R.loHz - 707.1f) < 4.0f && std::abs (R.hiHz - 1414.2f) < 6.0f, "measured -3 db edges of a one-octave band");
            }
            // typing values
            {
                Processor m (Pid::ms0);
                auto typed = [&] (const char* id, const char* text) { float v = -999.0f; return ui::parseTyped (*m.apvts.getParameter (id), text, v) ? v : -999.0f; };
                auto near = [] (float a, float b) { return std::abs (a - b) <= 1.0e-3f * std::max (1.0f, std::abs (b)); };
                const std::tuple<const char*, const char*, float> cases[] = {
                    { "d1freq", "1.2k", 1200.0f }, { "d1freq", "850", 850.0f }, { "d1freq", "850 hz", 850.0f }, { "d1freq", "2k5", 2500.0f }, { "d1freq", "12.5 khz", 12500.0f },
                    { "d1freq", "1,2K", 1200.0f }, { "d1freq", "2.8", 2800.0f }, { "d1freq", "a4", 440.0f }, { "d1freq", "c#3", 138.5913f }, { "d1freq", "bb2", 116.5409f },
                    { "d1freq", "a4 +50", 452.893f }, { "d1freq", "A4 -50 c", 427.474f }, { "d1freq", "5", 5000.0f }, { "d1freq", "99999", 20000.0f }, { "d1freq", "", -999.0f },
                    { "d1freq", "hello", -999.0f }, { "d1freq", "c", -999.0f }, { "e3freq", "g2", 97.9989f }, { "hpfreq", "30", 30.0f },
                    { "d1q", "2.5", 2.5f }, { "d1q", "99", 12.0f }, { "d1thr", "-24.5 db", -24.5f }, { "d1range", "+3", 3.0f }, { "d1range", "-30", -18.0f },
                    { "d1att", "20", 20.0f }, { "d1att", "0.02 s", 20.0f }, { "ambdecay", "600 ms", 0.6f }, { "ambdecay", "1.5", 1.5f },
                    { "b2width", "115%", 1.15f }, { "b2width", "115", 1.15f }, { "gratio", "2.5:1", 2.5f }, { "ceiling", "-0.3", -0.3f },
                    { "balance", "l 20", -0.2f }, { "balance", "r20", 0.2f }, { "balance", "c", 0.0f }, { "balance", "-30", -0.3f } };
                int bad = 0;
                for (auto& [id, text, want] : cases)
                {
                    const float got = typed (id, text);
                    if (! near (got, want)) { ++bad; std::cout << "    typed \"" << text << "\" into " << id << ": " << got << ", expected " << want << "\n"; }
                }
                std::cout << "  typed values: " << (int) std::size (cases) - bad << " of " << (int) std::size (cases) << " read as meant\n";
                check (bad == 0, "typed values are read as meant");
                check (ui::noteName (440.0f) == "a4" && ui::noteName (412.0f) == "g#4 -14 c" && ui::noteName (27.5f) == "a0", "note names");
                // through the window: the text field over dynamic band 1's frequency, "1.2k", return
                std::unique_ptr<juce::AudioProcessorEditor> ed (m.createEditor());
                if (auto* fe = dynamic_cast<Editor*> (ed.get()))
                {
                    fe->pageForTest (2); fe->pageForTest (3);
                    check (near (m.param ("d1freq"), 1200.0f), "typing 1.2k in the window sets the frequency");
                    fe->pageForTest (0);
                }
                // the meter's reference level travels with the session; a session saved before it existed opens at -18
                m.vuRef = -14;
                juce::MemoryBlock mb; m.getStateInformation (mb);
                Processor m2 (Pid::ms0); m2.setStateInformation (mb.getData(), (int) mb.getSize());
                check (m2.vuRef == -14 && near (m2.param ("d1freq"), 1200.0f), "meter reference in the state");
                if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
                {
                    xml->removeAttribute ("vuref");
                    juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
                    Processor m3 (Pid::ms0); m3.vuRef = -10; m3.setStateInformation (old.getData(), (int) old.getSize());
                    check (m3.vuRef == -18 && near (m3.param ("d1freq"), 1200.0f), "a session without the meter reference loads");
                }
            }
        }

        if (pi.id == Pid::kt2)
        {
            // the sequencer alone: a ratchet with the AERIA-style curve crowds its hits toward the end ("in") and
            // still fits the step; the clock curve stretches the steps of a loop and keeps step 1 on time
            {
                Sequencer<1> sq; sq.prepare (48000.0); Rng rg; rg.seed (7);
                std::array<TrackCtl, 1> ctl { TrackCtl { 16, 1.0f, 0.0f, false } };
                auto hits = [&] (int ratchet, int curve, SeqCurve cc, int len)
                {
                    Pattern<1> pat;
                    for (int s = 0; s < len; ++s) { auto& st = pat.t[0][(size_t) s]; st.on = true; st.ratchet = ratchet; st.curve = curve; }
                    ctl[0].length = len;
                    sq.prepare (48000.0);
                    std::vector<int> out; int pos = 0;
                    for (int blk = 0; blk < 400; ++blk)   // 400 x 512 = 204800 samples ≈ 2.1 bars at 120 bpm
                    {
                        sq.run (pat, ctl, 512, false, false, 0.0, 120.0, true, 0.5f, 0.0f, rg, [&] (const SeqEvent& e) { out.push_back (pos + e.sample); }, cc);
                        pos += 512;
                    }
                    return out;
                };
                const int stepS = 6000;   // samples per 16th at 120 bpm
                auto even = hits (4, 0, {}, 1);
                check (even.size() >= 8 && std::abs (even[1] - even[0] - stepS / 4) <= 2 && std::abs (even[3] - even[2] - stepS / 4) <= 2, "ratchet: four even hits");
                auto in = hits (4, 7, {}, 1);
                check (in.size() >= 8 && (in[1] - in[0]) > (in[2] - in[1]) && (in[2] - in[1]) > (in[3] - in[2]) && in[3] < in[0] + stepS && std::abs (in[4] - in[0] - stepS) <= 2,
                       "ratchet curve in: gaps shrink, the row fits the step");
                auto outc = hits (4, -7, {}, 1);
                check (outc.size() >= 8 && (outc[1] - outc[0]) < (outc[2] - outc[1]) && (outc[2] - outc[1]) < (outc[3] - outc[2]) && outc[3] < outc[0] + stepS,
                       "ratchet curve out: gaps grow");
                auto plain = hits (1, 0, {}, 16);
                check (plain.size() >= 32 && std::abs (plain[1] - plain[0] - stepS) <= 2, "clock curve off: even steps");
                auto acc = hits (1, 0, SeqCurve { 1, 1.0f }, 16);
                bool ok = acc.size() >= 32;
                if (ok)
                {
                    ok = (acc[1] - acc[0]) > stepS * 3 / 2 && (acc[15] - acc[14]) < stepS / 2;   // long steps first, short at the end
                    ok = ok && std::abs (acc[16] - acc[0] - 16 * stepS) <= 2;                   // the loop keeps its plain length
                    for (int k = 1; k < 16; ++k) ok = ok && acc[k] > acc[k - 1];
                }
                check (ok, "clock curve accel: long steps first, loop on time");
            }
            // per-step pitch: a 440 Hz sine as the loop, one step on, step 1 pitch +12 → 880 Hz out
            auto peakHz = [&] (float stepSemis)
            {
                Processor q (Pid::kt2);
                auto s = std::make_shared<LoopSample>();
                s->rate = 48000.0; s->name = "sine"; s->buf.setSize (2, 48000);
                for (int k = 0; k < 48000; ++k) { const float v = 0.5f * std::sin (kTwoPi * 440.0f * (float) k / 48000.0f); s->buf.setSample (0, k, v); s->buf.setSample (1, k, v); }
                q.setSample (s);
                q.setParam ("fit", 0.0f); q.setParam ("slices", 0.0f); q.setParam ("tone", 1.0f); q.setParam ("spread", 0.0f); q.setParam ("revprob", 0.0f);
                q.setParam ("pitch1", stepSemis);
                q.applyPatternText ("sl 0", 0);
                double r3; auto o = render (q, nullptr, 0.5, r3);
                const int N = 8192; juce::dsp::FFT fft (13); std::vector<float> x ((size_t) N * 2, 0.0f);
                for (int k = 0; k < N; ++k) x[(size_t) k] = o.getSample (0, 200 + k) * (0.5f - 0.5f * std::cos (kTwoPi * k / (N - 1)));
                fft.performFrequencyOnlyForwardTransform (x.data(), true);
                int best = 1; for (int k = 2; k < N / 2; ++k) if (x[(size_t) k] > x[(size_t) best]) best = k;
                return best * 48000.0 / N;
            };
            const double f0 = peakHz (0.0f), f12 = peakHz (12.0f), f7 = peakHz (-5.0f);
            std::cout << "  step pitch: 0 st -> " << f0 << " Hz, +12 st -> " << f12 << " Hz, -5 st -> " << f7 << " Hz\n";
            check (std::abs (f0 - 440.0) < 12.0 && std::abs (f12 - 880.0) < 20.0 && std::abs (f7 - 329.6) < 12.0, "per-step pitch changes the pitch");

            // length: with len 4 only slices 0-3 of a straight pattern ever play; ratchet 4 on step 1 gives four hits a bar
            {
                Processor q (Pid::kt2);
                q.applyPatternText ("sl 0123456789abcdef", 0);
                q.setParam ("len", 4.0f);
                double r3; render (q, nullptr, 4.0, r3);
                bool lo = true, hi = true;
                for (int k = 0; k < 4; ++k) lo = lo && q.hitCount[(size_t) k].load() > 0;
                for (int k = 4; k < 16; ++k) hi = hi && q.hitCount[(size_t) k].load() == 0;
                check (lo && hi, "length 4 truncates the loop");
            }
            {
                Processor q (Pid::kt2);
                q.applyPatternText ("sl 0\nrt 4\ncv 5", 0);
                q.setParam ("len", 16.0f);
                double r3; render (q, nullptr, 60.0 / 124.0 * 4.0, r3);   // one bar at the fake head's 124 bpm
                const int n = q.hitCount[0].load();
                check (n == 4, "ratchet 4 with a curve: four hits a bar (got " + juce::String (n) + ")");
            }
            // the pattern text keeps ratchets and curves; the state keeps the curve
            {
                Processor q (Pid::kt2);
                q.applyPatternText ("sl 0123\nrt 1234\ncv 07a0", 0);
                const auto& st = q.loopBank.p[0].t[0];
                check (st[3].ratchet == 4 && st[1].curve == 7 && st[2].curve == -1, "rt / cv pattern lines");
                juce::MemoryBlock mb2; q.getStateInformation (mb2);
                Processor q2 (Pid::kt2); q2.setStateInformation (mb2.getData(), (int) mb2.getSize());
                check (q2.loopBank.toString() == q.loopBank.toString() && q2.loopBank.p[0].t[0][1].curve == 7, "loop bank round trip with curves");
            }
        }

        // state round trip
        proc->loadProgram (proc->getNumPrograms() > 1 ? 1 : 0);
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        Processor other ((Pid) i);
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int k = 0; k < proc->getParameters().size(); ++k)
            same &= std::abs (proc->getParameters()[k]->getValue() - other.getParameters()[k]->getValue()) < 1.0e-5f;
        check (same, "state round trip");
        if (proc->getNumPrograms() > 1)
        {
            // REAPER's VST2 restore: the chunk, then "select program n" for the program the chunk was saved on.
            // The restored values must survive that; choosing another program afterwards still loads it
            Processor host ((Pid) i);
            auto* some = dynamic_cast<juce::RangedAudioParameter*> (proc->getParameters()[proc->getParameters().size() - 1]);
            const float before = some->getValue();
            some->setValueNotifyingHost (before > 0.5f ? 0.0f : 1.0f);
            juce::MemoryBlock edited; proc->getStateInformation (edited);
            host.setStateInformation (edited.getData(), (int) edited.getSize());
            host.setCurrentProgram (host.getCurrentProgram());
            bool kept = true;
            for (int k = 0; k < proc->getParameters().size(); ++k)
            {
                const float a = proc->getParameters()[k]->getValue(), b = host.getParameters()[k]->getValue();
                if (std::abs (a - b) >= 1.0e-5f) { kept = false; std::cout << "    " << proc->getParameters()[k]->getName (40) << ": " << a << " -> " << b << "\n"; }
            }
            check (kept, "state survives the host selecting its program after restoring (vst2)");
            {
                // REAPER keeping the instance on a VST2 reload sends only "select program n": a plug-in that opened on
                // its defaults (reporting program 0) must not take preset 0's values from that
                Processor fresh ((Pid) i);
                std::vector<float> was;
                for (auto* prm : fresh.getParameters()) was.push_back (prm->getValue());
                fresh.setCurrentProgram (fresh.getCurrentProgram());
                bool same0 = true;
                for (size_t k = 0; k < was.size(); ++k) same0 &= std::abs (was[k] - fresh.getParameters()[(int) k]->getValue()) < 1.0e-6f;
                check (same0, "re-selecting the current program from the host changes nothing");
            }
            host.setCurrentProgram (0);
            check (host.getCurrentProgram() == 0, "another program still loads after a restore");
            some->setValueNotifyingHost (before);
        }
        if (pi.id == Pid::kt1) check (proc->kitBank.toString() == other.kitBank.toString(), "kit bank round trip");
        if (pi.id == Pid::kt2) check (other.getSample() != nullptr && other.getSample()->buf.getNumSamples() > 1000, "sample round trip");

        if (shotDir != juce::File())
        {
            proc->loadProgram (0);
            // "dense experimental", pushed into the limiter, with audio in the scopes and the meters (stopped just after a kick)
            if (pi.id == Pid::ms0) { proc->loadProgram (2); proc->setParam ("limgain", 9.0f); double r4; render (*proc, &loop, 3.2, r4); }
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
            ed->setSize (pi.w, pi.h);
            if (auto* fe = dynamic_cast<Editor*> (ed.get())) for (int t = 0; t < 12; ++t) fe->tickForTest();
            if (pi.id == Pid::wb1)
                if (auto* fe = dynamic_cast<Editor*> (ed.get()))
                {
                    // second picture: the modulation page, after a held note so the scopes have moved
                    proc->loadProgram (12);   // "neuro movement"
                    double r5; render (*proc, nullptr, 2.0, r5);
                    fe->pageForTest (1);
                    for (int t = 0; t < 200; ++t) { juce::MidiBuffer mb; juce::AudioBuffer<float> blk (2, 512); blk.clear(); if (t == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 110), 0); proc->processBlock (blk, mb); fe->tickForTest(); }
                    juce::Image im2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                    const auto f2 = shotDir.getChildFile ("wb1-mod.png"); f2.deleteFile();
                    juce::FileOutputStream os2 (f2); juce::PNGImageFormat png2; png2.writeImageToStream (im2, os2);
                    fe->pageForTest (0);
                }
            if (pi.id == Pid::kt2)
                if (auto* fe = dynamic_cast<Editor*> (ed.get()))
                {
                    // second picture: the step features, on the "pitched roll" preset (pitches, a curved ratchet, a clock curve)
                    proc->loadProgram (proc->getNumPrograms() - 1);
                    for (int t = 0; t < 6; ++t) fe->tickForTest();
                    juce::Image im2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                    const auto f2 = shotDir.getChildFile ("kt2-steps.png"); f2.deleteFile();
                    juce::FileOutputStream os2 (f2); juce::PNGImageFormat png2; png2.writeImageToStream (im2, os2);
                    proc->loadProgram (0);
                    for (int t = 0; t < 6; ++t) fe->tickForTest();
                }
            if (pi.id == Pid::ms0)
                if (auto* fe = dynamic_cast<Editor*> (ed.get()))
                {
                    // more pictures: the mouse on dynamic band 1 (its read-out), and its frequency being typed
                    for (int pg = 1; pg <= 2; ++pg)
                    {
                        fe->pageForTest (pg);
                        juce::Image im2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                        const auto f2 = shotDir.getChildFile (pg == 1 ? "ms0-dyn.png" : "ms0-type.png"); f2.deleteFile();
                        juce::FileOutputStream os2 (f2); juce::PNGImageFormat png2; png2.writeImageToStream (im2, os2);
                    }
                    fe->pageForTest (0);
                }
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
