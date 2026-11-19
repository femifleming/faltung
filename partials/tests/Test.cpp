// Offline checks for the Partials engine: pitch, level, stability under random settings, speed, resynthesis, state.
#include "../Source/PluginProcessor.h"
using namespace pt;

static const double SR = 48000.0;
struct Rig
{
    // the processor lives on the heap: the checks below make dozens of these as locals of main()
    std::unique_ptr<PartialsProcessor> owner { new PartialsProcessor() };
    PartialsProcessor& p = *owner;
    Rig() { p.setPlayConfigDetails (0, 2, SR, 512); p.prepareToPlay (SR, 512); }
    void set (int gpIndex, float v) { auto* q = p.globalParam (gpIndex); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
    void setP (int part, int ppIndex, float v) { auto* q = p.partParam (part, ppIndex); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
    // notes: {note, onSec, offSec}
    juce::AudioBuffer<float> render (std::vector<std::array<float, 3>> notes, float secs, double* cpu = nullptr, std::function<void (float)> hook = nullptr)
    {
        const int total = (int) (secs * SR);
        juce::AudioBuffer<float> out (2, total), blk (2, 512);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += 512)
        {
            const int n = juce::jmin (512, total - pos);
            if (hook) hook ((float) pos / (float) total);
            juce::MidiBuffer mb;
            for (auto& nt : notes)
            {
                const int on = (int) (nt[1] * SR), off = (int) (nt[2] * SR);
                if (on >= pos && on < pos + n) mb.addEvent (juce::MidiMessage::noteOn (1, (int) nt[0], 0.8f), on - pos);
                if (off >= pos && off < pos + n) mb.addEvent (juce::MidiMessage::noteOff (1, (int) nt[0]), off - pos);
            }
            juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n);
            view.clear();
            p.processBlock (view, mb);
            out.copyFrom (0, pos, view, 0, 0, n); out.copyFrom (1, pos, view, 1, 0, n);
        }
        if (cpu) *cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (secs * 1000.0);
        return out;
    }
};
static float rms (const juce::AudioBuffer<float>& b, int from, int n) { return b.getRMSLevel (0, from, n); }
static bool finite (const juce::AudioBuffer<float>& b, float& peak)
{
    peak = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) return false; peak = juce::jmax (peak, std::abs (v)); }
    return true;
}
// spectrum of a stretch of the left channel: returns magnitude per bin (Hann, 32768)
static std::vector<float> spectrum (const juce::AudioBuffer<float>& b, int from)
{
    const int order = 15, W = 1 << order;
    std::vector<float> x ((size_t) W * 2, 0.0f);
    for (int i = 0; i < W && from + i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample (0, from + i) * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) W));
    juce::dsp::FFT f (order); f.performFrequencyOnlyForwardTransform (x.data());
    x.resize ((size_t) W / 2);
    for (auto& v : x) v *= 4.0f / (float) W;
    return x;
}
static float peakHz (const std::vector<float>& s, float lo, float hi, float* mag = nullptr)
{
    const float binHz = (float) SR / 32768.0f;
    int best = 1; for (int k = (int) (lo / binHz); k < (int) (hi / binHz) && k < (int) s.size() - 1; ++k) if (s[(size_t) k] > s[(size_t) best]) best = k;
    const float a = s[(size_t) best - 1], b = s[(size_t) best], c = s[(size_t) best + 1];
    if (mag) *mag = std::sqrt ((a * a + b * b + c * c + s[(size_t) best - 2] * s[(size_t) best - 2] + s[(size_t) best + 2] * s[(size_t) best + 2]) / 1.5f);
    return ((float) best + 0.5f * (a - c) / (a - 2 * b + c + 1.0e-20f)) * binHz;
}
static void writeWav (const juce::AudioBuffer<float>& b, const juce::String& name)
{
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), SR, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

//==================================================================================================
// pictures for the manual: what the output looks like while a control is moved
static void savePng (const juce::Image& im, const juce::File& f) { f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os); }
static void specPng (const juce::AudioBuffer<float>& b, const juce::File& f, int W = 560, int H = 220)
{
    const int total = b.getNumSamples(), order = total < (int) SR ? 11 : 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    juce::Image im (juce::Image::RGB, W, H, true);
    juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
    std::vector<float> x ((size_t) N * 2);
    const juce::Colour bg (0xff121214), fg (0xffe9e7e1);
    const float lo = std::log2 (40.0f), hi = std::log2 (20000.0f);
    for (int px = 0; px < W; ++px)
    {
        const int centre = (int) ((float) px / (float) W * (float) total);
        for (int i = 0; i < N; ++i)
        {
            const int sidx = centre - N / 2 + i;
            const float v = (sidx >= 0 && sidx < total) ? 0.5f * (b.getSample (0, sidx) + b.getSample (1, sidx)) : 0.0f;
            x[(size_t) i] = v * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N));
        }
        std::fill (x.begin() + N, x.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (x.data());
        for (int py = 0; py < H; ++py)
        {
            const float hz = std::exp2 (lo + (hi - lo) * (1.0f - (float) py / (float) H));
            const float k = hz * (float) N / (float) SR; const int k0 = juce::jlimit (1, N / 2 - 2, (int) k);
            const float m = (x[(size_t) k0] + (x[(size_t) k0 + 1] - x[(size_t) k0]) * (k - (float) k0)) * 4.0f / (float) N * std::sqrt (hz / 110.0f);      // tilted so the top end shows
            const float br = clamp01 ((20.0f * std::log10 (m + 1.0e-9f) + 66.0f) / 50.0f);
            bd.setPixelColour (px, py, bg.interpolatedWith (fg, br * br));
        }
    }
    savePng (im, f);
}
static void scopePng (const juce::AudioBuffer<float>& b, const juce::File& f, int start, int n, int W = 260, int H = 120)
{
    juce::Image im (juce::Image::RGB, W, H, true);
    juce::Graphics g (im); g.fillAll (juce::Colour (0xff121214));
    g.setColour (juce::Colour (0xff2b2b2e)); g.fillRect (0, H / 2, W, 1);
    float mx = 1.0e-6f; for (int i = 0; i < n; ++i) mx = juce::jmax (mx, std::abs (b.getSample (0, start + i)));
    juce::Path p;
    for (int px = 0; px < W; ++px)
    {
        const float fp = (float) px / (float) W * (float) (n - 1); const int i0 = (int) fp;
        const float v = b.getSample (0, start + i0) + (b.getSample (0, start + juce::jmin (n - 1, i0 + 1)) - b.getSample (0, start + i0)) * (fp - (float) i0);
        const float y = (float) H * 0.5f - v / mx * ((float) H * 0.5f - 8.0f);
        if (px == 0) p.startNewSubPath (0, y); else p.lineTo ((float) px, y);
    }
    g.setColour (juce::Colour (0xffe9e7e1)); g.strokePath (p, juce::PathStrokeType (1.6f));
    savePng (im, f);
}
static void setSine (Rig& r) { auto& d = r.p.edit (0); d.shape[SH::Timbre1].pts.clear(); d.shape[SH::Timbre1].raw.assign ((size_t) NH, 0.0f); d.shape[SH::Timbre1].raw[0] = 0.5f; r.p.commit (0); }

