#include "IrData.h"

namespace ecr
{
// Broadband level every response is matched to (RMS gain for noise-like input).
static constexpr float kTargetLevel = 0.4f;

void finishIrSource (IrSource& s)
{
    // energy arriving at each output
    double eL = 0.0, eR = 0.0;
    auto energy = [&] (int c) { double e = 0.0; for (float v : s.ch[c]) e += (double) v * v; return e; };
    if (s.numChannels == 1)       { eL = eR = energy (0); }
    else if (s.numChannels == 2)  { eL = energy (0); eR = energy (1); }
    else                          { eL = energy (0) + energy (3); eR = energy (1) + energy (2); }

    // referenced to 48 kHz so the same room has the same loudness at any file rate
    const double e = std::max (eL, eR) * 48000.0 / s.sampleRate;
    s.gain = e > 1e-20 ? (float) (kTargetLevel / std::sqrt (e)) : 1.0f;
    s.gain *= (float) (48000.0 / s.sampleRate);

    float peak = 0.0f;
    for (int c = 0; c < s.numChannels; ++c)
        for (float& v : s.ch[c]) { v *= s.gain; peak = std::max (peak, std::abs (v)); }
    s.peak = peak > 0.0f ? peak : 1.0f;

    // Overview: log amplitude, with level differences between channels exaggerated
    // (as on the module) so an off-centre response is obvious at a glance.
    double chEnergy[4] {}, maxE = 1e-30;
    for (int c = 0; c < s.numChannels; ++c) { chEnergy[c] = energy (c); maxE = std::max (maxE, chEnergy[c]); }
    for (int c = 0; c < s.numChannels; ++c)
    {
        const double offsetDb = 10.0 * std::log10 (std::max (chEnergy[c], 1e-30) / maxE); // <= 0
        for (int x = 0; x < 128; ++x)
        {
            const int a = (int) ((int64_t) x * s.length / 128), b = std::max (a + 1, (int) ((int64_t) (x + 1) * s.length / 128));
            float m = 0.0f;
            for (int i = a; i < b && i < s.length; ++i) m = std::max (m, std::abs (s.ch[c][(size_t) i]));
            const double db = 20.0 * std::log10 (std::max ((double) m / s.peak, 1e-9)) + offsetDb;
            s.overview[(size_t) c][(size_t) x] = (uint8_t) juce::jlimit (0.0, 255.0, (db + 72.0) / 72.0 * 255.0);
        }
    }
}

std::shared_ptr<IrSource> makeIrFromAudio (const float* const* channels, int numChannels, int length,
                                           double sampleRate, const juce::String& name)
{
    auto s = std::make_shared<IrSource>();
    s->name = name;
    s->sampleRate = sampleRate;
    s->numChannels = numChannels >= 4 ? 4 : (numChannels >= 2 ? 2 : 1);
    s->length = std::max (0, std::min (length, (int) (kMaxIrSeconds * sampleRate)));
    for (int c = 0; c < s->numChannels; ++c)
        s->ch[c].assign (channels[c], channels[c] + s->length);
    finishIrSource (*s);
    return s;
}

std::shared_ptr<IrSource> loadIrFromStream (std::unique_ptr<juce::InputStream> stream, const juce::String& name)
{
    if (stream == nullptr) return nullptr;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::move (stream)));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) return nullptr;

    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxIrSeconds * reader->sampleRate));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);
    return makeIrFromAudio (buf.getArrayOfReadPointers(), buf.getNumChannels(), len, reader->sampleRate, name);
}

static bool readStereo (const juce::File& f, juce::AudioBuffer<float>& buf, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->numChannels != 2 || reader->lengthInSamples <= 0) return false;
    rate = reader->sampleRate;
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxIrSeconds * rate));
    buf.setSize (2, len);
    return reader->read (&buf, 0, len, 0, true, true);
}

std::shared_ptr<IrSource> loadIrFromFile (const juce::File& f)
{
    // True-stereo sets that come as two stereo files: "Name L" is what the left
    // input sends to both outputs, "Name R" the same for the right input.
    const auto stem = f.getFileNameWithoutExtension();
    const auto partner = f.getSiblingFile (stem.dropLastCharacters (1) + "R" + f.getFileExtension());
    if (stem.endsWith (" L") && partner.existsAsFile())
    {
        juce::AudioBuffer<float> l, r;
        double rateL = 0.0, rateR = 0.0;
        if (readStereo (f, l, rateL) && readStereo (partner, r, rateR) && rateL == rateR)
        {
            const int len = std::max (l.getNumSamples(), r.getNumSamples());
            l.setSize (2, len, true, true); r.setSize (2, len, true, true);
            const float* chans[4] = { l.getReadPointer (0), r.getReadPointer (1), l.getReadPointer (1), r.getReadPointer (0) };
            auto s = makeIrFromAudio (chans, 4, len, rateL, stem.dropLastCharacters (2) + f.getFileExtension());
            s->file = f;
            return s;
        }
    }
    auto s = loadIrFromStream (f.createInputStream(), f.getFileName());
    if (s != nullptr) s->file = f;
    return s;
}

