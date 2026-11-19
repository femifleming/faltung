// faltung sndstudio — two devices about loops that are not clean: `skip` reads a sample the way a damaged disc is
// read (fragments repeat, the read point jumps, frames drop out), `wear` keeps a loop of its input going round and
// lets it lose a little on every pass. Included by StudioDevices.cpp (after StudioFx).

// ---- skip (instrument) ---------------------------------------------------------------------------------------
class SkipFx : public StudioFx, public SampleHolder
{
public:
    enum { INTENSITY, SYNC, DIVISION, FRAGMENT, START, END, PITCH, ERRORS, PLAY, LEVEL };
    SkipFx() : StudioFx ("st:skip", "skip", 0, 2, true, {
        F ("intensity", "intensity", 0, 1, 0.5f, pctText), B ("sync", "fragments on the grid", false), C ("division", "fragment", divNames(), 4),
        F ("fragment", "fragment length", 0.25f, 4, 1, [] (float v) { return "x" + juce::String (v, 2); }, 0.5f),
        F ("start", "from", 0, 1, 0, pctText), F ("end", "to", 0, 1, 1, pctText), F ("pitch", "pitch", -24, 24, 0, stText),
        F ("errors", "read errors", 0, 1, 0.5f, pctText), C ("play", "plays", { "until the next note", "while the note is held" }, 0), F ("level", "level", -24, 12, 0, dbText) }) {}
    struct Sampled { juce::String path; juce::AudioBuffer<float> data; double rate = 48000; };
    bool setSampleFile (const juce::File& f) override
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (! rd || rd->lengthInSamples < 64) return false;
        auto ns = std::make_shared<Sampled>(); ns->path = f.getFullPathName(); ns->rate = rd->sampleRate;
        ns->data.setSize (2, (int) rd->lengthInSamples);
        rd->read (&ns->data, 0, (int) rd->lengthInSamples, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) ns->data.copyFrom (1, 0, ns->data, 0, 0, ns->data.getNumSamples());
        { const juce::SpinLock::ScopedLockType sl (lock); sample = ns; }
        return true;
    }
    juce::String sampleFile() const override { const juce::SpinLock::ScopedLockType sl (lock); return sample ? sample->path : juce::String(); }
    int numSlices() const override { return 16; }
    void saveExtra (juce::XmlElement& x) override { x.setAttribute ("sampleFile", sampleFile()); }
    void loadExtra (const juce::XmlElement& x) override { const auto p = x.getStringAttribute ("sampleFile"); if (p.isNotEmpty()) setSampleFile (juce::File (p)); }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; on = false; rng.seed (2203); errorLeft = 0; }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        const int n = buf.getNumSamples(); buf.clear();
        { const juce::SpinLock::ScopedTryLockType sl (lock); if (sl.isLocked() && sample != cur) cur = sample; }
        if (! cur || buf.getNumChannels() < 2) { midi.clear(); return; }
        const auto& d = cur->data; const int ns = d.getNumSamples();
        const float inten = P (INTENSITY), g = dbToGain (P (LEVEL));
        const double a = juce::jlimit (0.0, 0.98, (double) std::min (P (START), P (END))) * ns, e = std::max (a + 64.0, juce::jlimit (0.02, 1.0, (double) std::max (P (START), P (END))) * ns);
        const double span = e - a, speed = cur->rate / sr * std::exp2 ((P (PITCH) + notePitch) / 12.0);
        auto newFragment = [&]
        {
            // how long the next fragment is, and how often it comes round before the reader moves on
            double ms;
            if (Pb (SYNC)) ms = divisions()[(size_t) juce::jlimit (0, 18, Pi (DIVISION))].beats * 60000.0 / bpm();
            else { const double lo = lerp (220.0f, 9.0f, inten), hi = lerp (1500.0f, 70.0f, inten); ms = lo * std::pow (hi / lo, (double) rng.uni()) * P (FRAGMENT); }
            fragLen = std::max (16.0, ms * 0.001 * sr);
            repeats = rng.uni() < inten * 0.6f ? 1 + (int) (rng.uni() * inten * 7.0f) : 0;
        };
        auto wrap = [&] (double x) { x = std::fmod (x - a, span); if (x < 0) x += span; return a + x; };
        int pos = 0;
        auto renderTo = [&] (int to)
        {
            auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
            const float* sl = d.getReadPointer (0); const float* sr2 = d.getReadPointer (1);
            const double ramp = std::max (8.0, 0.0005 * sr);
            for (; pos < to; ++pos)
            {
                if (! on) continue;
                float l = 0, r = 0;
                if (errorLeft > 0)
                {
                    --errorLeft;
                    if (errorType == 1) { l = heldL; r = heldR; }                       // a frame held, coarse
                    else if (errorType == 2 && errorLeft == errorLen - 1) { l = r = 0.5f * (rng.uni() < 0.5f ? -1.0f : 1.0f); }   // a click, then nothing
                }
                else
                {
                    const double p = wrap (fragStart + fragPhase * speed);
                    l = hermiteAt (sl, ns, p); r = hermiteAt (sr2, ns, p);
                    const double edge = std::min (fragPhase, fragLen - fragPhase);
                    if (edge < ramp) { const float w = (float) (edge / ramp); l *= w; r *= w; }
                }
                L[pos] = l * g; R[pos] = r * g;
                fragPhase += 1.0;
                if (fragPhase >= fragLen)
                {
                    fragPhase = 0.0;
                    if (repeats > 0) --repeats;
                    else
                    {
                        const float u = rng.uni(); const double step = fragLen * speed;
                        if (u < inten * 0.35f) fragStart = a + rng.uni() * span;            // somewhere else
                        else if (u < inten * 0.55f) fragStart = wrap (fragStart - 2.0 * step);   // back two
                        else fragStart = wrap (fragStart + step);                              // on
                        newFragment();
                    }
                    if (rng.uni() < inten * 0.24f * P (ERRORS))
                    {
                        errorLen = errorLeft = (int) ((0.002 + 0.023 * rng.uni()) * sr); errorType = (int) (rng.uni() * 2.999f);
                        const double p = wrap (fragStart);
                        heldL = std::round (hermiteAt (sl, ns, p) * 4.0f) / 4.0f; heldR = std::round (hermiteAt (sr2, ns, p) * 4.0f) / 4.0f;
                    }
                }
            }
        };
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            renderTo (std::min (n, m.samplePosition));
            if (msg.isNoteOn())
            {
                // the low notes (the steps' slices) are places in the file; from c3 up the note is the pitch
                const int nn = msg.getNoteNumber(); note = nn;
                notePitch = nn >= 60 ? (float) (nn - 60) : 0.0f;
                fragStart = nn < 60 ? a + span * (double) (((nn - 36) % 16 + 16) % 16) / 16.0 : a;
                fragPhase = 0.0; on = true; errorLeft = 0; newFragment();
            }
            else if (msg.isNoteOff()) { if (Pi (PLAY) == 1 && msg.getNoteNumber() == note) on = false; }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) on = false;
        }
        renderTo (n);
        midi.clear();
    }
    mutable juce::SpinLock lock; std::shared_ptr<Sampled> sample, cur;
    Rng rng; bool on = false; int note = 60, repeats = 0, errorLeft = 0, errorLen = 0, errorType = 0; float notePitch = 0, heldL = 0, heldR = 0;
    double fragStart = 0, fragLen = 4800, fragPhase = 0;
};

