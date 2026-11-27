#include "SessionPage.h"

namespace um::ui
{
// =====================================================================================================================
//  inspector
// =====================================================================================================================
void Inspector::addLabel (const juce::String& text)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setFont (mono (10.0f)); l->setColour (juce::Label::textColourId, theme.ink.withAlpha (0.62f)); l->setMinimumHorizontalScale (1.0f);
    l->setJustificationType (juce::Justification::topLeft);
    const int lines = std::max (1, (int) std::ceil (text.length() * 6.4 / 280.0)) + text.retainCharacters ("\n").length();
    add (std::move (l), 14 * lines + 4);
}
void Inspector::addLink (const juce::String& text, std::function<void()> fn, bool lit)
{
    auto l = std::make_unique<TextLink> (text, theme); l->onClick = std::move (fn); l->setLit (lit); add (std::move (l), 22);
}

void Inspector::show (juce::ValueTree b)
{
    rows.clear(); owned.clear(); meters = nullptr; recLink = nullptr; curve = nullptr; recInfo = nullptr;
    block = b;
    if (! b.isValid()) { contentHeight = 120; setSize (getWidth(), contentHeight); repaint(); return; }
    const auto t = typeFromId (b[ids::type]);
    const int id = b[ids::id];
    auto& eng = ctx.engine;
    auto db = [] (float v) { return dbText (v) + " db"; };
    addLabel (typeInfo (t).blurb);
    {
        auto bp = boundToggle (theme, b, ids::bypass, isSource (t) ? "silent" : isSink (t) ? "off" : "bypass"); add (std::move (bp), 22);
    }
    switch (t)
    {
        case BlockType::App:
        {
            addLabel (juce::String (juce::CharPointer_UTF8 ("apps (a dot: playing now) — tick one or several; they are found again when they restart")));
            auto p = std::make_unique<AppPicker> (theme, b, ids::bundles); auto* pp = p.get(); p->setSize (280, 40); p->reload(); const int h = p->getHeight(); add (std::move (p), h);
            addLink ("show background processes too", [this, pp, b] { pp->showAll = ! pp->showAll; pp->reload(); });
            add (boundToggle (theme, b, ids::mute, "mute where it plays (you hear it only through umleitung)"), 22);
            add (boundToggle (theme, b, ids::mono, "mono"), 22);
            break;
        }
        case BlockType::System:
            addLabel ("everything the mac plays, apart from the apps ticked here:");
            { auto p = std::make_unique<AppPicker> (theme, b, ids::exclude); p->setSize (280, 40); p->reload(); const int h = p->getHeight(); add (std::move (p), std::min (260, h)); }
            add (boundToggle (theme, b, ids::mute, "mute the mac's own output (you hear it only through umleitung)"), 22);
            break;
        case BlockType::Input:
        {
            juce::StringArray names, uids; deviceLists (false, names, uids, false);
            names.insert (0, "choose a device"); uids.insert (0, "");
            add (boundChoice (theme, b, ids::device, "device", names, uids), 22);
            add (boundSlider (theme, b, ids::first, "first channel", { 0, 255, 1 }, 0, [] (float v) { return juce::String ((int) v + 1); }), 22);
            add (boundSlider (theme, b, ids::count, "channels", { 1, 256, 1 }, 2, [] (float v) { return juce::String ((int) v); }), 22);
            break;
        }
        case BlockType::Tone:
            add (boundChoice (theme, b, ids::shape, "signal", { "sine", "pink noise", "white noise", "sweep", "pulse" }, { "0", "1", "2", "3", "4" }), 22);
            add (boundSlider (theme, b, ids::freq, "frequency", { 20, 20000, 0.0f, 0.3f }, 440, [] (float v) { return juce::String (v, 0) + " hz"; }), 22);
            add (boundSlider (theme, b, ids::level, "level", { -60, 0, 0.1f }, -18, db), 22);
            add (boundSlider (theme, b, ids::channels, "channels", { 1, 64, 1 }, 2, [] (float v) { return juce::String ((int) v); }), 22);
            break;
        case BlockType::File:
            addLink ("choose a file...  " + juce::File (b[ids::file].toString()).getFileName(), [this, b]
            {
                auto fc = std::make_shared<juce::FileChooser> ("a file to play", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg");
                fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, b, fc] (const juce::FileChooser& c) mutable { if (c.getResult().existsAsFile()) { b.setProperty (ids::file, c.getResult().getFullPathName(), nullptr); b.setProperty (ids::name, c.getResult().getFileNameWithoutExtension(), nullptr); show (b); } });
            });
            add (boundToggle (theme, b, ids::loop, "loop"), 22);
            add (boundSlider (theme, b, ids::level, "level", { -60, 12, 0.1f }, 0, db), 22);
            break;
        case BlockType::Volume:
            add (boundSlider (theme, b, ids::db, "level", { -60, 24, 0.1f }, 0, db), 22);
            add (boundSlider (theme, b, ids::balance, "balance", { -1, 1, 0.01f }, 0, [] (float v) { return std::abs (v) < 0.005f ? juce::String ("centre") : (v < 0 ? "l " : "r ") + juce::String ((int) std::lround (std::abs (v) * 100)); }), 22);
            add (boundToggle (theme, b, ids::mute, "mute"), 22);
            add (boundToggle (theme, b, ids::invert, "polarity inverted"), 22);
            break;
        case BlockType::Eq:
        {
            auto ge = std::make_unique<GraphicEqEditor> (theme, b); add (std::move (ge), 170);
            add (boundSlider (theme, b, ids::db, "preamp", { -24, 24, 0.1f }, 0, db), 22);
            addLink ("flat", [this, b] () mutable { b.setProperty (ids::bands, "0 0 0 0 0 0 0 0 0 0", nullptr); show (b); });
            break;
        }
        case BlockType::Peq:
        {
            auto ce = std::make_unique<CurveEditor> (theme, b, eng); curve = ce.get(); add (std::move (ce), 200);
            add (boundSlider (theme, b, ids::db, "output", { -24, 24, 0.1f }, 0, db), 22);
            addLink ("+ band", [this, b] () mutable { auto v = Model::peqBands (b); v.push_back ({ 0, 1000, 0, 1, true }); b.setProperty (ids::bands, Model::peqText (v), nullptr); show (b); });
            break;
        }
        case BlockType::Plugin:
        {
            juce::PopupMenu dummy;
            const auto name = b[ids::pluginName].toString();
            addLabel (name.isEmpty() ? "no plug-in chosen" : name);
            addLink ("choose a plug-in...", [this, b] () mutable
            {
                juce::PopupMenu m, byMaker; std::map<juce::String, juce::PopupMenu> makers;
                const auto types = ctx.plugins.known.getTypes();
                for (int i = 0; i < types.size(); ++i) makers[types[i].manufacturerName.isEmpty() ? juce::String ("other") : types[i].manufacturerName].addItem (i + 1, types[i].name + "  (" + types[i].pluginFormatName.toLowerCase() + (types[i].isInstrument ? ", instrument" : "") + ")");
                for (auto& [mk, sub] : makers) m.addSubMenu (mk, sub);
                if (types.isEmpty()) m.addItem (-1, "none found yet: scan on the plug-ins page", false);
                m.showMenuAsync (juce::PopupMenu::Options(), [this, b, types] (int r) mutable
                {
                    if (r <= 0 || r > types.size()) return;
                    const auto& d = types[r - 1];
                    b.setProperty (ids::state, "", nullptr); b.setProperty (ids::pluginName, d.name, nullptr); b.setProperty (ids::name, d.name.toLowerCase(), nullptr);
                    b.setProperty (ids::plugin, d.createXml()->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);
                    show (b);
                });
            });
            addLink ("open its window", [this, id] { if (ctx.openPlugin) ctx.openPlugin (id); });
            if (auto* rt = eng.runtime (id)) if (rt->status.isNotEmpty()) addLabel (rt->status);
            break;
        }
        case BlockType::Matrix:
        {
            auto* rt = eng.runtime (id);
            const int ins = rt ? rt->inChannels : 2;
            add (boundSlider (theme, b, ids::outs, "outputs", { 1, 256, 1 }, 2, [] (float v) { return juce::String ((int) v) + " ch"; }), 22);
            addLabel (juce::String (juce::CharPointer_UTF8 ("click: connect · again: off · drag up/down: level · right-click: inverted. ")) + juce::String (ins) + " in (what is wired in)");
            auto mg = std::make_unique<MatrixGrid> (theme, b, ins); auto* mgp = mg.get();
            add (std::move (mg), juce::jlimit (60, 420, 26 + ins * 18));
            addLink ("straight", [mgp] { mgp->preset (0); }); addLink ("everything to every output (mixed)", [mgp] { mgp->preset (1); }); addLink ("left / right swapped", [mgp] { mgp->preset (2); }); addLink ("fold inputs onto the outputs", [mgp] { mgp->preset (3); });
            break;
        }
        case BlockType::Delay: add (boundSlider (theme, b, ids::ms, "delay", { 0, 5000, 0.1f, 0.4f }, 0, [] (float v) { return juce::String (v, 1) + " ms"; }), 22); break;
        case BlockType::Leveler:
            add (boundSlider (theme, b, ids::target, "target", { -36, -6, 0.5f }, -18, db), 22);
            add (boundSlider (theme, b, ids::db, "at most", { 0, 30, 0.5f }, 12, db), 22);
            break;
        case BlockType::Gate:
            add (boundSlider (theme, b, ids::threshold, "threshold", { -90, 0, 0.5f }, -50, db), 22);
            add (boundSlider (theme, b, ids::release, "release", { 10, 2000, 1, 0.4f }, 150, [] (float v) { return juce::String ((int) v) + " ms"; }), 22);
            break;
        case BlockType::Limiter:
            add (boundSlider (theme, b, ids::ceiling, "ceiling", { -24, 0, 0.1f }, -1, db), 22);
            add (boundSlider (theme, b, ids::release, "release", { 5, 1000, 1, 0.4f }, 80, [] (float v) { return juce::String ((int) v) + " ms"; }), 22);
            break;
        case BlockType::Output:
        {
            juce::StringArray names, uids; deviceLists (true, names, uids, true);
            add (boundChoice (theme, b, ids::device, "device", names, uids), 22);
            add (boundSlider (theme, b, ids::first, "first channel", { 0, 255, 1 }, 0, [] (float v) { return juce::String ((int) v + 1); }), 22);
            add (boundSlider (theme, b, ids::level, "level", { -60, 12, 0.1f }, 0, db), 22);
            add (boundToggle (theme, b, ids::mute, "mute"), 22);
            addLabel ("a virtual device here sends the sound to other apps: they choose it as their microphone");
            break;
        }
        case BlockType::Recorder:
        {
            auto rl = std::make_unique<TextLink> ("record", theme); recLink = rl.get();
            recLink->onClick = [this, id] { if (ctx.engine.recording (id) || (bool) block[ids::armed]) ctx.engine.stopRecording (id); else ctx.engine.startRecording (id); };
            add (std::move (rl), 26);
            auto info = std::make_unique<juce::Label>(); recInfo = info.get(); info->setFont (mono (10.0f)); info->setColour (juce::Label::textColourId, theme.ink); info->setJustificationType (juce::Justification::topLeft); add (std::move (info), 46);
            addLink ("folder: " + Engine::recordingFolder (b).getFullPathName().replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~"), [this, b] () mutable
            {
                auto fc = std::make_shared<juce::FileChooser> ("where recordings go", Engine::recordingFolder (b));
                fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, b, fc] (const juce::FileChooser& c) mutable { if (c.getResult().isDirectory()) { b.setProperty (ids::folder, c.getResult().getFullPathName(), nullptr); show (b); } });
            });
            addLink ("show in finder", [b] { Engine::recordingFolder (b).createDirectory(); Engine::recordingFolder (b).revealToUser(); });
            {
                auto te = std::make_unique<juce::TextEditor>(); te->setText (b[ids::pattern].toString(), false); te->setFont (mono (11.0f));
                te->setColour (juce::TextEditor::backgroundColourId, theme.ink.withAlpha (0.06f)); te->setColour (juce::TextEditor::textColourId, theme.ink); te->setColour (juce::TextEditor::outlineColourId, theme.ink.withAlpha (0.25f));
                auto* tp = te.get(); te->onTextChange = [tp, b] () mutable { b.setProperty (ids::pattern, tp->getText(), nullptr); };
                addLabel ("file name: {session} {block} {date} {time} {n} {year} {month} {day}");
                add (std::move (te), 24);
            }
            add (boundChoice (theme, b, ids::format, "format", { "wav", "aiff", "flac" }, { "wav", "aiff", "flac" }), 22);
            add (boundChoice (theme, b, ids::bits, "bits", { "16", "24", "32 (float)" }, { "16", "24", "32" }), 22);
            add (boundSlider (theme, b, ids::splitMin, "new file every", { 0, 240, 1 }, 0, [] (float v) { return v < 0.5f ? juce::String ("never") : juce::String ((int) v) + " min"; }), 22);
            add (boundSlider (theme, b, ids::maxMin, "stop after", { 0, 600, 1 }, 0, [] (float v) { return v < 0.5f ? juce::String ("never") : juce::String ((int) v) + " min"; }), 22);
            add (boundToggle (theme, b, ids::pauseSilence, "pause while it is silent"), 22);
            add (boundSlider (theme, b, ids::silenceDb, "silence below", { -90, -20, 0.5f }, -60, db), 22);
            break;
        }
        case BlockType::Meter:
            addLink ("open it on the meters page", [this, id] { if (ctx.watch) ctx.watch (id); });
            break;
        default: break;
    }
    addLabel ("levels");
    auto mb = std::make_unique<MeterBridge> (theme); meters = mb.get(); meters->src = { &ctx.engine, id }; add (std::move (mb), 120);
    addLink ("look at it on the meters page", [this, id] { if (ctx.watch) ctx.watch (id); });
    contentHeight = 40; for (auto& r : rows) contentHeight += r.h + 4;
    setSize (getWidth(), contentHeight + 20);
    resized(); repaint();
}

