#pragma once
// The faltung palette, from faltung-kit/assets/palette/palette.json (the source of truth).
// Every colour is written once, as the LIGHT one; dark mode is computed with
// fal::ui::darkField() for grounds and fal::ui::darkAccent() for strips/lines (see field()/accent()).

namespace fal
{
/** the four picture looks */
enum class Style { Snd, Fell, Oval, Stdio };

namespace ui::palette
{
/** a colour field: a ground plus its accent (strips / lines / slider fill) and the look it is drawn in */
struct Field
{
    juce::uint32 groundArgb, accentArgb;
    Style style;
    const char* name;
    juce::Colour ground() const { return juce::Colour (groundArgb); }
    juce::Colour accent() const { return juce::Colour (accentArgb); }
};

// ---- ink ------------------------------------------------------------------------------------------
namespace ink
{
    constexpr juce::uint32 onLight  = 0xff1d1c15;
    constexpr juce::uint32 onDark   = 0xfff6f6f0;
    constexpr juce::uint32 paperInk = 0xff1d1d1d;
    constexpr float inkThreshold  = 0.42f;   // perceived brightness above which ink is dark
    constexpr float edgeThreshold = 0.86f;   // above which the slider edge mark is ink, not white
    constexpr float rule = 0.35f, sectionRule = 0.25f, rowRule = 0.15f, dim = 0.62f;   // ink alphas
}

// ---- slate ----------------------------------------------------------------------------------------
namespace slate
{
    constexpr juce::uint32 sidebar = 0xff1a242c, sidebarAccent = 0xff5aaad8;
    constexpr juce::uint32 deepNight = 0xff151c2a, deepNightAccent = 0xff5b7aa3;
    constexpr juce::uint32 studio = 0xff2b3a40;
}

// ---- fields (ground + accent pairs, one per product / page) ---------------------------------------
namespace fields
{
    constexpr Field mustard    { 0xffc9b53b, 0xffd8c64e, Style::Snd,   "mustard" };
    constexpr Field ochre      { 0xffb88a26, 0xffe3bf5f, Style::Fell,  "ochre" };
    constexpr Field sage       { 0xff9cc788, 0xff5e8f6a, Style::Snd,   "sage" };
    constexpr Field sky        { 0xff8ec3e6, 0xff6e80c8, Style::Snd,   "sky" };
    constexpr Field periwinkle { 0xff8d9bd6, 0xffcfe6f0, Style::Snd,   "periwinkle" };
    constexpr Field steel      { 0xffa9bccf, 0xffee8e6e, Style::Snd,   "steel" };
    constexpr Field apricot    { 0xfff6c590, 0xff8ec3e6, Style::Snd,   "apricot" };
    constexpr Field ice        { 0xffeef6fa, 0xff9fd3ec, Style::Stdio, "ice" };
    constexpr Field moss       { 0xff76a582, 0xffb9dcbf, Style::Fell,  "moss" };
    constexpr Field coral      { 0xffee8e6e, 0xfff6c590, Style::Snd,   "coral" };
    constexpr Field paper      { 0xfffbfbf8, 0xff2f7fd1, Style::Oval,  "paper" };
    constexpr Field paperWarm  { 0xfff4f2ec, 0xff2f7fd1, Style::Oval,  "paperWarm" };
    constexpr Field frost      { 0xfff4f9fb, 0xff9fd3ec, Style::Stdio, "frost" };
    constexpr Field acid       { 0xff79e04a, 0xffb7f39a, Style::Fell,  "acid" };
    constexpr Field indigo     { 0xff8593d4, 0xffcfe6f0, Style::Snd,   "indigo" };
    constexpr Field mist       { 0xffdfe9ed, 0xff8fa9b5, Style::Stdio, "mist" };
    constexpr Field teal       { 0xff2f4a52, 0xff7fd3c8, Style::Fell,  "teal" };
    constexpr Field night      { 0xff1a242c, 0xff5aaad8, Style::Stdio, "night" };
    constexpr Field health     { 0xffc9b53b, 0xfff4f2ec, Style::Fell,  "health" };

    constexpr Field all[] = { mustard, ochre, sage, sky, periwinkle, steel, apricot, ice, moss, coral,
                              paper, paperWarm, frost, acid, indigo, mist, teal, night, health };
}

// ---- snd folded strips (cycled in order, Fold::palette) -------------------------------------------
namespace fold
{
    constexpr juce::uint32 colours[8] = { 0xff5e8f6a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xfff6c590, 0xff97adc4, 0xffcfe6f0 };
    constexpr juce::uint32 inkOnStrips = 0xff1e2a20;
}

// ---- oval: coloured lines on white ----------------------------------------------------------------
namespace oval
{
    constexpr juce::uint32 lines[4]  = { 0xffe06a2e, 0xffe9a0a8, 0xff2f7fd1, 0xff8f6bb8 };
    constexpr juce::uint32 bundle[4] = { 0xff2f7fd1, 0xffe06a2e, 0xff8f6bb8, 0x99c9c2a8 };   // last at 60% as drawn
    constexpr juce::uint32 patchGreen = 0xff8fbf6a, patchPeach = 0xfff6c590;
}

// ---- stdio: pale chevrons, the yellow cube ---------------------------------------------------------
namespace stdio
{
    constexpr juce::uint32 chevron = 0xff9fd3ec, chevronLine = 0xff7cc2e4;
    constexpr juce::uint32 cubeTop = 0xffffe15a, cubeLeft = 0xfff2c100, cubeRight = 0xffd8a800, cubeInk = 0xff2a2100;
}

// ---- kinds (file-type / category colours) ----------------------------------------------------------
namespace kinds
{
    constexpr juce::uint32 audio = 0xffee8e6e, video = 0xff8593d4, images = 0xfff6c590, archives = 0xffc9b53b,
                           apps = 0xff8ec3e6, code = 0xff76a582, documents = 0xffa9bccf, other = 0xff9a9a92;
}

// ---- icon house style ------------------------------------------------------------------------------
namespace icon
{
    constexpr juce::uint32 faltungFg = 0xff1a242c, faltungBg = 0xffc9b53b;
    constexpr juce::uint32 putzFg = 0xffc9b53b, putzBg = 0xff1a242c;
}
} // namespace ui::palette
} // namespace fal
