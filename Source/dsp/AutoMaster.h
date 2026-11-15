#pragma once
// ms0 "auto": listen to the track for a while, then set the master once.
//
// While listening (audio thread, cheap): BS.1770 loudness + range, true peak, peak / RMS,
// left / right balance and correlation, and a filter bank on mid and side that gives the
// energy of ten regions (subsonic .. air) plus the four multiband bands (level and peak).
// Silence (below -60 dBFS) is not counted, so pauses in the track don't skew anything.
//
// Then decide() (any thread, pure): compares the track's tonal balance with a target curve
// (a gently sloping "finished master" shape, per octave), its dynamics with three kinds of
// track (dynamic / moderate / dense), and its stereo picture; returns parameter values and a
// one-line summary. Corrections are deliberately half of what the measurement asks for and
// never more than 3 dB: auto gets you a sound starting point, your ears finish it.
#include "Master.h"
#include <vector>
#include <utility>

namespace fal
{
struct AutoMeasures
{
    double seconds = 0;
    float integrated = -100, lra = 0, truePeak = -100, peak = -100, rms = -100, corr = 1, balanceDb = 0;
    // ten regions, energy per octave in dB (mid and side), and each region's RMS in dBFS (mid / side)
    std::array<float, 10> midPerOct {}, sidePerOct {}, midDbfs {}, sideDbfs {};
    // the four multiband bands (80 / 350 / 4000 Hz): RMS and peak in dBFS of the mid+side sum
    std::array<float, 4> bandRms {}, bandPeak {};
};

struct AutoDecision
{
    std::vector<std::pair<juce::String, float>> params;
    juce::String summary;
};

class AutoListen
{
public:
    static constexpr int kRegions = 10;
    static constexpr float kEdges[kRegions + 1] = { 20, 40, 80, 160, 350, 700, 1500, 4000, 8000, 16000, 20000 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int r = 0; r < kRegions; ++r)
        {
            const double lo = kEdges[r], hi = std::min ((double) kEdges[r + 1], sr * 0.45), f0 = std::sqrt (lo * hi);
            regionOk[(size_t) r] = hi > lo * 1.2;
            const double bw = std::log2 (hi / lo);
            // RBJ band-pass (0 dB peak), Q from the band's width in octaves
            const double w0 = 2 * kPi * f0 / sr, alpha = std::sin (w0) * std::sinh (std::log (2.0) / 2 * bw * w0 / std::sin (w0));
            for (auto* bq : { &bm[(size_t) r][0], &bm[(size_t) r][1], &bs[(size_t) r][0], &bs[(size_t) r][1] })
                bq->norm ((float) alpha, 0.0f, (float) -alpha, (float) (1 + alpha), (float) (-2 * std::cos (w0)), (float) (1 - alpha));
        }
        lu.prepare (sr);
        reset();
    }

    void start (double seconds)
    {
        want = seconds;
        reset();
        state.store (1);
    }
    void stop() { if (state.load() == 1) state.store (counted > sr * 4 ? 2 : 0); }   // stopped early: use it if there are 4 s
    void cancel() { state.store (0); }
    bool listening() const { return state.load() == 1; }
    bool ready() const { return state.load() == 2; }
    void consumed() { state.store (0); }
    float progress() const { return want > 0 ? (float) std::min (1.0, counted / (sr * want)) : 0.0f; }
    double heardSeconds() const { return counted / sr; }

    /** audio thread: the input as the chain sees it (after trim) */
    void push (const float* L, const float* R, int n, float trimGain)
    {
        if (state.load() != 1) return;
        double blk = 0;
        for (int i = 0; i < n; ++i) blk += (double) L[i] * L[i] + (double) R[i] * R[i];
        if (n > 0 && gainToDb ((float) std::sqrt (blk / (2.0 * n)) * trimGain) < -60.0f) return;   // silence: not counted
        for (int off = 0; off < n; off += 512)
        {
            const int m = std::min (512, n - off);
            float l[512], r[512];
            for (int i = 0; i < m; ++i) { l[i] = L[off + i] * trimGain; r[i] = R[off + i] * trimGain; }
            lu.process (l, r, m);
            for (int i = 0; i < m; ++i)
            {
                const float a = l[i], b = r[i];
                tp = std::max (tp, std::max (tpl.push (a), tpr.push (b)));
                pk = std::max (pk, std::max (std::abs (a), std::abs (b)));
                sll += (double) a * a; srr += (double) b * b; slr += (double) a * b;
                const float mid = 0.5f * (a + b), side = 0.5f * (a - b);
                float band[4] = { 0, 0, 0, 0 };
                for (int k = 0; k < kRegions; ++k)
                {
                    if (! regionOk[(size_t) k]) continue;
                    const float ym = bm[(size_t) k][1].tick (bm[(size_t) k][0].tick (mid));
                    const float ys = bs[(size_t) k][1].tick (bs[(size_t) k][0].tick (side));
                    em[(size_t) k] += (double) ym * ym; es[(size_t) k] += (double) ys * ys;
                    band[bandOf (k)] += ym + ys;
                }
                for (int q = 0; q < 4; ++q) { eb[(size_t) q] += (double) band[q] * band[q]; pb[(size_t) q] = std::max (pb[(size_t) q], std::abs (band[q])); }
            }
            counted += m;
        }
        if (counted >= sr * want) state.store (2);
    }

