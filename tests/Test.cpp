// faltungTest — renders every product offline and checks it.
//   faltungTest [--wav dir] [--screenshots dir] [--only kt1]
#include "Processor.h"
#include "Editor.h"
#include "MirrorTree.h"
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
        if (juce::String (argv[i]) == "--tree" && i + 2 < argc)
        {
            // faltungDeepTest --tree out.png <product index> [w h] : a still of the mirrored tree, for the manual
            const int idx = juce::String (argv[i + 2]).getIntValue();
            const int w = i + 4 < argc ? juce::String (argv[i + 3]).getIntValue() : 1600, h = i + 4 < argc ? juce::String (argv[i + 4]).getIntValue() : 1000;
            MirrorTree t; t.build ((uint32_t) (idx * 7919 + 17), w, h, 5, 10); t.setColours (juce::Colour (info (idx).ground), juce::Colour (info (idx).accent));
            juce::Image im (juce::Image::RGB, w, h, true);
            { juce::Graphics g (im); g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); g.drawImage (t.render (3.0f, 0.35f, 0.0f), juce::Rectangle<float> (0, 0, (float) w, (float) h)); }
            juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1])); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
            return 0;
        }
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
                        w.setCurrentProgram (pr);
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
            m.setCurrentProgram (juce::String (argv[i + 2]).getIntValue());
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
            proc->setCurrentProgram (pr);
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
            if (pi.id == Pid::wb1)
                if (auto* fe = dynamic_cast<Editor*> (ed.get()))
                {
                    // second picture: the modulation page, after a held note so the scopes have moved
                    proc->setCurrentProgram (12);   // "neuro movement"
                    double r5; render (*proc, nullptr, 2.0, r5);
                    fe->pageForTest (1);
                    for (int t = 0; t < 200; ++t) { juce::MidiBuffer mb; juce::AudioBuffer<float> blk (2, 512); blk.clear(); if (t == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 110), 0); proc->processBlock (blk, mb); fe->tickForTest(); }
                    juce::Image im2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                    const auto f2 = shotDir.getChildFile ("wb1-mod.png"); f2.deleteFile();
                    juce::FileOutputStream os2 (f2); juce::PNGImageFormat png2; png2.writeImageToStream (im2, os2);
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
