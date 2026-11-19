// faltung sndstudio — the spatial devices: an ambisonic encoder (mono / stereo → order 1 … 7), a speaker decoder
// (ambisonics → a ring, dome or sphere of as many speakers as the track is wide) and a binaural decoder (→ 2).
// Ambisonic channels are ACN ordered, SN3D normalised (the ambiX convention the IEM and SPARTA plug-ins use).
// Included by StudioDevices.cpp (after StudioFx).

namespace spatial
{
constexpr int kMaxOrder = 7, kMaxSh = 64;
inline int shCount (int order) { return (order + 1) * (order + 1); }
/** the highest order whose channel count fits into n channels (0 when n < 4) */
inline int orderFor (int channels) { int o = 0; while (o < kMaxOrder && shCount (o + 1) <= channels) ++o; return o; }

/** real spherical harmonics up to `order` for a direction (azimuth to the left, elevation up, radians): ACN / SN3D */
inline void sh (int order, float az, float el, float* y)
{
    const float x = std::sin (el), c = std::cos (el);
    float p[kMaxOrder + 1][kMaxOrder + 1] {};   // associated legendre P_n^m (x), no condon-shortley phase
    float pmm = 1.0f;
    for (int m = 0; m <= order; ++m)
    {
        if (m > 0) pmm *= (float) (2 * m - 1) * c;
        p[m][m] = pmm;
        if (m + 1 <= order) p[m + 1][m] = x * (float) (2 * m + 1) * pmm;
        for (int n = m + 2; n <= order; ++n) p[n][m] = ((float) (2 * n - 1) * x * p[n - 1][m] - (float) (n + m - 1) * p[n - 2][m]) / (float) (n - m);
    }
    for (int n = 0; n <= order; ++n)
        for (int m = 0; m <= n; ++m)
        {
            double f = 1.0; for (int k = n - m + 1; k <= n + m; ++k) f *= (double) k;   // (n+m)! / (n-m)!
            const float norm = (float) std::sqrt ((m == 0 ? 1.0 : 2.0) / f);
            const float v = norm * p[n][m];
            y[n * n + n + m] = v * std::cos ((float) m * az);
            if (m > 0) y[n * n + n - m] = v * std::sin ((float) m * az);
        }
}

/** max-rE weights per order (smooths the side lobes of a basic decoder) */
inline void maxRe (int order, float* w)
{
    const float x = std::cos (2.4068f / ((float) order + 1.51f));   // 137.9° / (N + 1.51)
    float p0 = 1.0f, p1 = x;
    for (int n = 0; n <= order; ++n)
    {
        w[n] = n == 0 ? 1.0f : n == 1 ? x : 0.0f;
        if (n >= 2) { const float pn = ((float) (2 * n - 1) * x * p1 - (float) (n - 1) * p0) / (float) n; p0 = p1; p1 = pn; w[n] = pn; }
    }
}

struct Dir { float az = 0, el = 0; };

/** speaker positions for a layout: 0 ring (all at ear height), 1 dome (rings and a top), 2 sphere (all around) */
inline std::vector<Dir> speakers (int count, int layout, float rotate, bool clockwise, bool pairAtFront)
{
    std::vector<Dir> v;
    const float two = juce::MathConstants<float>::twoPi, sign = clockwise ? -1.0f : 1.0f;
    auto ring = [&] (int n, float el, float off) { for (int k = 0; k < n; ++k) v.push_back ({ sign * (two * ((float) k + off) / (float) n) + rotate, el }); };
    const float front = pairAtFront ? 0.5f : 0.0f;
    if (layout == 0 || count < 6) ring (count, 0.0f, front);
    else if (layout == 1)
    {
        // half at ear height, a third up at 35°, the rest at 65°, a single speaker at the top when the count is odd
        const int top = count % 2;
        const int low = (count - top + 1) / 2, mid = (count - top - low) * 2 / 3, high = count - top - low - mid;
        ring (low, 0.0f, front); ring (mid, 0.61f, front);
        if (high > 0) ring (high, 1.13f, front);
        if (top) v.push_back ({ 0.0f, 1.5707963f });
    }
    else
        for (int k = 0; k < count; ++k)   // a fibonacci sphere
        {
            const float z = 1.0f - 2.0f * ((float) k + 0.5f) / (float) count;
            v.push_back ({ sign * (float) k * 2.3999632f + rotate, std::asin (z) });
        }
    return v;
}

/** a sampling decoder with max-rE weights for `nsh` ambisonic channels → the speakers; rows: speakers, columns: sh.
    Normalised so a source in a speaker's direction comes out at unity power over all speakers. */
inline void decoderMatrix (const std::vector<Dir>& spk, int order, std::vector<float>& m)
{
    const int nsh = shCount (order), L = (int) spk.size();
    m.assign ((size_t) (L * kMaxSh), 0.0f);
    float w[kMaxOrder + 1]; maxRe (order, w);
    for (int s = 0; s < L; ++s)
    {
        float y[kMaxSh]; sh (order, spk[(size_t) s].az, spk[(size_t) s].el, y);
        for (int n = 0; n <= order; ++n) for (int k = n * n; k < (n + 1) * (n + 1); ++k) m[(size_t) (s * kMaxSh + k)] = (float) (2 * n + 1) * w[n] * y[k];
    }
    if (L == 0) return;
    // a plane wave from the first speaker's direction, decoded: its power over the speakers becomes 1
    float y0[kMaxSh]; sh (order, spk[0].az, spk[0].el, y0);
    double e = 0;
    for (int s = 0; s < L; ++s) { double g = 0; for (int k = 0; k < nsh; ++k) g += m[(size_t) (s * kMaxSh + k)] * y0[k]; e += g * g; }
    const float norm = e > 1.0e-12 ? (float) (1.0 / std::sqrt (e)) : 1.0f;
    for (auto& x : m) x *= norm;
}
} // namespace spatial