static void makeFigures (const juce::File& dir)
{
    dir.createDirectory();
    using Notes = std::vector<std::array<float, 3>>;
    const Notes one { { 45, 0.05f, 2.4f } };
    auto fig = [&] (const char* name, std::function<void (Rig&)> setup, std::function<void (Rig&, float)> sweep = nullptr, Notes notes = {}, float secs = 2.5f)
    {
        Rig r; r.setP (0, PP::velVol, 0);
        if (setup) setup (r);
        if (notes.empty()) notes = { { 45, 0.05f, secs - 0.1f } };
        auto b = r.render (notes, secs, nullptr, sweep ? std::function<void (float)> ([&] (float t) { sweep (r, t); }) : nullptr);
        specPng (b, dir.getChildFile (juce::String (name) + ".png"));
        std::cout << "  " << name << std::endl;
    };
    auto scope = [&] (const char* name, std::function<void (Rig&)> setup)
    {
        Rig r; r.setP (0, PP::velVol, 0); r.setP (0, PP::declick, 0); r.setP (0, PP::ramp, 0);
        if (setup) setup (r);
        auto b = r.render ({ { 45, 0.0f, 1.0f } }, 0.6f);
        const int per = (int) (SR / 110.0);
        scopePng (b, dir.getChildFile (juce::String (name) + ".png"), 9600, per * 3);
    };
    #define SP(id, v) r.setP (0, PP::id, (float) (v))
    #define SG(id, v) r.set (GP::id, (float) (v))
    // timbre
    scope ("w_saw", nullptr);
    scope ("w_square", [] (Rig& r) { SP (timbreMix, 1); });
    scope ("w_phase_rand", [] (Rig& r) { SP (phaseRand, -1); });
    fig ("timbre_mix", nullptr, [] (Rig& r, float t) { SP (timbreMix, t); });
    fig ("subs_around", nullptr, [] (Rig& r, float t) { SP (sub1, t > 0.25f); SP (sub2, t > 0.5f); SP (sub3, t > 0.75f); }, { { 57, 0.05f, 2.4f } });
    fig ("subs_below", [] (Rig& r) { SP (subMode, 1); }, [] (Rig& r, float t) { SP (sub1, t > 0.25f); SP (sub2, t > 0.5f); SP (sub3, t > 0.75f); }, { { 69, 0.05f, 2.4f } });
    fig ("clip_high", nullptr, [] (Rig& r, float t) { SP (clip, 1.0f - 0.9f * t); });
    fig ("clip_low", [] (Rig& r) { SP (clipMode, 1); }, [] (Rig& r, float t) { SP (clip, 1.0f - t); });
    fig ("prot", [] (Rig& r) { SP (f1Type, 5); SP (f1Freq, 0.6f); }, [] (Rig& r, float t) { SP (prot, t); });
    // pitch
    fig ("detune", nullptr, [] (Rig& r, float t) { SP (detMul, 1.0f + t); });
    fig ("octmode", [] (Rig& r) { SP (pitchMode, 1); }, [] (Rig& r, float t) { SP (detMul, 1.0f + 5.0f * t); });
    fig ("vibrato", [] (Rig& r) { SP (vibSpeed, 4); }, [] (Rig& r, float t) { SP (vibDepth, 100.0f * t); }, { { 69, 0.05f, 2.4f } });
    fig ("porta", [] (Rig& r) { SP (porta, 1); SP (glideTime, -0.5f); }, nullptr, { { 45, 0.05f, 0.7f }, { 57, 0.8f, 1.5f }, { 50, 1.6f, 2.4f } });
    fig ("legato", [] (Rig& r) { SP (legato, 1); SP (glideTime, -0.4f); }, nullptr, { { 45, 0.05f, 2.4f }, { 57, 0.7f, 1.3f }, { 52, 1.6f, 2.0f } });
    fig ("strum", [] (Rig& r) { SG (strumTime, 0.95f); }, nullptr, { { 45, 0.2f, 2.3f }, { 52, 0.2f, 2.3f }, { 57, 0.2f, 2.3f }, { 61, 0.2f, 2.3f }, { 64, 0.2f, 2.3f } });
    // unison
    fig ("unison", [] (Rig& r) { SP (uniOrder, 7); }, [] (Rig& r, float t) { SP (uniPitch, t); }, { { 69, 0.05f, 2.4f } });
    // filters
    const char* ftn[] = { "filt_crude_lp", "filt_classic_lp", "filt_hp", "filt_bp", "filt_stop", "filt_phaser" }; const int fti[] = { 0, 1, 5, 9, 12, 14 };
    for (int i = 0; i < 6; ++i) { const int ty = fti[i]; fig (ftn[i], [ty] (Rig& r) { SP (f1Type, ty); }, [] (Rig& r, float t) { SP (f1Freq, 0.95f - 0.75f * t); }); }
    fig ("filt_width", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.55f); }, [] (Rig& r, float t) { SP (f1Width, 1.0f - t); });
    const char* rtn[] = { "res_lowpass", "res_classic", "res_cuberdon", "res_pedestal", "res_sedge", "res_wide", "res_double", "res_well", "res_wormhole", "res_noise" }; const int rti[] = { 11, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    for (int i = 0; i < 10; ++i) { const int ty = rti[i]; fig (rtn[i], [ty] (Rig& r) { SP (f1Type, 1); SP (f1Res, 0.8f); SP (f1ResType, ty); }, [] (Rig& r, float t) { SP (f1Freq, 0.9f - 0.6f * t); }); }
    fig ("res_amount", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.55f); SP (f1ResType, 0); }, [] (Rig& r, float t) { SP (f1Res, t); });
    fig ("res_offset", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.5f); SP (f1Res, 0.9f); SP (f1ResType, 0); }, [] (Rig& r, float t) { SP (f1ResOfs, -2400.0f + 4800.0f * t); });
    fig ("res_osc", [] (Rig& r) { SP (f1Type, 1); SP (f1ResOsc, 1); }, [] (Rig& r, float t) { SP (f1Freq, 0.9f - 0.6f * t); });
    fig ("filt_kb", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.5f); SP (f1Kb, 1); SP (f1Res, 0.5f); }, nullptr, { { 33, 0.05f, 0.6f }, { 45, 0.65f, 1.2f }, { 57, 1.25f, 1.8f }, { 69, 1.85f, 2.4f } });
    // blur, pluck
    fig ("blur_harm", [] (Rig& r) { SP (timbreMix, 1); }, [] (Rig& r, float t) { SP (blurHarm, t); });
    fig ("blur_time", [] (Rig& r) { SP (phMix, 1); SP (phSpeed, 0.75f); SP (phType, 8); }, [] (Rig& r, float t) { SP (blurAttack, t > 0.4f ? 0.25f : 0.0f); SP (blurDecay, t > 0.4f ? 0.35f : 0.0f); });
    fig ("pluck", [] (Rig& r) { SP (pluckTime, 0.45f); }, nullptr, { { 45, 0.05f, 0.8f }, { 52, 0.85f, 1.6f }, { 57, 1.65f, 2.4f } });
    fig ("pluck_time", nullptr, [] (Rig& r, float t) { SP (pluckTime, t < 0.33f ? 0.2f : (t < 0.66f ? 0.45f : 0.7f)); }, { { 45, 0.05f, 0.8f }, { 45, 0.85f, 1.6f }, { 45, 1.68f, 2.4f } });
    // phaser
    const char* ptn[] = { "ph_classic", "ph_triangle", "ph_eggs", "ph_deep", "ph_deeper", "ph_condom", "ph_twins", "ph_cascade", "ph_box" };
    for (int i = 0; i < 9; ++i) fig (ptn[i], [i] (Rig& r) { SP (phType, i); SP (phMix, 1); SP (phSpeed, 0.72f); });
    fig ("ph_width", [] (Rig& r) { SP (phMix, 1); SP (phSpeed, 0); }, [] (Rig& r, float t) { SP (phWidth, t); });
    fig ("ph_offset", [] (Rig& r) { SP (phMix, 1); SP (phSpeed, 0); }, [] (Rig& r, float t) { SP (phOfs, t); });
    // prism, harmonizer
    fig ("prism_add", nullptr, [] (Rig& r, float t) { SP (prismAmt, -1.0f + 2.0f * t); });
    fig ("prism_mul", [] (Rig& r) { SP (prismMode, 1); }, [] (Rig& r, float t) { SP (prismAmt, -1.0f + 2.0f * t); });
    fig ("hz_shift_plus", [] (Rig& r) { SP (hzMix, 1); SP (hzWidth, 1); }, [] (Rig& r, float t) { SP (hzShiftStep, 1 + juce::jmin (7, (int) (t * 8.0f))); });
    fig ("hz_shift_x", [] (Rig& r) { SP (hzMix, 1); SP (hzWidth, 1); SP (hzShiftMul, 1); }, [] (Rig& r, float t) { SP (hzShiftStep, 1 + juce::jmin (7, (int) (t * 8.0f))); });
    fig ("hz_gap", [] (Rig& r) { SP (hzMix, 1); SP (hzWidth, 1); SP (hzGapMul, 0); SP (hzShiftStep, 2); }, [] (Rig& r, float t) { SP (hzGapStep, 1 + juce::jmin (7, (int) (t * 8.0f))); });
    // articulation
    fig ("env_volume", [] (Rig& r) { auto& e = r.p.edit (0).tg[TG::Volume].env; e.on = true; e.adsr (0.4f, 0.5f, 0.4f, 0.6f); r.p.commit (0); }, nullptr, { { 45, 0.05f, 1.6f } });
    fig ("env_filter", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 1.0f); SP (f1Env, 0.75f); SP (f1Res, 0.5f); auto& e = r.p.edit (0).tg[TG::F1Freq].env; e.on = true; e.adsr (0.0f, 0.6f, 0.0f, 0.3f); r.p.commit (0); }, nullptr, { { 45, 0.05f, 1.15f }, { 45, 1.25f, 2.4f } });
    fig ("lfo_filter", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.5f); SP (f1Res, 0.5f); auto& l = r.p.edit (0).tg[TG::F1Freq].lfo; l.on = true; l.amt = 0.25f; l.speed = 0.5f; r.p.commit (0); });
    fig ("lfo_pitch", [] (Rig& r) { auto& l = r.p.edit (0).tg[TG::Pitch].lfo; l.on = true; l.amt = 0.12f; l.speed = 0.45f; l.tension = 1.0f; r.p.commit (0); }, nullptr, { { 69, 0.05f, 2.4f } });
    fig ("map_key", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.5f); auto& t = r.p.edit (0).tg[TG::F1Freq]; t.mapOn[MAP::Key] = true; t.map[MAP::Key].line ({ { 0, 0.1f }, { 1, 0.9f } }); r.p.commit (0); }, nullptr, { { 33, 0.05f, 0.6f }, { 45, 0.65f, 1.2f }, { 57, 1.25f, 1.8f }, { 69, 1.85f, 2.4f } });
    fig ("arp", [] (Rig& r)
    {
        auto& e = r.p.edit (0).tg[TG::Volume].env; e.c.pts.clear(); e.tempo = true; e.on = true;
        for (int i = 0; i < 8; ++i) { e.c.pts.push_back ({ (float) i * 0.25f, 1.0f, 0, Hold, 3 }); e.c.pts.push_back ({ (float) i * 0.25f + 0.2f, 0.25f, -0.5f, Single, 0 }); }
        e.c.pts.push_back ({ 2.0f, 0.0f, 0, Single, 0 }); e.loopStart = 0; e.sustain = (int) e.c.pts.size() - 1; r.p.commit (0);
    }, nullptr, { { 45, 0.05f, 2.4f }, { 52, 0.05f, 2.4f }, { 57, 0.05f, 2.4f }, { 64, 0.05f, 2.4f } });
    fig ("eq", [] (Rig& r) { r.p.edit (0).shape[SH::GlobalEQ].line ({ { 0, 0.5f }, { 0.42f, 0.5f }, { 0.5f, 0.02f }, { 0.58f, 0.5f }, { 0.7f, 0.5f }, { 0.76f, 0.95f }, { 0.82f, 0.5f }, { 1, 0.5f } }); r.p.commit (0); }, [] (Rig& r, float t) { SP (eqAmt, t); });
    fig ("ramp", [] (Rig& r) { SP (ramp, 1.0f); }, nullptr, { { 45, 0.05f, 0.55f } }, 0.6f);
    fig ("ramp_abs", [] (Rig& r) { SP (ramp, -1.0f); }, nullptr, { { 45, 0.05f, 0.55f } }, 0.6f);
    // image
    fig ("img_cloud", [] (Rig& r) { r.p.setImage (0, ImageData::randomCloud (3)); SP (imgSpeed, 2); });
    fig ("img_formant", [] (Rig& r) { r.p.setImage (0, ImageData::randomCloud (3)); SP (imgSpeed, 0); SP (imgTime, 0.45f); }, [] (Rig& r, float t) { SP (imgFormant, -600.0f + 1200.0f * t); });
    fig ("img_speed", [] (Rig& r) { r.p.setImage (0, ImageData::randomCloud (3)); SP (imgLoop, 2); }, [] (Rig& r, float t) { SP (imgSpeed, t < 0.5f ? 2.0f : -2.0f); });
    { savePng (ImageData::randomCloud (3)->toPicture (0).rescaled (560, 220), dir.getChildFile ("img_source.png")); }
    // effects
    fig ("fx_chorus", [] (Rig& r) { SG (chOrder, 5); SG (chDepth, 0.8f); }, nullptr, { { 69, 0.05f, 2.4f } });
    fig ("fx_delay", [] (Rig& r) { SG (dlOn, 1); SG (dlFb, 0.7f); SG (dlTime, 0.25f); }, nullptr, { { 57, 0.05f, 0.2f } });
    fig ("fx_delay_damp", [] (Rig& r) { SG (dlOn, 1); SG (dlFb, 0.7f); SG (dlTime, 0.25f); SG (dlDamp, 1); SG (dlHiCut, 0.6f); }, nullptr, { { 57, 0.05f, 0.2f } });
    fig ("fx_reverb", [] (Rig& r) { SG (rvOn, 1); SG (rvDecay, 0.5f); }, nullptr, { { 57, 0.05f, 0.3f } });
    fig ("fx_reverb_warm", [] (Rig& r) { SG (rvOn, 1); SG (rvDecay, 0.5f); SG (rvColor, 4); }, nullptr, { { 57, 0.05f, 0.3f } });
    fig ("fx_reverb_bright", [] (Rig& r) { SG (rvOn, 1); SG (rvDecay, 0.5f); SG (rvColor, 0); }, nullptr, { { 57, 0.05f, 0.3f } });
    const char* dn[] = { "off", "classic", "hill", "flathat", "cube", "log", "softsat", "rubber", "ribbon", "sincrush", "bitcrush", "bitjam" };
    for (int i = 0; i < 12; ++i) scope ((juce::String ("dist_") + dn[i]).toRawUTF8(), [i] (Rig& r) { setSine (r); SG (distType, i); SG (distAmt, 0.6f); SG (distHiCut, 1); });
    #undef SP
    #undef SG
}

static int fails = 0;
static void check (bool ok, const juce::String& what) { std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl; if (! ok) ++fails; }