void Inspector::timerCallback()
{
    if (meters) meters->tick();
    if (curve) curve->tick();
    if (! block.isValid()) return;
    const int id = block[ids::id];
    if (recLink)
    {
        const bool rec = ctx.engine.recording (id);
        recLink->setText (rec ? "stop recording" : ((bool) block[ids::armed] ? "waiting (the session is off)" : "record")); recLink->setLit (rec);
        if (recInfo)
        {
            juce::String s;
            if (auto* rt = ctx.engine.runtime (id))
            {
                if (rec) s << rt->currentFile.getFileName() << "\n" << timeText ((double) rt->recorded.load() / std::max (1.0, ctx.engine.sampleRate())) << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << juce::File::descriptionOfSizeInBytes (rt->currentFile.getSize()) << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << rt->channels << " ch" << (rt->silent.load() ? juce::String (juce::CharPointer_UTF8 ("  ·  paused (silence)")) : "");
                else s << (rt->status.isNotEmpty() ? rt->status : juce::String ("not recording"));
            }
            else s << "the session is off: switch it on to record";
            recInfo->setText (s, juce::dontSendNotification);
        }
    }
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (theme.ink.withAlpha (0.05f));
    if (! block.isValid()) { g.setColour (theme.ink.withAlpha (0.6f)); g.setFont (mono (11.0f)); g.drawFittedText ("click a block to see its controls.\ndrag from a block's dot on the right onto another block to wire them.", getLocalBounds().reduced (14), juce::Justification::topLeft, 4); return; }
    g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f)); g.drawText (typeInfo (typeFromId (block[ids::type])).name, 14, 8, getWidth() - 28, 12, juce::Justification::centredLeft);
    g.setColour (theme.ink); g.setFont (sans (17.0f, true)); g.drawText (block[ids::name].toString(), 14, 20, getWidth() - 28, 22, juce::Justification::centredLeft, true);
}
void Inspector::resized()
{
    int y = 46;
    for (auto& r : rows) { r.c->setBounds (14, y, getWidth() - 28, r.h); y += r.h + 4; }
}