// ---- encoder: mono / stereo → ambisonics ---------------------------------------------------------------------
class EncoderFx : public StudioFx, public WidthNegotiator
{
public:
    enum { AZ, EL, WIDTH, ORDER, GAIN };
    EncoderFx() : StudioFx ("st:encoder", "encoder", 2, 16, false, {
        F ("azimuth", "azimuth", -180, 180, 0, [] (float v) { const int d = juce::roundToInt (v); return d == 0 ? juce::String ("front") : juce::String (std::abs (d)) + juce::String::fromUTF8 ("°") + (d > 0 ? " left" : " right"); }),
        F ("elevation", "elevation", -90, 90, 0, [] (float v) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("°"); }),
        F ("width", "stereo width", 0, 180, 60, [] (float v) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("°"); }),
        C ("order", "order", { "as wide as the track", "1 (4 ch)", "2 (9 ch)", "3 (16 ch)", "4 (25 ch)", "5 (36 ch)", "6 (49 ch)", "7 (64 ch)" }, 0),
        F ("gain", "gain", -24, 12, 0, dbText) }) {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const int i = l.getMainInputChannelSet().size(), o = l.getMainOutputChannelSet().size();
        return (i == 1 || i == 2) && o >= 4 && o <= 64;
    }
    std::pair<int, int> widthsFor (int incoming, int trackWidth) const override
    {
        const int order = std::max (1, spatial::orderFor (trackWidth));
        return { incoming <= 1 ? 1 : 2, spatial::shCount (order) };
    }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; first = true; }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
        if (buf.getNumChannels() < outs || n <= 0) return;
        int order = spatial::orderFor (outs);
        if (Pi (ORDER) > 0) order = std::min (order, Pi (ORDER));
        const int nsh = spatial::shCount (order);
        const float deg = juce::MathConstants<float>::pi / 180.0f;
        const float az = P (AZ) * deg, el = P (EL) * deg, half = ins >= 2 ? P (WIDTH) * 0.5f * deg : 0.0f, g = dbToGain (P (GAIN));
        float yl[spatial::kMaxSh] {}, yr[spatial::kMaxSh] {};
        spatial::sh (order, az + half, el, yl); spatial::sh (order, az - half, el, yr);
        for (int k = 0; k < nsh; ++k) { yl[k] *= g; yr[k] *= g; }
        if (first) { for (int k = 0; k < spatial::kMaxSh; ++k) { gl[k] = yl[k]; gr[k] = yr[k]; } first = false; }
        if ((int) inL.size() < n) { inL.resize ((size_t) n); inR.resize ((size_t) n); }
        std::copy_n (buf.getReadPointer (0), n, inL.begin());
        if (ins >= 2) std::copy_n (buf.getReadPointer (1), n, inR.begin()); else std::fill_n (inR.begin(), n, 0.0f);
        const float inv = 1.0f / (float) n;
        for (int k = 0; k < outs; ++k)
        {
            auto* d = buf.getWritePointer (k);
            if (k >= nsh) { juce::FloatVectorOperations::clear (d, n); gl[k] = gr[k] = 0.0f; continue; }
            // the gains ramp across the block, so a moving source does not click
            const float l0 = gl[k], r0 = gr[k], dl = (yl[k] - l0) * inv, dr = (yr[k] - r0) * inv;
            for (int i = 0; i < n; ++i) d[i] = inL[(size_t) i] * (l0 + dl * (float) i) + inR[(size_t) i] * (r0 + dr * (float) i);
            gl[k] = yl[k]; gr[k] = yr[k];
        }
    }
    float gl[spatial::kMaxSh] {}, gr[spatial::kMaxSh] {}; bool first = true;
    std::vector<float> inL, inR;
};

