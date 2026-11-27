#pragma once
// umleitung — what every page shares: the model, the engine, the plug-ins; the faltung look (fonts, fields, skewed
// shapes) and small drawing helpers.
#include <faltung_ui/faltung_ui.h>
#include "../engine/Engine.h"
#include "../engine/PluginHost.h"
#include "../platform/DriverControl.h"

namespace um::ui
{
using namespace fal::ui;
namespace pal = fal::ui::palette;

struct Ctx
{
    Model& model; Engine& engine; PluginHost& plugins;
    std::function<void (int page, int sessionId)> show;      // go to a page (and a session)
    std::function<void (const juce::String&)> status;        // the line at the bottom
    std::function<void (int blockId)> openPlugin;            // a plug-in's own window
    std::function<void (int blockId)> watch;                 // the meters page looks at this block
};

enum Page { Sessions = 0, SystemEq, Devices, Meters, Recordings, Plugins, Settings, NumPages };
inline const char* pageName (int p) { static const char* n[] = { "sessions", "system eq", "virtual devices", "meters", "recordings", "plug-ins", "settings" }; return n[juce::jlimit (0, NumPages - 1, p)]; }
inline const char* pageCode (int p) { static const char* n[] = { "se1", "eq2", "vd3", "mt4", "rc5", "pl6", "st7" }; return n[juce::jlimit (0, NumPages - 1, p)]; }
inline const pal::Field& pageField (int p)
{
    switch (p)
    {
        case Sessions: return pal::fields::sky;      case SystemEq: return pal::fields::sage;     case Devices: return pal::fields::apricot;
        case Meters: return pal::fields::night;      case Recordings: return pal::fields::paperWarm; case Plugins: return pal::fields::periwinkle;
        default: return pal::fields::mist;
    }
}
inline Theme pageTheme (int p) { return themeOf (pageField (p)); }

/** the colour a block's group wears on the canvas */
inline juce::Colour groupColour (BlockType t)
{
    // (written as the light colours; in the dark look they are its dimmer accents)
    if (isSource (t)) return accent (juce::Colour (pal::fields::sage.ground()));
    if (isSink (t)) return accent (juce::Colour (pal::fields::coral.ground()));
    if (t == BlockType::Plugin) return accent (juce::Colour (pal::fields::periwinkle.ground()));
    return accent (juce::Colour (pal::fields::apricot.ground()));
}

/** level (0..1 linear) to a meter's 0..1 (-60 … +6 db) */
inline float meterPos (float lin, float lo = -60.0f, float hi = 6.0f) { return juce::jlimit (0.0f, 1.0f, (dsp::gainToDb (lin) - lo) / (hi - lo)); }
inline juce::Colour meterColour (float db, juce::Colour ok)
{
    if (db > -0.5f) return juce::Colour (0xffee5a3e);
    if (db > -9.0f) return juce::Colour (0xffe3bf5f);
    return ok;
}
inline juce::String dbText (float db) { return db <= -119.0f ? juce::String ("-inf") : (db > 0 ? "+" : "") + juce::String (db, 1); }
inline juce::String timeText (double secs) { const int s = (int) secs; return juce::String (s / 3600) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2); }

/** a parallelogram outline / fill, the house shape */
inline void skewBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill, float skew = 0.35f, float stroke = 1.0f)
{
    auto p = skewRect (r, skew);
    g.setColour (c);
    if (fill) g.fillPath (p); else g.strokePath (p, juce::PathStrokeType (stroke));
}

/** a page: one colour field, a title with its code */
class PageBase : public juce::Component
{
public:
    PageBase (Ctx& c, int pg) : ctx (c), page (pg), theme (pageTheme (pg)) {}
    virtual void tick() {}                 // ~30 times a second while shown
    virtual void refresh() {}              // the model changed under it
    void paintHeader (juce::Graphics& g, const juce::String& title, const juce::String& sub)
    {
        g.fillAll (theme.ground);
        g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f)); g.drawText (pageCode (page), 24, 14, 60, 14, juce::Justification::centredLeft);
        g.setColour (theme.ink); g.setFont (sans (30.0f, true)); g.drawText (title, 24, 26, getWidth() - 48, 38, juce::Justification::centredLeft, true);
        g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (sans (13.0f)); g.drawText (sub, 24, 64, getWidth() - 48, 18, juce::Justification::centredLeft, true);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (24, 90, getWidth() - 48, 1);
    }
    Ctx& ctx; int page; Theme theme;
    static constexpr int kTop = 100;
};
} // namespace um::ui