// =====================================================================================================================
//  canvas
// =====================================================================================================================
juce::ValueTree BlockCanvas::blockAt (juce::Point<float> p) const
{
    for (int i = session.getNumChildren(); --i >= 0;) { auto b = session.getChild (i); if (b.hasType (ids::block) && boxOf (b).expanded (6.0f, 0.0f).contains (p)) return b; }
    return {};
}
juce::Path BlockCanvas::wirePath (juce::Point<float> a, juce::Point<float> b)
{
    juce::Path p; p.startNewSubPath (a);
    const float dx = std::max (40.0f, std::abs (b.x - a.x) * 0.5f);
    p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    return p;
}
juce::Point<int> BlockCanvas::wireAt (juce::Point<float> p) const
{
    for (auto w : session)
    {
        if (! w.hasType (ids::wire)) continue;
        auto a = Model::blockById (session, w[ids::from]), b = Model::blockById (session, w[ids::to]);
        if (! a.isValid() || ! b.isValid()) continue;
        auto path = wirePath (outPort (a), inPort (b));
        juce::Point<float> np; path.getNearestPoint (p, np);
        if (np.getDistanceFrom (p) < 6.0f) return { (int) w[ids::from], (int) w[ids::to] };
    }
    return { -1, -1 };
}

void BlockCanvas::timerCallback()
{
    if (! isShowing()) return;
    for (auto b : session)
    {
        if (! b.hasType (ids::block)) continue;
        const int id = b[ids::id];
        auto& v = levels[id];
        if (auto* rt = ctx.engine.runtime (id))
        {
            const int n = std::min (rt->levels.channels.load(), 16); v.resize ((size_t) n, 0.0f);
            for (int c = 0; c < n; ++c) { const float p = rt->levels.takePeak (c); v[(size_t) c] = std::max (p, v[(size_t) c] * 0.85f); }
        }
        else v.clear();
    }
    repaint();
}