// ---- wear: a loop that wears out ---------------------------------------------------------------------------------
class WearFx : public StudioFx
{
public:
    enum { SYNC, DIVISION, TIME, RECORD, WEAR, DROPS, MIX, LEVEL };
    static constexpr double kMaxSeconds = 16.0;
    WearFx() : StudioFx ("st:wear", "wear", 2, 2, false, {
        B ("sync", "sync", true), C ("division", "loop", divNames(), 14), F ("time", "loop time", 50, 16000, 2000, msText, 0.35f),
        B ("record", "record", true), F ("wear", "wear", 0, 1, 0.3f, pctText), F ("dropouts", "dropouts", 0, 1, 0.3f, pctText),
        F ("mix", "mix", 0, 1, 1, pctText), F ("level", "level", -24, 12, 0, dbText) }) {}
    void prepareToPlay (double s, int b) override
    {
        sr = s; bs = b; const int cap = (int) (kMaxSeconds * sr) + 8;
        for (auto& c : tape) c.assign ((size_t) cap, 0.0f);
        pos = 0; lp[0] = lp[1] = 0; rng.seed (811); holes = 0; lastSync = Pb (SYNC);
    }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), nch = std::min (2, buf.getNumChannels());
        if (nch < 1 || tape[0].empty()) return;
        keepTime ("sync", "division", "time", lastSync);
        const double secs = Pb (SYNC) ? divisions()[(size_t) juce::jlimit (0, 18, Pi (DIVISION))].beats * 60.0 / bpm() : P (TIME) * 0.001;
        const int len = juce::jlimit (64, (int) tape[0].size() - 4, (int) (secs * sr));
        const float wear = P (WEAR), mix = P (MIX), g = dbToGain (P (LEVEL)); const bool rec = Pb (RECORD);
        // one pass through the machine: the highs go first, the level sags, and the holes stay
        const float cut = lerp (19000.0f, 2500.0f, wear), k = 1.0f - std::exp (-kTwoPi * cut / (float) sr), keep = 1.0f - 0.06f * wear;
        for (int i = 0; i < n; ++i)
        {
            if (pos >= len) pos = 0;
            if (pos == 0) holes = (int) (P (DROPS) * wear * 6.0f + rng.uni());        // new damage for this pass
            if (holes > 0 && holeLeft <= 0 && rng.uni() < (float) holes / (float) len) { holeLeft = (int) ((0.004 + 0.04 * rng.uni()) * sr); --holes; }
            const float hole = holeLeft > 0 ? 0.35f : 1.0f; if (holeLeft > 0) --holeLeft;
            for (int c = 0; c < nch; ++c)
            {
                auto* dta = buf.getWritePointer (c);
                const float in = dta[i], was = tape[(size_t) c][(size_t) pos];
                lp[c] += (was - lp[c]) * k;
                const float worn = (wear > 0.0005f ? lp[c] * keep : was) * hole;
                const float next = worn + (rec ? in : 0.0f);
                tape[(size_t) c][(size_t) pos] = next > 1.5f ? 1.5f : next < -1.5f ? -1.5f : next;
                dta[i] = (in * (1.0f - mix) + was * mix) * g;
            }
            ++pos;
        }
    }
    std::array<std::vector<float>, 2> tape; int pos = 0, holes = 0, holeLeft = 0; float lp[2] = { 0, 0 }; Rng rng; bool lastSync = true;
};