//==============================================================================
void IrRenderer::resample (const std::vector<float>& in, std::vector<float>& out, double ratio, int maxOut)
{
    const int inLen = (int) in.size();
    const int outLen = std::min (maxOut, (int) std::ceil ((double) inLen / ratio));
    out.assign ((size_t) std::max (0, outLen), 0.0f);
    if (outLen <= 0) return;

    if (std::abs (ratio - 1.0) < 1e-9)
    {
        std::copy (in.begin(), in.begin() + outLen, out.begin());
        return;
    }

    // Kaiser-windowed sinc, 16 zero crossings each side, tabulated for speed.
    constexpr int Z = 16, RES = 512;
    static const std::vector<float> table = []
    {
        std::vector<float> t ((size_t) (Z * RES + 2), 0.0f);
        const double beta = 10.0;
        auto bessel = [] (double x) { double s = 1.0, term = 1.0; for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; } return s; };
        const double norm = bessel (beta);
        for (int i = 0; i <= Z * RES; ++i)
        {
            const double x = (double) i / RES;
            const double sinc = i == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const double r = x / Z;
            t[(size_t) i] = (float) (sinc * bessel (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm);
        }
        return t;
    }();

    const double fc = std::min (1.0, 1.0 / ratio);   // also the anti-alias cutoff when pitching up
    const double half = Z / fc;
    const float scale = (float) (fc * ratio);        // keeps the frequency response level
    const float* src = in.data();

    for (int n = 0; n < outLen; ++n)
    {
        const double t = n * ratio;
        const int k0 = std::max (0, (int) std::ceil (t - half));
        const int k1 = std::min (inLen - 1, (int) std::floor (t + half));
        float sum = 0.0f;
        for (int k = k0; k <= k1; ++k)
        {
            const double x = std::abs (t - k) * fc * RES;
            const int i = (int) x;
            if (i >= Z * RES) continue;
            const float fr = (float) (x - i);
            sum += src[k] * (table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * fr);
        }
        out[(size_t) n] = sum * scale;
    }
}

ProcessedIr IrRenderer::render (const IrSourcePtr& src, const IrSettings& st, double hostRate)
{
    ProcessedIr out;
    if (src == nullptr || src->length <= 0 || src->numChannels <= 0)
        return out;

    const double octaves = curve::pitchOctaves (st.pitch / (double) kKnobSteps);
    const double ratio = src->sampleRate / hostRate * std::pow (2.0, octaves);

    if (src != cachedSource || ratio != cachedRatio)
    {
        const int maxOut = (int) (kMaxPitchedSeconds * hostRate);
        for (int c = 0; c < 4; ++c) cached[c].clear();
        for (int c = 0; c < src->numChannels; ++c)
            resample (src->ch[c], cached[c], ratio, maxOut);
        cachedLen = (int) cached[0].size();
        cachedSource = src;
        cachedRatio = ratio;
    }

    const int start = std::min (cachedLen, (int) std::lround (st.pos / (double) kKnobSteps * cachedLen));
    const int attack = (int) std::lround (curve::timeSeconds (st.attack / (double) kKnobSteps) * hostRate);
    const bool full = curve::releaseIsFull (st.release / (double) kKnobSteps);
    const int release = std::max (1, (int) std::lround (curve::timeSeconds (st.release / (double) kKnobSteps) * hostRate));

    int len = cachedLen - start;
    if (! full) len = std::min (len, attack + release);
    len = std::max (0, len);

    const int nch = src->numChannels;
    for (int c = 0; c < nch; ++c)
    {
        auto& h = out.h[c];
        h.resize ((size_t) len);
        const float* s = cached[c].data() + start;
        for (int i = 0; i < len; ++i)
        {
            float e = 1.0f;
            if (i < attack) e = (float) i / (float) attack;
            else if (! full) e = 1.0f - (float) (i - attack) / (float) release;
            h[(size_t) i] = s[i] * e;
        }
    }

    // drop a tail that has faded below -100 dB of the peak
    float peak = 0.0f;
    for (int c = 0; c < nch; ++c) for (float v : out.h[c]) peak = std::max (peak, std::abs (v));
    const float floor = peak * 1e-5f;
    int last = len;
    while (last > 1)
    {
        bool quiet = true;
        for (int c = 0; c < nch && quiet; ++c) quiet = std::abs (out.h[c][(size_t) last - 1]) <= floor;
        if (! quiet) break;
        --last;
    }
    len = std::max (len > 0 ? 1 : 0, std::min (len, last));
    for (int c = 0; c < nch; ++c) out.h[c].resize ((size_t) len);

    out.length = len;
    if (len > 0)
    {
        if (nch == 1) { out.h[1] = out.h[0]; out.active[0] = out.active[1] = true; }
        else for (int c = 0; c < nch; ++c) out.active[c] = true;
    }
    return out;
}
} // namespace ecr