void BlockCanvas::paint (juce::Graphics& g)
{
    g.setColour (theme.ink.withAlpha (0.05f));
    for (float x = std::fmod (origin.x, 40.0f); x < getWidth(); x += 40.0f) for (float y = std::fmod (origin.y, 40.0f); y < getHeight(); y += 40.0f) g.fillRect (x, y, 1.5f, 1.5f);
    if (! session.isValid()) return;
    const bool on = (bool) session[ids::on];
    // wires (brighter when sound goes through them)
    for (auto w : session)
    {
        if (! w.hasType (ids::wire)) continue;
        auto a = Model::blockById (session, w[ids::from]), b = Model::blockById (session, w[ids::to]);
        if (! a.isValid() || ! b.isValid()) continue;
        float lv = 0; for (float x : levels[(int) w[ids::from]]) lv = std::max (lv, x);
        const bool sel = selectedWire == juce::Point<int> ((int) w[ids::from], (int) w[ids::to]);
        g.setColour (sel ? theme.ink : theme.ink.withAlpha (on ? 0.35f + 0.6f * meterPos (lv) : 0.3f));
        g.strokePath (wirePath (outPort (a), inPort (b)), juce::PathStrokeType (sel ? 3.0f : 1.6f + 2.0f * meterPos (lv)));
    }
    if (wireFrom) { auto a = Model::blockById (session, wireFrom); if (a.isValid()) { g.setColour (theme.ink); g.strokePath (wirePath (outPort (a), wireEnd), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); } }
    // blocks
    for (auto b : session)
    {
        if (! b.hasType (ids::block)) continue;
        const auto t = typeFromId (b[ids::type]); const int id = b[ids::id];
        auto r = boxOf (b);
        const auto col = groupColour (t);
        const bool bypassed = (bool) b[ids::bypass];
        auto* rt = ctx.engine.runtime (id);
        g.setColour (col.withAlpha (bypassed ? 0.35f : 1.0f)); g.fillPath (skewRect (r, 0.12f));
        if (id == selected) { g.setColour (theme.ink); g.strokePath (skewRect (r.expanded (3.0f), 0.12f), juce::PathStrokeType (2.0f)); }
        const auto ink = inkFor (col);
        g.setColour (ink.withAlpha (0.65f)); g.setFont (mono (9.0f)); g.drawText (juce::String (typeInfo (t).id) + (bypassed ? juce::String (juce::CharPointer_UTF8 ("  · bypassed")) : ""), (int) r.getX() + 14, (int) r.getY() + 6, (int) r.getWidth() - 20, 11, juce::Justification::centredLeft);
        g.setColour (ink); g.setFont (sans (13.0f, true)); g.drawText (b[ids::name].toString(), (int) r.getX() + 14, (int) r.getY() + 18, (int) r.getWidth() - 24, 18, juce::Justification::centredLeft, true);
        juce::String sub;
        if (rt && rt->status.isNotEmpty()) sub = rt->status;
        else if (rt) sub = juce::String (rt->channels) + " ch";
        else sub = on ? "" : "off";
        if (t == BlockType::Recorder && rt && rt->writer.load()) { sub = "rec " + timeText ((double) rt->recorded.load() / std::max (1.0, ctx.engine.sampleRate())); g.setColour (juce::Colour (0xffd2452c)); g.fillEllipse (r.getRight() - 26.0f, r.getY() + 7.0f, 10.0f, 10.0f); }
        g.setColour (ink.withAlpha (0.75f)); g.setFont (mono (9.5f)); g.drawText (sub, (int) r.getX() + 14, (int) r.getY() + 37, (int) r.getWidth() - 24, 12, juce::Justification::centredLeft, true);
        // the levels: a thin bar per channel along the bottom
        const auto& lv = levels[id];
        if (! lv.empty())
        {
            const float w = (r.getWidth() - 34.0f) / (float) lv.size();
            for (size_t c = 0; c < lv.size(); ++c)
            {
                auto bar = juce::Rectangle<float> (r.getX() + 16.0f + c * w, r.getBottom() - 22.0f, std::max (1.0f, w - 1.5f), 14.0f);
                g.setColour (ink.withAlpha (0.15f)); g.fillRect (bar);
                g.setColour (meterColour (dsp::gainToDb (lv[c]), ink)); g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * meterPos (lv[c])));
            }
        }
        // the dots: in (left), out (right)
        if (! isSource (t)) { g.setColour (theme.ground); g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre (inPort (b))); g.setColour (ink); g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre (inPort (b)), 1.5f); }
        if (! isSink (t)) { g.setColour (ink); g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (outPort (b))); }
    }
    if (session.getNumChildren() == 0)
    {
        g.setColour (theme.ink.withAlpha (0.6f)); g.setFont (mono (12.0f));
        g.drawFittedText ("an empty session.\ndouble-click (or right-click) here to add a block, or start from a template (above).", getLocalBounds().reduced (40), juce::Justification::centred, 3);
    }
}