// ---- decoder: ambisonics → speakers ----------------------------------------------------------------------------
class DecoderFx : public StudioFx, public WidthNegotiator
{
public:
    enum { LAYOUT, ROTATE, DIRECTION, FRONT, GAIN };
    DecoderFx() : StudioFx ("st:decoder", "decoder", 16, 8, false, {
        C ("layout", "speakers", { "ring", "dome", "sphere" }, 0),
        F ("rotate", "rotate", -180, 180, 0, [] (float v) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("°"); }),
        C ("direction", "numbering", { "anticlockwise", "clockwise" }, 1),
        C ("front", "front", { "a speaker at the front", "a pair around the front" }, 0),
        F ("gain", "gain", -24, 12, 0, dbText) }) {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const int i = l.getMainInputChannelSet().size(), o = l.getMainOutputChannelSet().size();
        return i >= 4 && i <= 64 && o >= 1 && o <= 64;
    }
    std::pair<int, int> widthsFor (int incoming, int trackWidth) const override
    {
        const int order = std::max (1, spatial::orderFor (incoming >= 4 ? incoming : trackWidth));
        return { spatial::shCount (order), juce::jlimit (1, 64, trackWidth) };
    }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; key = -1; in.setSize (spatial::kMaxSh, b, false, false, true); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
        if (buf.getNumChannels() < std::max (ins, outs) || n <= 0) return;
        const int order = std::max (1, spatial::orderFor (ins)), nsh = spatial::shCount (order);
        const int k = Pi (LAYOUT) * 1000003 + juce::roundToInt (P (ROTATE) * 10.0f) * 4 + Pi (DIRECTION) * 2 + Pi (FRONT) + outs * 7919 + order * 104729;
        if (k != key)
        {
            key = k;
            spatial::decoderMatrix (spatial::speakers (outs, Pi (LAYOUT), P (ROTATE) * juce::MathConstants<float>::pi / 180.0f, Pi (DIRECTION) == 1, Pi (FRONT) == 1), order, matrix);
        }
        if (in.getNumSamples() < n) in.setSize (spatial::kMaxSh, n, false, false, true);
        for (int c = 0; c < nsh; ++c) in.copyFrom (c, 0, buf, c, 0, n);
        const float g = dbToGain (P (GAIN));
        for (int s = 0; s < outs; ++s)
        {
            auto* d = buf.getWritePointer (s);
            juce::FloatVectorOperations::clear (d, n);
            for (int c = 0; c < nsh; ++c) juce::FloatVectorOperations::addWithMultiply (d, in.getReadPointer (c), matrix[(size_t) (s * spatial::kMaxSh + c)] * g, n);
        }
        for (int c = outs; c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);
    }
    std::vector<float> matrix; int key = -1; juce::AudioBuffer<float> in;
};