//==================================================================================================
// measuring tools for the articulation checks
static float bandLevel (const juce::AudioBuffer<float>& b, int ch, int from, int order, float lo, float hi)
{
    const int W = 1 << order;
    std::vector<float> x ((size_t) W * 2, 0.0f);
    for (int i = 0; i < W && from + i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample (ch, from + i) * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) W));
    juce::dsp::FFT f (order); f.performFrequencyOnlyForwardTransform (x.data());
    double e = 0; const float binHz = (float) SR / (float) W;
    for (int k = juce::jmax (1, (int) (lo / binHz)); k < W / 2 && (float) k * binHz < hi; ++k) e += (double) x[(size_t) k] * x[(size_t) k];
    return (float) std::sqrt (e) * 4.0f / (float) W;
}
// frequency of a single sine over a stretch, from its zero crossings
static float zcHz (const juce::AudioBuffer<float>& b, int from, int n)
{
    int first = -1, last = -1, count = 0;
    for (int i = from + 1; i < from + n && i < b.getNumSamples(); ++i)
        if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f) { if (first < 0) first = i; last = i; ++count; }
    return count > 1 ? (float) (count - 1) * (float) SR / (float) (last - first) : 0.0f;
}
// how strongly the level of a sound wobbles at a given rate (0..1), from 5 ms level readings
static float wobbleAt (const juce::AudioBuffer<float>& b, int from, int n, float hz)
{
    const int hop = 240; const int frames = n / hop;
    std::vector<float> lv ((size_t) frames); double mean = 0;
    for (int i = 0; i < frames; ++i) { lv[(size_t) i] = b.getRMSLevel (0, from + i * hop, hop); mean += lv[(size_t) i]; }
    mean /= juce::jmax (1, frames);
    double re = 0, im = 0;
    for (int i = 0; i < frames; ++i) { const double ph = 6.283185307 * hz * (double) (i * hop) / SR; re += (lv[(size_t) i] - mean) * std::cos (ph); im += (lv[(size_t) i] - mean) * std::sin (ph); }
    return mean > 1.0e-9 ? (float) (2.0 * std::sqrt (re * re + im * im) / frames / mean) : 0.0f;
}
// the largest jump between neighbouring samples, against the peak: a click shows up as a jump no sine could make
static float worstJump (const juce::AudioBuffer<float>& b)
{
    float j = 0, pk = 1.0e-9f;
    for (int i = 1; i < b.getNumSamples(); ++i) { j = juce::jmax (j, std::abs (b.getSample (0, i) - b.getSample (0, i - 1))); pk = juce::jmax (pk, std::abs (b.getSample (0, i))); }
    return j / pk;
}
static float lfoSpeedFor (float hz) { return std::log (hz / 0.05f) / std::log (800.0f); }