    /** after ready(): what was heard */
    AutoMeasures measures() const
    {
        AutoMeasures m;
        const double n = std::max (1.0, counted);
        m.seconds = counted / sr;
        m.integrated = lu.integrated.load(); m.lra = lu.range.load();
        m.truePeak = gainToDb (tp); m.peak = gainToDb (pk);
        m.rms = gainToDb ((float) std::sqrt ((sll + srr) / (2 * n)));
        m.corr = (float) (slr / std::sqrt (std::max (1e-20, sll * srr)));
        m.balanceDb = (float) (10 * std::log10 (std::max (1e-20, sll) / std::max (1e-20, srr)));
        for (int k = 0; k < kRegions; ++k)
        {
            const double oct = std::log2 ((double) kEdges[k + 1] / kEdges[k]);
            auto db = [] (double e) { return (float) (10 * std::log10 (std::max (1e-20, e))); };
            m.midPerOct[(size_t) k] = db (em[(size_t) k] / n / oct);
            m.sidePerOct[(size_t) k] = db (es[(size_t) k] / n / oct);
            m.midDbfs[(size_t) k] = db (em[(size_t) k] / n);
            m.sideDbfs[(size_t) k] = db (es[(size_t) k] / n);
        }
        for (int q = 0; q < 4; ++q) { m.bandRms[(size_t) q] = gainToDb ((float) std::sqrt (eb[(size_t) q] / n)); m.bandPeak[(size_t) q] = gainToDb (pb[(size_t) q]); }
        return m;
    }

    std::atomic<int> state { 0 };     // 0 idle, 1 listening, 2 heard enough

private:
    static int bandOf (int region) { return region <= 1 ? 0 : (region <= 3 ? 1 : (region <= 6 ? 2 : 3)); }   // 80 / 350 / 4000 Hz
    void reset()
    {
        for (auto& p : bm) for (auto& b : p) b.reset();
        for (auto& p : bs) for (auto& b : p) b.reset();
        em.fill (0); es.fill (0); eb.fill (0); pb.fill (0);
        sll = srr = slr = 0; tp = pk = 0; counted = 0;
        lu.reset();
        tpl = TruePeak(); tpr = TruePeak();
    }
    double sr = 48000, want = 20, counted = 0;
    std::array<std::array<Biquad, 2>, kRegions> bm, bs;      // two in series: a steeper band
    std::array<bool, kRegions> regionOk {};
    std::array<double, kRegions> em {}, es {};
    std::array<double, 4> eb {}; std::array<float, 4> pb {};
    double sll = 0, srr = 0, slr = 0; float tp = 0, pk = 0;
    Loudness lu; TruePeak tpl, tpr;
};