juce::ValueTree BlockCanvas::addBlockAt (BlockType t, juce::Point<float> at)
{
    auto b = ctx.model.addBlock (session, t, std::round ((at.x - origin.x) / 10.0f) * 10.0f, std::round ((at.y - origin.y) / 10.0f) * 10.0f);
    // wire it after the block that is selected, when that makes sense
    auto from = Model::blockById (session, selected);
    if (from.isValid() && ! isSink (typeFromId (from[ids::type])) && ! isSource (t)) ctx.model.connect (session, selected, b[ids::id]);
    select (b[ids::id]);
    return b;
}
void BlockCanvas::addMenu (juce::Point<float> at)
{
    juce::PopupMenu m; std::map<juce::String, juce::PopupMenu> groups;
    for (int i = 0; i < (int) BlockType::Count; ++i) groups[typeInfo ((BlockType) i).group].addItem (i + 1, typeInfo ((BlockType) i).name);
    m.addSectionHeader ("add a block");
    for (auto g : { "sources", "effects", "outputs" }) m.addSubMenu (g, groups[g]);
    m.showMenuAsync (juce::PopupMenu::Options(), [this, at] (int r) { if (r > 0) addBlockAt ((BlockType) (r - 1), at); });
}
void BlockCanvas::blockMenu (juce::ValueTree b)
{
    const int id = b[ids::id]; const auto t = typeFromId (b[ids::type]);
    juce::PopupMenu m;
    m.addItem (1, "rename..."); m.addItem (2, (bool) b[ids::bypass] ? "un-bypass" : "bypass"); m.addItem (3, "duplicate");
    if (t == BlockType::Plugin) m.addItem (5, "open the plug-in's window");
    m.addItem (6, "look at it on the meters page");
    m.addSeparator(); m.addItem (4, "remove");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, b, id, t] (int r) mutable
    {
        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("rename", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("n", b[ids::name].toString()); w->addButton ("ok", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([w, b] (int res) mutable { if (res == 1 && w->getTextEditorContents ("n").trim().isNotEmpty()) b.setProperty (ids::name, w->getTextEditorContents ("n").trim(), nullptr); }), true);
        }
        else if (r == 2) b.setProperty (ids::bypass, ! (bool) b[ids::bypass], nullptr);
        else if (r == 3) { auto c = b.createCopy(); c.setProperty (ids::id, ctx.model.nextId(), nullptr); c.setProperty (ids::x, (float) b[ids::x] + 30.0f, nullptr); c.setProperty (ids::y, (float) b[ids::y] + 90.0f, nullptr); c.setProperty (ids::state, b[ids::state], nullptr); session.appendChild (c, &ctx.model.undo); select (c[ids::id]); }
        else if (r == 4) { ctx.model.removeBlock (session, id); selected = 0; if (onSelect) onSelect ({}); }
        else if (r == 5 && ctx.openPlugin) ctx.openPlugin (id);
        else if (r == 6 && ctx.watch) ctx.watch (id);
        repaint();
        (void) t;
    });
}

void BlockCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    auto b = blockAt (p);
    if (b.isValid())
    {
        if (e.mods.isPopupMenu()) { select (b[ids::id]); blockMenu (b); return; }
        if (hitOut (b, p)) { wireFrom = b[ids::id]; wireEnd = p; return; }
        dragBlock = b[ids::id]; dragOff = p - boxOf (b).getPosition();
        if ((int) b[ids::id] != selected) select (b[ids::id]);
        return;
    }
    const auto w = wireAt (p);
    if (w.x >= 0) { selectedWire = w; selected = 0; if (onSelect) onSelect ({}); repaint(); return; }
    if (e.mods.isPopupMenu()) { addMenu (p); return; }
    selected = 0; selectedWire = { -1, -1 }; if (onSelect) onSelect ({});
    panning = true; panStart = origin;
    repaint();
}
void BlockCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (wireFrom) { wireEnd = e.position; repaint(); return; }
    if (panning) { origin = panStart + e.getOffsetFromDragStart().toFloat(); repaint(); return; }
    if (dragBlock)
    {
        auto b = Model::blockById (session, dragBlock); if (! b.isValid()) return;
        const auto at = e.position - dragOff - origin;
        b.setProperty (ids::x, std::round (at.x / 10.0f) * 10.0f, nullptr); b.setProperty (ids::y, std::round (at.y / 10.0f) * 10.0f, nullptr);
        repaint();
    }
}
void BlockCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (wireFrom)
    {
        auto to = blockAt (e.position);
        if (to.isValid() && ! isSource (typeFromId (to[ids::type])) && (int) to[ids::id] != wireFrom)
        {
            if (Model::wouldLoop (session, wireFrom, to[ids::id])) ctx.status ("that would make a loop: the sound would go round for ever");
            else ctx.model.connect (session, wireFrom, to[ids::id]);
        }
        wireFrom = 0;
    }
    dragBlock = 0; panning = false; repaint();
}
void BlockCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto b = blockAt (e.position);
    if (! b.isValid()) { addMenu (e.position); return; }
    if (typeFromId (b[ids::type]) == BlockType::Plugin && ctx.openPlugin) ctx.openPlugin (b[ids::id]);
    if (typeFromId (b[ids::type]) == BlockType::Meter && ctx.watch) ctx.watch (b[ids::id]);
}
bool BlockCanvas::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (selected) { ctx.model.removeBlock (session, selected); selected = 0; if (onSelect) onSelect ({}); }
        else if (selectedWire.x >= 0)
            for (int i = session.getNumChildren(); --i >= 0;) { auto w = session.getChild (i); if (w.hasType (ids::wire) && (int) w[ids::from] == selectedWire.x && (int) w[ids::to] == selectedWire.y) session.removeChild (i, &ctx.model.undo); }
        selectedWire = { -1, -1 }; repaint(); return true;
    }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { ctx.model.undo.undo(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { ctx.model.undo.redo(); return true; }
    return false;
}

// =====================================================================================================================
//  the page
// =====================================================================================================================
SessionPage::SessionPage (Ctx& c) : PageBase (c, Sessions), canvas (c, theme), inspector (c, theme),
    run ("run", theme), add ("+ block", theme), tmpl ("templates", theme), timer ("timer", theme), rename ("rename", theme), remove ("remove session", theme)
{
    addAndMakeVisible (canvas);
    inspectorView.setViewedComponent (&inspector, false); inspectorView.setScrollBarsShown (true, false); addAndMakeVisible (inspectorView);
    for (auto* l : { &run, &add, &tmpl, &timer, &rename, &remove }) addAndMakeVisible (l);
    run.onClick = [this] { if (canvas.session.isValid()) canvas.session.setProperty (ids::on, ! (bool) canvas.session[ids::on], nullptr); };
    run.setTooltip ("run the session: its sources are captured, its effects and outputs play, its recorders record");
    add.onClick = [this] { canvas.addMenu ({ 60.0f + (float) canvas.getWidth() * 0.3f, 60.0f }); };
    tmpl.onClick = [this] { templates (&tmpl); };
    timer.onClick = [this] { timerDialog(); };
    rename.onClick = [this]
    {
        auto s = canvas.session; if (! s.isValid()) return;
        auto* w = new juce::AlertWindow ("rename the session", {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", s[ids::name].toString()); w->addButton ("ok", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, s] (int r) mutable { if (r == 1 && w->getTextEditorContents ("n").trim().isNotEmpty()) s.setProperty (ids::name, w->getTextEditorContents ("n").trim(), nullptr); }), true);
    };
    remove.onClick = [this]
    {
        auto s = canvas.session; if (! s.isValid()) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "remove \"" + s[ids::name].toString() + "\"?", "its blocks and wires go too (recordings stay where they are).", "remove", "keep", nullptr,
            juce::ModalCallbackFunction::create ([this, s] (int r) { if (r == 1) { s.getParent().removeChild (s, &ctx.model.undo); if (ctx.show) ctx.show (Sessions, 0); } }));
    };
    canvas.onSelect = [this] (juce::ValueTree b) { inspector.setSize (inspectorView.getWidth() - 10, 200); inspector.show (b); };
    ctx.model.root.addListener (this);
}
void SessionPage::setSession (int id)
{
    canvas.session = Model::sessionById (ctx.model.root, id);
    canvas.selected = 0; canvas.selectedWire = { -1, -1 };
    inspector.show ({});
    repaint(); canvas.repaint();
}
void SessionPage::refresh() { if (canvas.session.isValid() && ! canvas.session.getParent().isValid()) setSession (0); repaint(); }
void SessionPage::tick() { run.setLit ((bool) canvas.session[ids::on]); run.setText ((bool) canvas.session[ids::on] ? "running" : "run"); }
void SessionPage::paint (juce::Graphics& g)
{
    auto s = canvas.session;
    juce::String sub;
    if (s.isValid())
    {
        int nb = 0; for (auto c : s) if (c.hasType (ids::block)) ++nb;
        sub << nb << juce::String (juce::CharPointer_UTF8 (" blocks  ·  ")) << ((bool) s[ids::on] ? "running" : "off");
        if ((bool) s[ids::on] && ctx.engine.running()) sub << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << juce::String (ctx.engine.sampleRate() / 1000.0, 1) << juce::String (juce::CharPointer_UTF8 (" khz  ·  load ")) << juce::String (juce::roundToInt (ctx.engine.host().load() * 100)) << " %";
        if (s[ids::startAt].toString().isNotEmpty()) sub << juce::String (juce::CharPointer_UTF8 ("  ·  starts at ")) << s[ids::startAt].toString();
        if ((double) s.getProperty (ids::stopAfter, 0.0) > 0) sub << juce::String (juce::CharPointer_UTF8 ("  ·  stops after ")) << (int) (double) s[ids::stopAfter] << " min";
        if (ctx.engine.lastError().isNotEmpty() && (bool) s[ids::on]) sub << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << ctx.engine.lastError();
    }
    paintHeader (g, s.isValid() ? s[ids::name].toString() : "sessions", s.isValid() ? sub : "make a session in the sidebar (+ session)");
}
void SessionPage::resized()
{
    int x = 24; for (auto* l : { &run, &add, &tmpl, &timer, &rename }) { l->setBounds (x, kTop + 6, l == &run ? 76 : 86, 22); x += (l == &run ? 76 : 86) + 6; }
    remove.setBounds (getWidth() - 24 - 130, kTop + 6, 130, 22);
    const int iw = 330;
    canvas.setBounds (24, kTop + 38, getWidth() - 48 - iw - 12, getHeight() - kTop - 38 - 20);
    inspectorView.setBounds (getWidth() - 24 - iw, kTop + 38, iw, getHeight() - kTop - 38 - 20);
    inspector.setSize (iw - 10, std::max (inspector.contentHeight + 20, inspectorView.getHeight()));
    inspector.resized();
}
void SessionPage::timerDialog()
{
    auto s = canvas.session; if (! s.isValid()) return;
    auto* w = new juce::AlertWindow ("timer", "start the session every day at a time (hh:mm, empty: never), and stop it after so many minutes (0: never)", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("at", s[ids::startAt].toString(), "start at");
    w->addTextEditor ("for", juce::String ((int) (double) s.getProperty (ids::stopAfter, 0.0)), "stop after (minutes)");
    w->addButton ("ok", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, s] (int r) mutable
    {
        if (r != 1) return;
        auto at = w->getTextEditorContents ("at").trim();
        if (at.isNotEmpty()) { auto p = juce::StringArray::fromTokens (at, ":", ""); at = p.size() == 2 ? juce::String (juce::jlimit (0, 23, p[0].getIntValue())).paddedLeft ('0', 2) + ":" + juce::String (juce::jlimit (0, 59, p[1].getIntValue())).paddedLeft ('0', 2) : juce::String(); }
        s.setProperty (ids::startAt, at, nullptr);
        s.setProperty (ids::stopAfter, std::max (0, w->getTextEditorContents ("for").getIntValue()), nullptr);
    }), true);
}
void SessionPage::templates (juce::Component* near)
{
    juce::PopupMenu m;
    m.addItem (1, "record an app (app -> recorder, and to your speakers)");
    m.addItem (2, "an app into a virtual device (for a call or a stream), with a monitor");
    m.addItem (3, "microphone + app, mixed, into a virtual device");
    m.addItem (4, "an app through plug-ins to your speakers");
    m.addItem (5, "record the whole mac (system audio -> recorder)");
    m.addItem (6, "podcast: microphone -> gate -> eq -> leveler -> limiter -> recorder");
    m.addItem (7, "a multichannel interface's inputs -> recorder (all channels, one file)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (near), [this] (int r)
    {
        auto s = canvas.session; if (! s.isValid() || r <= 0) return;
        auto& mdl = ctx.model;
        auto chain = [&] (std::initializer_list<BlockType> ts, float y) { std::vector<juce::ValueTree> v; float x = 20; for (auto t : ts) { v.push_back (mdl.addBlock (s, t, x, y)); x += 210; } for (size_t i = 1; i < v.size(); ++i) mdl.connect (s, v[i - 1][ids::id], v[i][ids::id]); return v; };
        juce::String virtualUid; for (auto& d : CoreAudioHost::devices()) if (d.isOurs) { virtualUid = d.uid; break; }
        switch (r)
        {
            case 1: { auto v = chain ({ BlockType::App, BlockType::Volume, BlockType::Recorder }, 40); auto o = mdl.addBlock (s, BlockType::Output, 440, 160); mdl.connect (s, v[1][ids::id], o[ids::id]); break; }
            case 2: { auto v = chain ({ BlockType::App, BlockType::Volume, BlockType::Output }, 40); v[0].setProperty (ids::mute, true, nullptr); v[2].setProperty (ids::device, virtualUid, nullptr); v[2].setProperty (ids::name, "virtual device", nullptr); auto mon = mdl.addBlock (s, BlockType::Output, 440, 160); mon.setProperty (ids::name, "monitor", nullptr); mdl.connect (s, v[1][ids::id], mon[ids::id]); break; }
            case 3: { auto mic = mdl.addBlock (s, BlockType::Input, 20, 40); mic.setProperty (ids::device, CoreAudioHost::defaultInputUid(), nullptr); mic.setProperty (ids::count, 1, nullptr); auto app = mdl.addBlock (s, BlockType::App, 20, 160); auto vol = mdl.addBlock (s, BlockType::Volume, 240, 100); auto out = mdl.addBlock (s, BlockType::Output, 460, 100); out.setProperty (ids::device, virtualUid, nullptr); mdl.connect (s, mic[ids::id], vol[ids::id]); mdl.connect (s, app[ids::id], vol[ids::id]); mdl.connect (s, vol[ids::id], out[ids::id]); break; }
            case 4: chain ({ BlockType::App, BlockType::Plugin, BlockType::Plugin, BlockType::Output }, 40); break;
            case 5: chain ({ BlockType::System, BlockType::Recorder }, 40); break;
            case 6: { auto v = chain ({ BlockType::Input, BlockType::Gate, BlockType::Peq, BlockType::Leveler, BlockType::Limiter, BlockType::Recorder }, 40); v[0].setProperty (ids::device, CoreAudioHost::defaultInputUid(), nullptr); v[0].setProperty (ids::count, 1, nullptr); auto m2 = mdl.addBlock (s, BlockType::Meter, 1060, 160); mdl.connect (s, v[4][ids::id], m2[ids::id]); break; }
            case 7: { auto v = chain ({ BlockType::Input, BlockType::Recorder }, 40); v[0].setProperty (ids::count, 16, nullptr); break; }
            default: break;
        }
        canvas.repaint();
    });
}
} // namespace um::ui