// ---- binaural: ambisonics → headphones --------------------------------------------------------------------------
// decoded to sixteen virtual speakers around the head; each reaches the two ears with the delay and the head shadow
// of a rigid sphere (Woodworth's time difference, Brown and Duda's shelving filter). No measured ears: neutral, and
// good enough to place things while arranging on headphones.
class BinauralFx : public StudioFx, public WidthNegotiator
{
public:
    enum { YAW, SIZE, GAIN };
    static constexpr int kSpk = 16;
    BinauralFx() : StudioFx ("st:binaural", "binaural", 16, 2, false, {
        F ("yaw", "head turn", -180, 180, 0, [] (float v) { const int d = juce::roundToInt (v); return d == 0 ? juce::String ("ahead") : juce::String (std::abs (d)) + juce::String::fromUTF8 ("°") + (d > 0 ? " left" : " right"); }),
        F ("size", "head size", 0.8f, 1.2f, 1.0f, pctText),
        F ("gain", "gain", -24, 12, 0, dbText) }) {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const int i = l.getMainInputChannelSet().size(), o = l.getMainOutputChannelSet().size();
        return i >= 4 && i <= 64 && o == 2;
    }
    std::pair<int, int> widthsFor (int incoming, int trackWidth) const override
    {
        const int order = std::max (1, spatial::orderFor (incoming >= 4 ? incoming : trackWidth));
        return { spatial::shCount (order), 2 };
    }
    void prepareToPlay (double s, int b) override
    {
        sr = s; bs = b; key = -1e9f; sizeKey = -1;
        in.setSize (spatial::kMaxSh, b, false, false, true); feed.assign ((size_t) b, 0.0f);
        for (auto& e : ear) for (auto& x : e) x = Ear();
        for (auto& e : ear) for (auto& x : e) x.line.prepare ((int) (sr * 0.002) + 8);
    }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), ins = getTotalNumInputChannels();
        if (buf.getNumChannels() < std::max (ins, 2) || n <= 0) return;
        const int order = std::max (1, spatial::orderFor (ins)), nsh = spatial::shCount (order);
        const float yaw = P (YAW) * juce::MathConstants<float>::pi / 180.0f;
        if (std::abs (yaw - key) > 1.0e-4f || order != orderKey)
        {
            // turning the head is the same as turning the field the other way: the virtual speakers sample it rotated
            key = yaw; orderKey = order;
            auto spk = spatial::speakers (kSpk, 2, 0.0f, false, false);
            auto turned = spk; for (auto& d : turned) d.az += yaw;
            spatial::decoderMatrix (turned, order, matrix);
            dirs = spk; sizeKey = -1;
        }
        if (std::abs (P (SIZE) - sizeKey) > 1.0e-4f)
        {
            sizeKey = P (SIZE);
            const float a = 0.0875f * sizeKey, c = 343.0f, w0 = c / a;
            for (int s = 0; s < kSpk; ++s)
                for (int e = 0; e < 2; ++e)
                {
                    // the angle between the speaker and this ear's axis (left ear at +90°, right at -90°)
                    const float az = dirs[(size_t) s].az, el = dirs[(size_t) s].el;
                    const float cosT = juce::jlimit (-1.0f, 1.0f, std::cos (el) * std::sin (az) * (e == 0 ? 1.0f : -1.0f));
                    const float theta = std::acos (cosT);                    // 0 = straight at the ear, pi = opposite
                    auto& x = ear[(size_t) s][(size_t) e];
                    x.delay = (float) sr * (a / c) * (theta < 1.5707963f ? 1.0f - cosT : 1.0f + (theta - 1.5707963f));
                    // brown-duda head shadow: H(s) = (alpha s + 2 w0) / (s + 2 w0), alpha from 2 (facing) to 0.1 (shadowed)
                    const float alpha = 1.05f + 0.95f * std::cos (theta * 1.2f);
                    const float k2 = 2.0f * (float) sr, b0 = (alpha * k2 + 2.0f * w0) / (k2 + 2.0f * w0), b1 = (2.0f * w0 - alpha * k2) / (k2 + 2.0f * w0), a1 = (2.0f * w0 - k2) / (k2 + 2.0f * w0);
                    x.b0 = b0; x.b1 = b1; x.a1 = a1;
                }
        }
        if (in.getNumSamples() < n) in.setSize (spatial::kMaxSh, n, false, false, true);
        if ((int) feed.size() < n) feed.resize ((size_t) n);
        for (int c = 0; c < nsh; ++c) in.copyFrom (c, 0, buf, c, 0, n);
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        juce::FloatVectorOperations::clear (L, n); juce::FloatVectorOperations::clear (R, n);
        const float g = dbToGain (P (GAIN)) * 0.7f;
        for (int s = 0; s < kSpk; ++s)
        {
            juce::FloatVectorOperations::clear (feed.data(), n);
            for (int c = 0; c < nsh; ++c) juce::FloatVectorOperations::addWithMultiply (feed.data(), in.getReadPointer (c), matrix[(size_t) (s * spatial::kMaxSh + c)] * g, n);
            for (int e = 0; e < 2; ++e)
            {
                auto& x = ear[(size_t) s][(size_t) e]; auto* out = e == 0 ? L : R;
                for (int i = 0; i < n; ++i)
                {
                    x.line.push (feed[(size_t) i]);
                    const float d = x.line.read (1.0f + x.delay);
                    const float y = x.b0 * d + x.b1 * x.x1 - x.a1 * x.y1;
                    x.x1 = d; x.y1 = y;
                    out[i] += y;
                }
            }
        }
        for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);
    }
    struct Ear { DelayLine line; float delay = 0, b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0; };
    std::array<std::array<Ear, 2>, kSpk> ear;
    std::vector<spatial::Dir> dirs; std::vector<float> matrix, feed; juce::AudioBuffer<float> in;
    float key = -1e9f, sizeKey = -1; int orderKey = -1;
};