/** The settings for what was heard. Every value set here is relative to ms0's defaults. */
inline AutoDecision decideAuto (const AutoMeasures& m)
{
    AutoDecision d;
    auto set = [&] (const char* id, float v) { d.params.push_back ({ id, v }); };
    juce::StringArray notes;
    auto r05 = [] (float v) { return std::round (v * 2.0f) / 2.0f; };

    // ---- tonal balance: per octave, relative to the mid region (700 - 1500 Hz) -------------------
    // the target: a finished master's gentle slope -- fuller lows, softer highs, per octave
    static const float target[10] = { -6.0f, 5.0f, 4.0f, 2.0f, 0.0f, 0.0f, -2.5f, -5.5f, -9.0f, -14.0f };
    // each region against the target, then the average of that over 40 Hz - 16 kHz taken out:
    // so a dip in any one region can't make all the others look wrong
    std::array<float, 10> dev {};
    float mean = 0;
    for (int k = 0; k < 10; ++k)
    {
        const float tot = 10 * std::log10 (std::pow (10.0f, m.midPerOct[(size_t) k] / 10) + std::pow (10.0f, m.sidePerOct[(size_t) k] / 10));
        dev[(size_t) k] = tot - target[k];
        if (k >= 1 && k <= 8) mean += dev[(size_t) k] / 8.0f;
    }
    for (auto& v : dev) v -= mean;
    // region -> eq band: (band, type, freq, q)   type 0 bell, 1 low shelf, 2 high shelf
    struct Move { int region, band, type; float freq, q; const char* name; };
    static const Move moves[] = { { 1, 1, 1, 70.0f, 0.7f, "lows" }, { 2, 2, 0, 120.0f, 0.9f, "body" }, { 3, 3, 0, 250.0f, 0.9f, "low-mids" },
                                  { 4, 4, 0, 500.0f, 1.0f, "box" }, { 6, 6, 0, 2800.0f, 0.8f, "presence" }, { 7, 7, 0, 6000.0f, 0.8f, "detail" },
                                  { 8, 8, 2, 10000.0f, 0.7f, "air" } };
    float eqNet = 0;
    for (auto& mv : moves)
    {
        const float dv = dev[(size_t) mv.region];
        if (std::abs (dv) < 1.5f) continue;                              // close enough: leave it alone
        if (mv.region == 6 && dv > 1.0f) continue;                       // too much presence: the dynamic cut below handles it
        const float gain = r05 (juce::jlimit (-3.0f, 3.0f, -dv * 0.5f));  // half the way, at most 3 dB
        if (std::abs (gain) < 0.5f) continue;
        const juce::String x = "e" + juce::String (mv.band);
        set ((x + "on").toRawUTF8(), 1); set ((x + "type").toRawUTF8(), (float) mv.type); set ((x + "freq").toRawUTF8(), mv.freq);
        set ((x + "gain").toRawUTF8(), gain); set ((x + "q").toRawUTF8(), mv.q); set ((x + "ch").toRawUTF8(), 0);
        notes.add (juce::String (mv.name) + " " + (gain > 0 ? "+" : "") + juce::String (gain, 1));
        eqNet += gain * (mv.region >= 1 && mv.region <= 4 ? 0.35f : 0.15f);   // what it does to loudness, roughly
    }

    // ---- the very bottom: rumble, and a mono sub ----------------------------------------------
    const float rumble = m.midPerOct[0] - m.midPerOct[1];                 // 20-40 Hz against 40-80 Hz
    set ("hpon", 1);
    if (rumble > -8.0f) { set ("hpfreq", 28); set ("hpslope", 1); notes.add ("rumble: high-pass 28 hz"); }
    else { set ("hpfreq", 18); set ("hpslope", 0); }
    const float sideLow = std::max (m.sidePerOct[1], m.sidePerOct[2]) - std::max (m.midPerOct[1], m.midPerOct[2]);
    if (sideLow > -14.0f) { set ("x1", 120); notes.add ("wide lows: mono below 120 hz"); }
    else set ("x1", 90);
    set ("b1width", 0);

    // ---- stereo -------------------------------------------------------------------------------
    if (m.corr < 0.25f) { set ("b2width", 0.8f); set ("b3width", 0.92f); set ("b4width", 1.0f); notes.add ("very wide (corr " + juce::String (m.corr, 2) + "): narrowed"); }
    else if (m.corr > 0.85f) { set ("b2width", 0.9f); set ("b3width", 1.1f); set ("b4width", 1.25f); set ("air", 0.2f); notes.add ("narrow (corr " + juce::String (m.corr, 2) + "): widened the top"); }
    else { set ("b2width", 0.85f); set ("b3width", 1.0f); set ("b4width", 1.1f); }

    // side low-mid mud: a dynamic cut on the side only, where it builds up
    const float sideMud = m.sideDbfs[3] - m.midDbfs[3];
    if (sideMud > -9.0f && m.sideDbfs[3] > -45.0f && m.corr < 0.9f)       // only where there is real side content
    {
        set ("dynon", 1); set ("d2on", 1); set ("d2ch", 2); set ("d2freq", 300); set ("d2q", 1.0f);
        set ("d2thr", juce::jlimit (-60.0f, 0.0f, m.sideDbfs[3] + 3.0f)); set ("d2range", -3); set ("d2att", 10); set ("d2rel", 150);
        notes.add ("side mud: dynamic cut 300 hz (side)");
    }
    // harshness: a dynamic cut on the mid where presence pokes out
    if (dev[6] > 1.0f)
    {
        set ("dynon", 1); set ("d1on", 1); set ("d1ch", 1); set ("d1freq", 3000); set ("d1q", 1.2f);
        set ("d1thr", juce::jlimit (-60.0f, 0.0f, m.midDbfs[6] + 4.0f)); set ("d1range", -2.5f); set ("d1att", 5); set ("d1rel", 100);
        notes.add ("harsh: dynamic cut 3 khz (mid)");
    }
    else if (dev[4] > 1.5f)
    {
        set ("dynon", 1); set ("d1on", 1); set ("d1ch", 1); set ("d1freq", 450); set ("d1q", 1.0f);
        set ("d1thr", juce::jlimit (-60.0f, 0.0f, m.midDbfs[4] + 3.0f)); set ("d1range", -2.5f);
        notes.add ("boxy: dynamic cut 450 hz (mid)");
    }

    // ---- dynamics: what kind of track is it? ----------------------------------------------------
    const float crest = m.peak - m.rms;
    const int kind = (m.lra > 9.0f || crest > 17.0f) ? 0 : ((m.lra < 5.0f && crest < 12.0f) ? 2 : 1);   // dynamic / moderate / dense
    static const char* kindName[3] = { "dynamic", "moderate", "dense" };
    static const int targetIndex[3] = { 7, 5, 3 };          // -16 / -12 / -10 lufs (the "target" choices)
    static const float targetLufsOf[3] = { -16.0f, -12.0f, -10.0f };
    // never quieter than the track already is: the master raises loudness, it doesn't turn a mix down
    static const float choices[10] = { 0, -8, -9, -10, -11, -12, -14, -16, -18, -23 };
    float tLufs = targetLufsOf[kind];
    int tIndex = targetIndex[kind];
    bool alreadyLoud = false;
    if (m.integrated > tLufs + 1.0f)
    {
        alreadyLoud = true;
        for (int c = 9; c >= 1; --c) if (choices[c] >= m.integrated - 0.5f) { tIndex = c; tLufs = choices[c]; break; }
        if (choices[tIndex] < m.integrated - 0.5f) { tIndex = 1; tLufs = -8.0f; }
    }
    static const float ratio[3] = { 1.15f, 1.3f, 1.5f }, above[3] = { 6.0f, 4.0f, 3.0f };
    set ("mbon", 1);
    for (int q = 0; q < 4; ++q)
    {
        const juce::String x = "b" + juce::String (q + 1);
        // just above each band's average, so only its peaks are touched
        set ((x + "thr").toRawUTF8(), juce::jlimit (-60.0f, 0.0f, m.bandRms[(size_t) q] + above[kind] + (m.bandPeak[(size_t) q] - m.bandRms[(size_t) q]) * 0.15f));
        set ((x + "ratio").toRawUTF8(), ratio[kind] - (q == 3 ? 0.1f : 0.0f));
    }
    if (kind == 0) set ("glueon", 0);
    else
    {
        set ("glueon", 1); set ("gthr", juce::jlimit (-40.0f, 0.0f, m.rms + (kind == 2 ? 4.0f : 6.0f)));
        set ("gratio", kind == 2 ? 1.6f : 1.3f); set ("gmix", kind == 2 ? 1.0f : 0.7f); set ("gauto", 1);
    }
    // shave the sharpest transients before the limiter when there's a long way to go up
    const float toGo = tLufs - m.integrated - eqNet;
    if (crest > 14.0f && toGo > 3.0f && kind > 0)
    {
        set ("clipon", 1); set ("clipdrive", juce::jlimit (0.5f, 2.5f, (crest - 12.0f) * 0.35f)); set ("clipsoft", 0.6f);
        notes.add ("sharp peaks: clipper " + juce::String (juce::jlimit (0.5f, 2.5f, (crest - 12.0f) * 0.35f), 1) + " db");
    }
    else set ("clipon", 0);
    // dull top: a little saturation on the highs only
    if (dev[7] < -2.5f && dev[8] < -3.0f)
    {
        set ("saton", 1); set ("satcurve", 0); set ("satfocus", 2); set ("satdrive", 2.0f); set ("satmix", 0.35f);
        notes.add ("dull top: tape on the highs");
    }
    else set ("saton", 0);
    set ("ambon", 0);

    // ---- loudness: limiter gain toward the target, true-peak ceiling ----------------------------
    set ("limon", 1); set ("target", (float) tIndex);
    if (alreadyLoud) notes.add ("already loud: kept its level" + juce::String (m.truePeak > -1.0f ? ", the limiter catches its peaks" : ""));
    set ("limgain", r05 (juce::jlimit (0.0f, 12.0f, toGo)));
    set ("ceiling", -1.0f); set ("limos", 2); set ("limauto", 1);
    set ("dcon", 1);

    d.summary = "auto (" + juce::String (m.seconds, 0) + " s heard): " + juce::String (kindName[kind]) + " track, lra " + juce::String (m.lra, 1)
              + ", crest " + juce::String (crest, 1) + " db, " + juce::String (m.integrated, 1) + " lufs in -> target " + juce::String (tLufs, 0)
              + " lufs" + (notes.isEmpty() ? juce::String() : ". " + notes.joinIntoString (", "));
    return d;
}
} // namespace fal
