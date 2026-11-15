// Offline checks for the Partials engine: pitch, level, stability under random settings, speed, resynthesis, state.
#include "../Source/PluginProcessor.h"
using namespace pt;

static const double SR = 48000.0;
struct Rig
{
    PartialsProcessor p;
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
    fig ("env_filter", [] (Rig& r) { SP (f1Type, 1); SP (f1Freq, 0.25f); SP (f1Res, 0.5f); auto& e = r.p.edit (0).tg[TG::F1Freq].env; e.on = true; e.adsr (0.0f, 0.6f, 0.0f, 0.3f); r.p.commit (0); }, nullptr, { { 45, 0.05f, 1.15f }, { 45, 1.25f, 2.4f } });
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool wavs = argc > 1 && juce::String (argv[1]) == "--wav";
    if (argc > 2 && juce::String (argv[1]) == "--figs") { makeFigures (juce::File (argv[2])); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--shot")
    {
        // picture of the window, with a note sounding so the display has something to show
        Rig r;
        const int tab = argc > 3 ? juce::String (argv[3]).getIntValue() : 0;
        if (tab == 1) r.p.setImage (0, ImageData::randomCloud (3));
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
        ed->setSize (1440, 900);
        if (tab > 0) if (auto* root = ed->getChildComponent (0)) for (auto* c : root->getChildren()) if (auto* b = c) if (b->getBounds() == juce::Rectangle<int> (20 + tab * 49, 592, 50, 20)) b->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0, 0, 0, 0, b, b, juce::Time(), {}, juce::Time(), 1, false));
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
