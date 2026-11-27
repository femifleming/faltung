namespace fal::ui
{
// ---- fonts ----------------------------------------------------------------------------------------
namespace
{
    constexpr int kRoles = (int) Fonts::Role::numRoles;
    // freed with JUCE's other singletons (a function-local static would outlive the typeface machinery)
    struct FontStore : juce::DeletedAtShutdown
    {
        ~FontStore() override { instance = nullptr; }
        static inline FontStore* instance = nullptr;
        juce::CriticalSection lock;
        std::array<juce::Typeface::Ptr, kRoles> faces;
        std::array<bool, kRoles> resolved {};
        juce::File folder;
       #if FALTUNG_UI_BINARYDATA
        bool registeredBinary = false;
       #endif
    };
    FontStore& store() { if (FontStore::instance == nullptr) FontStore::instance = new FontStore(); return *FontStore::instance; }
    std::atomic<bool> gDark { false };
}

const char* Fonts::fileName (Role r)
{
    switch (r)
    {
        case Role::Sans:       return "Inter-Regular.ttf";
        case Role::SansMedium: return "Inter-Medium.ttf";
        case Role::Mono:       return "IBMPlexMono-Regular.ttf";
        case Role::MonoMedium: return "IBMPlexMono-Medium.ttf";
        case Role::Cond:       return "BarlowCondensed-Regular.ttf";
        case Role::CondMedium: return "BarlowCondensed-Medium.ttf";
        case Role::Light:      return "Barlow-Light.ttf";
        case Role::numRoles:   break;
    }
    return "";
}

void Fonts::set (Role r, const void* data, size_t size)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    s.faces[(size_t) r] = (data != nullptr && size > 0) ? juce::Typeface::createSystemTypefaceFor (data, size) : nullptr;
    s.resolved[(size_t) r] = s.faces[(size_t) r] != nullptr;
}

void Fonts::loadFromFolder (const juce::File& folder)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    s.folder = folder;
    for (int i = 0; i < kRoles; ++i)
        if (s.faces[(size_t) i] == nullptr) s.resolved[(size_t) i] = false;   // look again
}

juce::Typeface::Ptr Fonts::typeface (Role r)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
   #if FALTUNG_UI_BINARYDATA
    if (! s.registeredBinary)
    {
        s.registeredBinary = true;
        struct E { Role r; const char* d; int n; };
        const E e[] = { { Role::Sans,       FaltungFonts::InterRegular_ttf,           FaltungFonts::InterRegular_ttfSize },
                        { Role::SansMedium, FaltungFonts::InterMedium_ttf,            FaltungFonts::InterMedium_ttfSize },
                        { Role::Mono,       FaltungFonts::IBMPlexMonoRegular_ttf,     FaltungFonts::IBMPlexMonoRegular_ttfSize },
                        { Role::MonoMedium, FaltungFonts::IBMPlexMonoMedium_ttf,      FaltungFonts::IBMPlexMonoMedium_ttfSize },
                        { Role::Cond,       FaltungFonts::BarlowCondensedRegular_ttf, FaltungFonts::BarlowCondensedRegular_ttfSize },
                        { Role::CondMedium, FaltungFonts::BarlowCondensedMedium_ttf,  FaltungFonts::BarlowCondensedMedium_ttfSize },
                        { Role::Light,      FaltungFonts::BarlowLight_ttf,            FaltungFonts::BarlowLight_ttfSize } };
        for (auto& x : e)
            if (s.faces[(size_t) x.r] == nullptr)
            {
                s.faces[(size_t) x.r] = juce::Typeface::createSystemTypefaceFor (x.d, (size_t) x.n);
                s.resolved[(size_t) x.r] = s.faces[(size_t) x.r] != nullptr;
            }
    }
   #endif
    auto& f = s.faces[(size_t) r];
    if (! s.resolved[(size_t) r])
    {
        s.resolved[(size_t) r] = true;
        if (f == nullptr && s.folder.isDirectory())
        {
            juce::MemoryBlock mb;
            if (s.folder.getChildFile (fileName (r)).loadFileAsData (mb) && mb.getSize() > 0)
                f = juce::Typeface::createSystemTypefaceFor (mb.getData(), mb.getSize());
        }
    }
    return f;
}

static juce::Font make (Fonts::Role r, const char* fallbackName, bool bold, float size)
{
    if (auto t = Fonts::typeface (r))
        return juce::Font (juce::FontOptions (t).withHeight (size));
    return juce::Font (juce::FontOptions (fallbackName, size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font sans (float size, bool medium)
{
    return make (medium ? Fonts::Role::SansMedium : Fonts::Role::Sans, "Helvetica Neue", medium, size);
}
juce::Font mono (float size, bool medium)
{
    return make (medium ? Fonts::Role::MonoMedium : Fonts::Role::Mono, "Menlo", medium, size);
}
juce::Font cond (float size, bool medium)
{
    return make (medium ? Fonts::Role::CondMedium : Fonts::Role::Cond, "Helvetica Neue", medium, size).withExtraKerningFactor (0.04f);
}
juce::Font light (float size)
{
    return make (Fonts::Role::Light, "Helvetica Neue", false, size);
}

// ---- light / dark ---------------------------------------------------------------------------------
void setDark (bool d) { gDark.store (d); }
bool isDark() { return gDark.load(); }

juce::Colour darkField (juce::Colour c)
{
    float h, s, l; c.getHSL (h, s, l);
    // white paper goes to near-black with its tint kept; a saturated mid field keeps more of its colour
    const float chroma = s * (1.0f - std::abs (2.0f * l - 1.0f));           // 0 (grey, white, black) .. 1 (pure colour)
    const float l2 = 0.085f + 0.085f * juce::jlimit (0.0f, 1.0f, chroma * 1.6f);
    const float s2 = juce::jlimit (0.0f, 0.62f, 0.10f + 0.80f * chroma + (l > 0.9f ? 0.12f * s : 0.0f));
    return juce::Colour::fromHSL (h, s2, l2, 1.0f).withAlpha (c.getAlpha());
}
juce::Colour darkAccent (juce::Colour c)
{
    float h, s, l; c.getHSL (h, s, l);
    const float l2 = 0.47f + (l - 0.47f) * 0.62f;        // pulled toward the middle: pale pastels dim, deep colours lift
    return juce::Colour::fromHSL (h, juce::jmin (1.0f, s * 0.94f), l2, 1.0f).withAlpha (c.getAlpha());
}
Theme themeOf (juce::Colour lightGround, juce::Colour lightAccent, bool dark)
{
    const juce::Colour ground = field (lightGround, dark);
    juce::Colour fill = accent (lightAccent, dark);
    if (std::abs (fill.getPerceivedBrightness() - ground.getPerceivedBrightness()) < 0.12f)
        fill = inkFor (ground).withAlpha (0.55f);
    return { ground, inkFor (ground), fill, edgeFor (ground) };
}

juce::Path skewRect (juce::Rectangle<float> r, float skew)
{
    const float d = r.getHeight() * skew;
    juce::Path p;
    p.startNewSubPath (r.getX() + d, r.getY());
    p.lineTo (r.getRight() + d, r.getY());
    p.lineTo (r.getRight(), r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.closeSubPath();
    return p;
}
} // namespace fal::ui
