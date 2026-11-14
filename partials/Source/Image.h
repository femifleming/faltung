// Partials: the image behind image synthesis and resynthesis. One column per moment, one row per partial.
#pragma once
#include "Core.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

namespace pt
{
struct ImageData
{
    int w = 0, h = 0;                    // columns (time), rows (partials, row 0 is the lowest)
    std::vector<float> gain;             // level per partial (brown-calibrated: amplitude times harmonic number)
    std::vector<float> freq;             // frequency offset in semitones, 0 is none
    std::vector<uint8_t> phase;          // resynthesis only: analysed phase, 0..255 is one cycle
    std::vector<uint8_t> transient;      // resynthesis only: 1 where a column starts a transient
    bool resynth = false;
    float baseFreq = kC4;                // resynthesis: frequency of row 0 when the root key is played
    float secPerCol = 0.02f;
    float loopStart = 0.0f, loopEnd = 1.0f, loopXfade = 0.0f, loopGain = 0.0f;   // fractions of the width
    bool hasLoop = false;
    std::vector<float> markers;          // positions (0..1) of regions, from audio markers or the drum-loop slicer
    juce::String name;

    bool valid() const { return w > 0 && h > 0 && gain.size() == (size_t) (w * h); }
    float* g (int x) { return gain.data() + (size_t) x * (size_t) h; }
    const float* g (int x) const { return gain.data() + (size_t) x * (size_t) h; }
    const float* f (int x) const { return freq.data() + (size_t) x * (size_t) h; }
    float seconds() const { return (float) w * secPerCol; }
    void alloc (int cols, int rows)
    {
        w = cols; h = rows;
        gain.assign ((size_t) (w * h), 0.0f);
        freq.assign ((size_t) (w * h), 0.0f);
        phase.clear(); transient.clear();
    }

    //----------------------------------------------------------------------------------------------
    // From a picture: brightness is level, the bottom row is the first partial
    static std::shared_ptr<ImageData> fromPicture (const juce::Image& src, const std::shared_ptr<const ImageData>& keepFreqOf = nullptr, bool intoFreqPlane = false)
    {
        if (! src.isValid()) return nullptr;
        const int rows = juce::jmin (NH, src.getHeight());
        const int cols = juce::jlimit (1, 4096, src.getWidth());
        juce::Image im = src.rescaled (cols, rows, juce::Graphics::mediumResamplingQuality).convertedToFormat (juce::Image::ARGB);
        auto d = std::make_shared<ImageData>();
        if (keepFreqOf != nullptr && keepFreqOf->valid() && intoFreqPlane) *d = *keepFreqOf;
        else d->alloc (cols, rows);
        juce::Image::BitmapData bd (im, juce::Image::BitmapData::readOnly);
        const int W = juce::jmin (cols, d->w), H = juce::jmin (rows, d->h);
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
            {
                const float v = bd.getPixelColour (x, rows - 1 - y).getPerceivedBrightness();
                if (intoFreqPlane) d->freq[(size_t) (x * d->h + y)] = (v - 0.5f) * 2.0f;
                else               d->gain[(size_t) (x * d->h + y)] = v * v;
            }
        d->resynth = intoFreqPlane ? d->resynth : false;
        return d;
    }