// ---- line: lines of sound through the speakers of a wide track -------------------------------------------------
// Each input channel is a line that travels along the track's channels, speaker to speaker: no ambisonics, the
// speakers themselves are the places. The second line runs against the first. Time is in seconds, or on the grid.
class LineFx : public StudioFx, public WidthNegotiator
{
public:
    enum { SYNC, DIVISION, TIME, REST, LEGATO, FIGURE, COUNTER, SIEVE, LEVEL };
    static const juce::StringArray& sieves() { static const juce::StringArray s { "every speaker", "2@0", "3@0 | 4@1", "5@0 | 7@3", "3@1 | 5@2 | 7@0" }; return s; }
    LineFx() : StudioFx ("st:line", "line", 2, 8, false, {
        B ("sync", "pass on the grid", false), C ("division", "pass", divNames(), 15),
        F ("time", "pass time", 0.05f, 60, 4, [] (float v) { return juce::String (v, v < 10 ? 2 : 1) + " s"; }, 0.35f),
        F ("rest", "rest", 0, 0.9f, 0, pctText), F ("legato", "legato", 0, 1, 0.5f, pctText),
        C ("figure", "figure", { "there and back", "round", "swinging", "at random" }, 0), B ("counter", "second line against the first", true),
        C ("sieve", "speakers", sieves(), 0), F ("level", "level", -24, 12, 0, dbText) }) {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const int i = l.getMainInputChannelSet().size(), o = l.getMainOutputChannelSet().size();
        return (i == 1 || i == 2) && o >= 2 && o <= 64;
    }
    std::pair<int, int> widthsFor (int incoming, int trackWidth) const override { return { incoming <= 1 ? 1 : 2, juce::jlimit (2, 64, trackWidth) }; }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; phase = 0; pass = 0; rng.seed (4409); from = 0; to = 1; }
    /** where line 1 is right now: the two speakers it sits between and how far it is towards the second (tests, displays) */
    std::atomic<float> shownPosition { 0.0f };
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
        if (buf.getNumChannels() < outs || n <= 0 || outs < 2) return;
        // the speakers the sieve leaves in, in order
        int spk[64]; int m = 0; const auto expr = sieves()[juce::jlimit (0, sieves().size() - 1, Pi (SIEVE))];
        for (int c = 0; c < outs; ++c) if (Pi (SIEVE) == 0 || fal::daw::tools::sieveHolds (expr, c)) spk[m++] = c;
        if (m < 2) { m = 0; for (int c = 0; c < outs; ++c) spk[m++] = c; }
        if ((int) in0.size() < n) { in0.resize ((size_t) n); in1.resize ((size_t) n); }
        std::copy_n (buf.getReadPointer (0), n, in0.begin());
        if (ins >= 2) std::copy_n (buf.getReadPointer (1), n, in1.begin()); else std::fill_n (in1.begin(), n, 0.0f);
        for (int c = 0; c < outs; ++c) buf.clear (c, 0, n);
        const double passSecs = Pb (SYNC) ? divisions()[(size_t) juce::jlimit (0, 18, Pi (DIVISION))].beats * 60.0 / bpm() : (double) P (TIME);
        const double inc = 1.0 / std::max (16.0, passSecs * sr);
        const float rest = P (REST), w = std::max (0.03f, P (LEGATO)), g = dbToGain (P (LEVEL)); const int figure = Pi (FIGURE); const bool counter = Pb (COUNTER);
        const bool closed = figure == 1;
        auto place = [&] (float x, const float* src, int i)
        {
            // between two neighbouring speakers: equal power, the change-over as sudden or as slow as legato says
            const int steps = closed ? m : m - 1;
            x = closed ? x - std::floor (x / (float) m) * (float) m : juce::jlimit (0.0f, (float) (m - 1), x);
            int a = juce::jlimit (0, steps - 1 + (closed ? 0 : 0), (int) x); if (! closed && a > m - 2) a = m - 2;
            const float f = juce::jlimit (0.0f, 1.0f, x - (float) a);
            float t = juce::jlimit (0.0f, 1.0f, (f - (0.5f - w * 0.5f)) / w); t = t * t * (3.0f - 2.0f * t);
            const int b = closed ? (a + 1) % m : a + 1;
            buf.getWritePointer (spk[a])[i] += src[i] * std::cos (t * 1.5707963f) * g;
            buf.getWritePointer (spk[b])[i] += src[i] * std::sin (t * 1.5707963f) * g;
        };
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            if (phase >= 1.0) { phase -= 1.0; ++pass; from = to; if (figure == 3) { to = (int) (rng.uni() * (float) m) % m; if (to == from) to = (to + 1) % m; } }
            const float u = juce::jlimit (0.0f, 1.0f, (float) phase / std::max (0.05f, 1.0f - rest));   // moves, then rests where it arrived
            float x;
            if (figure == 1) x = u * (float) m;                                                          // round: once round the speakers in a pass
            else if (figure == 3) x = (float) from + ((float) to - (float) from) * (u * u * (3.0f - 2.0f * u));
            else { const float e = figure == 2 ? 0.5f - 0.5f * std::cos (u * 3.1415927f) : u; x = ((pass & 1) ? 1.0f - e : e) * (float) (m - 1); }
            if (figure == 3 && ! closed) x = juce::jlimit (0.0f, (float) (m - 1), x);
            place (x, in0.data(), i);
            if (ins >= 2) place (counter ? (closed ? (float) m - x : (float) (m - 1) - x) : x, in1.data(), i);
            if (i == n - 1) shownPosition.store (closed ? x - std::floor (x / (float) m) * (float) m : x);
        }
    }
    double phase = 0; long long pass = 0; int from = 0, to = 1; Rng rng; std::vector<float> in0, in1;
};