//==================================================================================================
// Articulation matrix: every target x every articulator part. The same patch is rendered with the part off and on;
// the two renders have to differ (the engine is deterministic, so any difference is the articulator reaching the sound).
static float relDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double d = 0, e = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) { const double x = a.getSample (c, i), y = b.getSample (c, i); d += (x - y) * (x - y); e += x * x + y * y; }
    return e > 1.0e-12 ? (float) std::sqrt (d / e) : 0.0f;
}
static std::shared_ptr<ImageData> probeImage()
{
    auto d = ImageData::randomCloud (7)->clone();
    for (int x = 0; x < d->w; ++x) for (int y = 0; y < d->h; ++y) d->freq[(size_t) (x * d->h + y)] = 0.6f * std::sin ((float) x * 0.11f + (float) y * 0.3f);
    return d;
}
// makes the target's own control audible, so that an articulator has something to move
static void probePatch (Rig& r, int t)
{
    auto P = [&r] (int id, float v) { r.setP (0, id, v); };
    r.setP (0, PP::velVol, 0);
    switch (t)
    {
        case TG::FxMix: P (PP::fxMix, 1.0f); r.set (GP::rvOn, 1); r.set (GP::rvWet, 1.0f); break;
        case TG::TimbreMix: P (PP::timbreMix, 1.0f); break;
        case TG::F1Freq: case TG::F1Width: case TG::F1Env: P (PP::f1Type, 1); P (PP::f1Freq, 0.5f); P (PP::f1Res, 0.3f); break;
        case TG::F2Freq: case TG::F2Width: case TG::F2Env: P (PP::f2Type, 1); P (PP::f2Freq, 0.5f); P (PP::f2Res, 0.3f); break;
        case TG::F1Res: case TG::F1ResWidth: case TG::F1ResOfs: P (PP::f1Type, 1); P (PP::f1Freq, 0.5f); P (PP::f1Res, 0.8f); break;
        case TG::F2Res: case TG::F2ResWidth: case TG::F2ResOfs: P (PP::f2Type, 1); P (PP::f2Freq, 0.5f); P (PP::f2Res, 0.8f); break;
        case TG::FiltMix: P (PP::f1Type, 1); P (PP::f1Freq, 0.4f); P (PP::f2Type, 5); P (PP::f2Freq, 0.6f); break;
        case TG::PluckAmt: P (PP::pluckTime, 0.5f); break;
        case TG::PhMix: case TG::PhWidth: case TG::PhOfs: P (PP::phMix, 0.8f); P (PP::phSpeed, 0.0f); break;
        case TG::HzMix: case TG::HzWidth: P (PP::hzMix, 0.8f); P (PP::hzShiftStep, 3); break;
        case TG::Clip: P (PP::clip, 0.5f); break;
        case TG::BlurHarm: P (PP::timbreMix, 1.0f); P (PP::blurHarm, 0.8f); break;
        case TG::PrismAmt: P (PP::prismAmt, 0.5f); break;
        case TG::VibDepth: P (PP::vibDepth, 50.0f); break;
        case TG::UniPitch: case TG::UniPhase: P (PP::uniOrder, 3); P (PP::uniPitch, 0.5f); P (PP::uniPhase, 0.5f); break;
        case TG::ImgTime: case TG::ImgSpeed: case TG::ImgFormant: case TG::ImgFreqScale:
            r.p.edit (0).image = probeImage(); P (PP::imgLoop, 1); if (t == TG::ImgFormant) P (PP::imgFormant, 200.0f); if (t == TG::ImgTime) P (PP::imgTime, 0.3f); break;
        case TG::EnvAtkTime: case TG::EnvAtkScale: case TG::EnvDecScale: case TG::EnvSusOfs: case TG::EnvRelScale:
            r.p.edit (0).tg[TG::Volume].env.adsr (0.15f, 0.2f, 0.5f, 0.2f); break;
        case TG::LfoAmt: case TG::LfoSpeed: case TG::LfoPhase:
            { auto& l = r.p.edit (0).tg[TG::Pitch].lfo; l.on = true; l.amt = 0.02f; l.speed = 0.6f; } break;
        case TG::Predelay: P (PP::predelay, -0.3f); break;
        default: break;
    }
    if (t == TG::F1Env || t == TG::F2Env)
    {
        auto& e = r.p.edit (0).tg[t == TG::F1Env ? TG::F1Freq : TG::F2Freq].env;
        e.adsr (0.05f, 0.2f, 0.3f, 0.2f); e.on = true;
        r.setP (0, t == TG::F1Env ? PP::f1Env : PP::f2Env, -0.8f);
    }
}
static const char* const artNames[2 + MAP::Count] = { "env", "lfo", "key", "vel", "modX", "modY", "modZ", "random", "unison", "held", "xspeed" };
// art: 0 envelope, 1 LFO, 2.. mappings
static bool gProbe = true;
static float artEffect (int t, int art)
{
    juce::AudioBuffer<float> out[2];
    for (int on = 0; on < 2; ++on)
    {
        Rig r;
        if (gProbe) probePatch (r, t); else if (t >= TG::ImgTime && t <= TG::ImgFreqScale) r.p.edit (0).image = probeImage();
        if (art == 2 + MAP::Unison) { r.setP (0, PP::uniOrder, 3); r.setP (0, PP::uniPitch, 0.3f); }
        r.set (GP::modX, 0.8f); r.set (GP::modY, 0.8f); r.set (GP::modZ, 0.8f); r.set (GP::smoothMod, 0);
        auto& td = r.p.edit (0).tg[t];
        const bool sum = targetInfo[t].mode == BSum;
        if (on)
        {
            if (art == 0) { td.env.c.pts = { { 0, 0.15f, 0, Single, 0 }, { 0.25f, 1.0f, 0, Single, 0 }, { 0.5f, 0.35f, 0, Single, 0 }, { 0.7f, 0.0f, 0, Single, 0 } }; td.env.sustain = 2; td.env.on = true; }
            else if (art == 1) { td.lfo.on = true; td.lfo.speed = 0.6f; td.lfo.amt = sum ? 0.3f : 1.0f; }
            else { td.mapOn[art - 2] = true; td.map[art - 2].line ({ { 0, sum ? 0.3f : 0.1f }, { 1, sum ? 0.7f : 0.9f } }); }
        }
        r.p.commit (0);
        std::vector<std::array<float, 3>> notes = { { 57, 0.0f, 0.8f } };
        if (art == 2 + MAP::Held) notes = { { 57, 0.0f, 0.9f }, { 64, 0.2f, 0.9f }, { 69, 0.4f, 0.9f } };
        std::function<void (float)> hook;
        if (art == 2 + MAP::XSpeed) hook = [&r] (float f) { r.set (GP::modX, 0.5f + 0.5f * std::sin (f * 40.0f)); };
        out[on] = r.render (notes, 1.2f, nullptr, hook);
    }
    float pk; if (! finite (out[1], pk)) return -1.0f;
    return relDiff (out[0], out[1]);
}
static int artMatrix (bool verbose)
{
    int dead = 0, total = 0;
    for (int t = 0; t < TG::Count; ++t)
    {
        juce::String line = juce::String (targetInfo[t].name).paddedRight (' ', 32);
        for (int art = (t >= TG::FirstMapping ? 2 : 0); art < 2 + MAP::Count; ++art)
        {
            const float d = artEffect (t, art);
            ++total;
            const bool ok = d > 2.0e-3f;
            if (! ok) { ++dead; std::cout << "  FAIL  " << targetInfo[t].name << " <- " << artNames[art] << ": no change in the sound (" << juce::String (d, 5) << ")" << std::endl; }
            line << artNames[art] << ' ' << juce::String (d, 3) << "  ";
        }
        if (verbose) std::cout << "        " << line << std::endl;
    }
    std::cout << (dead == 0 ? "  ok    " : "  FAIL  ") << "articulation matrix: " << (total - dead) << " of " << total << " target x part combinations change the sound" << std::endl;
    return dead;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool wavs = argc > 1 && juce::String (argv[1]) == "--wav";
    if (argc > 2 && juce::String (argv[1]) == "--pack")
    {
        // PartialsTest --pack <dir> : one-shots of the factory sounds, 24-bit / 44.1 kHz, C2 / C3 / C4
        const double sr = 44100.0;
        juce::File dir (argv[2]); dir.createDirectory();
        struct Patch { const char* name; std::function<void (PartialsProcessor&)> set; float secs, gate; };
        auto P = [] (PartialsProcessor& p, int part, int id, float v) { p.setParam (p.partParam (part, id), v); };
        auto G = [] (PartialsProcessor& p, int id, float v) { p.setParam (p.globalParam (id), v); };
        std::vector<Patch> patches = {
            { "saw", [] (PartialsProcessor&) {}, 2.0f, 1.2f },
            { "pluck", [&] (PartialsProcessor& p) { P (p, 0, PP::pluckTime, 0.42f); P (p, 0, PP::uniOrder, 3); P (p, 0, PP::uniPitch, 0.2f);
                         auto& a = p.edit (0); a.tg[TG::Volume].env.adsr (0.001f, 1.2f, 0.0f, 0.4f); a.tg[TG::Volume].env.on = true; G (p, GP::rvOn, 1); G (p, GP::rvWet, 0.3f); }, 2.5f, 1.0f },
            { "wide pad", [&] (PartialsProcessor& p) { P (p, 0, PP::uniOrder, 7); P (p, 0, PP::uniPitch, 0.45f); P (p, 0, PP::uniPan, 0.9f); P (p, 0, PP::f1Type, 1); P (p, 0, PP::f1Freq, 0.62f);
                         P (p, 0, PP::f1Res, 0.2f); P (p, 0, PP::blurAttack, 0.3f); auto& a = p.edit (0); a.tg[TG::Volume].env.adsr (0.6f, 0.5f, 0.8f, 1.5f); a.tg[TG::Volume].env.on = true;
                         a.tg[TG::F1Freq].lfo.on = true; a.tg[TG::F1Freq].lfo.amt = 0.1f; a.tg[TG::F1Freq].lfo.speed = 0.2f; G (p, GP::rvOn, 1); G (p, GP::rvDecay, 0.6f); G (p, GP::chOrder, 4); }, 6.0f, 3.0f },
            { "phaser bass", [&] (PartialsProcessor& p) { P (p, 0, PP::freqDiv, 2); P (p, 0, PP::phMix, 0.8f); P (p, 0, PP::phSpeed, 0.5f); P (p, 0, PP::f1Type, 2); P (p, 0, PP::f1Freq, 0.95f);
                         P (p, 0, PP::f1Res, 0.5f); P (p, 0, PP::f1Env, 0.4f); P (p, 0, PP::sub1, 0.4f); auto& a = p.edit (0); a.tg[TG::F1Freq].env.adsr (0.001f, 0.25f, 0.0f, 0.2f);
                         a.tg[TG::F1Freq].env.on = true; a.tg[TG::Volume].env.on = true; G (p, GP::distType, 1); G (p, GP::distAmt, 0.3f); }, 1.5f, 0.6f },
            { "prism bell", [&] (PartialsProcessor& p) { P (p, 0, PP::timbreMix, 1); P (p, 0, PP::prismAmt, 0.55f); P (p, 0, PP::pluckTime, 0.6f); P (p, 0, PP::hzMix, 0.4f); P (p, 0, PP::hzShiftStep, 3);
                         auto& a = p.edit (0); a.tg[TG::Volume].env.adsr (0.001f, 2.5f, 0.0f, 1.5f); a.tg[TG::Volume].env.on = true; G (p, GP::rvOn, 1); G (p, GP::rvDecay, 0.55f); }, 4.5f, 2.0f },
            { "vowel", [&] (PartialsProcessor& p) { P (p, 0, PP::f1Type, 14); P (p, 0, PP::f1Freq, 0.55f); P (p, 0, PP::f1Width, 0.7f); P (p, 0, PP::f1Res, 0.5f); P (p, 0, PP::f1ResType, 3);
                         P (p, 0, PP::vibDepth, 12); P (p, 0, PP::uniOrder, 2); auto& a = p.edit (0); a.tg[TG::Volume].env.adsr (0.05f, 0.3f, 0.9f, 0.3f); a.tg[TG::Volume].env.on = true; }, 2.5f, 1.6f },
        };
        int count = 0;
        for (auto& pa : patches)
            for (int note : { 36, 48, 60 })
            {
                PartialsProcessor p;
                p.setPlayConfigDetails (0, 2, sr, 512); p.prepareToPlay (sr, 512);
                pa.set (p); p.commit (0);
                const int total = (int) (pa.secs * sr), off = (int) (pa.gate * sr);
                juce::AudioBuffer<float> out (2, total), blk (2, 512);
                for (int pos = 0; pos < total; pos += 512)
                {
                    const int n = juce::jmin (512, total - pos);
                    juce::MidiBuffer mb;
                    if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, note, 0.85f), 0);
                    if (off >= pos && off < pos + n) mb.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
                    juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n); view.clear();
                    p.processBlock (view, mb);
                    out.copyFrom (0, pos, view, 0, 0, n); out.copyFrom (1, pos, view, 1, 0, n);
                }
                // trim, fade, normalise to -1 dBFS
                int last = 0; const float thr = 0.0005f * juce::jmax (1.0e-6f, out.getMagnitude (0, total));
                for (int c = 0; c < 2; ++c) for (int k = total - 1; k > last; --k) if (std::abs (out.getSample (c, k)) > thr) { last = k; break; }
                const int len = juce::jmin (total, last + (int) (0.01 * sr));
                juce::AudioBuffer<float> o (2, juce::jmax (64, len));
                for (int c = 0; c < 2; ++c) o.copyFrom (c, 0, out, c, 0, o.getNumSamples());
                const int fade = juce::jmin (o.getNumSamples() / 4, (int) (0.01 * sr));
                for (int c = 0; c < 2; ++c) o.applyGainRamp (c, o.getNumSamples() - fade, fade, 1.0f, 0.0f);
                const float pk = o.getMagnitude (0, o.getNumSamples()); if (pk > 1.0e-6f) o.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
                const auto f = dir.getChildFile (juce::String ("faltung partials ") + pa.name + " (" + juce::MidiMessage::getMidiNoteName (note, true, true, 3) + ").wav");
                f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
                if (w) w->writeFromAudioSampleBuffer (o, 0, o.getNumSamples());
                ++count;
            }
        std::cout << count << " partials one-shots written\n";
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--state")
    {
        // PartialsTest --state <file> : the plug-in's own state for the host checks. A sound in which the pad matters:
        // X opens filter 1, Y is the level, Z is an octave of pitch; three unison voices spread left and right.
        Rig r;
        r.setP (0, PP::uniOrder, 3); r.setP (0, PP::uniPan, 0.9f); r.setP (0, PP::uniPitch, 0.4f); r.setP (0, PP::f1Type, 1); r.setP (0, PP::f1Freq, 0.3f); r.setP (0, PP::velVol, 0);
        r.set (GP::masterVol, -12.0f);
        auto& d = r.p.edit (0);
        d.tg[TG::F1Freq].mapOn[MAP::ModX] = true; d.tg[TG::F1Freq].map[MAP::ModX].line ({ { 0, 0.5f }, { 1, 1.0f } });
        d.tg[TG::Volume].mapOn[MAP::ModY] = true; d.tg[TG::Volume].map[MAP::ModY].line ({ { 0, 1.0f }, { 1, 0.25f } });
        d.tg[TG::Pitch].mapOn[MAP::ModZ] = true; d.tg[TG::Pitch].map[MAP::ModZ].line ({ { 0, 0.5f }, { 1, 0.625f } });
        d.tg[TG::Pan].lfo.on = true; d.tg[TG::Pan].lfo.speed = 0.3f; d.tg[TG::Pan].lfo.amt = 0.3f; d.tg[TG::Pan].lfo.amp.pts = { { 0.5f, 0.9f, 0, Single, 0 } };
        d.tg[TG::F2Res].env.adsr (0.1f, 0.2f, 0.4f, 0.3f); d.tg[TG::F2Res].env.decay = 1;
        r.p.commit (0);
        { auto* q = r.p.envModParam (0, TG::Volume, EM::Rel); q->setValueNotifyingHost (q->convertTo0to1 (2.0f)); }
        r.p.presetName = "Host check"; r.p.presetInfo = "x: filter, y: level, z: an octave up";
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        juce::File f (argv[2]); f.deleteFile(); f.appendData (mb.getData(), mb.getSize());
        std::cout << mb.getSize() << " bytes" << std::endl;
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--art") return artMatrix (true);
    if (argc > 1 && juce::String (argv[1]) == "--art0")
    {
        gProbe = false;      // every control at its default: which envelopes / LFOs are heard the moment they are switched on?
        for (int t = 0; t < TG::FirstMapping; ++t) std::cout << juce::String (targetInfo[t].name).paddedRight (' ', 32) << "env " << juce::String (artEffect (t, 0), 3) << "  lfo " << juce::String (artEffect (t, 1), 3) << "  modX " << juce::String (artEffect (t, 2 + MAP::ModX), 3) << std::endl;
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--figs") { makeFigures (juce::File (argv[2])); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--shot")
    {
        // picture of the window, with a note sounding so the display has something to show
        Rig r;
        const int tab = argc > 3 ? juce::String (argv[3]).getIntValue() : 0;
        if (tab == 1) r.p.setImage (0, ImageData::randomCloud (3));
        // PartialsTest --shot file.png [tab] [target] [part of the target: 0 envelope, 1 LFO, 2.. mappings] [width]
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
        const int width = argc > 6 ? juce::String (argv[6]).getIntValue() : 1857;
        ed->setSize (width, width * ed->getHeight() / juce::jmax (1, ed->getWidth()));
        const int target = argc > 4 ? juce::String (argv[4]).getIntValue() : -1, art = argc > 5 ? juce::String (argv[5]).getIntValue() : 0;
        r.set (GP::modX, 0.62f); r.set (GP::modY, 0.4f); r.set (GP::modZ, 0.3f);
        r.setP (0, PP::f1Type, 1); r.setP (0, PP::f1Res, 0.4f); r.setP (0, PP::f1Env, -0.6f);
        r.p.edit (0).tg[TG::F1Freq].env.adsr (0.0f, 1.2f, 0.0f, 0.3f); r.p.edit (0).tg[TG::F1Freq].env.on = true; r.p.commit (0);
        juce::AudioBuffer<float> blk (2, 512);
        for (int i = 0; i < 420; ++i)
        {
            juce::MidiBuffer mb;
            if (i % 140 == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 40 + (i / 140) * 5, 0.9f), 0);
            if (i % 140 == 120) mb.addEvent (juce::MidiMessage::noteOff (1, 40 + (i / 140) * 5), 0);
            blk.clear(); r.p.processBlock (blk, mb);
            if (i % 3 == 0) if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) t->timerCallback();
        }
        partialsEditorShow (ed.get(), tab, target <= -2 ? -target - 2 : -1, target, art, 0);
        if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) for (int k = 0; k < 8; ++k) t->timerCallback();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (argv[2]); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        return 0;
    }

    { // polyphony: a held chord, an 8-note cluster, notes that overlap
        auto sineRig = [] (Rig& r) { auto& d = r.p.edit (0); d.shape[SH::Timbre1].raw.assign ((size_t) NH, 0.0f); d.shape[SH::Timbre1].raw[0] = 0.5f; d.shape[SH::Timbre1].pts.clear(); r.p.commit (0); };
        Rig r; sineRig (r);
        auto b = r.render ({ { 60, 0.0f, 1.5f }, { 64, 0.0f, 1.5f }, { 67, 0.0f, 1.5f } }, 1.6f);
        auto sp = spectrum (b, (int) (0.4 * SR));
        float m1, m2, m3;
        const float h1 = peakHz (sp, 240, 290, &m1), h2 = peakHz (sp, 310, 350, &m2), h3 = peakHz (sp, 370, 410, &m3);
        check (std::abs (h1 - 261.63f) < 1 && std::abs (h2 - 329.63f) < 1 && std::abs (h3 - 392.0f) < 1 && std::min (m1, std::min (m2, m3)) > 0.5f * std::max (m1, std::max (m2, m3)),
               "chord C E G: " + juce::String (h1, 1) + " / " + juce::String (h2, 1) + " / " + juce::String (h3, 1) + " Hz, levels " + juce::String (m1, 3) + " " + juce::String (m2, 3) + " " + juce::String (m3, 3));
        {
            Rig rd;   // the default patch, untouched
            auto bd = rd.render ({ { 48, 0.0f, 1.5f }, { 55, 0.1f, 1.5f }, { 64, 0.2f, 1.5f } }, 1.6f);
            auto sd = spectrum (bd, (int) (0.6 * SR));
            float a1, a2, a3;
            const float f1 = peakHz (sd, 125, 136, &a1), f2 = peakHz (sd, 190, 202, &a2), f3 = peakHz (sd, 322, 337, &a3);
            check (std::abs (f1 - 130.8f) < 1.5f && std::abs (f2 - 196.0f) < 1.5f && std::abs (f3 - 329.6f) < 1.5f && std::min (a1, std::min (a2, a3)) > 0.01f,
                   "default patch, staggered chord C3 G3 E4: " + juce::String (f1, 1) + " / " + juce::String (f2, 1) + " / " + juce::String (f3, 1) + " Hz, levels " + juce::String (a1, 3) + " " + juce::String (a2, 3) + " " + juce::String (a3, 3));
        }
        Rig r2; sineRig (r2);
        std::vector<std::array<float, 3>> cl; for (int k = 0; k < 8; ++k) cl.push_back ({ (float) (48 + k * 5), 0.0f, 1.5f });
        auto b2 = r2.render (cl, 1.6f); auto sp2 = spectrum (b2, (int) (0.4 * SR));
        int found = 0;
        for (int k = 0; k < 8; ++k) { const float f = 440.0f * std::exp2 ((48 + k * 5 - 69) / 12.0f); float m; const float h = peakHz (sp2, f * 0.97f, f * 1.03f, &m); if (std::abs (h - f) < 1.5f && m > 0.02f) ++found; }
        check (found == 8, "8-note cluster: " + juce::String (found) + " of 8 notes sound");
        Rig r3; sineRig (r3);
        auto b3 = r3.render ({ { 60, 0.0f, 2.0f }, { 67, 0.5f, 2.0f } }, 2.1f); auto sp3 = spectrum (b3, (int) (1.0 * SR));
        float ma, mb; peakHz (sp3, 250, 270, &ma); peakHz (sp3, 380, 400, &mb);
        check (ma > 0.5f * mb, "an earlier held note keeps sounding when a new one starts (" + juce::String (ma, 3) + " vs " + juce::String (mb, 3) + ")");
    }
    { // the volume envelope: attack and release shape the sound when it is on
        auto envRig = [] (Rig& r, float a, float d, float s, float rl, bool on)
        {
            auto& dt = r.p.edit (0); dt.tg[TG::Volume].env.adsr (a, d, s, rl); dt.tg[TG::Volume].env.on = on; r.p.commit (0);
        };
        Rig a; envRig (a, 0.6f, 0.2f, 1.0f, 0.1f, true);
        auto ba = a.render ({ { 60, 0.0f, 1.0f } }, 1.2f);
        const float early = rms (ba, (int) (0.05 * SR), 2400), late = rms (ba, (int) (0.7 * SR), 2400);
        check (early < 0.3f * late, "attack 0.6 s: level at 50 ms is " + juce::String (20 * std::log10 (early / late + 1e-9f), 1) + " dB below the held level");
        Rig rl; envRig (rl, 0.005f, 0.2f, 1.0f, 1.5f, true);
        auto br = rl.render ({ { 60, 0.0f, 0.5f } }, 2.5f);
        const float held = rms (br, (int) (0.3 * SR), 2400), after = rms (br, (int) (1.0 * SR), 2400);
        check (after > 0.2f * held, "release 1.5 s: 0.5 s after note-off the sound is " + juce::String (20 * std::log10 (after / held + 1e-9f), 1) + " dB");
        Rig sd; envRig (sd, 0.005f, 0.3f, 0.2f, 0.1f, true);
        auto bs = sd.render ({ { 60, 0.0f, 1.5f } }, 1.6f);
        const float pk = rms (bs, (int) (0.01 * SR), 480), sus = rms (bs, (int) (1.0 * SR), 2400);
        check (sus < 0.45f * pk && sus > 0.05f * pk, "decay to sustain 20%: sustain level " + juce::String (20 * std::log10 (sus / pk + 1e-9f), 1) + " dB under the peak");
    }

    { // articulation: every target x every part reaches the sound
        fails += artMatrix (false) > 0 ? 1 : 0;
    }
    { // an envelope does what its line says: pitch, filter, the modifiers under it, the D marker
        Rig r; setSine (r); r.setP (0, PP::velVol, 0);
        auto& e = r.p.edit (0).tg[TG::Pitch].env;
        e.c.pts = { { 0, 0.5f, 0, Single, 0 }, { 0.4f, 0.625f, 0, Single, 0 }, { 1.4f, 0.625f, 0, Single, 0 }, { 1.6f, 0.5f, 0, Single, 0 } }; e.sustain = 2; e.on = true; r.p.commit (0);
        auto b = r.render ({ { 57, 0.0f, 1.2f } }, 1.3f);
        const float f0 = zcHz (b, (int) (0.01 * SR), 1200), f1 = zcHz (b, (int) (0.2 * SR), 2400), f2 = zcHz (b, (int) (0.8 * SR), 9600);
        check (std::abs (f0 - 220.0f) < 12.0f && f1 > 280.0f && f1 < 340.0f && std::abs (f2 - 440.0f) < 2.0f,
               "pitch envelope: starts at " + juce::String (f0, 1) + " Hz, half way " + juce::String (f1, 1) + " Hz, holds at " + juce::String (f2, 1) + " Hz (220 > 440 expected, +1200 ct)");

        Rig fq;      // nothing touched but the envelope: the filter is fully open and the envelope amount is +100 %, as in a new sound
        auto& fe = fq.p.edit (0).tg[TG::F1Freq].env;
        fe.c.pts = { { 0, 1.0f, 0, Single, 0 }, { 0.5f, 0.2f, 0, Single, 0 }, { 1.5f, 0.2f, 0, Single, 0 } }; fe.sustain = 1; fe.on = true; fq.p.commit (0);
        auto bf = fq.render ({ { 45, 0.0f, 1.2f } }, 1.3f);
        const float hi0 = bandLevel (bf, 0, (int) (0.02 * SR), 11, 3000, 12000), hi1 = bandLevel (bf, 0, (int) (0.7 * SR), 11, 3000, 12000), lo1 = bandLevel (bf, 0, (int) (0.7 * SR), 11, 80, 300);
        check (hi1 < 0.05f * hi0 && lo1 > 0.01f, "filter envelope on a new sound (freq fully open, env +100%): highs fall " + juce::String (20 * std::log10 (hi1 / hi0 + 1e-9f), 1) + " dB as the line falls, lows stay");
        fq.setP (0, PP::f1Env, -1.0f);       // a negative amount turns the line over: closed at the start, open at the end
        auto bn = fq.render ({ { 45, 0.0f, 1.2f } }, 1.3f);
        const float n0 = bandLevel (bn, 0, (int) (0.02 * SR), 11, 3000, 12000), n1 = bandLevel (bn, 0, (int) (0.7 * SR), 11, 3000, 12000);
        check (n1 > 10.0f * n0, "filter envelope amount -100%: the line is turned over (highs rise " + juce::String (20 * std::log10 (n1 / (n0 + 1e-9f)), 1) + " dB)");

        // modifiers: attack x8, sustain x0.5, release x0.1 (parameters of the volume envelope of part A)
        auto envRig = [] (Rig& q) { auto& d = q.p.edit (0); d.tg[TG::Volume].env.adsr (0.05f, 0.2f, 0.8f, 0.6f); q.p.commit (0); q.setP (0, PP::velVol, 0); };
        auto mod = [] (Rig& q, int k, float v) { auto* prm = q.p.envModParam (0, TG::Volume, k); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        Rig m0; envRig (m0); auto b0 = m0.render ({ { 60, 0.0f, 1.0f } }, 1.8f);
        Rig m1; envRig (m1); mod (m1, EM::Att, 8.0f); auto b1 = m1.render ({ { 60, 0.0f, 1.0f } }, 1.8f);
        Rig m2; envRig (m2); mod (m2, EM::Sus, 0.5f); auto b2 = m2.render ({ { 60, 0.0f, 1.0f } }, 1.8f);
        Rig m3; envRig (m3); mod (m3, EM::Rel, 0.1f); auto b3 = m3.render ({ { 60, 0.0f, 1.0f } }, 1.8f);
        Rig m4; envRig (m4); mod (m4, EM::Dec, 4.0f); auto b4 = m4.render ({ { 60, 0.0f, 1.0f } }, 1.8f);
        const int at = (int) (0.06 * SR), su = (int) (0.8 * SR), rl = (int) (1.25 * SR), dc = (int) (0.2 * SR);
        check (rms (b1, at, 480) < 0.35f * rms (b0, at, 480), "attack modifier x8: at 60 ms the level is " + juce::String (20 * std::log10 (rms (b1, at, 480) / rms (b0, at, 480)), 1) + " dB under the unmodified envelope");
        check (std::abs (rms (b2, su, 2400) / rms (b0, su, 2400) - 0.5f) < 0.03f, "sustain modifier x0.5: sustain level ratio " + juce::String (rms (b2, su, 2400) / rms (b0, su, 2400), 3));
        check (rms (b3, rl, 480) < 0.02f * rms (b0, rl, 480), "release modifier x0.1: 250 ms after note-off the sound is gone (" + juce::String (20 * std::log10 (rms (b3, rl, 480) / rms (b0, rl, 480) + 1e-9f), 1) + " dB against x1)");
        check (rms (b4, dc, 480) > 1.1f * rms (b0, dc, 480) && std::abs (rms (b4, at - 1200, 480) / rms (b0, at - 1200, 480) - 1.0f) < 0.02f, "decay modifier x4: the decay is slower, the attack (before the D marker) is untouched");
        // a sine through the same envelope, short release: starting and stopping makes no click
        Rig ck; setSine (ck); envRig (ck); mod (ck, EM::Rel, 0.1f); mod (ck, EM::Att, 0.0625f);
        auto bk = ck.render ({ { 60, 0.0f, 0.5f }, { 72, 0.6f, 0.9f } }, 1.2f);
        float pk; check (finite (bk, pk) && pk > 0.05f && worstJump (bk) < 0.12f, "a sine through a fast envelope starts and stops without a click (largest step " + juce::String (worstJump (bk), 3) + " of the peak; a clean C5 sine steps 0.068)");
    }
    { // unison: the same level left and right for every order, and really stereo
        bool ok = true; juce::String worst;
        for (int U = 2; U <= 9; ++U)
        {
            Rig r; r.setP (0, PP::uniOrder, (float) U); r.setP (0, PP::uniPan, 1.0f); r.setP (0, PP::uniPitch, 0.5f); r.setP (0, PP::uniType, 1);
            auto b = r.render ({ { 48, 0.0f, 1.9f } }, 2.0f);
            const float l = b.getRMSLevel (0, 9600, 76800), rr = b.getRMSLevel (1, 9600, 76800);
            double sd = 0, md = 0; for (int i = 9600; i < 86400; ++i) { const double a = b.getSample (0, i), c = b.getSample (1, i); sd += (a - c) * (a - c); md += (a + c) * (a + c); }
            const float bal = 20.0f * std::log10 (l / rr);
            if (std::abs (bal) > 1.0f || sd < 0.05 * md) { ok = false; worst << " order " << U << ": L/R " << juce::String (bal, 1) << " dB"; }
        }
        check (ok, "unison orders 2 to 9 with pan at 100%: left and right within 1 dB of each other, and wide" + worst);
    }
    { // tempo, loop and global: the switches under the envelope
        Rig t; setSine (t); t.setP (0, PP::velVol, 0);
        { auto& e = t.p.edit (0).tg[TG::Volume].env; e.c.pts = { { 0, 0, 0, Single, 0 }, { 2.0f, 1.0f, 0, Single, 0 }, { 2.5f, 1.0f, 0, Single, 0 }, { 2.6f, 0, 0, Single, 0 } }; e.sustain = 2; e.decay = 1; e.tempo = true; e.on = true; t.p.commit (0); }
        auto bt = t.render ({ { 60, 0.0f, 1.4f } }, 1.5f);       // no host tempo in the test: 120 bpm, so 2 beats are one second
        const float half = rms (bt, (int) (0.5 * SR), 480), full = rms (bt, (int) (1.2 * SR), 480);
        check (std::abs (half / full - 0.5f) < 0.04f, "tempo: a 2-beat attack at 120 bpm is half way after 0.5 s (" + juce::String (half / full, 3) + ")");

        Rig l; setSine (l); l.setP (0, PP::velVol, 0);       // a loop between L and S, 0.25 s long: the level goes round at 4 Hz while the key is down
        { auto& e = l.p.edit (0).tg[TG::Volume].env; e.c.pts = { { 0, 1.0f, 0, Single, 0 }, { 0.125f, 0.2f, 0, Single, 0 }, { 0.25f, 1.0f, 0, Single, 0 }, { 0.3f, 0, 0, Single, 0 } }; e.loopStart = 0; e.sustain = 2; e.on = true; l.p.commit (0); }
        auto bl = l.render ({ { 60, 0.0f, 1.9f } }, 2.0f);
        check (wobbleAt (bl, (int) (0.25 * SR), (int) (1.5 * SR), 4.0f) > 0.3f, "loop: the envelope keeps going round between L and S while the key is held (4 Hz, " + juce::String (wobbleAt (bl, (int) (0.25 * SR), (int) (1.5 * SR), 4.0f), 2) + ")");

        float dip[2];
        for (int gl = 0; gl < 2; ++gl)
        {
            Rig g; setSine (g); g.setP (0, PP::velVol, 0);
            { auto& e = g.p.edit (0).tg[TG::Volume].env; e.adsr (0.3f, 0.1f, 1.0f, 0.1f); e.global = gl == 1; e.on = true; g.p.commit (0); }
            auto bg = g.render ({ { 48, 0.0f, 1.9f }, { 84, 1.0f, 1.9f } }, 2.0f);
            dip[gl] = bandLevel (bg, 0, (int) (1.03 * SR), 11, 100, 180) / bandLevel (bg, 0, (int) (0.8 * SR), 11, 100, 180);     // the low note, just after the high one starts
        }
        check (dip[0] > 0.9f && dip[1] < 0.5f, "global: a new note restarts the envelope of the notes already sounding (the held note drops to x" + juce::String (dip[1], 2) + "; without global it stays at x" + juce::String (dip[0], 2) + ")");
    }
    { // an LFO: rate, amplitude, start time
        auto lfoRig = [] (Rig& q, float hz, bool start)
        {
            setSine (q); q.setP (0, PP::velVol, 0);
            auto& l = q.p.edit (0).tg[TG::Volume].lfo; l.on = true; l.speed = lfoSpeedFor (hz); l.amt = 1.0f;
            if (start) l.amp.pts = { { 0.6f, 1.0f, 0, Single, 0 } };
            q.p.commit (0);
        };
        Rig a; lfoRig (a, 4.0f, false); auto ba = a.render ({ { 60, 0.0f, 2.0f } }, 2.1f);
        const float w4 = wobbleAt (ba, (int) (0.3 * SR), (int) (1.5 * SR), 4.0f), w7 = wobbleAt (ba, (int) (0.3 * SR), (int) (1.5 * SR), 7.0f);
        check (w4 > 0.5f && w7 < 0.25f * w4, "LFO on volume at 4 Hz: level wobbles at 4 Hz (" + juce::String (w4, 2) + "), not at 7 Hz (" + juce::String (w7, 2) + ")");
        Rig c; lfoRig (c, 8.0f, true); auto bc = c.render ({ { 60, 0.0f, 2.0f } }, 2.1f);
        const float early = wobbleAt (bc, (int) (0.1 * SR), (int) (0.45 * SR), 8.0f), late = wobbleAt (bc, (int) (0.8 * SR), (int) (1.0 * SR), 8.0f);
        check (early < 0.05f && late > 0.5f, "LFO start time 0.6 s: still before it (" + juce::String (early, 3) + "), wobbling after (" + juce::String (late, 2) + ")");
        Rig p; setSine (p); { auto& l = p.p.edit (0).tg[TG::Pitch].lfo; l.on = true; l.speed = lfoSpeedFor (2.0f); l.amt = 0.25f; p.p.commit (0); }     // +-1200 ct
        auto bp = p.render ({ { 69, 0.0f, 1.2f } }, 1.3f);
        float lo = 1.0e9f, hi = 0;
        for (int k = 0; k < 20; ++k) { const float f = zcHz (bp, (int) ((0.1 + 0.05 * k) * SR), 2400); lo = juce::jmin (lo, f); hi = juce::jmax (hi, f); }
        check (lo < 260.0f && hi > 750.0f, "LFO on pitch, amount 25% (an octave each way): pitch swings " + juce::String (lo, 0) + " .. " + juce::String (hi, 0) + " Hz around 440");
    }
    { // X / Y / Z: the pad's values move whatever is mapped to them
        for (int ax = 0; ax < 3; ++ax)
        {
            float hf[2];
            for (int v = 0; v < 2; ++v)
            {
                Rig r; r.setP (0, PP::f1Type, 1); r.setP (0, PP::f1Freq, 0.35f); r.set (GP::smoothMod, 0);
                auto& t = r.p.edit (0).tg[TG::F1Freq]; t.mapOn[MAP::ModX + ax] = true; t.map[MAP::ModX + ax].line ({ { 0, 0.5f }, { 1, 1.0f } }); r.p.commit (0);
                r.set (GP::modX + ax, (float) v);
                auto b = r.render ({ { 45, 0.0f, 0.9f } }, 1.0f);
                hf[v] = bandLevel (b, 0, (int) (0.2 * SR), 13, 2500, 12000);
            }
            check (hf[1] > 8.0f * hf[0], juce::String ("mod ") + "xyz"[ax] + " mapped to filter 1 frequency: highs " + juce::String (20 * std::log10 (hf[1] / (hf[0] + 1e-9f)), 1) + " dB up with the control at 100%");
        }
        // moved while a note sounds, with smoothing: a glide, no steps; a MIDI controller does the same as the parameter
        Rig r; setSine (r); r.setP (0, PP::velVol, 0);
        { auto& t = r.p.edit (0).tg[TG::Volume]; t.mapOn[MAP::ModX] = true; t.map[MAP::ModX].line ({ { 0, 0.1f }, { 1, 1.0f } }); r.p.commit (0); }
        auto b = r.render ({ { 60, 0.0f, 1.9f } }, 2.0f, nullptr, [&r] (float f) { r.set (GP::modX, f < 0.3f ? 0.0f : (f < 0.7f ? 1.0f : 0.0f)); });
        const float a0 = rms (b, (int) (0.4 * SR), 2400), a1 = rms (b, (int) (1.1 * SR), 2400), a2 = rms (b, (int) (1.8 * SR), 2400);
        check (std::abs (a1 / a0 - 10.0f) < 0.6f && std::abs (a2 / a0 - 1.0f) < 0.05f && worstJump (b) < 0.09f,
               "mod x moved under a held note: level x" + juce::String (a1 / a0, 2) + " and back x" + juce::String (a2 / a0, 2) + ", largest step " + juce::String (worstJump (b), 3) + " of the peak (no click)");
        Rig c; setSine (c); c.setP (0, PP::velVol, 0);
        { auto& t = c.p.edit (0).tg[TG::Volume]; t.mapOn[MAP::ModX] = true; t.map[MAP::ModX].line ({ { 0, 0.1f }, { 1, 1.0f } }); c.p.commit (0); }
        {
            juce::AudioBuffer<float> out (2, (int) SR), blk (2, 512);
            for (int pos = 0; pos < (int) SR; pos += 512)
            {
                juce::MidiBuffer mb;
                if (pos == 0) { mb.addEvent (juce::MidiMessage::controllerEvent (1, 1, 0), 0); mb.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 1); }
                if (pos == 512 * 40) mb.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
                const int n = juce::jmin (512, (int) SR - pos);
                juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n); view.clear(); c.p.processBlock (view, mb);
                out.copyFrom (0, pos, view, 0, 0, n); out.copyFrom (1, pos, view, 1, 0, n);
            }
            const float c0 = rms (out, (int) (0.3 * SR), 2400), c1 = rms (out, (int) (0.8 * SR), 2400);
            check (std::abs (c1 / c0 - 10.0f) < 0.6f, "MIDI CC 1 drives mod x by default: level x" + juce::String (c1 / c0, 2) + " when the wheel goes to the top");
        }
    }
    { // keyboard, velocity, unison index and predelay mappings
        Rig r; setSine (r);
        { auto& t = r.p.edit (0).tg[TG::Pan]; t.mapOn[MAP::Key] = true; t.map[MAP::Key].line ({ { 0, 0.0f }, { 1, 1.0f } }); r.p.commit (0); }
        auto lo = r.render ({ { 36, 0.0f, 0.4f } }, 0.5f); auto hi = r.render ({ { 96, 0.0f, 0.4f } }, 0.5f);
        const float loL = lo.getRMSLevel (0, 4800, 9600), loR = lo.getRMSLevel (1, 4800, 9600), hiL = hi.getRMSLevel (0, 4800, 9600), hiR = hi.getRMSLevel (1, 4800, 9600);
        check (loL > 1.5f * loR && hiR > 1.5f * hiL, "keyboard mapping on panning: C2 sits left (" + juce::String (loL / loR, 2) + " : 1), C7 right (1 : " + juce::String (hiR / hiL, 2) + ")");

        Rig v; setSine (v); v.setP (0, PP::velVol, 0);
        { auto& t = v.p.edit (0).tg[TG::Volume]; t.mapOn[MAP::Vel] = true; t.map[MAP::Vel].line ({ { 0, 0.0f }, { 1, 1.0f } }); v.p.commit (0); }
        float lv[2];
        for (int k = 0; k < 2; ++k)
        {
            juce::AudioBuffer<float> out (2, 24000), blk (2, 512);
            for (int pos = 0; pos < 24000; pos += 512)
            {
                juce::MidiBuffer mb; if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 60, k == 0 ? 0.25f : 1.0f), 0);
                const int n = juce::jmin (512, 24000 - pos); juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n); view.clear(); v.p.processBlock (view, mb);
                out.copyFrom (0, pos, view, 0, 0, n);
            }
            lv[k] = out.getRMSLevel (0, 9600, 9600);
            juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0); for (int i = 0; i < 80; ++i) { blk.clear(); v.p.processBlock (blk, off); off.clear(); }
        }
        check (std::abs (lv[1] / lv[0] - 4.0f) < 0.3f, "velocity mapping on volume (a straight line): velocity 100% is x" + juce::String (lv[1] / lv[0], 2) + " the level of 25%");

        // unison index mapping on something other than pan / volume / pitch: voice 1 dark, voice 2 bright, one on each side
        Rig u; u.setP (0, PP::uniOrder, 2); u.setP (0, PP::uniPan, 1.0f); u.setP (0, PP::uniPitch, 0.0f); u.setP (0, PP::f1Type, 1); u.setP (0, PP::f1Freq, 0.3f);
        { auto& t = u.p.edit (0).tg[TG::F1Freq]; t.mapOn[MAP::Unison] = true; t.map[MAP::Unison].line ({ { 0, 0.5f }, { 1, 1.0f } }); u.p.commit (0); }
        auto bu = u.render ({ { 45, 0.0f, 0.9f } }, 1.0f);
        const float hL = bandLevel (bu, 0, (int) (0.2 * SR), 13, 2500, 12000), hR = bandLevel (bu, 1, (int) (0.2 * SR), 13, 2500, 12000);
        check (hR > 8.0f * hL, "unison index mapping on filter 1 frequency: the second unison voice (right) is " + juce::String (20 * std::log10 (hR / (hL + 1e-9f)), 1) + " dB brighter than the first (left)");
        // and on pitch: two unison voices an octave apart ("dual voice")
        Rig d; setSine (d); d.setP (0, PP::uniOrder, 2); d.setP (0, PP::uniPitch, 0.0f);
        { auto& t = d.p.edit (0).tg[TG::Pitch]; t.mapOn[MAP::Unison] = true; t.map[MAP::Unison].line ({ { 0, 0.5f }, { 1, 0.375f } }); d.p.commit (0); }
        auto bd = d.render ({ { 69, 0.0f, 0.9f } }, 1.0f); auto sd = spectrum (bd, 4800);
        float mA, mB; const float fA = peakHz (sd, 400, 480, &mA), fB = peakHz (sd, 200, 240, &mB);
        check (std::abs (fA - 440.0f) < 1.0f && std::abs (fB - 220.0f) < 1.0f && mB > 0.3f * mA, "unison index mapping on pitch: voices at " + juce::String (fA, 1) + " and " + juce::String (fB, 1) + " Hz");

        // predelay with a velocity mapping: soft notes wait, hard notes do not
        Rig pd; setSine (pd); pd.setP (0, PP::predelay, -0.5f);      // 250 ms
        { auto& t = pd.p.edit (0).tg[TG::Predelay]; t.mapOn[MAP::ModX] = true; t.map[MAP::ModX].line ({ { 0, 1.0f }, { 1, 0.5f } }); pd.p.commit (0); }
        pd.set (GP::smoothMod, 0); pd.set (GP::modX, 0.0f);
        auto p0 = pd.render ({ { 60, 0.0f, 0.5f } }, 0.6f);
        Rig pe; setSine (pe); pe.setP (0, PP::predelay, -0.5f);
        { auto& t = pe.p.edit (0).tg[TG::Predelay]; t.mapOn[MAP::ModX] = true; t.map[MAP::ModX].line ({ { 0, 1.0f }, { 1, 0.5f } }); pe.p.commit (0); }
        pe.set (GP::smoothMod, 0); pe.set (GP::modX, 1.0f);
        auto p1 = pe.render ({ { 60, 0.0f, 0.5f } }, 0.6f);
        auto onset = [] (const juce::AudioBuffer<float>& b) { for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 0.01f) return (float) i / (float) SR; return -1.0f; };
        check (std::abs (onset (p0) - 0.25f) < 0.02f && onset (p1) < 0.02f, "predelay mapping: 250 ms with the mapping at the top (" + juce::String (onset (p0) * 1000.0f, 0) + " ms), none where it crosses the middle (" + juce::String (onset (p1) * 1000.0f, 0) + " ms)");
    }
    { // older sounds keep sounding the same: the filter envelope of a state from before, the parameter list
        Rig a; a.setP (0, PP::f1Type, 2); a.setP (0, PP::f1Freq, 0.55f); a.setP (0, PP::f1Env, 0.4f); a.setP (0, PP::glideLimit, 700.0f);
        { auto& e = a.p.edit (0).tg[TG::F1Freq].env; e.adsr (0.001f, 0.25f, 0.0f, 0.2f); e.on = true; a.p.edit (0).filtEnvAdd = true; a.p.commit (0); }
        auto ref = a.render ({ { 45, 0.0f, 0.5f } }, 0.8f);
        // the state as the first release wrote it: none of the later properties
        auto st = a.p.makeState();
        st.setProperty ("version", 1, nullptr); st.removeProperty ("info", nullptr);
        auto g = st.getChildWithName ("Global"); g.removeProperty ("modCC", nullptr); for (int i = 0; i < XP::Count; ++i) g.removeProperty (extraDefs[i].id, nullptr);
        for (auto c : st) if (c.hasType ("Part")) { c.removeProperty ("fenv", nullptr); c.removeChild (c.getChildWithName ("EnvMods"), nullptr); for (auto t : c) if (t.hasType ("T")) t.removeProperty ("dec", nullptr); }
        Rig b; b.p.applyState (st);
        auto old = b.render ({ { 45, 0.0f, 0.5f } }, 0.8f);
        check (b.p.edit (0).filtEnvAdd && relDiff (ref, old) < 1.0e-5f && std::abs (b.p.extraValue (XP::legLimit) - 700.0f) < 0.5f,
               "a state from before this version loads with its filter sweep unchanged (difference " + juce::String (relDiff (ref, old), 7) + ") and its legato limit kept");
        // the same sweep in a new sound: freq at the top of the sweep, envelope below it
        Rig n; n.setP (0, PP::f1Type, 2); n.setP (0, PP::f1Freq, 0.95f); n.setP (0, PP::f1Env, 0.4f);
        { auto& e = n.p.edit (0).tg[TG::F1Freq].env; e.adsr (0.001f, 0.25f, 0.0f, 0.2f); e.on = true; n.p.commit (0); }
        auto nw = n.render ({ { 45, 0.0f, 0.5f } }, 0.8f);
        check (! n.p.edit (0).filtEnvAdd && relDiff (ref, nw) < 1.0e-5f, "the same sweep written the new way (freq 0.95, env below it) sounds identical (" + juce::String (relDiff (ref, nw), 7) + ")");
        // parameters: everything that existed keeps its place; the new ones follow
        auto& ps = a.p.getParameters(); bool order = true; int idx = 0;
        auto idAt = [&ps] (int i) { auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]); return w != nullptr ? w->paramID : juce::String(); };
        for (int i = 0; i < GP::Count; ++i) order = order && idAt (idx++) == globalDefs[i].id;
        for (int p = 0; p < 2; ++p) for (int i = 0; i < PP::Count; ++i) order = order && idAt (idx++) == PartialsProcessor::partId (p, i);
        check (order && ps.size() == GP::Count + 2 * PP::Count + XP::Count + 2 * TG::FirstMapping * EM::Count && idAt (idx) == extraDefs[0].id,
               "parameter list: the " + juce::String (idx) + " earlier parameters are in their old places, " + juce::String (ps.size() - idx) + " new ones follow");
        bool defs = true; Rig fresh;
        for (int i = 0; i < GP::Count; ++i) defs = defs && std::abs (fresh.p.globalValue (i) - globalDefs[i].def) < 1.0e-5f;
        for (int p = 0; p < 2; ++p) for (int i = 0; i < PP::Count; ++i) defs = defs && std::abs (fresh.p.partValue (p, i) - partDefs[i].def) < 1.0e-4f;
        for (int i = 0; i < XP::Count; ++i) defs = defs && std::abs (fresh.p.extraValue (i) - extraDefs[i].def) < 1.0e-5f;
        for (int p = 0; p < 2; ++p) for (int t = 0; t < TG::FirstMapping; ++t) for (int m = 0; m < EM::Count; ++m) defs = defs && std::abs (fresh.p.envModValue (p, t, m) - 1.0f) < 1.0e-4f;
        check (defs && fresh.p.presetName == "Default", "a new instance opens with every parameter at its default and no preset loaded");
    }
    { // the whole sound survives the host's save and reload: parameters, envelopes, LFOs, mappings, modifiers, X / Y / Z, controllers
        Rig r; juce::Random rnd (77);
        for (int part = 0; part < 2; ++part)
        {
            for (int i = 0; i < PP::Count; ++i) { auto* q = r.p.partParam (part, i); q->setValueNotifyingHost (rnd.nextFloat()); }
            auto& d = r.p.edit (part);
            for (int t = 0; t < TG::Count; ++t)
            {
                auto& td = d.tg[t];
                td.smooth = rnd.nextFloat() * 0.3f;
                if (t < TG::FirstMapping)
                {
                    td.env.on = rnd.nextBool(); td.env.tempo = rnd.nextBool(); td.env.global = rnd.nextBool();
                    td.env.c.pts = { { 0, rnd.nextFloat(), 0, Single, 0 }, { 0.1f, rnd.nextFloat(), rnd.nextFloat() - 0.5f, Smooth, 0 }, { 0.3f, rnd.nextFloat(), 0, Double, 3 }, { 0.5f, rnd.nextFloat(), 0, Stairs, 0 }, { 0.9f, 0, 0, Single, 0 } };
                    td.env.sustain = 3; td.env.loopStart = 1; td.env.decay = 2;
                    td.lfo.on = rnd.nextBool(); td.lfo.tempo = rnd.nextBool(); td.lfo.global = rnd.nextBool(); td.lfo.amt = rnd.nextFloat(); td.lfo.speed = rnd.nextFloat(); td.lfo.tension = rnd.nextFloat();
                    td.lfo.skew = rnd.nextFloat() - 0.5f; td.lfo.pw = rnd.nextFloat() - 0.5f; td.lfo.phase = rnd.nextFloat(); td.lfo.amp.pts = { { rnd.nextFloat(), rnd.nextFloat(), 0, Single, 0 }, { 1.5f, rnd.nextFloat(), 0, Single, 0 } };
                    for (int m = 0; m < EM::Count; ++m) { auto* q = r.p.envModParam (part, t, m); q->setValueNotifyingHost (rnd.nextFloat()); }
                }
                for (int m = 0; m < MAP::Count; ++m) { td.mapOn[m] = rnd.nextBool(); td.map[m].line ({ { 0, rnd.nextFloat() }, { 0.5f, rnd.nextFloat() }, { 1, rnd.nextFloat() } }); }
            }
            for (int s2 = 0; s2 < SH::Count; ++s2) d.shape[s2].line ({ { 0, rnd.nextFloat() }, { 1, rnd.nextFloat() } });
            d.image = ImageData::randomCloud ((uint32_t) part + 9);
            r.p.commit (part);
        }
        for (int i = 0; i < GP::Count; ++i) { auto* q = r.p.globalParam (i); q->setValueNotifyingHost (rnd.nextFloat()); }
        for (int i = 0; i < XP::Count; ++i) { auto* q = r.p.extraParam (i); q->setValueNotifyingHost (rnd.nextFloat()); }
        r.set (GP::modX, 0.31f); r.set (GP::modY, 0.62f); r.set (GP::modZ, 0.93f); r.set (GP::masterVol, 0); r.set (GP::partAOn, 1); r.set (GP::partBOn, 1);
        r.p.modCC[0].store (21); r.p.modCC[1].store (-1); r.p.modCC[2].store (7); r.p.presetName = "Round trip"; r.p.presetInfo = "x: brightness, y: speed";
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        Rig q; q.p.setStateInformation (mb.getData(), (int) mb.getSize());
        // saved a second time, the state says the same thing (numbers to 5 places: a float through the host's 0..1 range may move in its last digit)
        auto round5 = [] (const juce::ValueTree& v) { auto x = v.toXmlString(); juce::String o; o.preallocateBytes ((size_t) x.length());
                                                      auto t = x.getCharPointer(); int digits = -1;
                                                      while (! t.isEmpty()) { const auto c = t.getAndAdvance(); if (c == '.') digits = 0; else if (c >= '0' && c <= '9') { if (digits >= 0 && ++digits > 5) continue; } else digits = -1; o << juce::String::charToString (c); }
                                                      return o; };
        const auto s1 = round5 (r.p.makeState()), s2 = round5 (q.p.makeState());
        const bool same = s1 == s2;
        if (! same) { int at = 0; while (at < s1.length() && at < s2.length() && s1[at] == s2[at]) ++at; std::cout << "        first difference: ..." << s1.substring (juce::jmax (0, at - 120), at + 60) << "... | ..." << s2.substring (juce::jmax (0, at - 20), at + 60) << std::endl; }
        bool vals = std::abs (q.p.globalValue (GP::modX) - 0.31f) < 1.0e-4f && std::abs (q.p.globalValue (GP::modY) - 0.62f) < 1.0e-4f && std::abs (q.p.globalValue (GP::modZ) - 0.93f) < 1.0e-4f
                    && q.p.modCC[0].load() == 21 && q.p.modCC[1].load() == -1 && q.p.modCC[2].load() == 7 && q.p.presetInfo == "x: brightness, y: speed";
        for (int p = 0; p < 2; ++p) for (int t = 0; t < TG::FirstMapping; ++t) for (int m = 0; m < EM::Count; ++m) vals = vals && std::abs (q.p.envModValue (p, t, m) - r.p.envModValue (p, t, m)) < 1.0e-4f;
        vals = vals && q.p.edit (1).tg[TG::F2Res].env.decay == 2 && q.p.edit (1).tg[TG::F2Res].lfo.amp.pts.size() == 2 && q.p.edit (0).tg[TG::Predelay].map[MAP::XSpeed].pts.size() == 3;
        auto x1 = r.render ({ { 50, 0.0f, 0.3f }, { 62, 0.1f, 0.4f } }, 0.6f); auto x2 = q.render ({ { 50, 0.0f, 0.3f }, { 62, 0.1f, 0.4f } }, 0.6f);
        float pk; const bool fin = finite (x1, pk);
        if (! vals) std::cout << "        values differ after the reload" << std::endl;
        check (same && vals && fin && relDiff (x1, x2) < 1.0e-5f, "full state round trip (every envelope, LFO, mapping, modifier, X / Y / Z, controllers): saved again it reads the same, " + juce::String ((int) mb.getSize() / 1024)
                                                                    + " KB, and the reloaded instance renders the same (difference " + juce::String (relDiff (x1, x2), 7) + ")");
    }
    { // the window, driven like a user would
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
        auto bad = partialsEditorSelfTest (ed.get());
        for (auto& b : bad) std::cout << "        " << b << std::endl;
        check (bad.isEmpty(), "window: every parameter has a control; the pad, quick map, envelope / LFO editing and the modifiers reach the engine");
        const int w0 = ed->getWidth(), h0 = ed->getHeight();
        check (std::abs ((double) w0 / h0 - 1857.0 / 1022.0) < 0.01, "window opens " + juce::String (w0) + " x " + juce::String (h0) + " (the proportions of the reference layout)");
    }
    { // a single sine partial: pitch and level
        Rig r;
        auto& d = r.p.edit (0); d.shape[SH::Timbre1].raw.assign ((size_t) NH, 0.0f); d.shape[SH::Timbre1].raw[0] = 0.5f; d.shape[SH::Timbre1].pts.clear(); r.p.commit (0);
        r.setP (0, PP::velVol, 0);
        auto b = r.render ({ { 69, 0.0f, 1.5f } }, 2.0f);
        auto s = spectrum (b, 12000); float mag = 0; const float hz = peakHz (s, 100, 2000, &mag);
        check (std::abs (hz - 440.0f) < 0.3f, "sine at A4 is " + juce::String (hz, 2) + " Hz, level " + juce::String (mag, 4));
        float other = 0; for (int k = 20; k < (int) s.size(); ++k) { const float f = (float) k * (float) SR / 32768.0f; if (std::abs (f - 440.0f) > 30.0f) other = juce::jmax (other, s[(size_t) k]); }
        check (other < mag * 0.0005f, "sine is clean: strongest other component " + juce::String (20.0f * std::log10 (other / mag + 1.0e-12f), 1) + " dB");
        check (std::abs (mag - 0.25f) < 0.02f, "sine level matches (0.25 expected)");
        check (rms (b, (int) (1.9 * SR), 2000) < 1.0e-4f, "note ends after release");
    }
    { // default saw: harmonics fall as 1/n
        Rig r; r.setP (0, PP::velVol, 0);
        auto b = r.render ({ { 45, 0.0f, 1.5f } }, 1.5f);
        auto s = spectrum (b, 12000); float m1, m2, m5, m40;
        peakHz (s, 100, 120, &m1); peakHz (s, 210, 230, &m2); peakHz (s, 540, 560, &m5); peakHz (s, 4380, 4420, &m40);
        check (std::abs (m1 / m2 - 2.0f) < 0.1f && std::abs (m1 / m5 - 5.0f) < 0.3f && std::abs (m1 / m40 - 40.0f) < 4.0f,
               "saw harmonics 1/n: " + juce::String (m1 / m2, 2) + " " + juce::String (m1 / m5, 2) + " " + juce::String (m1 / m40, 1));
        if (wavs) writeWav (b, "saw.wav");
    }
    { // filter with resonance, pluck, unison, effects: still sane
        Rig r;
        r.setP (0, PP::f1Type, 1); r.setP (0, PP::f1Freq, 0.5f); r.setP (0, PP::f1Res, 0.6f); r.setP (0, PP::pluckTime, 0.5f);
        r.setP (0, PP::uniOrder, 5); r.setP (0, PP::prismAmt, 0.3f); r.setP (0, PP::hzMix, 0.5f); r.setP (0, PP::hzShiftStep, 3); r.setP (0, PP::blurHarm, 0.5f);
        r.setP (0, PP::phMix, 0.7f); r.set (GP::rvOn, 1); r.set (GP::dlOn, 1); r.set (GP::chOrder, 4); r.set (GP::distType, 1); r.set (GP::cpType, 3);
        r.set (GP::partBOn, 1); r.setP (1, PP::timbreMix, 1); r.setP (1, PP::pitch, 1200);
        double cpu = 0;
        auto b = r.render ({ { 40, 0.0f, 1.0f }, { 47, 0.0f, 1.0f }, { 52, 0.5f, 1.5f }, { 59, 1.0f, 2.0f } }, 3.0f, &cpu);
        float pk; check (finite (b, pk) && pk > 0.01f && pk < 2.0f, "full chain renders, peak " + juce::String (pk, 3) + ", cpu " + juce::String (cpu * 100.0, 1) + "%");
        if (wavs) writeWav (b, "chain.wav");
    }
    { // speed: 8 low notes, unison 4
        Rig r; r.setP (0, PP::uniOrder, 4);
        double cpu = 0;
        std::vector<std::array<float, 3>> notes; for (int i = 0; i < 8; ++i) notes.push_back ({ (float) (33 + i * 3), 0.0f, 3.0f });
        auto b = r.render (notes, 3.0f, &cpu);
        float pk; finite (b, pk);
        check (cpu < 0.6, "8 low notes x unison 4: cpu " + juce::String (cpu * 100.0, 1) + "% of one core, " + juce::String (r.p.engine.activePartials()) + " partials, peak " + juce::String (pk, 2));
    }
    { // random settings
        Rig r; juce::Random rnd (1234); bool ok = true; float worst = 0; int silent = 0;
        for (int round = 0; round < 60 && ok; ++round)
        {
            for (int part = 0; part < 2; ++part) for (int i = 0; i < PP::Count; ++i) { auto* q = r.p.partParam (part, i); q->setValueNotifyingHost (rnd.nextFloat()); }
            for (int i = 0; i < GP::Count; ++i) { auto* q = r.p.globalParam (i); q->setValueNotifyingHost (rnd.nextFloat()); }
            r.set (GP::masterVol, 0);
            for (int part = 0; part < 2; ++part)
            {
                auto& d = r.p.edit (part);
                for (int t = 0; t < TG::Count; ++t)
                    if (rnd.nextFloat() < 0.2f)
                    {
                        d.tg[t].env.on = rnd.nextBool(); d.tg[t].env.adsr (rnd.nextFloat() * 0.1f, rnd.nextFloat() * 0.2f, rnd.nextFloat(), rnd.nextFloat() * 0.2f);
                        d.tg[t].lfo.on = rnd.nextBool(); d.tg[t].lfo.speed = rnd.nextFloat();
                        const int m = rnd.nextInt (MAP::Count); d.tg[t].mapOn[m] = true; d.tg[t].map[m].line ({ { 0, rnd.nextFloat() }, { 1, rnd.nextFloat() } });
                    }
                if (round % 3 == 1) d.image = ImageData::randomCloud ((uint32_t) round + 1);
                for (int i = 0; i < 8; ++i) std::swap (d.unitOrder[rnd.nextInt (UNIT::Count)], d.unitOrder[rnd.nextInt (UNIT::Count)]);
                r.p.commit (part);
            }
            auto b = r.render ({ { (float) (30 + rnd.nextInt (60)), 0.0f, 0.2f }, { (float) (30 + rnd.nextInt (60)), 0.05f, 0.3f }, { (float) (30 + rnd.nextInt (60)), 0.1f, 0.25f } }, 0.4f);
            float pk; ok = finite (b, pk); worst = juce::jmax (worst, pk); if (pk < 1.0e-5f) ++silent;
            r.p.initPreset();
        }
        check (ok && worst <= 4.0f, "60 rounds of random settings: no NaN, worst peak " + juce::String (worst, 2) + ", silent rounds " + juce::String (silent));
    }
    { // resynthesis: analyse a made-up sound and play it back at the root key and an octave up
        juce::AudioBuffer<float> src (1, (int) SR * 2);
        for (int i = 0; i < src.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) SR;
            src.setSample (0, i, 0.4f * std::sin (kTwoPi * 330.0f * t) + 0.2f * std::sin (kTwoPi * 1245.0f * t) + 0.1f * std::sin (kTwoPi * 3301.0f * t));
        }
        for (int hp = 0; hp < 2; ++hp)
        {
            Rig r; r.setP (0, PP::velVol, 0);
            auto img = ImageData::fromAudio (src, SR, hp == 1, 0);
            check (img != nullptr && img->valid(), "analysis made an image " + (img ? juce::String (img->w) + " x " + juce::String (img->h) + ", grid " + juce::String (img->baseFreq, 2) + " Hz" : juce::String()));
            if (img == nullptr) continue;
            r.p.setImage (0, img);
            auto b = r.render ({ { 60, 0.0f, 1.5f } }, 1.6f);
            auto s = spectrum (b, 20000); float m1, m2, m3;
            const float f1 = peakHz (s, 250, 400, &m1), f2 = peakHz (s, 1100, 1400, &m2), f3 = peakHz (s, 3100, 3500, &m3);
            check (std::abs (f1 - 330.0f) < 1.5f && std::abs (f2 - 1245.0f) < 3.0f && std::abs (f3 - 3301.0f) < 6.0f,
                   juce::String (hp ? "high precision" : "generic") + " resynthesis: " + juce::String (f1, 1) + " " + juce::String (f2, 1) + " " + juce::String (f3, 1) + " Hz, levels " + juce::String (m1 * 4, 3) + " " + juce::String (m2 * 4, 3) + " " + juce::String (m3 * 4, 3));
            auto b2 = r.render ({ { 72, 0.0f, 1.0f } }, 1.2f);
            auto s2 = spectrum (b2, 12000);
            const float g1 = peakHz (s2, 500, 800);
            check (std::abs (g1 - 660.0f) < 3.0f, "an octave up plays " + juce::String (g1, 1) + " Hz");
            if (wavs && hp == 0) writeWav (b, "resynth.wav");
        }
    }
    { // state round trip
        Rig r;
        r.setP (0, PP::f1Freq, 0.37f); r.set (GP::rvOn, 1);
        r.p.edit (1).shape[SH::GlobalEQ].line ({ { 0, 0.2f }, { 0.5f, 0.9f }, { 1, 0.4f } });
        r.p.edit (1).tg[TG::Pitch].lfo.on = true; r.p.edit (1).image = ImageData::randomCloud (5); r.p.commit (1);
        int fo[5] = { 4, 3, 2, 1, 0 }; r.p.setFxOrder (fo);
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        Rig q; q.p.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool ok = std::abs (q.p.partValue (0, PP::f1Freq) - 0.37f) < 1.0e-4f && q.p.globalValue (GP::rvOn) > 0.5f && q.p.edit (1).shape[SH::GlobalEQ].pts.size() == 3
                        && q.p.edit (1).tg[TG::Pitch].lfo.on && q.p.edit (1).image != nullptr && q.p.edit (1).image->w == 256 && q.p.fxOrder[0] == 4;
        check (ok, "state round trip, " + juce::String ((int) mb.getSize() / 1024) + " KB");
    }
    std::cout << (fails == 0 ? "ALL OK" : juce::String (fails) + " FAILED") << std::endl;
    return fails;
}