    juce::Image toPicture (int plane) const       // 0 gain, 1 frequency, 2 both
    {
        juce::Image im (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), true);
        if (! valid()) return im;
        juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y)
            {
                const float gv = std::sqrt (clamp01 (gain[(size_t) (x * h + y)]));
                const float fv = clamp01 (0.5f + 0.5f * freq[(size_t) (x * h + y)]);
                juce::Colour c = plane == 0 ? juce::Colour::greyLevel (gv)
                               : plane == 1 ? juce::Colour::greyLevel (fv)
                                            : juce::Colour::fromFloatRGBA (gv, gv * (0.5f + 0.5f * fv), gv * (1.5f - fv) * 0.66f, 1.0f);
                bd.setPixelColour (x, h - 1 - y, c);
            }
        return im;
    }

    //----------------------------------------------------------------------------------------------
    // From audio: split the spectrum into 516 bands on a grid of baseFreq and follow level, pitch and phase in each
    static std::shared_ptr<ImageData> fromAudio (const juce::AudioBuffer<float>& buf, double sr, bool highPrecision, int channelMode /*0 mono, 1 left, 2 right*/)
    {
        const int len = buf.getNumSamples();
        if (len < 64 || sr <= 0) return nullptr;
        std::vector<float> x ((size_t) len);
        const int nc = buf.getNumChannels();
        for (int i = 0; i < len; ++i)
        {
            const float l = buf.getSample (0, i), r = buf.getSample (nc > 1 ? 1 : 0, i);
            x[(size_t) i] = channelMode == 2 ? 0.5f * (l - r) : 0.5f * (l + r);      // 2 is the side signal, anything else the middle
        }

        float fa = highPrecision ? 40.0f : 80.0f;      // generic: 255 wider bands, high precision: 516
        if (highPrecision)
        {
            const float p = detectPitch (x, sr);
            if (p > 0) fa = p / std::ceil (p / 40.0f);
        }
        const int order = sr > 60000 ? 13 : 12;
        const int W = 1 << order;
        int hop = juce::jmax (64, (int) (sr / 174.0));      // about 174 columns a second
        while (len / hop > 8000) hop *= 2;
        const int cols = juce::jmax (1, len / hop + 1);
        const int rows = juce::jmin (highPrecision ? NH : 255, (int) ((float) sr * 0.5f / fa) - 1);
        if (rows < 4) return nullptr;

        auto d = std::make_shared<ImageData>();
        d->alloc (cols, rows);
        d->resynth = true; d->baseFreq = fa; d->secPerCol = (float) hop / (float) sr;
        d->phase.assign ((size_t) (cols * rows), 0);
        d->transient.assign ((size_t) cols, 0);

        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) W), a ((size_t) W * 2), b ((size_t) W * 2), flux ((size_t) cols, 0.0f), prevMag ((size_t) W / 2, 0.0f);
        for (int i = 0; i < W; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) W);
        const float binHz = (float) sr / (float) W;
        const int dH = juce::jmax (1, W / 32);          // second frame, a little later, gives the true frequency of each bin
        const float ampScale = 4.0f / ((float) W * std::sqrt (1.5f));
        std::vector<float> bandE ((size_t) rows), bandPk ((size_t) rows); std::vector<uint8_t> used ((size_t) W / 2 + 4);

        auto frame = [&] (std::vector<float>& out, int centre)
        {
            for (int i = 0; i < W; ++i)
            {
                const int s = centre - W / 2 + i;
                out[(size_t) i] = (s >= 0 && s < len) ? x[(size_t) s] * win[(size_t) i] : 0.0f;
            }
            std::fill (out.begin() + W, out.end(), 0.0f);
            fft.performRealOnlyForwardTransform (out.data(), true);
        };

        for (int c = 0; c < cols; ++c)
        {
            const int centre = c * hop;
            frame (a, centre);
            frame (b, centre + dH);
            float fl = 0.0f;
            for (int k = 1; k < W / 2; ++k)
            {
                const float m = std::sqrt (a[(size_t) (2 * k)] * a[(size_t) (2 * k)] + a[(size_t) (2 * k + 1)] * a[(size_t) (2 * k + 1)]);
                fl += juce::jmax (0.0f, m - prevMag[(size_t) k]);
                prevMag[(size_t) k] = m;
            }
            flux[(size_t) c] = fl;
            float* gcol = d->g (c);
            // peaks first: each goes to the band its true frequency falls in, with the energy of its whole lobe
            std::fill (bandE.begin(), bandE.end(), 0.0f); std::fill (bandPk.begin(), bandPk.end(), 0.0f);
            std::fill (used.begin(), used.end(), (uint8_t) 0);
            auto p2 = [&] (int k) { return a[(size_t) (2 * k)] * a[(size_t) (2 * k)] + a[(size_t) (2 * k + 1)] * a[(size_t) (2 * k + 1)]; };
            for (int k = 2; k < W / 2 - 2; ++k)
            {
                const float m = p2 (k);
                if (m <= p2 (k - 1) || m < p2 (k + 1) || m < 1.0e-9f) continue;
                const float pa = std::atan2 (a[(size_t) (2 * k + 1)], a[(size_t) (2 * k)]), pb = std::atan2 (b[(size_t) (2 * k + 1)], b[(size_t) (2 * k)]);
                float dp = pb - pa - kTwoPi * (float) k * (float) dH / (float) W;
                dp -= kTwoPi * std::round (dp / kTwoPi);
                const float fTrue = clampf (((float) k + dp * (float) W / (kTwoPi * (float) dH)), (float) k - 1.0f, (float) k + 1.0f) * binHz;
                const int n = (int) std::floor (fTrue / fa - 0.5f);
                if (n < 0 || n >= rows) continue;
                float e = 0.0f;
                for (int q = k - 2; q <= k + 2; ++q) if (! used[(size_t) q]) { e += p2 (q); used[(size_t) q] = 1; }
                bandE[(size_t) n] += e;
                if (m > bandPk[(size_t) n])
                {
                    bandPk[(size_t) n] = m;
                    const float fc = (float) (n + 1) * fa;
                    d->freq[(size_t) (c * rows + n)] = 12.0f * std::log2 (fTrue / fc);
                    float ph = (pa + 3.14159265f * fTrue / binHz) / kTwoPi; ph -= std::floor (ph);
                    d->phase[(size_t) (c * rows + n)] = (uint8_t) (ph * 255.99f);
                }
            }
            // what is left over is noise, and stays in its own band
            for (int k = 1; k < W / 2; ++k)
                if (! used[(size_t) k])
                {
                    const int n = (int) std::floor ((float) k * binHz / fa - 0.5f);
                    if (n >= 0 && n < rows) bandE[(size_t) n] += p2 (k);
                }
            for (int n = 0; n < rows; ++n) gcol[n] = std::sqrt (bandE[(size_t) n]) * ampScale * (float) (n + 1);
        }
        // transients: columns whose rise in level stands well above their neighbourhood
        for (int c = 1; c < cols; ++c)
        {
            float mean = 0.0f; int cnt = 0;
            for (int j = juce::jmax (0, c - 8); j < juce::jmin (cols, c + 8); ++j) { mean += flux[(size_t) j]; ++cnt; }
            mean /= (float) juce::jmax (1, cnt);
            if (flux[(size_t) c] > 2.0f * mean && flux[(size_t) c] >= flux[(size_t) c - 1] && (c + 1 >= cols || flux[(size_t) c] >= flux[(size_t) c + 1]))
                d->transient[(size_t) c] = 1;
        }
        d->transient[0] = 1;
        return d;
    }

    static float detectPitch (const std::vector<float>& x, double sr)
    {
        const int n = juce::jmin ((int) x.size(), 8192);
        if (n < 2048) return 0.0f;
        const int start = ((int) x.size() - n) / 2;
        const int minLag = (int) (sr / 1000.0), maxLag = juce::jmin (n / 2, (int) (sr / 45.0));
        float e0 = 0.0f; for (int i = 0; i < n; ++i) e0 += x[(size_t) (start + i)] * x[(size_t) (start + i)];
        if (e0 < 1.0e-6f) return 0.0f;
        float best = 0.0f; int bestLag = 0;
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            float s = 0.0f, e1 = 0.0f, e2 = 0.0f;
            for (int i = 0; i + lag < n; i += 2)
            {
                const float u = x[(size_t) (start + i)], v = x[(size_t) (start + i + lag)];
                s += u * v; e1 += u * u; e2 += v * v;
            }
            const float r = s / (std::sqrt (e1 * e2) + 1.0e-12f);
            if (r > best + 0.02f) { best = r; bestLag = lag; }      // prefer the shortest lag that is clearly best
        }
        return (best > 0.8f && bestLag > 0) ? (float) (sr / (double) bestLag) : 0.0f;
    }

    //----------------------------------------------------------------------------------------------
    // Edits (each returns a new image; the one being played is never changed)
    std::shared_ptr<ImageData> clone() const { return std::make_shared<ImageData> (*this); }

    std::shared_ptr<ImageData> toImageSynthesis() const { auto d = clone(); d->resynth = false; d->phase.clear(); d->transient.clear(); return d; }

    static std::shared_ptr<ImageData> randomCloud (uint32_t seed)
    {
        auto d = std::make_shared<ImageData>();
        d->alloc (256, 256);
        Rng r; r.s = seed | 1u;
        for (int i = 0; i < 400; ++i)
        {
            const int cx = (int) (r.uni() * 256), cy = (int) (r.uni() * r.uni() * 256), rad = 2 + (int) (r.uni() * 14);
            const float lv = 0.2f + 0.8f * r.uni();
            for (int x = juce::jmax (0, cx - rad); x < juce::jmin (256, cx + rad); ++x)
                for (int y = juce::jmax (0, cy - rad); y < juce::jmin (256, cy + rad); ++y)
                {
                    const float dd = (float) ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (float) (rad * rad);
                    if (dd < 1.0f) d->g (x)[y] = juce::jmax (d->g (x)[y], lv * (1.0f - dd));
                }
        }
        return d;
    }
    std::shared_ptr<ImageData> degraded (bool horizontal) const
    {
        auto d = clone();
        const int k = horizontal ? juce::jmax (2, w / 48) : juce::jmax (2, h / 48);
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y)
            {
                const int bx = horizontal ? (x / k) * k : x, by = horizontal ? y : (y / k) * k;
                float s = 0.0f; int c = 0;
                for (int j = 0; j < k; ++j)
                {
                    const int xx = horizontal ? bx + j : bx, yy = horizontal ? by : by + j;
                    if (xx < w && yy < h) { s += gain[(size_t) (xx * h + yy)]; ++c; }
                }
                d->gain[(size_t) (x * h + y)] = s / (float) juce::jmax (1, c);
            }
        return d;
    }
    std::shared_ptr<ImageData> crossfaded (float fraction) const        // blends the end into the start so the image loops
    {
        const int n = juce::jlimit (1, juce::jmax (1, w / 2), (int) ((float) w * fraction));
        auto d = std::make_shared<ImageData> (*this);
        d->w = w - n;
        d->gain.resize ((size_t) (d->w * h)); d->freq.resize ((size_t) (d->w * h));
        if (! d->phase.empty()) d->phase.resize ((size_t) (d->w * h));
        if (! d->transient.empty()) d->transient.resize ((size_t) d->w);
        for (int x = 0; x < n && x < d->w; ++x)
        {
            const float t = (float) x / (float) n;
            for (int y = 0; y < h; ++y)
                d->gain[(size_t) (x * h + y)] = gain[(size_t) (x * h + y)] * t + gain[(size_t) ((w - n + x) * h + y)] * (1.0f - t);
        }
        return d;
    }
    std::shared_ptr<ImageData> croppedTop() const
    {
        auto d = clone();
        for (int x = 0; x < w; ++x) for (int y = h / 2; y < h; ++y) d->gain[(size_t) (x * h + y)] = 0.0f;
        return d;
    }
    std::shared_ptr<ImageData> inverted (int plane) const
    {
        auto d = clone();
        if (plane == 1) for (auto& v : d->freq) v = -v;
        else            for (auto& v : d->gain) v = juce::jmax (0.0f, 1.0f - v);
        return d;
    }
    std::shared_ptr<ImageData> flippedVertically() const
    {
        auto d = clone();
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y)
            {
                d->gain[(size_t) (x * h + y)] = gain[(size_t) (x * h + (h - 1 - y))];
                d->freq[(size_t) (x * h + y)] = freq[(size_t) (x * h + (h - 1 - y))];
            }
        return d;
    }
    std::shared_ptr<ImageData> withLoop (float a, float b, float xfade = 0.1f) const
    {
        auto d = clone();
        d->hasLoop = b > a; d->loopStart = clamp01 (juce::jmin (a, b)); d->loopEnd = clamp01 (juce::jmax (a, b)); d->loopXfade = xfade;
        return d;
    }
    std::shared_ptr<ImageData> withColumn (int x, const float* levels) const
    {
        auto d = clone();
        if (x >= 0 && x < w) std::memcpy (d->g (x), levels, sizeof (float) * (size_t) h);
        return d;
    }
    // slice points for "prepare time envelope for drum loop": transient positions, 0..1
    std::vector<float> slices() const
    {
        std::vector<float> out;
        if (! markers.empty()) return markers;
        float last = -1.0f;
        for (int c = 0; c < (int) transient.size(); ++c)
            if (transient[(size_t) c])
            {
                const float p = (float) c / (float) juce::jmax (1, w);
                if (p - last > 0.02f) { out.push_back (p); last = p; }
            }
        if (out.empty()) for (int i = 0; i < 8; ++i) out.push_back ((float) i / 8.0f);
        return out;
    }

    //----------------------------------------------------------------------------------------------
    // State: planes are quantised and zipped
    void save (juce::ValueTree& t) const
    {
        t.setProperty ("w", w, nullptr); t.setProperty ("h", h, nullptr); t.setProperty ("resynth", resynth, nullptr);
        t.setProperty ("baseFreq", baseFreq, nullptr); t.setProperty ("secPerCol", secPerCol, nullptr);
        t.setProperty ("hasLoop", hasLoop, nullptr); t.setProperty ("loopStart", loopStart, nullptr); t.setProperty ("loopEnd", loopEnd, nullptr);
        t.setProperty ("loopXfade", loopXfade, nullptr); t.setProperty ("loopGain", loopGain, nullptr); t.setProperty ("name", name, nullptr);
        juce::String mk; for (float m : markers) mk << juce::String (m, 5) << ' ';
        t.setProperty ("markers", mk, nullptr);
        juce::MemoryOutputStream raw;
        for (float v : gain) raw.writeShort ((short) (uint16_t) juce::jlimit (0, 65535, (int) (std::sqrt (juce::jmax (0.0f, v) / 64.0f) * 65535.0f)));
        for (float v : freq) raw.writeShort ((short) juce::jlimit (-32767, 32767, (int) (v * 2048.0f)));
        raw.writeInt ((int) phase.size()); if (! phase.empty()) raw.write (phase.data(), phase.size());
        raw.writeInt ((int) transient.size()); if (! transient.empty()) raw.write (transient.data(), transient.size());
        juce::MemoryOutputStream zipped;
        { juce::GZIPCompressorOutputStream gz (zipped, 6); gz.write (raw.getData(), raw.getDataSize()); }
        t.setProperty ("data", juce::var (zipped.getMemoryBlock()), nullptr);
    }
    static std::shared_ptr<ImageData> load (const juce::ValueTree& t)
    {
        auto d = std::make_shared<ImageData>();
        const int w = t.getProperty ("w", 0), h = t.getProperty ("h", 0);
        auto* mb = t.getProperty ("data").getBinaryData();
        if (w <= 0 || h <= 0 || h > NH || w > 8192 || mb == nullptr) return nullptr;
        d->alloc (w, h);
        d->resynth = t.getProperty ("resynth", false); d->baseFreq = t.getProperty ("baseFreq", kC4); d->secPerCol = t.getProperty ("secPerCol", 0.02f);
        d->hasLoop = t.getProperty ("hasLoop", false); d->loopStart = t.getProperty ("loopStart", 0.0f); d->loopEnd = t.getProperty ("loopEnd", 1.0f);
        d->loopXfade = t.getProperty ("loopXfade", 0.0f); d->loopGain = t.getProperty ("loopGain", 0.0f); d->name = t.getProperty ("name").toString();
        for (auto& s : juce::StringArray::fromTokens (t.getProperty ("markers").toString(), " ", "")) if (s.isNotEmpty()) d->markers.push_back (s.getFloatValue());
        juce::MemoryInputStream zin (*mb, false);
        juce::GZIPDecompressorInputStream gz (zin);
        juce::MemoryBlock rawBlock; gz.readIntoMemoryBlock (rawBlock);
        juce::MemoryInputStream in (rawBlock, false);
        if (rawBlock.getSize() < (size_t) (w * h) * 4) return nullptr;
        for (auto& v : d->gain) { const float q = (float) (uint16_t) in.readShort() / 65535.0f; v = q * q * 64.0f; }
        for (auto& v : d->freq) v = (float) in.readShort() / 2048.0f;
        int n = in.readInt(); if (n == w * h) { d->phase.resize ((size_t) n); in.read (d->phase.data(), n); }
        n = in.readInt(); if (n == w) { d->transient.resize ((size_t) n); in.read (d->transient.data(), n); }
        return d;
    }
};
} // namespace pt
