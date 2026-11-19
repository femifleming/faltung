// Partials: the additive engine. Every voice is a list of up to 516 sine partials that is shaped
// (filters, blur, prism, harmonizer, pluck, phaser, EQ, clipping) before it is ever turned into sound.
// Sound is made a frame at a time: each partial is written into a spectrum as the main lobe of a window,
// one inverse FFT per frame turns all voices into audio, and frames are cross-faded.
#pragma once
#include "Core.h"
#include "Image.h"

namespace pt
{
struct EnvState { float t = 0; bool started = false, rel = false, done = false, arpSeen = false; float relGain = 1; int arp = 0; };
struct LfoState { float ph = 0, t = 0; };

// atkSc / decSc / relSc stretch the three sections (attack: up to the D marker, decay: from there to the S marker,
// release: after it). susMul and susOfs move the level of the S point; the lines into and out of it follow.
inline float envAdvance (const EnvDef& e, EnvState& s, float dt, bool gate, float atkSc, float decSc, float relSc, float susMul, float susOfs, bool invertArp)
{
    const auto& p = e.c.pts;
    const int n = (int) p.size();
    if (n == 0) { s.done = ! gate; return 1.0f; }
    const int sus = (e.sustain >= 0 && e.sustain < n) ? e.sustain : -1;
    const float tEnd = p[(size_t) n - 1].x, tSus = sus >= 0 ? p[(size_t) sus].x : tEnd;
    const float susY = sus >= 0 ? clamp01 (p[(size_t) sus].y * susMul + susOfs) : 0.0f;
    if (! gate && ! s.rel)
    {
        s.rel = true;
        if (sus >= 0)
        {
            const float cur = e.c.eval (s.t, sus, susY);
            s.relGain = susY > 1.0e-4f ? cur / susY : 0.0f;
            if (s.t < tSus) s.t = tSus;
        }
    }
    const float prev = s.started ? s.t : -1.0f;
    if (s.started)
    {
        const float sc = s.rel ? relSc : ((n > 1 && s.t < p[(size_t) e.decayIndex()].x) ? atkSc : decSc);
        s.t += dt / juce::jmax (sc, 1.0e-3f);
    }
    s.started = true;
    bool wrapped = false; float l0 = 0;
    if (! s.rel && sus >= 0 && s.t >= tSus)
    {
        if (e.loopStart >= 0 && e.loopStart < sus)
        {
            l0 = p[(size_t) e.loopStart].x;
            const float len = tSus - l0;
            if (len > 1.0e-4f) { s.t = l0 + std::fmod (s.t - l0, len); wrapped = true; } else s.t = tSus;
        }
        else s.t = tSus;
    }
    for (int i = 0; i < n; ++i)
        if (p[(size_t) i].arp)
        {
            const float x = p[(size_t) i].x;
            const bool hit = wrapped ? ((x > prev && x <= tSus) || (x >= l0 && x <= s.t)) : (x > prev && x <= s.t);
            if (hit) { const int st = (int) p[(size_t) i].arp - 2; s.arp += invertArp ? -st : st; s.arpSeen = true; }
        }
    float v = e.c.eval (juce::jmin (s.t, tEnd), sus, susY);
    if (s.rel && sus >= 0) v *= s.relGain;
    s.done = s.t >= tEnd && (s.rel || sus < 0);
    return clamp01 (v);
}

inline float lfoShape (float ph, const LfoDef& d)
{
    float p = ph - std::floor (ph);
    const float h = clampf (0.5f + 0.49f * d.pw, 0.01f, 0.99f);
    p = p < h ? 0.5f * p / h : 0.5f + 0.5f * (p - h) / (1.0f - h);
    p += 0.25f; p -= std::floor (p);
    const float s = clampf (0.5f + 0.499f * d.skew, 0.001f, 0.999f);
    const float v = p < s ? p / s : 1.0f - (p - s) / (1.0f - s);
    float w = 2.0f * v - 1.0f;
    const float k = 1.0f / juce::jmax (0.02f, (1.0f - d.tension) * (1.0f - d.tension));
    w = (w < 0 ? -1.0f : 1.0f) * (1.0f - std::pow (1.0f - std::abs (w), k));
    return w;
}

struct VizFrame { int count = 0; float freq[NP]; float amp[NP]; };

//==================================================================================================
// Everything that can differ between the unison voices of a note has one slot per unison voice. Slot 0 is the only
// one in use until a unison index mapping asks for the voices to be shaped apart (UE > 1).
struct ImgState { double run = 0; float ofs = 0; int dir = 1, lastCol = -1; bool ended = false, init = false; };
struct PartVoice
{
    uint32_t ph[MAXU][NP];
    float amp[MAXU][NP], dAmp[MAXU][NP], cyc[MAXU][NP], dCyc[MAXU][NP], us[MAXU][NP];
    float blur[MAXU][NH], rnd[NH];
    EnvState es[MAXU][TG::Count]; LfoState ls[MAXU][TG::Count];
    float trnd[TG::Count], sm[MAXU][TG::Count];
    float uGL[MAXU], uGR[MAXU], uDet[MAXU], uHz[MAXU], uRnd[MAXU], uJit[MAXU];
    int U = 1, UE = 1, K[MAXU] = {}, Klast[MAXU] = {};
    bool active = false, started = false, smInit = false, volDone = false, pdInit = false;
    float predelayLeft = 0, age = 0, gateGain = 0, tremPh = 0, vibPh = 0, f0 = 0;
    ImgState im[MAXU];
    Rng rng;
};

struct Voice
{
    bool on = false, gate = false, held = false;
    int note = 60, heldIndex = 0, chordIdx = 0;
    float vel = 1, relVel = 0.5f, pitchCur = 60, pitchTarget = 60, glideFrom = 60, strumDelay = 0, relScale = 1;
    uint64_t order = 0;
    int legStack[16]; int legCount = 0;
    PartVoice pv[2];
};

class Engine
{
public:
    Engine()
    {
        voices.reset (new Voice[MAXV]);
        for (int i = 0; i < 4097; ++i) { sinT[i] = std::sin (kTwoPi * (float) i / 4096.0f); cosT[i] = std::cos (kTwoPi * (float) i / 4096.0f); }
        for (int p = 0; p < 2; ++p) { for (int i = 0; i < PP::Count; ++i) pp[p][i] = partDefs[i].def; data[p] = std::make_shared<PartData>(); }
        for (int i = 0; i < GP::Count; ++i) gp[i] = globalDefs[i].def;
        for (int i = 0; i < XP::Count; ++i) xp[i] = extraDefs[i].def;
        for (auto& part : em) for (auto& t : part) for (auto& m : t) m = 1.0f;
    }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        int h = 64; while ((float) h * 48000.0f / sr < 0.75f * 64.0f) h *= 2;       // keep frames near 1.3 ms
        H = h; N = 4 * H;
        int order = 0; while ((1 << order) < N) ++order;
        fft.reset (new juce::dsp::FFT (order));
        // main lobe of a Blackman-Harris window, for 512 fractional bin positions
        std::vector<double> w ((size_t) N);
        for (int n = 0; n < N; ++n)
        {
            const double x = 2.0 * 3.141592653589793 * (double) n / (double) N;
            w[(size_t) n] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
        }
        lobe.resize ((size_t) kRows * 9);
        for (int r = 0; r < kRows; ++r)
        {
            const double d = ((double) r + 0.5) / (double) kRows - 0.5;
            for (int t = 0; t < 9; ++t)
            {
                const double arg = (double) (t - 4) - d;
                double s = 0;
                for (int n = 0; n < N; ++n) s += w[(size_t) n] * std::cos (2.0 * 3.141592653589793 * arg * (double) (n - N / 2) / (double) N);
                lobe[(size_t) (r * 9 + t)] = (float) (0.5 * s * ((t & 1) ? -1.0 : 1.0));
            }
        }
        corr.resize ((size_t) (2 * H));
        for (int i = 0; i < 2 * H; ++i)
        {
            const int n = N / 4 + i;
            const float tri = 1.0f - std::abs ((float) (n - N / 2)) / (float) H;
            corr[(size_t) i] = tri / (float) w[(size_t) n];
        }
        for (auto& s : spec) s.assign ((size_t) (N + 2), 0.0f);
        cin.assign ((size_t) N, {}); cout.assign ((size_t) N, {});
        for (auto& o : ola) o.assign ((size_t) (2 * H), 0.0f);
        for (auto& o : outBuf) o.assign ((size_t) H, 0.0f);
        outPos = H;
        // find the scale of this FFT so that an amplitude of 1 comes out as 1
        fftScale = 1.0f;
        {
            std::fill (spec[0].begin(), spec[0].end(), 0.0f); std::fill (spec[1].begin(), spec[1].end(), 0.0f);
            addPartial (spec[0].data(), spec[1].data(), 1.0f, 0, 20.3f / 4.0f, 1.0f, 0.0f);
            inverse (0, 1, true);
            float pk = 0; for (int i = H / 2; i < 3 * H / 2; ++i) pk = juce::jmax (pk, std::abs (cout[(size_t) (N / 4 + i)].real()) / (float) w[(size_t) (N / 4 + i)]);
            if (pk > 1.0e-9f) fftScale = 1.0f / pk;
            for (auto& o : ola) std::fill (o.begin(), o.end(), 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (int i = 0; i < MAXV; ++i) { voices[i].on = false; voices[i].pv[0].active = voices[i].pv[1].active = false; }
        events.clear(); sampleTime = 0; tick = 0; lastNote = -1; heldCount = 0; sustainDown = false;
    }

    // called once per block, before events and render
    // x: the extra globals (XP), mods: the envelope modifiers, [part][target][EM], or null to leave them as they are
    void setParams (const float* g, const float* a, const float* b, double bpmIn, bool offline, const float* x = nullptr, const float* mods = nullptr)
    {
        std::memcpy (gp, g, sizeof (gp)); std::memcpy (pp[0], a, sizeof (pp[0])); std::memcpy (pp[1], b, sizeof (pp[1]));
        if (x != nullptr) std::memcpy (xp, x, sizeof (xp));
        if (mods != nullptr) std::memcpy (em, mods, sizeof (em));
        bps = (float) juce::jlimit (20.0, 999.0, bpmIn) / 60.0f;
        const int prec = (offline && gp[GP::hq] > 0.5f) ? 2 : (int) gp[GP::precision];
        ctrlDiv = prec == 0 ? 4 : (prec == 1 ? 2 : 1);
    }
    void setPartData (int p, std::shared_ptr<const PartData> d) { if (d != nullptr) data[p] = std::move (d); }
    void swapPartData (int p, std::shared_ptr<const PartData>& d) { if (d != nullptr) std::swap (data[p], d); }
    std::shared_ptr<const PartData> getPartData (int p) const { return data[p]; }

    enum EvType { NoteOn, NoteOff, Bend, Sustain, AllOff };
    void addEvent (int offset, EvType type, int note, float value) { events.push_back ({ sampleTime + (int64_t) offset, type, note, value }); }

    // out: wet L, wet R, dry L, dry R
    void render (float* const* out, int n)
    {
        int done = 0;
        while (done < n)
        {
            if (outPos >= H) { hopStep(); outPos = 0; }
            const int c = juce::jmin (n - done, H - outPos);
            for (int ch = 0; ch < 4; ++ch) std::memcpy (out[ch] + done, outBuf[ch].data() + outPos, sizeof (float) * (size_t) c);
            outPos += c; done += c; sampleTime += c;
        }
    }

    int latency() const { return H; }
    int activeVoices() const { int c = 0; for (int i = 0; i < MAXV; ++i) c += voices[i].on ? 1 : 0; return c; }
    int activePartials() const { return vizPartials.load(); }

    // latest partials of the newest voice, for the display
    bool readViz (VizFrame& dst) const
    {
        for (int tries = 0; tries < 4; ++tries)
        {
            const int s0 = vizSeq.load (std::memory_order_acquire);
            if (s0 & 1) continue;
            std::memcpy (&dst, &viz, sizeof (VizFrame));
            if (vizSeq.load (std::memory_order_acquire) == s0) return true;
        }
        return false;
    }
    std::atomic<int> vizPart { 0 };
    // for the envelope editor: where the newest voice is in the envelope / LFO of the target on show
    std::atomic<int> viewTarget { -1 };
    std::atomic<float> viewEnvPos { -1.0f }, viewLfoT { -1.0f };
    std::atomic<float> playPos[2] { { -1.0f }, { -1.0f } };      // image position of the newest voice, 0..1
    std::atomic<int> lastKey { -1 };

private:
    struct Ev { int64_t time; EvType type; int note; float value; };
    static constexpr int kRows = 512;

    //----------------------------------------------------------------------------------------------
    // spectrum writing
    inline void addPartial (float* SL, float* SR, float a, uint32_t phase, float cyclesPerHop, float gL, float gR) const
    {
        const float fb = cyclesPerHop * 4.0f;
        const int k0 = (int) (fb + 0.5f);
        const float d = fb - (float) k0;
        const int row = juce::jlimit (0, kRows - 1, (int) ((d + 0.5f) * (float) kRows));
        const float fi = (float) (phase >> 8) * (4096.0f / 16777216.0f);
        const int i0 = (int) fi; const float fr = fi - (float) i0;
        const float c = cosT[i0] + (cosT[i0 + 1] - cosT[i0]) * fr, s = sinT[i0] + (sinT[i0 + 1] - sinT[i0]) * fr;
        const float sg = (k0 & 1) ? -a : a;
        const float crL = c * sg * gL, ciL = s * sg * gL, crR = c * sg * gR, ciR = s * sg * gR;
        const float* w = lobe.data() + (size_t) (row * 9);
        const int kb = k0 - 4;
        if (kb >= 1 && kb + 8 < N / 2)
        {
            float* l = SL + 2 * kb; float* r = SR + 2 * kb;
            for (int t = 0; t < 9; ++t)
            {
                const float wv = w[t];
                l[2 * t] += wv * crL; l[2 * t + 1] += wv * ciL;
                r[2 * t] += wv * crR; r[2 * t + 1] += wv * ciR;
            }
        }
        else
        {
            for (int t = 0; t < 9; ++t)
            {
                const int k = kb + t; const float wv = w[t];
                if (k >= N / 2) break;
                if (k > 0)      { SL[2 * k] += wv * crL; SL[2 * k + 1] += wv * ciL; SR[2 * k] += wv * crR; SR[2 * k + 1] += wv * ciR; }
                else if (k < 0) { SL[-2 * k] += wv * crL; SL[-2 * k + 1] -= wv * ciL; SR[-2 * k] += wv * crR; SR[-2 * k + 1] -= wv * ciR; }
                else            { SL[0] += 2 * wv * crL; SR[0] += 2 * wv * crR; }
            }
        }
    }

    // two real spectra in, one complex inverse out: left in the real part, right in the imaginary part
    void inverse (int sl, int sr_, bool calibrating = false)
    {
        const float* L = spec[(size_t) sl].data(); const float* R = spec[(size_t) sr_].data();
        cin[0] = { L[0], R[0] };
        for (int k = 1; k < N / 2; ++k)
        {
            const float lr = L[2 * k], li = L[2 * k + 1], rr = R[2 * k], ri = R[2 * k + 1];
            cin[(size_t) k] = { lr - ri, li + rr };
            cin[(size_t) (N - k)] = { lr + ri, rr - li };
        }
        cin[(size_t) (N / 2)] = { 0.0f, 0.0f };
        fft->perform (cin.data(), cout.data(), true);
        juce::ignoreUnused (calibrating);
    }

    void hopStep()
    {
        // events that fall inside this frame
        const int64_t now = sampleTime;      // start of the frame being made
        size_t ne = 0;
        pendingOn.clear();
        for (auto& e : events)
        {
            if (e.time < now + H)
            {
                if (e.type == NoteOn) pendingOn.push_back (e);
                else { flushNoteOns(); handle (e); }
            }
            else events[ne++] = e;
        }
        events.resize (ne);
        flushNoteOns();

        const float dtHop = (float) H / sr;
        timeSec += dtHop;
        const bool ctl = (tick % ctrlDiv) == 0;
        if (ctl) globalTick (dtHop * (float) ctrlDiv);
        ++tick;

        for (auto& s : spec) std::fill (s.begin(), s.end(), 0.0f);
        const float abm = gp[GP::abMix];
        int partialCount = 0;
        for (int p = 0; p < 2; ++p)
        {
            if (gp[p == 0 ? GP::partAOn : GP::partBOn] < 0.5f) continue;
            bool any = false;
            std::fill (spec[4].begin(), spec[4].end(), 0.0f); std::fill (spec[5].begin(), spec[5].end(), 0.0f);
            for (int i = 0; i < MAXV; ++i)
            {
                Voice& v = voices[i];
                if (! v.on || ! v.pv[p].active) continue;
                if (ctl) controlTick (v, p, dtHop * (float) ctrlDiv);
                if (v.pv[p].started) { partialCount += synth (v.pv[p], spec[4].data(), spec[5].data()); any = true; }
            }
            if (! any) continue;
            const float pg = (p == 0 ? juce::jmin (1.0f, 2.0f * (1.0f - abm)) : juce::jmin (1.0f, 2.0f * abm)) * dbToGain (pp[p][PP::vol]);
            const float m = partFxMix[p];
            const float wetG = pg * (1.0f - std::abs (m)), dryG = pg * m;
            const float side = (gp[GP::sideStereo] > 0.5f && p == 1) ? -1.0f : 1.0f;      // part B carries the side signal
            for (int k = 0; k < N + 2; ++k)
            {
                const float l = spec[4][(size_t) k], r = spec[5][(size_t) k];
                spec[0][(size_t) k] += l * wetG; spec[1][(size_t) k] += r * wetG * side;
                spec[2][(size_t) k] += l * dryG; spec[3][(size_t) k] += r * dryG * side;
            }
        }
        vizPartials.store (partialCount);
        for (int i = 0; i < MAXV; ++i)
        {
            Voice& v = voices[i];
            if (v.on && ! v.pv[0].active && ! v.pv[1].active) { v.on = false; }
        }

        for (int b = 0; b < 2; ++b)
        {
            inverse (2 * b, 2 * b + 1);
            float* oL = ola[(size_t) (2 * b)].data(); float* oR = ola[(size_t) (2 * b + 1)].data();
            for (int i = 0; i < 2 * H; ++i)
            {
                const auto c = cout[(size_t) (N / 4 + i)];
                const float g = corr[(size_t) i] * fftScale;
                oL[i] += c.real() * g; oR[i] += c.imag() * g;
            }
            for (int ch = 2 * b; ch < 2 * b + 2; ++ch)
            {
                float* o = ola[(size_t) ch].data();
                std::memcpy (outBuf[(size_t) ch].data(), o, sizeof (float) * (size_t) H);
                std::memmove (o, o + H, sizeof (float) * (size_t) H);
                std::fill (o + H, o + 2 * H, 0.0f);
            }
        }
    }

    int synth (PartVoice& pv, float* SL, float* SR)
    {
        int count = 0;
        const int UE = juce::jlimit (1, MAXU, pv.UE);
        for (int e = 0; e < UE; ++e)
        {
            const int ranges[2][2] = { { 0, pv.K[e] }, { NH, NH + NX } };
            float* am = pv.amp[e]; float* cy = pv.cyc[e]; const float* da = pv.dAmp[e]; const float* dc = pv.dCyc[e];
            for (auto& rg : ranges) for (int n = rg[0]; n < rg[1]; ++n) { am[n] += da[n]; cy[n] += dc[n]; }
        }
        const float maxCyc = 0.5f * (float) H - 1.3f;     // keeps the whole lobe below Nyquist
        for (int u = 0; u < pv.U; ++u)
        {
            const int e = juce::jmin (u, UE - 1);
            const int ranges[2][2] = { { 0, pv.K[e] }, { NH, NH + NX } };
            const float* amps = pv.amp[e]; const float* cycs = pv.cyc[e]; const float* uss = pv.us[e];
            const float gL = pv.uGL[u], gR = pv.uGR[u], du = pv.uDet[u], dHz = pv.uHz[u];
            uint32_t* ph = pv.ph[u];
            for (auto& rg : ranges)
                for (int n = rg[0]; n < rg[1]; ++n)
                {
                    const float cy = cycs[n] * (1.0f + du * uss[n]) + dHz;
                    const uint32_t p0 = ph[n];
                    ph[n] = p0 + (uint32_t) (int64_t) ((cy - std::floor (cy)) * 4294967296.0f);
                    const float a = amps[n];
                    if (a < 1.0e-7f || cy > maxCyc || cy < 0.001f) continue;
                    addPartial (SL, SR, a, p0, cy, gL, gR);
                    ++count;
                }
        }
        return count;
    }

    //----------------------------------------------------------------------------------------------
    // notes
    void flushNoteOns()
    {
        if (pendingOn.empty()) return;
        const int mode = (int) gp[GP::strumMode];
        bool down = mode == 1;
        if (mode == 2) { down = strumFlip; strumFlip = ! strumFlip; }
        if (mode == 3) down = rng.uni() < 0.5f;
        std::sort (pendingOn.begin(), pendingOn.end(), [down] (const Ev& a, const Ev& b) { return down ? a.note > b.note : a.note < b.note; });
        for (auto& e : pendingOn) noteOn (e.note, e.value);
        pendingOn.clear();
    }
    void handle (const Ev& e)
    {
        switch (e.type)
        {
            case NoteOff: noteOff (e.note, e.value); break;
            case Bend:    bend = e.value; break;
            case Sustain: sustainDown = e.value > 0.5f;
                          if (! sustainDown) for (int i = 0; i < MAXV; ++i) if (voices[i].on && voices[i].gate && ! voices[i].held) release (voices[i], 0.5f);
                          break;
            case AllOff:  for (int i = 0; i < MAXV; ++i) { voices[i].held = false; if (voices[i].on && voices[i].gate) release (voices[i], 0.5f); } break;
            default: break;
        }
    }
    int firstPart() const { return gp[GP::partAOn] > 0.5f ? 0 : 1; }

    void noteOn (int note, float vel)
    {
        lastKey.store (note);
        if (timeSec - lastOnTime > 0.03) chordCount = 0;
        lastOnTime = timeSec;
        const int chordIdx = chordCount++;
        heldCount = 0; for (int i = 0; i < MAXV; ++i) if (voices[i].on && voices[i].held) ++heldCount;
        heldIndex = heldCount == 0 ? 0 : heldIndex + 1;

        const float* P = pp[firstPart()];
        // legato: an overlapping note bends the playing voice instead of starting a new one
        if (P[PP::legato] > 0.5f)
        {
            Voice* cur = nullptr;
            for (int i = 0; i < MAXV; ++i) if (voices[i].on && voices[i].held && (cur == nullptr || voices[i].order > cur->order)) cur = &voices[i];
            const float lim = xp[XP::legLimit];
            if (cur != nullptr && (lim <= 0.0f || std::abs ((float) (note - cur->note)) * 100.0f <= lim) && cur->legCount < 16)
            {
                cur->legStack[cur->legCount++] = note;
                cur->glideFrom = cur->pitchCur; cur->pitchTarget = (float) note; cur->vel = vel;
                return;
            }
        }
        const int mono = (int) gp[GP::mono];
        for (int i = 0; i < MAXV; ++i)
        {
            Voice& o = voices[i];
            if (! o.on) continue;
            if ((mono == 1 && o.note == note) || mono == 2) kill (o);
            else if (mono == 3 && o.gate) { o.held = false; release (o, 0.5f); }
            else if (! o.gate) o.relScale = juce::jmin (o.relScale, juce::jmax (0.02f, juce::jmax (pp[0][PP::polyRel], pp[1][PP::polyRel])));
        }
        Voice* v = nullptr;
        for (int i = 0; i < MAXV; ++i) if (! voices[i].on) { v = &voices[i]; break; }
        if (v == nullptr)
        {
            for (int i = 0; i < MAXV; ++i) if (! voices[i].gate && (v == nullptr || voices[i].order < v->order)) v = &voices[i];
            if (v == nullptr) for (int i = 0; i < MAXV; ++i) if (v == nullptr || voices[i].order < v->order) v = &voices[i];
        }
        v->on = true; v->gate = true; v->held = true; v->note = note; v->vel = vel; v->relVel = 0.5f; v->order = ++orderCounter;
        v->heldIndex = heldIndex; v->chordIdx = chordIdx; v->legCount = 0; v->relScale = 1.0f;
        v->pitchTarget = (float) note; v->pitchCur = (float) note;
        if (P[PP::porta] > 0.5f && lastNote >= 0)
        {
            float d = (float) (lastNote - note);
            const float lim = P[PP::glideLimit] * 0.01f;
            if (lim > 0.0f) d = clampf (d, -lim, lim);
            v->pitchCur = (float) note + d;
        }
        v->glideFrom = v->pitchCur;
        const float st = gp[GP::strumTime];
        v->strumDelay = st > 0.0f ? st * st * 0.25f * std::pow ((float) chordIdx, std::exp2 (gp[GP::strumTension])) : 0.0f;
        lastNote = note;
        uint32_t seedA = rng.next(), seedB = rng.next();
        const int rm = (int) gp[GP::randomness];
        if (rm == 3) { seedA = 0x9E3779B9u; seedB = 0x7F4A7C15u; }
        else if (rm == 0) seedB = seedA;
        for (int p = 0; p < 2; ++p)
        {
            PartVoice& pv = v->pv[p];
            pv.active = gp[p == 0 ? GP::partAOn : GP::partBOn] > 0.5f;
            pv.started = false; pv.smInit = false; pv.volDone = false; pv.pdInit = false;
            pv.rng.s = (p == 0 ? seedA : seedB) | 1u;
            pv.age = 0; pv.gateGain = 0; pv.U = 1; pv.UE = 1; pv.tremPh = 0; pv.vibPh = 0;
            for (int e = 0; e < MAXU; ++e)
            {
                pv.K[e] = pv.Klast[e] = 0; pv.im[e] = ImgState();
                std::fill (pv.amp[e], pv.amp[e] + NP, 0.0f); std::fill (pv.dAmp[e], pv.dAmp[e] + NP, 0.0f);
                std::fill (pv.cyc[e], pv.cyc[e] + NP, 0.0f); std::fill (pv.dCyc[e], pv.dCyc[e] + NP, 0.0f);
                std::fill (pv.us[e], pv.us[e] + NP, 1.0f); std::fill (pv.blur[e], pv.blur[e] + NH, 0.0f);
                for (int t = 0; t < TG::Count; ++t) { pv.es[e][t] = EnvState(); pv.ls[e][t] = LfoState(); pv.sm[e][t] = 0; }
            }
            for (int n = 0; n < NH; ++n) pv.rnd[n] = pv.rng.bi();
            const float shared = pv.rng.uni();
            for (int t = 0; t < TG::Count; ++t) pv.trnd[t] = gp[GP::sameMaps] > 0.5f ? shared : pv.rng.uni();
            for (int u = 0; u < MAXU; ++u) { pv.uRnd[u] = pv.rng.bi(); pv.uJit[u] = 0; pv.uGL[u] = pv.uGR[u] = 0; pv.uDet[u] = pv.uHz[u] = 0; }
            pv.predelayLeft = v->strumDelay;          // the part's own predelay is added on its first control tick, once its mappings are known
            for (int t = 0; t < TG::Count; ++t) if (data[p]->tg[t].env.global) genv[p][t] = EnvState();
        }
    }
    void noteOff (int note, float relVel)
    {
        for (int i = 0; i < MAXV; ++i)
        {
            Voice& v = voices[i];
            if (! v.on || ! v.held) continue;
            // legato stack: releasing the top note slides back to the one below
            if (v.legCount > 0)
            {
                bool found = false;
                for (int j = 0; j < v.legCount; ++j) if (v.legStack[j] == note) { for (int k = j; k + 1 < v.legCount; ++k) v.legStack[k] = v.legStack[k + 1]; --v.legCount; found = true; break; }
                if (found) { v.glideFrom = v.pitchCur; v.pitchTarget = (float) (v.legCount > 0 ? v.legStack[v.legCount - 1] : v.note); return; }
                if (v.note == note) { v.note = v.legStack[0]; for (int k = 0; k + 1 < v.legCount; ++k) v.legStack[k] = v.legStack[k + 1]; --v.legCount; return; }
                continue;
            }
            if (v.note == note) { v.held = false; if (! sustainDown) release (v, relVel); }
        }
    }
    void release (Voice& v, float relVel) { v.gate = false; v.relVel = relVel; }
    void kill (Voice& v) { v.gate = false; v.held = false; v.relScale = 0.02f; }

    //----------------------------------------------------------------------------------------------
    // once per control period, shared by all voices
    void globalTick (float dt)
    {
        const float a = gp[GP::smoothMod] > 0.5f ? 1.0f - std::exp (-dt / 0.03f) : 1.0f;
        const float px = mod[0];
        mod[0] += (gp[GP::modX] - mod[0]) * a; mod[1] += (gp[GP::modY] - mod[1]) * a; mod[2] += (gp[GP::modZ] - mod[2]) * a;
        const float sp = std::abs (mod[0] - px) / juce::jmax (1.0e-5f, dt) * 0.5f;
        modXSpeed += (clamp01 (sp) - modXSpeed) * (1.0f - std::exp (-dt / 0.05f));
        for (int p = 0; p < 2; ++p)
        {
            const float s = pp[p][PP::phSpeed];
            if (std::abs (s) > 0.02f)
            {
                const float periodSteps = std::pow (128.0f, 1.0f - std::abs (s));
                phaserClock[p] += (s > 0 ? 1.0f : -1.0f) * dt * bps * 4.0f / periodSteps;
                phaserClock[p] -= std::floor (phaserClock[p]);
            }
            genvStamp[p] = false;
        }
        // chord, sorted, for arpeggiator points
        chordN = 0;
        for (int i = 0; i < MAXV; ++i) if (voices[i].on && voices[i].gate && chordN < MAXV) chord[chordN++] = voices[i].note;
        std::sort (chord, chord + chordN);
        ++ctlTick;
    }

    // The value of one target for one unison slot: the control's own value combined with every active part.
    // ux is the slot's place among the unison voices (0..1), or below zero when the voices share one value
    // (panning, volume and pitch then get their unison index mapping where the unison voices are laid out).
    float evalTarget (Voice& v, int p, int t, float dt, const float* T, int ue = 0, float ux = -1.0f)
    {
        const TargetInfo& ti = targetInfo[t];
        const TargetDef& td = data[p]->tg[t];
        PartVoice& pv = v.pv[p];
        const float* P = pp[p];
        const float k = ti.pp >= 0 ? P[ti.pp] : ti.base;
        if (! td.active()) return k;
        float prod = 1.0f, sum = 0.0f;
        if (t < TG::FirstMapping)
        {
            if (td.env.on)
            {
                EnvState* s = &pv.es[ue][t];
                float edt = td.env.tempo ? dt * bps : dt;
                if ((t == TG::F1Freq && P[PP::f1AdaptEnv] > 0.5f) || (t == TG::F2Freq && P[PP::f2AdaptEnv] > 0.5f)) edt *= std::sqrt (midiHz (v.pitchCur) / kC4);
                const float* M = em[p][t];
                float atk = T[TG::EnvAtkScale] * std::exp2 (2.0f * T[TG::EnvAtkTime]) * M[EM::Att], rel = T[TG::EnvRelScale] * v.relScale * M[EM::Rel];
                if (P[PP::velAtkSpeed] > 0.5f) atk *= 1.6f - 1.3f * v.vel;
                if (P[PP::velRel] > 0.5f) rel *= 0.5f + 2.5f * v.vel;
                if (td.env.global)
                {
                    s = &genv[p][t];
                    if (genvDone[p][t] == ctlTick) edt = 0.0f; else genvDone[p][t] = ctlTick;
                }
                const bool wasStarted = s->started;
                if (t == TG::Volume && ! wasStarted && P[PP::velAtkSkip] > 0.5f && td.env.c.pts.size() > 1) s->t = td.env.c.pts[(size_t) td.env.decayIndex()].x * v.vel;
                const float y = envAdvance (td.env, *s, edt, v.gate, atk, T[TG::EnvDecScale] * M[EM::Dec], rel, M[EM::Sus], T[TG::EnvSusOfs], gp[GP::invertArp] > 0.5f);
                if (ue == 0 && t == viewTarget.load (std::memory_order_relaxed) && p == vizPart.load (std::memory_order_relaxed) && v.order == orderCounter) viewEnvPos.store (s->t, std::memory_order_relaxed);
                if (ti.mode == BSum)
                {
                    if (t == TG::F1Freq || t == TG::F2Freq)
                    {
                        // The envelope works below the frequency control: at the top of the graph the filter sits where the
                        // control is, at the bottom it is the envelope amount lower. A negative amount turns the graph over.
                        const float amt = T[t == TG::F1Freq ? TG::F1Env : TG::F2Env];
                        sum += (data[p]->filtEnvAdd || amt < 0.0f) ? y * amt : (y - 1.0f) * amt;
                    }
                    else sum += (2.0f * y - 1.0f) * (t == TG::Pitch ? P[PP::pitchEnv] : 1.0f);
                }
                else prod *= (ti.mode == BProd ? 2.0f * y - 1.0f : y);
            }
            if (td.lfo.on)
            {
                LfoState& ls = pv.ls[ue][t];
                const float spd = clamp01 (td.lfo.speed);
                float hz = td.lfo.tempo ? bps / (16.0f * std::exp2 (-8.0f * spd)) : 0.05f * std::pow (800.0f, spd);
                hz *= T[TG::LfoSpeed];
                float ph;
                if (td.lfo.global) ph = (float) std::fmod (timeSec * (double) hz, 1.0);
                else { ls.ph += hz * dt; ls.ph -= std::floor (ls.ph); ph = ls.ph; }
                const float w = lfoShape (ph + td.lfo.phase + T[TG::LfoPhase], td.lfo);
                const float depth = td.lfo.amt * T[TG::LfoAmt] * td.lfo.ampAt (td.lfo.tempo ? ls.t * bps : ls.t);
                ls.t += dt;
                if (ue == 0 && t == viewTarget.load (std::memory_order_relaxed) && p == vizPart.load (std::memory_order_relaxed) && v.order == orderCounter) viewLfoT.store (ls.t, std::memory_order_relaxed);
                if (ti.mode == BSum) sum += w * depth;
                else { const float a = juce::jmin (1.0f, std::abs (depth)); prod *= 1.0f - a * (0.5f - 0.5f * (depth < 0 ? -w : w)); }
            }
        }
        for (int m = 0; m < MAP::Count; ++m)
        {
            if (! td.mapOn[m] || td.map[m].empty()) continue;
            float x = 0.5f;
            switch (m)
            {
                case MAP::Key: x = v.pitchCur / 127.0f; break;
                case MAP::Vel: x = v.vel; break;
                case MAP::ModX: x = mod[0]; break; case MAP::ModY: x = mod[1]; break; case MAP::ModZ: x = mod[2]; break;
                case MAP::Random: x = pv.trnd[t]; break;
                case MAP::Unison:
                    if (ux >= 0.0f) x = ux;
                    else if (t == TG::Pan || t == TG::Volume || t == TG::Pitch) continue;      // laid out per unison voice further on
                    break;
                case MAP::Held: x = clamp01 ((float) v.heldIndex / 8.0f); break;
                case MAP::XSpeed: x = modXSpeed; break;
                default: break;
            }
            const float y = td.map[m].eval (x);
            if (ti.mode == BSum) sum += 2.0f * y - 1.0f;
            else prod *= (ti.mode == BProd ? 2.0f * y - 1.0f : y);
        }
        float out = ti.mode == BSum ? k + sum * ti.range : k * prod;
        if (t == TG::Pitch) out = clampf (out, -9600.0f, 9600.0f);
        else if (t == TG::PhOfs) {}                                                  // the offset goes round: no end stops
        else if (ti.pp >= 0) out = clampf (out, partDefs[ti.pp].lo, partDefs[ti.pp].hi);
        if (td.smooth > 0.0f)
        {
            if (! pv.smInit) pv.sm[ue][t] = out;
            pv.sm[ue][t] += (out - pv.sm[ue][t]) * (1.0f - std::exp (-dt / (td.smooth * td.smooth * 0.5f + 1.0e-4f)));
            out = pv.sm[ue][t];
        }
        return out;
    }

    static float filterShape (int type, float d, const float* custom1, const float* custom2)
    {
        const float ad = std::abs (d);
        switch (type)
        {
            case 0:  return d < 0 ? 1.0f : juce::jmax (0.0f, 1.0f - d);
            case 1:  return 1.0f / std::sqrt (1.0f + std::exp2 (4.0f * d));
            case 2:  return 1.0f / std::sqrt (1.0f + std::exp2 (8.0f * d));
            case 3:  return 1.0f / std::sqrt (1.0f + std::exp2 (2.0f * d));
            case 4:  return d > 0 ? 1.0f : juce::jmax (0.0f, 1.0f + d);
            case 5:  return 1.0f / std::sqrt (1.0f + std::exp2 (-4.0f * d));
            case 6:  return 1.0f / std::sqrt (1.0f + std::exp2 (-8.0f * d));
            case 7:  return 1.0f / std::sqrt (1.0f + std::exp2 (-2.0f * d));
            case 8:  return juce::jmax (0.0f, 1.0f - ad);
            case 9:  { const float q = std::exp2 (d) - std::exp2 (-d); return 1.0f / std::sqrt (1.0f + 1.5f * q * q); }
            case 10: return ad < 1.0f ? 1.0f : juce::jmax (0.0f, 2.0f - ad);
            case 11: return 0.15f + 0.85f * juce::jmax (0.0f, 1.0f - ad);
            case 12: return 1.0f - juce::jmax (0.0f, 1.0f - ad);
            case 13: return 1.0f - std::exp (-8.0f * d * d);
            case 14: return 0.5f + 0.5f * std::cos (3.14159265f * d);
            case 15: return (0.5f + 0.5f * std::cos (3.14159265f * d)) * juce::jmax (0.0f, 1.0f - ad * 0.25f);
            case 16: { const float c = std::cos (1.5707963f * d); return c * c * c * c; }
            default:
            {
                const float* tb = type == 17 ? custom1 : custom2;
                const float x = clamp01 (d * 0.125f + 0.5f) * (float) (NH - 1);
                const int i = (int) x; const int j = juce::jmin (NH - 1, i + 1);
                return tb[i] + (tb[j] - tb[i]) * (x - (float) i);
            }
        }
    }
    float resShape (int type, float d, const float* c1, const float* c2, Rng& r) const
    {
        const float ad = std::abs (d);
        switch (type)
        {
            case 0: return std::exp (-8.0f * d * d);                                                          // Classic: one narrow peak
            case 1: { const float t = juce::jmax (0.0f, 1.0f - ad); return t * std::sqrt (t); }               // Cuberdon: a pointed cone
            case 2: return (ad < 1.0f ? 0.4f : 0.4f * juce::jmax (0.0f, 1.5f - ad) * 2.0f) + 0.6f * std::exp (-14.0f * d * d);   // Pedestal: a peak on a flat base
            case 3: return 0.45f * juce::jmax (0.0f, 1.0f - ad * 0.4f) + 0.55f * juce::jmax (0.0f, 1.0f - ad * 2.0f);          // Sedge hat: cone with a wide brim
            case 4: return 1.0f / (1.0f + d * d);                                                             // Wide bump
            case 5: return juce::jmax (0.0f, 1.0f - std::abs (ad - 0.9f) * 1.6f);                              // Double cone
            case 6: return std::exp (-5.0f * (ad - 1.0f) * (ad - 1.0f)) - 0.18f * std::exp (-5.0f * d * d);   // Well: a cut with a peak either side
            case 7: return std::exp (-24.0f * (ad - 0.6f) * (ad - 0.6f)) - 0.2f * std::exp (-30.0f * d * d);  // Wormhole: the same, tight
            case 8: return std::exp (-2.0f * d * d) * 2.0f * r.uni();                                         // Noise
            case 11: return std::exp (-(d < 0 ? 2.0f : 6.0f) * d * d) - (d > 0 ? 0.16f * std::exp (-2.0f * (d - 1.3f) * (d - 1.3f)) : 0.0f);   // Low pass: peak, then a dip
            default:
            {
                const float* tb = type == 9 ? c1 : c2;
                const float x = clamp01 (d * 0.125f + 0.5f) * (float) (NH - 1);
                return tb[(int) x];
            }
        }
    }
    static inline float freqTab (const float* tb, float lf)
    {
        const float x = clamp01 ((lf - kLog2Fmin) / kFreqOctaves) * (float) (NH - 1);
        return tb[(int) x];
    }

    static void sampleImage (const ImageData& im, const std::vector<float>& plane, double pos, int interp, float* out)
    {
        const int w = im.w, h = im.h;
        pos = juce::jlimit (0.0, (double) (w - 1), pos);
        const int c1 = (int) pos; const float fr = (float) (pos - (double) c1);
        const int c0 = juce::jmax (0, c1 - 1), c2 = juce::jmin (w - 1, c1 + 1), c3 = juce::jmin (w - 1, c1 + 2);
        const float* a = plane.data() + (size_t) c0 * (size_t) h; const float* b = plane.data() + (size_t) c1 * (size_t) h;
        const float* c = plane.data() + (size_t) c2 * (size_t) h; const float* d = plane.data() + (size_t) c3 * (size_t) h;
        switch (interp)
        {
            case 0: std::memcpy (out, b, sizeof (float) * (size_t) h); break;
            case 1: { const float t = fr * fr * (3 - 2 * fr); for (int y = 0; y < h; ++y) out[y] = b[y] + (c[y] - b[y]) * t; break; }
            case 2: for (int y = 0; y < h; ++y) { const float s0 = 0.25f * (a[y] + 2 * b[y] + c[y]), s1 = 0.25f * (b[y] + 2 * c[y] + d[y]); out[y] = s0 + (s1 - s0) * fr; } break;
            case 4: for (int y = 0; y < h; ++y)
                    {
                        const float p0 = a[y], p1 = b[y], p2 = c[y], p3 = d[y];
                        out[y] = p1 + 0.5f * fr * (p2 - p0 + fr * (2 * p0 - 5 * p1 + 4 * p2 - p3 + fr * (3 * (p1 - p2) + p3 - p0)));
                    } break;
            case 5: for (int y = 0; y < h; ++y) out[y] = juce::jmax (b[y], b[y] + (c[y] - b[y]) * fr); break;
            case 6: for (int y = 0; y < h; ++y) out[y] = juce::jmin (b[y], b[y] + (c[y] - b[y]) * fr); break;
            default: for (int y = 0; y < h; ++y) out[y] = b[y] + (c[y] - b[y]) * fr; break;
        }
    }

    //----------------------------------------------------------------------------------------------
    // one voice of one part: work out every partial's level and pitch for the next control period
    void controlTick (Voice& v, int p, float dt)
    {
        PartVoice& pv = v.pv[p];
        const float* P = pp[p];
        const PartData& D = *data[p];
        if (! pv.pdInit)
        {
            // the part's predelay, with its mappings (key, velocity, X / Y / Z, random...) taken at the start of the note
            pv.pdInit = true;
            float T0[TG::Count];
            for (int t = TG::FirstMapping; t < TG::Count; ++t) T0[t] = targetInfo[t].pp >= 0 ? P[targetInfo[t].pp] : targetInfo[t].base;
            const float pd = clampf (evalTarget (v, p, TG::Predelay, dt, T0), -1.0f, 1.0f);
            pv.predelayLeft += pd < 0 ? pd * pd : pd * 16.0f * 0.25f / bps;
        }
        if (pv.predelayLeft > 0.0f)
        {
            pv.predelayLeft -= dt;
            if (! v.gate && ! v.held) { pv.active = false; }
            if (pv.predelayLeft > 0.0f) return;
        }
        const bool first = ! pv.started;

        // Unison voices share one set of partials unless a unison index mapping pulls them apart (anything but panning,
        // volume and pitch, which are cheap to lay out per voice). Then every unison voice is shaped on its own.
        const int U = juce::jlimit (1, MAXU, (int) P[PP::uniOrder]);
        bool perU = false;
        if (U > 1)
            for (int t = 0; t < TG::Count && ! perU; ++t)
                perU = t != TG::Pan && t != TG::Volume && t != TG::Pitch && D.tg[t].mapOn[MAP::Unison] && ! D.tg[t].map[MAP::Unison].empty();
        const int UE = perU ? U : 1;
        if (UE > pv.UE && ! first)
            for (int e = pv.UE; e < UE; ++e)      // new slots carry on from where the shared one is
            {
                std::memcpy (pv.amp[e], pv.amp[0], sizeof (pv.amp[0])); std::memcpy (pv.cyc[e], pv.cyc[0], sizeof (pv.cyc[0]));
                std::memcpy (pv.us[e], pv.us[0], sizeof (pv.us[0])); std::memcpy (pv.blur[e], pv.blur[0], sizeof (pv.blur[0]));
                std::fill (pv.dAmp[e], pv.dAmp[e] + NP, 0.0f); std::fill (pv.dCyc[e], pv.dCyc[e] + NP, 0.0f);
                for (int t = 0; t < TG::Count; ++t) { pv.es[e][t] = pv.es[0][t]; pv.ls[e][t] = pv.ls[0][t]; pv.sm[e][t] = pv.sm[0][t]; }
                pv.im[e] = pv.im[0]; pv.K[e] = pv.K[0]; pv.Klast[e] = pv.Klast[0];
            }
        pv.UE = UE;

        // targets: mapping ones first, they steer the envelopes and LFOs of the rest
        float Tu[MAXU][TG::Count];
        for (int e = 0; e < UE; ++e)
        {
            float* T = Tu[e];
            const float ux = perU ? (float) e / (float) (U - 1) : -1.0f;
            for (int t = TG::FirstMapping; t < TG::Count; ++t) T[t] = targetInfo[t].pp >= 0 ? P[targetInfo[t].pp] : targetInfo[t].base;
            for (int t = TG::FirstMapping; t < TG::Count; ++t) T[t] = evalTarget (v, p, t, dt, T, e, ux);
            for (int t = 0; t < TG::FirstMapping; ++t) T[t] = evalTarget (v, p, t, dt, T, e, ux);
        }
        pv.smInit = true;
        partFxMix[p] = Tu[0][TG::FxMix];

        // glide
        if (p == firstPart() || ! v.pv[firstPart()].active)
        {
            const float g = P[PP::glideTime];
            float time = g < 0 ? g * g * 1.5f : g * 16.0f * 0.25f / bps;
            const float dist = v.pitchTarget - v.glideFrom;
            if (P[PP::glideVar] > 0.5f) time *= std::abs (dist) / 12.0f;
            if (P[PP::glideVel] > 0.5f) time *= 1.5f - v.vel;
            if (time < 0.002f || std::abs (v.pitchTarget - v.pitchCur) < 1.0e-4f) v.pitchCur = v.pitchTarget;
            else if (P[PP::glideLog] > 0.5f) v.pitchCur += (v.pitchTarget - v.pitchCur) * (1.0f - std::exp (-dt * 4.0f / time));
            else
            {
                const float step = std::abs (dist) / time * dt;
                v.pitchCur = v.pitchCur < v.pitchTarget ? juce::jmin (v.pitchTarget, v.pitchCur + step) : juce::jmax (v.pitchTarget, v.pitchCur - step);
            }
        }

        pv.vibPh += P[PP::vibSpeed] * dt; pv.vibPh -= std::floor (pv.vibPh);
        float f0u[MAXU];
        for (int e = 0; e < UE; ++e)
            if (! shapeSlot (v, p, dt, e, Tu[e], first, f0u[e])) { finish (pv); return; }

        // unison: where each voice sits in pitch and in the stereo picture
        if (first) startPhases (pv, P, D, U, Tu[0][TG::UniPhase]);
        pv.U = U;
        const int utype = (int) P[PP::uniType];
        pv.tremPh += dt / (0.016f * std::pow (1000.0f, P[PP::tremSpeed])); pv.tremPh -= std::floor (pv.tremPh);
        const float td = P[PP::tremDepth];
        const float trL = 1.0f - td * (0.5f + 0.5f * std::sin (kTwoPi * pv.tremPh)), trR = 1.0f - td * (0.5f + 0.5f * std::sin (kTwoPi * pv.tremPh + P[PP::tremGap] * 0.01745329f));
        const float uNorm = 1.0f / std::sqrt ((float) U);
        const float hopsPerSec = sr / (float) H;
        for (int u = 0; u < U; ++u)
        {
            const float* T = Tu[juce::jmin (u, UE - 1)];
            const float f0 = f0u[juce::jmin (u, UE - 1)];
            const float thick = T[TG::UniPitch];
            const float maxCents = 60.0f * thick * thick * (utype == 0 ? 1.4f : 1.0f);
            const float panBase = clampf (T[TG::Pan], -1.0f, 1.0f);
            const float pu = U == 1 ? 0.0f : 2.0f * (float) u / (float) (U - 1) - 1.0f;
            float dpos = pu;
            if (utype == 0) dpos = pu * std::abs (pu) * 0.5f + pu * 0.5f;
            else if (utype == 2) { pv.uJit[u] = clampf (pv.uJit[u] + pv.rng.bi() * 0.08f, -0.3f, 0.3f); dpos = pu + pv.uJit[u]; }
            else if (utype == 3) dpos = U == 1 ? 0.0f : pv.uRnd[u];
            float uPitch = 0.0f, uVol = 1.0f, uPan = 0.0f;
            const float ux = U == 1 ? 0.5f : (float) u / (float) (U - 1);
            if (perU) {}        // every slot already carries its own unison index mappings
            else if (D.tg[TG::Pitch].mapOn[MAP::Unison] && ! D.tg[TG::Pitch].map[MAP::Unison].empty()) uPitch = (2.0f * D.tg[TG::Pitch].map[MAP::Unison].eval (ux) - 1.0f) * 4800.0f;
            if (! perU && D.tg[TG::Volume].mapOn[MAP::Unison] && ! D.tg[TG::Volume].map[MAP::Unison].empty()) uVol = D.tg[TG::Volume].map[MAP::Unison].eval (ux);
            if (! perU && D.tg[TG::Pan].mapOn[MAP::Unison] && ! D.tg[TG::Pan].map[MAP::Unison].empty()) uPan = 2.0f * D.tg[TG::Pan].map[MAP::Unison].eval (ux) - 1.0f;
            if (utype == 4) { pv.uDet[u] = std::exp2 (uPitch / 1200.0f) - 1.0f; pv.uHz[u] = dpos * thick * f0 * 0.03f / hopsPerSec; }
            else { pv.uDet[u] = std::exp2 ((dpos * maxCents + uPitch) / 1200.0f) - 1.0f; pv.uHz[u] = 0.0f; }
            // Neighbours in pitch go to opposite sides, and the two voices at the same distance from the middle too, so the
            // picture is balanced for every order (with an odd order the middle voice stays in the middle).
            const int mirror = U - 1 - u;
            const float sgn = u == mirror ? 0.0f : ((juce::jmin (u, mirror) & 1) ? 1.0f : -1.0f) * (u < mirror ? 1.0f : -1.0f);
            const float side = (U == 1) ? 0.0f : sgn * (0.3f + 0.7f * std::abs (pu));
            const float pan = clampf (panBase + side * P[PP::uniPan] + uPan, -1.0f, 1.0f);
            const float ang = (pan + 1.0f) * 0.7853982f;
            float gl = std::cos (ang) * 1.41421356f, gr = std::sin (ang) * 1.41421356f;
            if (u & 1) gr *= 1.0f - 2.0f * P[PP::uniAlt];
            pv.uGL[u] = gl * trL * uNorm * uVol; pv.uGR[u] = gr * trR * uNorm * uVol;
        }

        pv.started = true;
        pv.age += dt;
    }

    // The partials of one unison slot (slot 0 is the whole note unless the unison voices are shaped apart).
    // Returns false when the note has faded out and the voice can stop.
    bool shapeSlot (Voice& v, int p, float dt, int ue, const float* T, bool first, float& f0Out)
    {
        PartVoice& pv = v.pv[p];
        const float* P = pp[p];
        const PartData& D = *data[p];
        // pitch
        const ImageData* img = (D.image != nullptr && D.image->valid()) ? D.image.get() : nullptr;
        const bool resynth = img != nullptr && img->resynth;
        const float cents = T[TG::Pitch] + xp[XP::gPitch] + bend * gp[GP::bendRange] * 100.0f + T[TG::VibDepth] * std::sin (kTwoPi * pv.vibPh);
        const float base = resynth ? img->baseFreq * std::exp2 ((v.pitchCur - 60.0f) / 12.0f) : midiHz (v.pitchCur);
        const float f0 = base * P[PP::freqMul] / juce::jmax (1.0f, P[PP::freqDiv]) * std::exp2 (cents / 1200.0f);
        if (ue == 0) pv.f0 = f0;
        f0Out = f0;
        const float det = P[PP::detMul] / juce::jmax (1.0f, P[PP::detDiv]);
        const bool octMode = P[PP::pitchMode] > 0.5f;
        const float maxF = juce::jmin (21500.0f, sr * 0.46f);
        int K = (int) gp[GP::partials];
        if (resynth) K = juce::jmin (K, img->h);
        if (f0 >= maxF || f0 < 1.0f) K = 0;
        else if (det > 1.0e-4f)
            K = juce::jmin (K, octMode ? (int) (12.0f * std::log2 (maxF / f0) / det) + 1 : (int) ((maxF / f0 - 1.0f) / det) + 1);
        K = juce::jlimit (0, NH, K);

        float rat[NP], a[NP], lv[NP], lf[NP], orig[NP], pg[NP], tmp[NP];
        const float lf0 = std::log2 (juce::jmax (1.0f, f0));
        for (int n = 0; n < K; ++n) rat[n] = octMode ? std::exp2 ((float) n * det / 12.0f) : 1.0f + (float) n * det;

        // pluck gains come first: the pluck blend mode needs them
        const float pluckT = T[TG::PluckAmt];
        const bool pluckOn = pluckT < 0.999f;
        if (pluckOn)
        {
            const float tb = 0.03f * std::pow (400.0f, clamp01 (pluckT));
            const bool hyper = P[PP::pluckBlur] > 0.5f;
            pg[0] = 1.0f;
            for (int n = 1; n < K; ++n)
            {
                const float tau = tb * (0.02f + D.tab[SH::PluckShape][n]);
                pg[n] = hyper ? 1.0f / (1.0f + pv.age / tau) : std::exp (-pv.age / tau);
            }
        }
        else for (int n = 0; n < K; ++n) pg[n] = 1.0f;

        // timbre
        const float mix = T[TG::TimbreMix];
        const int blend = (int) P[PP::blendMode];
        const float* t1 = D.tab[SH::Timbre1]; const float* t2 = D.tab[SH::Timbre2];
        for (int n = 0; n < K; ++n)
        {
            float l;
            switch (blend)
            {
                case 1: l = juce::jmax (0.0f, t1[n] - mix * t2[n]); break;
                case 2: l = t1[n] * (1.0f + mix * (t2[n] - 1.0f)); break;
                case 3: l = t1[n] + mix * (juce::jmax (t1[n], t2[n]) - t1[n]); break;
                case 4: l = t1[n] + mix * (juce::jmin (t1[n], t2[n]) - t1[n]); break;
                case 5: l = t1[n] + mix * (1.0f - pg[n]) * (t2[n] - t1[n]); break;
                default: l = t1[n] + mix * (t2[n] - t1[n]); break;
            }
            l *= juce::jmax (0.0f, 1.0f + D.tab[SH::HarmRand][n] * pv.rnd[n]);
            lv[n] = l;
        }

        // image
        bool imgSilent = false;
        float uniImg[NP];
        const int fmode = (int) P[PP::imgFreqMode];
        if (img != nullptr)
        {
            const int w = img->w, h = img->h;
            const float speed = T[TG::ImgSpeed] * std::pow (64.0f, P[PP::imgCoarse]);
            const float ofsT = T[TG::ImgTime];
            ImgState& is = pv.im[ue];
            if (! is.init) { is.init = true; is.run = 0; is.ofs = ofsT; is.dir = 1; }
            else is.run += (double) ((float) is.dir * speed * dt / img->secPerCol);
            const float sm = P[PP::imgSmooth];
            is.ofs += (ofsT - is.ofs) * (std::abs (sm) < 0.01f ? 1.0f : 1.0f - std::exp (-dt / (sm * sm)));
            if (sm > 0 && std::abs (ofsT - is.ofs) > 0.25f) is.ofs = ofsT;
            double pos = is.run + (double) is.ofs * (double) (w - 1);
            const int loop = (int) P[PP::imgLoop];
            const double la = img->hasLoop ? (double) img->loopStart * (w - 1) : 0.0, lb = img->hasLoop ? (double) img->loopEnd * (w - 1) : (double) (w - 1);
            float xfMix = 0.0f; double xfPos = 0.0;
            if (loop == 0) { if (pos > (double) (w - 1) || pos < 0.0) { imgSilent = true; is.ended = true; } }
            else if (lb - la > 1.0)
            {
                if (loop == 1)
                {
                    if (pos > lb) { is.run -= (lb - la); pos -= (lb - la); }
                    else if (pos < la && speed < 0) { is.run += (lb - la); pos += (lb - la); }
                    const double xf = (double) img->loopXfade * (lb - la);
                    if (xf > 1.0 && pos > lb - xf && la - (lb - pos) >= 0.0) { xfMix = (float) ((pos - (lb - xf)) / xf); xfPos = la - (lb - pos); }
                }
                else
                {
                    if (pos > lb) { is.dir = -is.dir; is.run -= 2.0 * (pos - lb); pos = lb - (pos - lb); }
                    else if (pos < la && (is.dir < 0) != (speed < 0)) { is.dir = -is.dir; is.run += 2.0 * (la - pos); pos = la + (la - pos); }
                }
            }
            if (ue == 0 && v.order == orderCounter) playPos[p].store ((float) (pos / (double) juce::jmax (1, w - 1)));

            float gcol[NP], fcol[NP];
            std::fill (gcol, gcol + NP, 0.0f); std::fill (fcol, fcol + NP, 0.0f);
            sampleImage (*img, img->gain, pos, (int) P[PP::imgGainInterp], gcol);
            sampleImage (*img, img->freq, pos, (int) P[PP::imgFreqInterp], fcol);
            if (xfMix > 0.0f)
            {
                sampleImage (*img, img->gain, xfPos, (int) P[PP::imgGainInterp], tmp);
                for (int y = 0; y < h; ++y) gcol[y] += (tmp[y] - gcol[y]) * xfMix;
            }
            const float gscale = std::pow (16.0f, 0.5f - P[PP::imgGainScale]);
            const float gmix = P[PP::imgGainMix], dn = P[PP::imgDenoise] * 0.05f;
            const float form = T[TG::ImgFormant], formMix = P[PP::imgFormMix];
            const bool formOn = std::abs (form) > 0.5f && formMix > 0.0f;
            const float fscale = T[TG::ImgFreqScale];
            for (int n = 0; n < K; ++n)
            {
                float gI = n < h ? gcol[n] : 0.0f;
                if (formOn)
                {
                    const float mut = 1.0f + D.tab[SH::ImgFormMut][n];
                    const float row = (float) (n + 1) * std::exp2 (-form * mut / 1200.0f) - 1.0f;
                    float gs = 0.0f;
                    if (row >= 0.0f && row < (float) (h - 1)) { const int r0 = (int) row; gs = gcol[r0] + (gcol[r0 + 1] - gcol[r0]) * (row - (float) r0); gs *= (float) (n + 1) / (row + 1.0f); }
                    gI += (gs - gI) * formMix;
                }
                if (dn > 0.0f) gI = juce::jmax (0.0f, gI - dn * (float) (n + 1) * 0.02f);
                gI *= gscale;
                lv[n] *= 1.0f + gmix * (gI - 1.0f);
                const float dv = (n < h ? fcol[n] : 0.0f) * fscale * D.tab[SH::ImgFreqMask][n];
                uniImg[n] = 1.0f;
                if (dv != 0.0f)
                {
                    if (fmode == 0) rat[n] *= std::exp2 (dv / 12.0f);
                    else if (fmode == 1) rat[n] += dv * 0.5f;
                    else if (fmode == 2) rat[n] += dv * 4.0f;
                    else uniImg[n] = juce::jmax (0.0f, 1.0f + dv);
                }
            }
            // sharpening: at a transient, put every partial back on its analysed phase
            const int col = juce::jlimit (0, w - 1, (int) pos);
            const float sharp = P[PP::imgSharp];
            if (resynth && col != is.lastCol && std::abs (sharp) > 0.02f && ! img->phase.empty()
                && (img->transient[(size_t) col] || (sharp > 0 && is.lastCol < 0)))
            {
                const uint8_t* phs = img->phase.data() + (size_t) col * (size_t) h;
                for (int n = 0; n < juce::jmin (K, h); ++n) pv.ph[ue][n] = (uint32_t) phs[n] << 24;
            }
            is.lastCol = col;
        }

        // levels to amplitudes (the built-in brown slope), plus the sub sliders
        for (int n = 0; n < K; ++n) a[n] = lv[n] / rat[n];
        const float subs[3] = { P[PP::sub1], P[PP::sub2], P[PP::sub3] };
        const bool subBelow = P[PP::subMode] > 0.5f;
        float power0 = 1.0e-12f;
        for (int n = 0; n < K; ++n) { orig[n] = a[n]; power0 += a[n] * a[n]; lf[n] = lf0 + std::log2 (rat[n]); }

        float volScalar = 1.0f;
        bool volLast = D.unitOrder[UNIT::Count - 1] == UNIT::VolumeArt;
        float selfOsc[2] = { 0, 0 }, selfLf[2] = { 0, 0 };
        const float lkey = lf0 - std::log2 (kC4);

        for (int ui = 0; ui < UNIT::Count; ++ui)
        {
            switch (D.unitOrder[ui])
            {
                case UNIT::Prism:
                {
                    const float amt = T[TG::PrismAmt];
                    if (std::abs (amt) < 1.0e-4f) break;
                    const int lm = (int) P[PP::prismLvl]; const bool mul = P[PP::prismMode] > 0.5f;
                    for (int n = 1; n < K; ++n)
                    {
                        float s = amt * D.tab[SH::HarmPrism][n];
                        if (lm == 1) s *= clamp01 (lv[n]); else if (lm == 2) s *= 1.0f - clamp01 (lv[n]);
                        if (mul) rat[n] *= std::exp2 (s * std::log2 ((float) (n + 1)) * (1.0f / 27.0f));
                        else rat[n] = juce::jmax (0.05f, rat[n] + s * 0.5f);
                        lf[n] = lf0 + std::log2 (rat[n]);
                    }
                    break;
                }
                case UNIT::Harmonizer:
                {
                    const float hm = T[TG::HzMix];
                    if (hm <= 1.0e-4f || K < 2) break;
                    const float str = P[PP::hzStr];
                    const int limit = 1 + (int) ((float) (K - 1) * clamp01 (T[TG::HzWidth] * 2.0f));
                    const int so = (int) P[PP::hzShiftOfs], ss = (int) P[PP::hzShiftStep], go = (int) P[PP::hzGapOfs], gs = (int) P[PP::hzGapStep];
                    const bool sx = P[PP::hzShiftMul] > 0.5f, gx = P[PP::hzGapMul] > 0.5f;
                    const int gap = juce::jlimit (1, NH, (gx ? 2 * gs - 1 : gs) << go);      // offset counts octaves, step counts harmonics
                    std::memcpy (tmp, a, sizeof (float) * (size_t) K);
                    float add[NP]; std::fill (add, add + K, 0.0f);
                    int m0 = sx ? 2 * ss - 1 : 0;
                    for (int j = 0; j < 9; ++j)
                    {
                        if (! sx) m0 = (j + 1) * ss;
                        else if (j > 0) m0 = (m0 - 1) * ss + 1;
                        if (m0 > limit) break;
                        const int m = m0 << so;
                        if (m <= 1) { if (sx && ss == 1) break; continue; }
                        if (m > limit) break;
                        const float dj = D.tab[SH::HzDist][juce::jmin (NH - 1, j * NH / 9 + NH / 18)] * 2.0f / (float) m;
                        for (int n = 0; n < K; n += gap)
                        {
                            const int t = (n + 1) * m - 1;
                            if (t >= limit || t >= K) break;
                            add[t] += tmp[n] * dj;
                        }
                    }
                    for (int n = 0; n < K; ++n)
                    {
                        const float mk = hm * D.tab[SH::HzMask][n];
                        const float keep = (gap > 1 && (n % gap) != 0) ? 1.0f - mk * str * 0.7f : 1.0f;
                        a[n] = a[n] * keep + mk * str * add[n];
                    }
                    break;
                }
                case UNIT::Blur:
                {
                    const float harm = T[TG::BlurHarm], bm = P[PP::blurMix];
                    if (harm > 1.0e-3f && bm > 0.0f && K > 1)
                    {
                        const float ru = std::log2 (juce::jmax (1.0e-4f, std::pow (harm, 0.5f + P[PP::blurTop]))), rd = std::log2 (juce::jmax (1.0e-4f, std::pow (harm, 1.5f)));
                        const float gb = 2.0f * P[PP::blurBottom];
                        for (int n = 0; n < K; ++n) tmp[n] = a[n] * rat[n];
                        float y = tmp[0];
                        float up[NP]; up[0] = y;
                        for (int n = 1; n < K; ++n) { y = juce::jmax (tmp[n], y * std::exp2 (ru * (lf[n] - lf[n - 1]))); up[n] = y; }
                        y = up[K - 1];
                        for (int n = K - 2; n >= 0; --n) { y = juce::jmax (up[n], y * std::exp2 (rd * (lf[n + 1] - lf[n]))); up[n] = y; }
                        for (int n = 0; n < K; ++n)
                            a[n] = (tmp[n] + bm * D.tab[SH::BlurMask][n] * juce::jmax (0.0f, up[n] * gb - tmp[n])) / rat[n];
                    }
                    const float ba = P[PP::blurAttack], bd = P[PP::blurDecay];
                    if ((ba > 1.0e-3f || bd > 1.0e-3f) && bm > 0.0f)
                    {
                        const float ca = ba > 1.0e-3f ? 1.0f - std::exp (-dt / (ba * ba * 4.0f)) : 1.0f, cd = bd > 1.0e-3f ? 1.0f - std::exp (-dt / (bd * bd * 4.0f)) : 1.0f;
                        for (int n = 0; n < K; ++n)
                        {
                            float& s = pv.blur[ue][n];
                            s += (a[n] - s) * (a[n] > s ? ca : cd);
                            a[n] += (s - a[n]) * bm * D.tab[SH::BlurMask][n];
                        }
                    }
                    break;
                }
                case UNIT::Pluck:
                    if (pluckOn && blend != 5) for (int n = 1; n < K; ++n) a[n] *= pg[n];
                    break;
                case UNIT::Filter:
                {
                    float G[2][NP];
                    const float fm = T[TG::FiltMix];
                    for (int f = 0; f < 2; ++f)
                    {
                        const float* Q = P + f * kFilterStride;
                        float* g = G[f];
                        const bool used = f == 0 ? fm > -0.999f : std::abs (fm) > 1.0e-3f;
                        const float lfc = std::log2 (20.0f) + clamp01 (T[TG::F1Freq + f]) * 9.9658f + Q[PP::f1Kb] * lkey;
                        const float res = T[TG::F1Res + f], osc = Q[PP::f1ResOsc];
                        const float lfr = lfc + T[TG::F1ResOfs + f] / 1200.0f;
                        selfOsc[f] = used ? osc : 0.0f; selfLf[f] = lfr;
                        if (! used) { for (int n = 0; n < K; ++n) g[n] = 1.0f; continue; }
                        const int type = (int) Q[PP::f1Type], rtype = (int) Q[PP::f1ResType];
                        const float wOct = 0.1f * std::pow (40.0f, clamp01 (T[TG::F1Width + f]));
                        const bool hzScale = Q[PP::f1Scale] > 0.5f, rHz = Q[PP::f1ResScale] > 0.5f;
                        const float fc = std::exp2 (lfc), frs = std::exp2 (lfr);
                        float rw = 0.05f * std::pow (40.0f, clamp01 (T[TG::F1ResWidth + f]));
                        if (Q[PP::f1Adapt] > 0.5f) rw *= std::sqrt (clampf (f0 / kC4, 0.25f, 4.0f));
                        const float* mask = D.tab[SH::F1Mask + f]; const float* rmask = D.tab[SH::F1ResMask + f];
                        const bool open = (type <= 3 && lfc > 14.2f && res <= 0.0f);       // low pass fully open: nothing to do
                        for (int n = 0; n < K; ++n)
                        {
                            if (open) { g[n] = 1.0f; continue; }
                            const float d = hzScale ? (f0 * rat[n] - fc) / (fc * wOct) : (lf[n] - lfc) / wOct;
                            float gg = filterShape (type, d, D.tab[SH::FiltShape1], D.tab[SH::FiltShape2]);
                            if (res > 0.0f)
                            {
                                const float dr = rHz ? (f0 * rat[n] - frs) / (frs * rw) : (lf[n] - lfr) / rw;
                                if (std::abs (dr) < 6.0f)
                                {
                                    const float r = resShape (rtype, dr, D.tab[SH::ResShape1], D.tab[SH::ResShape2], pv.rng) * freqTab (rmask, lf[n]);
                                    gg = rtype == 8 ? gg + res * 2.0f * r : juce::jmax (0.0f, gg * (1.0f + res * 5.0f * r) + res * r * 0.5f);
                                }
                            }
                            g[n] = 1.0f + freqTab (mask, lf[n]) * (gg - 1.0f);
                        }
                    }
                    if (fm >= 0) for (int n = 0; n < K; ++n) a[n] *= G[0][n] * (1.0f + fm * (G[1][n] - 1.0f));
                    else         for (int n = 0; n < K; ++n) a[n] *= G[0][n] - fm * (G[1][n] - G[0][n]);
                    break;
                }
                case UNIT::Phaser:
                {
                    const float pm = T[TG::PhMix];
                    if (pm <= 1.0e-4f) break;
                    const int type = (int) P[PP::phType], scale = (int) P[PP::phScale];
                    const float wd = clamp01 (T[TG::PhWidth] + (P[PP::phLfo] > 0.5f ? 0.25f * P[PP::lfoAmt] * (float) std::sin (6.283185307 * timeSec * (double) bps * 0.25) : 0.0f));
                    const float per = scale == 0 ? 0.25f * std::pow (16.0f, wd) : (scale == 1 ? 100.0f * std::pow (40.0f, wd) : 1.0f + 40.0f * wd * wd);
                    const float ofs = T[TG::PhOfs] + phaserClock[p] + P[PP::phKb] * lkey;
                    const float* mask = D.tab[SH::PhMask]; const float* ps = D.tab[SH::PhaserShape];
                    for (int n = 0; n < K; ++n)
                    {
                        const float pos = scale == 0 ? lf[n] : (scale == 1 ? f0 * rat[n] : (float) n);
                        float u = pos / per - ofs; u -= std::floor (u);
                        const float c = 0.5f + 0.5f * std::cos (kTwoPi * u);
                        const float mk = pm * freqTab (mask, lf[n]);
                        float g = 1.0f;
                        const float tri = std::abs (2.0f * u - 1.0f);          // 1 at the notch centre, 0 half way between
                        switch (type)
                        {
                            case 0: g = 1.0f - mk * c * c; break;                                                    // Classic
                            case 1: g = 1.0f - mk * tri; break;                                                      // Triangle
                            case 2: g = 1.0f - mk * (1.0f - std::sqrt (juce::jmax (0.0f, 1.0f - tri * tri))); break;  // Eggs
                            case 3: g = 1.0f - mk * std::sqrt (c); break;                                            // Deep
                            case 4: g = 1.0f - mk * std::sqrt (std::sqrt (c)); break;                                // Deeper
                            case 5: { const float q = clamp01 ((c - 0.5f) * 4.0f + 0.5f); g = 1.0f - mk * q * q * (3.0f - 2.0f * q); break; }   // Condom: long rounded notch
                            case 6: { const float c2 = 0.5f + 0.5f * std::cos (kTwoPi * (u - 0.3f)); const float q = c * c * c, q2 = c2 * c2 * c2; g = 1.0f - mk * juce::jmin (1.0f, q * q + q2 * q2); break; }   // Twins
                            case 7: { const float c4 = 0.5f + 0.5f * std::cos (kTwoPi * 4.0f * u); g = (1.0f - mk * c4 * c4) * (1.0f + mk * 0.3f * std::floor (u * 4.0f)); break; }                            // Cascade
                            case 8: g = c > 0.5f ? 1.0f - mk : 1.0f; break;                                          // Box
                            case 9: rat[n] *= std::exp2 (mk * (2.0f * ps[(int) (u * (float) (NH - 1))] - 1.0f) / 12.0f); lf[n] = lf0 + std::log2 (rat[n]); break;   // freq
                            default: g = 1.0f - mk * (1.0f - ps[(int) (u * (float) (NH - 1))]); break;               // custom
                        }
                        a[n] *= g;
                    }
                    break;
                }
                case UNIT::EQ:
                {
                    const float ea = P[PP::eqAmt];
                    const float* le = D.tab[SH::LocalEQ]; const float* ge = D.tab[SH::GlobalEQ];
                    for (int n = 0; n < K; ++n) a[n] *= le[n] * (1.0f + ea * (freqTab (ge, lf[n]) - 1.0f));
                    break;
                }
                case UNIT::HarmProt:
                {
                    const float pr = P[PP::prot];
                    if (pr <= 0.0f) break;
                    for (int n = 0; n < K; ++n) { const float s = pr * D.tab[SH::HarmProt][n]; if (s > 0.0f) a[n] = juce::jmax (a[n], s * orig[n]); }
                    break;
                }
                case UNIT::HarmClip:
                {
                    const float th = clamp01 (T[TG::Clip]);
                    const int mode = (int) P[PP::clipMode];
                    if (mode == 0 && th >= 0.999f) break;
                    const float* cs = D.tab[SH::HarmClip];
                    const float vs = volLast ? 1.0f : volScalar;
                    for (int n = 0; n < K; ++n)
                    {
                        const float t = juce::jmax (1.0e-6f, th * th * cs[n] * 1.2f * vs);
                        const float x = a[n];
                        switch (mode)
                        {
                            case 0: a[n] = juce::jmin (x, t); break;
                            case 1: { float g = clamp01 ((x / t - 0.5f) / 1.5f); a[n] = x * g * g * (3 - 2 * g); break; }
                            case 2: { float g = clamp01 ((x / t - 0.8f) / 0.4f); a[n] = x * g * g * (3 - 2 * g); break; }
                            case 3: a[n] = x >= t ? x : 0.0f; break;
                            default: a[n] = juce::jmax (0.0f, x - t); break;
                        }
                    }
                    break;
                }
                case UNIT::VolumeArt:
                {
                    float vg = dbToGain (P[PP::envVol]) * T[TG::Volume];
                    if (P[PP::velVol] > 0.5f) vg *= v.vel * v.vel;
                    volScalar = vg;
                    if (! volLast) for (int n = 0; n < K; ++n) a[n] *= vg;
                    break;
                }
                default: break;
            }
        }

        // auto-gain, then the volume articulation if it sits at the end of the chain
        const float ag = P[PP::autoGain];
        float gain = volLast ? volScalar : 1.0f;
        if (ag > 0.0f)
        {
            float pw = 1.0e-12f; for (int n = 0; n < K; ++n) pw += a[n] * a[n];
            const float target = P[PP::autoGainMode] > 0.5f ? 1.3f : power0;
            gain *= clampf (std::pow (target / pw, 0.5f * ag), 0.05f, 8.0f);
        }

        // gates: note gate (when no volume envelope), arpeggiator points, image end
        const TargetDef& vt = D.tg[TG::Volume];
        const EnvState& ves = vt.env.global ? genv[p][TG::Volume] : pv.es[ue][TG::Volume];
        if (vt.env.on && ves.arpSeen && chordN > 1 && v.gate)
        {
            int rank = 0; for (int i = 0; i < chordN; ++i) if (chord[i] < v.note) ++rank;
            if (((ves.arp % chordN) + chordN) % chordN != rank) gain = 0.0f;
        }
        const float dck = juce::jmax (0.0005f, P[PP::declick] * 0.001f);
        if (ue == 0)
        {
            bool open = vt.env.on ? ! ves.done : v.gate;
            if (imgSilent) open = false;
            const float gateTarget = open ? 1.0f : 0.0f;
            const float gstep = dt / (open ? dck : juce::jmax (dck, 0.004f));
            const float prevGate = pv.gateGain;
            pv.gateGain = pv.gateGain < gateTarget ? juce::jmin (gateTarget, pv.gateGain + gstep) : juce::jmax (gateTarget, pv.gateGain - gstep);
            if (first && P[PP::declick] <= 0.01f) pv.gateGain = gateTarget;
            if (! open && prevGate <= 0.0f && ! first) return false;       // one more period at zero, so the fade completes
        }
        gain *= pv.gateGain;

        // ramp: how the partials come in at the start of the note
        const float rp = P[PP::ramp];
        const float rampT = std::abs (rp) * 0.2f;
        const bool ramping = pv.age < rampT + dck + 0.05f;

        // write the targets the synthesis loop will glide to
        const float cycPerHz = (float) H / sr;
        const float inv = 1.0f / (float) ctrlDiv;
        const int Kall = juce::jmax (K, pv.Klast[ue]);
        float* amp = pv.amp[ue]; float* dAmp = pv.dAmp[ue]; float* cyc = pv.cyc[ue]; float* dCyc = pv.dCyc[ue]; float* us = pv.us[ue];
        const float* usT = D.tab[SH::HarmUniPitch];
        const bool imgUni = img != nullptr && fmode == 3;
        for (int n = 0; n < Kall; ++n)
        {
            float tg = 0.0f;
            if (n < K)
            {
                tg = a[n] * gain;
                if (ramping)
                {
                    if (rp < 0) tg *= clamp01 (pv.age / juce::jmax (1.0e-4f, rampT));
                    else if (rp > 0) tg *= clamp01 ((pv.age - rampT * (float) n / (float) juce::jmax (1, K - 1)) / dck + 1.0f);
                    tg *= clamp01 ((pv.age + dt) / (dck * (0.2f + 0.8f * (float) n / (float) juce::jmax (1, K))));
                }
                const float c = f0 * rat[n] * cycPerHz;
                if (first || n >= pv.Klast[ue] || cyc[n] <= 0.0f) { cyc[n] = c; dCyc[n] = 0.0f; }
                else dCyc[n] = (c - cyc[n]) * inv;
                us[n] = usT[n] * (imgUni ? uniImg[n] : 1.0f);
            }
            else dCyc[n] = 0.0f;
            if (! std::isfinite (tg)) tg = 0.0f;
            dAmp[n] = (tg - amp[n]) * inv;
        }
        // extra partials: subs below the fundamental and the filters' self-oscillation
        const float xg = gain * (volLast ? 1.0f : volScalar);
        for (int i = 0; i < NX; ++i)
        {
            const int n = NH + i;
            float tg = 0.0f, c = 0.0f;
            if (i < 3)
            {
                // sub 1 is an octave down; the others sit two and three octaves down, or on the 3rd and 5th harmonic of sub 1
                static const float below[3] = { 0.5f, 0.25f, 0.125f }, around[3] = { 0.5f, 1.5f, 2.5f };
                if (subs[i] > 0.0f) { tg = subs[i] * 0.8f * xg; c = f0 * (subBelow ? below[i] : around[i]) * cycPerHz; }
            }
            else if (selfOsc[i - 3] > 0.0f) { tg = selfOsc[i - 3] * 0.6f * xg; c = std::exp2 (selfLf[i - 3]) * cycPerHz; }
            if (first || cyc[n] <= 0.0f) { cyc[n] = c; dCyc[n] = 0.0f; } else if (c > 0.0f) dCyc[n] = (c - cyc[n]) * inv; else dCyc[n] = 0.0f;
            us[n] = 1.0f;
            dAmp[n] = (tg - amp[n]) * inv;
        }
        pv.K[ue] = Kall; pv.Klast[ue] = K;

        // display: the newest voice of the part on show
        if (ue == 0 && p == vizPart.load (std::memory_order_relaxed) && v.order == orderCounter)
        {
            vizSeq.fetch_add (1, std::memory_order_acq_rel);
            viz.count = K;
            for (int n = 0; n < K; ++n) { viz.freq[n] = f0 * rat[n]; viz.amp[n] = a[n] * gain; }
            vizSeq.fetch_add (1, std::memory_order_acq_rel);
        }
        return true;
    }

    void finish (PartVoice& pv)
    {
        pv.active = false; pv.started = false;
    }

    void startPhases (PartVoice& pv, const float* P, const PartData& D, int U, float uniPhase)
    {
        const float r = P[PP::phaseRand];
        float th0 = P[PP::phaseStart] / 360.0f;
        if (r > 0) th0 += pv.rng.uni() * r;
        const bool blurPh = uniPhase >= 0.99f && U > 1;
        const float* pt = D.tab[SH::Phase];
        for (int u = 0; u < MAXU; ++u)
        {
            float th = th0;
            if (U > 1 && ! blurPh) th += uniPhase < 0.5f ? ((float) u / (float) U) * (1.0f - 2.0f * uniPhase) : (2.0f * uniPhase - 1.0f);
            for (int n = 0; n < NP; ++n)
            {
                float c = (float) (n + 1) * th + (n < NH ? pt[n] * 0.5f : 0.0f);
                if (r < 0) c += pv.rng.bi() * 0.5f * -r;
                if (blurPh) c += pv.rng.uni();
                c -= 0.25f;                 // partials are sines
                c -= std::floor (c);
                pv.ph[u][n] = (uint32_t) (int64_t) (c * 4294967296.0f);
            }
        }
    }

    //----------------------------------------------------------------------------------------------
    float sr = 48000.0f; int H = 64, N = 256, ctrlDiv = 4;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> lobe, corr;
    float sinT[4097], cosT[4097];
    std::vector<float> spec[6];             // wet L/R, dry L/R, part L/R (half spectra, interleaved)
    std::vector<std::complex<float>> cin, cout;
    std::vector<float> ola[4], outBuf[4];
    int outPos = 0; float fftScale = 1.0f;

    std::unique_ptr<Voice[]> voices;
    float gp[GP::Count], pp[2][PP::Count], xp[XP::Count], em[2][TG::FirstMapping][EM::Count];
    std::shared_ptr<const PartData> data[2];
    std::vector<Ev> events, pendingOn;
    int64_t sampleTime = 0; uint64_t tick = 0, orderCounter = 0; int ctlTick = 0;
    double timeSec = 0, lastOnTime = -1;
    float bps = 2.0f, bend = 0.0f, mod[3] = { 0, 0, 0 }, modXSpeed = 0, phaserClock[2] = { 0, 0 }, partFxMix[2] = { 0, 0 };
    int lastNote = -1, heldCount = 0, heldIndex = 0, chordCount = 0, chordN = 0, chord[MAXV];
    bool sustainDown = false, strumFlip = false, genvStamp[2] = { false, false };
    EnvState genv[2][TG::Count]; int genvDone[2][TG::Count] = {};
    Rng rng;
    VizFrame viz; std::atomic<int> vizSeq { 0 }, vizPartials { 0 };
};
} // namespace pt
