#include "Pages.h"
#include "../engine/Correction.h"

namespace um::ui
{
// =====================================================================================================================
//  the system eq
// =====================================================================================================================
const std::vector<std::pair<juce::String, std::array<float, 10>>>& EqPage::presets()
{
    static const std::vector<std::pair<juce::String, std::array<float, 10>>> p {
        { "flat",          { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "bass boost",    { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 } },
        { "bass cut",      { -6, -5, -3, -1, 0, 0, 0, 0, 0, 0 } },
        { "treble boost",  { 0, 0, 0, 0, 0, 0, 1.5f, 3, 4.5f, 5 } },
        { "treble cut",    { 0, 0, 0, 0, 0, 0, -1.5f, -3, -4.5f, -6 } },
        { "vocal",         { -3, -2, -1, 1, 3, 3.5f, 3, 1, 0, -1 } },
        { "loudness",      { 6, 4, 0, -1, -2, -1, 0, 2, 4, 5 } },
        { "podcast",       { -6, -3, 0, 1, 2, 3, 3, 2, 0, -2 } },
        { "acoustic",      { 3, 3, 2, 1, 1, 1, 2, 3, 2, 1 } },
        { "electronic",    { 4.5f, 4, 1, 0, -2, 1, 0.5f, 1, 4, 5 } },
        { "rock",          { 5, 4, 3, 1, -1, -1, 1, 2.5f, 3.5f, 4.5f } },
        { "classical",     { 4.5f, 3.5f, 3, 2.5f, -1.5f, -1.5f, 0, 2, 3, 3.5f } },
        { "late night (quiet bass)", { -8, -6, -3, -1, 0, 1, 1, 0, -1, -2 } },
        { "laptop speakers", { 0, 0, 2, 3, 1, 0, 0, 1, 2, 1 } },
    };
    return p;
}

EqPage::EqPage (Ctx& c) : PageBase (c, SystemEq), onLink ("off", theme), savePreset ("save as a preset...", theme), presetLink ("presets", theme), flat ("flat", theme),
    importLink ("import a filter file...", theme), pasteLink ("paste filters...", theme), clearLink ("clear", theme), levels (theme)
{
    for (auto* l : { &onLink, &savePreset, &presetLink, &flat, &importLink, &pasteLink, &clearLink }) addAndMakeVisible (l);
    addAndMakeVisible (levels); levels.compact = true; addChildComponent (tableView); tableView.setScrollBarsShown (true, false);
    onLink.onClick = [this] { session.setProperty (ids::on, ! (bool) session[ids::on], nullptr); };
    flat.onClick = [this] { applyPreset (presets()[0].second); };
    presetLink.onClick = [this]
    {
        juce::PopupMenu m; int k = 1;
        for (auto& [n, g] : presets()) m.addItem (k++, n);
        auto user = ctx.model.eqSettings().getChildWithName ("presets");
        if (user.isValid() && user.getNumChildren() > 0) { m.addSeparator(); for (int i = 0; i < user.getNumChildren(); ++i) m.addItem (100 + i, user.getChild (i)[ids::name].toString()); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetLink), [this, user] (int r)
        {
            if (r >= 1 && r <= (int) presets().size()) applyPreset (presets()[(size_t) r - 1].second);
            else if (r >= 100 && user.isValid())
            {
                // a saved preset is every stage as it was
                auto p = user.getChild (r - 100);
                for (auto st : p) if (auto b = stage (st[ids::role].toString().toRawUTF8()); b.isValid()) for (int i = 0; i < st.getNumProperties(); ++i) { auto nm = st.getPropertyName (i); if (nm != ids::role) b.setProperty (nm, st[nm], nullptr); }
                if (! p.getNumChildren()) stage ("graphic").setProperty (ids::bands, p[ids::bands], nullptr);
                build();
            }
        });
    };
    savePreset.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("save the eq as a preset", "every stage is kept: graphic, parametric, correction, speakers", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", "my speakers"); w->addButton ("save", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            auto es = ctx.model.eqSettings(); auto user = es.getChildWithName ("presets"); if (! user.isValid()) { user = juce::ValueTree ("presets"); es.appendChild (user, nullptr); }
            juce::ValueTree p ("preset"); p.setProperty (ids::name, w->getTextEditorContents ("n"), nullptr);
            for (auto role : { "graphic", "tone", "correction", "speakers" }) { auto b = stage (role); juce::ValueTree st ("stage"); st.setProperty (ids::role, role, nullptr);
                for (auto id : { ids::bands, ids::db, ids::bypass, ids::align, juce::Identifier ("count") }) if (b.hasProperty (id)) st.setProperty (id, b[id], nullptr); p.appendChild (st, nullptr); }
            user.appendChild (p, nullptr); ctx.status ("preset saved");
        }), true);
    };
    importLink.onClick = [this]
    {
        auto fc = std::make_shared<juce::FileChooser> ("filters from room eq wizard, equalizer apo or autoeq", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.txt;*.cfg;*.csv");
        fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, fc] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) importCorrection (c.getResult().loadFileAsString(), c.getResult().getFileName()); });
    };
    pasteLink.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("paste filters", "lines like: Filter 1: ON PK Fc 63.5 Hz Gain -4.3 dB Q 2.5  (room eq wizard, equalizer apo, autoeq)", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("t", juce::SystemClipboard::getTextFromClipboard()); w->getTextEditor ("t")->setMultiLine (true); w->getTextEditor ("t")->setSize (520, 220);
        w->addButton ("import", 1); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) { if (r == 1) importCorrection (w->getTextEditorContents ("t"), "pasted text"); }), true);
    };
    clearLink.onClick = [this] { stage ("correction").setProperty (ids::bands, "", nullptr); stage ("correction").setProperty (ids::db, 0.0f, nullptr); build(); };
    build();
}
juce::ValueTree EqPage::stage (const char* role) { for (auto b : session) if (b.hasType (ids::block) && b[ids::role].toString() == role) return b; return {}; }
void EqPage::importCorrection (const juce::String& text, const juce::String& from)
{
    const auto c = parseCorrection (text);
    if (c.read == 0) { ctx.status ("no filters found in " + from); return; }
    auto b = stage ("correction");
    b.setProperty (ids::bands, Model::peqText (c.bands), nullptr); b.setProperty (ids::db, c.preamp, nullptr); b.setProperty (ids::bypass, false, nullptr);
    ctx.status (juce::String (c.read) + " filters read from " + from + (c.preamp != 0 ? ", preamp " + juce::String (c.preamp, 2) + " db" : juce::String()) + (c.skipped > 0 ? " (" + juce::String (c.skipped) + " lines not understood)" : juce::String()));
    build();
}
void EqPage::applyPreset (const std::array<float, 10>& g)
{
    // a 10-band preset onto 10 or 31 bands (in between: along the log of the frequency)
    auto b = stage ("graphic"); auto gains = Model::graphicBands (b); const auto& f = dsp::graphicCentres ((int) gains.size()); const auto& f10 = dsp::graphicCentres (10);
    for (size_t i = 0; i < gains.size(); ++i)
    {
        const double lf = std::log2 (f[i]);
        size_t k = 0; while (k + 1 < 10 && std::log2 (f10[k + 1]) < lf) ++k;
        const double a = std::log2 (f10[k]), bb = std::log2 (f10[std::min<size_t> (k + 1, 9)]);
        const double t = bb > a ? juce::jlimit (0.0, 1.0, (lf - a) / (bb - a)) : 0.0;
        gains[i] = (float) (g[k] + t * (g[std::min<size_t> (k + 1, 9)] - g[k]));
    }
    b.setProperty (ids::bands, Model::graphicText (gains), nullptr); b.setProperty (ids::bypass, false, nullptr);
    build();
}
void EqPage::build()
{
    session = ctx.engine.systemEqSession();
    auto vol = stage ("volume"), graphic = stage ("graphic"), tone = stage ("tone"), corr = stage ("correction"), spk = stage ("speakers"), lim = stage ("limiter"), out = stage ("output");
    auto es = ctx.model.eqSettings();
    currentMode = juce::jlimit (0, NumModes - 1, (int) es.getProperty (ids::mode, 1));
    juce::StringArray names, uids; deviceLists (true, names, uids, true);
    device = boundChoice (theme, out, ids::device, "output", names, uids); device->labelW = 70; addAndMakeVisible (*device);
    volume = std::make_unique<PreciseSlider> (theme, "volume", -80.0, 12.0, 0.0, 2, "db"); volume->set ((float) vol[ids::db], false);
    volume->onChange = [vol] (double v) mutable { vol.setProperty (ids::db, v, nullptr); }; addAndMakeVisible (*volume);
    balance = std::make_unique<PreciseSlider> (theme, "balance", -100.0, 100.0, 0.0, 1, "%"); balance->set ((float) vol[ids::balance] * 100.0, false);
    balance->onChange = [vol] (double v) mutable { vol.setProperty (ids::balance, v / 100.0, nullptr); }; addAndMakeVisible (*balance);
    preamp = std::make_unique<PreciseSlider> (theme, "preamp", -30.0, 12.0, 0.0, 2, "db"); preamp->set ((float) tone[ids::db], false);
    preamp->onChange = [tone] (double v) mutable { tone.setProperty (ids::db, v, nullptr); }; addAndMakeVisible (*preamp);
    mode = std::make_unique<ValueChoice> (theme, "mode", juce::StringArray { "basic", "10-band", "31-band", "parametric", "room correction", "speakers" }, currentMode); mode->asTabs = true; mode->labelW = 46; addAndMakeVisible (*mode);
    mode->onChange = [this] (int i)
    {
        ctx.model.eqSettings().setProperty (ids::mode, i, nullptr);
        auto gr = stage ("graphic");
        if (i == Ten && Model::graphicCount (gr) != 10) { gr.setProperty ("count", 10, nullptr); gr.setProperty (ids::bands, "0 0 0 0 0 0 0 0 0 0", nullptr); }
        if (i == ThirtyOne && Model::graphicCount (gr) != 31) { auto old = Model::graphicBands (gr); gr.setProperty ("count", 31, nullptr); std::array<float, 10> a {}; for (size_t k = 0; k < 10 && k < old.size(); ++k) a[k] = old[k]; applyPreset (a); }
        if (i == Basic && Model::graphicCount (gr) != 10) { gr.setProperty ("count", 10, nullptr); gr.setProperty (ids::bands, "0 0 0 0 0 0 0 0 0 0", nullptr); }
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<EqPage> (this)] { if (sp) sp->build(); });
    };
    const int ph = (bool) graphic[ids::linear] ? 1 : (bool) graphic[ids::exact] ? 0 : 2;
    phase = std::make_unique<ValueChoice> (theme, "filters", juce::StringArray { "exact, minimum phase", "exact, linear phase (0.7 s delay)", "light (matched iir)" }, ph); phase->labelW = 56; addAndMakeVisible (*phase);
    phase->setTooltip ("exact: a long filter made from the ideal curve, right to the hundredth of a db at every frequency (minimum phase: no delay; linear phase: no phase shift at all, but 0.7 s late). light: matched biquads, close to the ideal (within 0.4 db up to 10 khz), the least cpu");
    phase->onChange = [this] (int i) { for (auto r : { "graphic", "tone", "correction" }) { stage (r).setProperty (ids::linear, i == 1, nullptr); stage (r).setProperty (ids::exact, i == 0, nullptr); } };
    scale = std::make_unique<ValueChoice> (theme, "scale", juce::StringArray { juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "3 db")), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "6 db")), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "12 db")), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "24 db")) }, (int) es.getProperty ("scale", 2)); scale->labelW = 46; addAndMakeVisible (*scale);
    scale->onChange = [this] (int i) { ctx.model.eqSettings().setProperty ("scale", i, nullptr); const float r = i == 0 ? 3.0f : i == 1 ? 6.0f : i == 2 ? 12.0f : 24.0f; if (plot) { plot->range = r; plot->repaint(); } if (faders) { faders->range = std::max (6.0f, r); faders->repaint(); } };
    protect = std::make_unique<ValueToggle> (theme, "clip protection", ! (bool) lim[ids::bypass]); protect->onChange = [lim] (bool v) mutable { lim.setProperty (ids::bypass, ! v, nullptr); }; addAndMakeVisible (*protect);
    perDevice = std::make_unique<ValueToggle> (theme, "per output device", (bool) es[ids::perDevice]); perDevice->onChange = [this] (bool v) { ctx.model.eqSettings().setProperty (ids::perDevice, v, nullptr); }; addAndMakeVisible (*perDevice);
    // the stage this mode edits, and its on / off
    juce::ValueTree edited = currentMode == Parametric ? tone : currentMode == Correction ? corr : currentMode == Speakers ? spk : graphic;
    stageOn = std::make_unique<ValueToggle> (theme, juce::String ("this stage on: ") + (currentMode == Parametric ? "parametric" : currentMode == Correction ? "room correction" : currentMode == Speakers ? "speakers" : "graphic"), ! (bool) edited[ids::bypass]);
    stageOn->onChange = [edited] (bool v) mutable { edited.setProperty (ids::bypass, ! v, nullptr); }; addAndMakeVisible (*stageOn);
    // the response of every stage, the spectrum of what comes out
    plot = std::make_unique<ResponsePlot> (theme, ctx.engine); addAndMakeVisible (*plot);
    plot->stages = { graphic, tone, corr }; plot->watch (spk[ids::id]);
    const int sc = (int) es.getProperty ("scale", 2); plot->range = sc == 0 ? 3.0f : sc == 1 ? 6.0f : sc == 2 ? 12.0f : 24.0f;
    plot->editable = currentMode == Parametric ? tone : currentMode == Correction ? corr : juce::ValueTree();
    if (auto* rt = ctx.engine.runtime (spk[ids::id])) rt->analysed = true;
    // the editor of the mode
    bass.reset(); mid.reset(); treble.reset(); faders.reset(); table.reset(); speakers.reset(); corrGain.reset(); tableView.setViewedComponent (nullptr, false); tableView.setVisible (false);
    if (currentMode == Basic)
    {
        auto g = Model::graphicBands (graphic);
        auto avg = [&] (int a, int b) { float s = 0; for (int i = a; i <= b; ++i) s += g[(size_t) i]; return s / (float) (b - a + 1); };
        auto three = [this, graphic] (int a, int b, const juce::String& label, float now) mutable
        {
            auto s = std::make_unique<PreciseSlider> (theme, label, -12.0, 12.0, 0.0, 2, "db"); s->set (now, false);
            s->onChange = [graphic, a, b] (double v) mutable { auto gg = Model::graphicBands (graphic); for (int i = a; i <= b; ++i) { const double w = (i == a || i == b) && b > a ? 0.6 : 1.0; gg[(size_t) i] = (float) (v * w); } graphic.setProperty (ids::bands, Model::graphicText (gg), nullptr); };
            addAndMakeVisible (*s); return s;
        };
        bass = three (0, 3, "bass", avg (0, 2)); mid = three (3, 6, "mid", avg (4, 5)); treble = three (7, 9, "treble", avg (7, 9));
    }
    else if (currentMode == Ten || currentMode == ThirtyOne) { faders = std::make_unique<FaderBank> (theme, graphic); faders->range = std::max (6.0f, plot->range); faders->onEdit = [this] { if (plot) plot->repaint(); }; addAndMakeVisible (*faders); }
    else if (currentMode == Parametric || currentMode == Correction)
    {
        auto b = currentMode == Parametric ? tone : corr;
        table = std::make_unique<BandTable> (theme, b); table->onEdit = [this] { if (plot) plot->repaint(); };
        plot->onEdit = [this] { if (table) table->rebuild(); };
        plot->onSelect = [this] (int i) { if (table) { table->highlighted = i; table->repaint(); } };
        tableView.setViewedComponent (table.get(), false); tableView.setVisible (true);
        if (currentMode == Correction) { corrGain = std::make_unique<PreciseSlider> (theme, "correction gain", -30.0, 12.0, 0.0, 2, "db"); corrGain->set ((float) corr[ids::db], false); corrGain->onChange = [corr] (double v) mutable { corr.setProperty (ids::db, v, nullptr); }; addAndMakeVisible (*corrGain); }
    }
    else if (currentMode == Speakers) { speakers = std::make_unique<SpeakerTable> (theme, spk, 2, ctx.engine.sampleRate() > 0 ? ctx.engine.sampleRate() : 48000.0); addAndMakeVisible (*speakers); }
    for (auto* l : { &importLink, &pasteLink, &clearLink }) l->setVisible (currentMode == Correction);
    levels.src = { &ctx.engine, (int) out[ids::id] };
    resized(); repaint();
}
void EqPage::tick()
{
    onLink.setText ((bool) session[ids::on] ? "on" : "off"); onLink.setLit ((bool) session[ids::on]);
    if (auto* rt = ctx.engine.runtime (stage ("speakers")[ids::id])) rt->analysed = isShowing();
    if (plot) plot->tick();
    levels.tick();
    // per output device: when the output changes, its eq comes back (every stage)
    auto es = ctx.model.eqSettings();
    const auto outUid = stage ("output")[ids::device].toString().isEmpty() ? CoreAudioHost::defaultOutputUid() : stage ("output")[ids::device].toString();
    if ((bool) es[ids::perDevice] && outUid.isNotEmpty())
    {
        auto store = es.getChildWithName ("devices"); if (! store.isValid()) { store = juce::ValueTree ("devices"); es.appendChild (store, nullptr); }
        auto entry = store.getChildWithProperty (ids::device, outUid);
        const juce::Identifier keep[] = { ids::bands, ids::db, ids::bypass, ids::align, juce::Identifier ("count") };
        if (outUid != lastOutput && lastOutput.isNotEmpty() && entry.isValid())
        {
            for (auto st : entry) if (auto b = stage (st[ids::role].toString().toRawUTF8()); b.isValid()) for (auto& k : keep) if (st.hasProperty (k)) b.setProperty (k, st[k], nullptr);
            ctx.status ("the eq for this output"); build();
        }
        if (! entry.isValid()) { entry = juce::ValueTree ("dev"); entry.setProperty (ids::device, outUid, nullptr); store.appendChild (entry, nullptr); }
        entry.removeAllChildren (nullptr);
        for (auto role : { "graphic", "tone", "correction", "speakers" }) { auto b = stage (role); juce::ValueTree st ("stage"); st.setProperty (ids::role, role, nullptr); for (auto& k : keep) if (b.hasProperty (k)) st.setProperty (k, b[k], nullptr); entry.appendChild (st, nullptr); }
    }
    lastOutput = outUid;
    repaint (0, 0, getWidth(), 100);
}
void EqPage::paint (juce::Graphics& g)
{
    juce::String sub = "everything the mac plays: captured, the original muted, through these stages in 64-bit (matched filters, exact to 20 khz), to the speakers";
    auto src = stage ("source");
    if ((bool) session[ids::on]) if (auto* rt = ctx.engine.runtime (src[ids::id])) if (rt->status.isNotEmpty()) sub = rt->status;
    if ((bool) stage ("graphic")[ids::exact] && ! (bool) stage ("graphic")[ids::linear]) sub = juce::String (juce::CharPointer_UTF8 ("exact eq (minimum phase): the ideal curve to a hundredth of a db at every frequency, no delay  ·  ")) + sub.upToFirstOccurrenceOf (":", false, false);
    if ((bool) stage ("graphic")[ids::linear]) { int lat = 0; for (auto r : { "graphic", "tone", "correction" }) if (auto* rt = ctx.engine.runtime (stage (r)[ids::id])) lat = std::max (lat, rt->linearLatency); sub = "linear phase: " + juce::String (lat / std::max (1.0, ctx.engine.sampleRate()) * 1000.0, 0) + juce::String (juce::CharPointer_UTF8 (" ms of delay (exact magnitude, no phase shift)  ·  ")) + sub.upToFirstOccurrenceOf (":", false, false); }
    if ((bool) session[ids::on] && CoreAudioHost::capturePermission() == CoreAudioHost::Permission::Denied) sub = "umleitung may not capture the mac's sound yet: allow it in system settings > privacy & security > screen & system audio recording";
    paintHeader (g, "system eq", sub);
}
void EqPage::resized()
{
    const int y = kTop + 8, w = getWidth();
    onLink.setBounds (24, y, 60, 24);
    if (device) device->setBounds (92, y, 340, 22);
    presetLink.setBounds (440, y, 76, 22); flat.setBounds (520, y, 46, 22); savePreset.setBounds (570, y, 150, 22);
    if (perDevice) perDevice->setBounds (730, y, 190, 22);
    if (protect) protect->setBounds (930, y, 170, 22);
    const int third = (w - 48 - 24) / 3;
    if (volume) volume->setBounds (24, y + 32, third, 24);
    if (balance) balance->setBounds (24 + third + 12, y + 32, third, 24);
    if (preamp) preamp->setBounds (24 + 2 * (third + 12), y + 32, third, 24);
    if (mode) mode->setBounds (24, y + 66, 640, 22);
    if (phase) phase->setBounds (676, y + 66, 260, 22);
    if (scale) scale->setBounds (946, y + 66, std::max (140, w - 946 - 24), 22);
    auto area = juce::Rectangle<int> (24, y + 98, w - 48 - 96, getHeight() - (y + 98) - 26);
    levels.setBounds (w - 24 - 84, y + 98, 84, area.getHeight());
    if (stageOn) stageOn->setBounds (area.getX(), area.getY(), 300, 22);
    for (auto* l : { &importLink, &pasteLink, &clearLink }) (void) l;
    importLink.setBounds (area.getX() + 310, area.getY(), 180, 22); pasteLink.setBounds (area.getX() + 496, area.getY(), 130, 22); clearLink.setBounds (area.getX() + 630, area.getY(), 60, 22);
    if (corrGain) corrGain->setBounds (area.getX() + 700, area.getY(), std::min (420, area.getWidth() - 700), 22);
    area.removeFromTop (28);
    const bool tall = currentMode == Parametric || currentMode == Correction || currentMode == Speakers || currentMode == Basic;
    auto top = area.removeFromTop (tall ? area.getHeight() * 55 / 100 : area.getHeight() * 45 / 100);
    if (plot) plot->setBounds (top);
    area.removeFromTop (10);
    if (faders) faders->setBounds (area);
    if (bass) { const int bw = std::min (620, area.getWidth()); bass->setBounds (area.getX(), area.getY() + 10, bw, 26); mid->setBounds (area.getX(), area.getY() + 50, bw, 26); treble->setBounds (area.getX(), area.getY() + 90, bw, 26); }
    if (table) { tableView.setBounds (area); table->setSize (std::max (720, area.getWidth() - 12), table->getHeight()); }
    if (speakers) { speakers->setBounds (area.withHeight (std::min (area.getHeight(), speakers->heightNeeded() + 10))); }
}

// =====================================================================================================================
//  virtual devices
// =====================================================================================================================
DevicesPage::DevicesPage (Ctx& c) : PageBase (c, Devices), install ("install the driver", theme), addDev ("+ device", theme), removeDev ("remove", theme), editRouting ("its routing...", theme), applyLink ("apply", theme)
{
    for (auto* l : { &install, &addDev, &removeDev, &editRouting, &applyLink }) addAndMakeVisible (l);
    name = std::make_unique<juce::TextEditor>(); name->setFont (mono (12.0f));
    name->setColour (juce::TextEditor::backgroundColourId, theme.ink.withAlpha (0.06f)); name->setColour (juce::TextEditor::textColourId, theme.ink); name->setColour (juce::TextEditor::outlineColourId, theme.ink.withAlpha (0.3f));
    addAndMakeVisible (*name);
    channels = std::make_unique<ValueSlider> (theme, "channels", juce::NormalisableRange<float> (1, 256, 1), 2, 2); channels->textFromValue = [] (float v) { return juce::String ((int) v) + " in / out"; }; channels->setLabelWidth (80); addAndMakeVisible (*channels);
    hidden = std::make_unique<ValueToggle> (theme, "hidden from other apps (only umleitung uses it)", false); addAndMakeVisible (*hidden);
    install.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, installed ? "update the driver?" : "install the driver?",
            "umleitung's virtual devices need its audio driver in /library/audio/plug-ins/hal. the mac asks for an administrator password, and every app's sound stops for a moment while the audio system restarts.",
            installed ? "update" : "install", "not now", nullptr,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r != 1) return; juce::String err; if (DriverControl::install (err)) message = "the driver is installed"; else message = err; reload(); }));
    };
    addDev.onClick = [this] { VirtualDevice d; d.name = "umleitung " + juce::String ((int) devs.size() + 1); d.channels = 2; devs.push_back (d); sel = (int) devs.size() - 1; apply(); };
    removeDev.onClick = [this] { if (sel >= 0 && sel < (int) devs.size()) { devs.erase (devs.begin() + sel); sel = -1; apply(); } };
    applyLink.onClick = [this]
    {
        if (sel < 0 || sel >= (int) devs.size()) return;
        devs[(size_t) sel].name = name->getText().trim().isEmpty() ? devs[(size_t) sel].name : name->getText().trim();
        devs[(size_t) sel].channels = (int) channels->getValue(); devs[(size_t) sel].hidden = hidden->isOn();
        apply();
    };
    editRouting.onClick = [this]
    {
        if (sel < 0 || sel >= (int) devs.size()) return;
        auto s = routingSession (devs[(size_t) sel], true);
        if (ctx.show) ctx.show (Sessions, s[ids::id]);
    };
    reload();
}
void DevicesPage::reload()
{
    installed = DriverControl::installed(); version = DriverControl::installedVersion();
    devs = installed ? DriverControl::devices() : std::vector<VirtualDevice>();
    if (sel >= (int) devs.size()) sel = (int) devs.size() - 1;
    if (sel >= 0) { name->setText (devs[(size_t) sel].name, false); channels->setValue ((float) devs[(size_t) sel].channels, juce::dontSendNotification); hidden->setOn (devs[(size_t) sel].hidden, juce::dontSendNotification); }
    install.setText (installed ? "update the driver" : "install the driver");
    for (auto* c : std::initializer_list<juce::Component*> { &addDev, &removeDev, &editRouting, &applyLink, name.get(), channels.get(), hidden.get() }) c->setVisible (installed);
    for (auto* c : std::initializer_list<juce::Component*> { &removeDev, &editRouting, &applyLink, name.get(), channels.get(), hidden.get() }) c->setVisible (installed && sel >= 0);
    repaint();
}
void DevicesPage::apply()
{
    juce::String err;
    if (! DriverControl::setDevices (devs, err)) message = err; else message = "devices set";
    // the devices appear a moment later
    juce::Timer::callAfterDelay (600, [sp = juce::Component::SafePointer<DevicesPage> (this)] { if (sp) sp->reload(); });
}
juce::ValueTree DevicesPage::routingSession (const VirtualDevice& d, bool make)
{
    for (auto s : ctx.model.root) if (s.hasType (ids::session) && s[ids::kind].toString() == "device" && s[ids::device].toString() == d.uid) return s;
    if (! make) return {};
    auto s = ctx.model.addSession ("into " + d.name);
    s.setProperty (ids::kind, "device", nullptr); s.setProperty (ids::device, d.uid, nullptr);
    auto app = ctx.model.addBlock (s, BlockType::App, 20, 40);
    auto mx = ctx.model.addBlock (s, BlockType::Matrix, 240, 40); mx.setProperty (ids::outs, d.channels, nullptr);
    auto out = ctx.model.addBlock (s, BlockType::Output, 460, 40); out.setProperty (ids::device, d.uid, nullptr); out.setProperty (ids::name, d.name, nullptr);
    ctx.model.connect (s, app[ids::id], mx[ids::id]); ctx.model.connect (s, mx[ids::id], out[ids::id]);
    return s;
}
void DevicesPage::tick() { if (++tickCount % 60 == 0 && ! installed) reload(); }
void DevicesPage::mouseDown (const juce::MouseEvent& e)
{
    const int i = (e.y - (kTop + 74)) / 26;
    if (e.x < 460 && e.y > kTop + 74 && i >= 0 && i < (int) devs.size()) { sel = i; reload(); }
}
void DevicesPage::paint (juce::Graphics& g)
{
    paintHeader (g, "virtual devices", installed ? "devices other apps can choose: what umleitung plays into one comes out of it as a microphone, and what an app plays into one, umleitung can take (driver " + version + ")"
                                                 : "umleitung's virtual devices need its driver (one install, with your password). everything else works without it");
    g.setFont (mono (11.0f));
    if (message.isNotEmpty()) { g.setColour (theme.ink.withAlpha (0.7f)); g.drawText (message, 24, getHeight() - 22, getWidth() - 48, 16, juce::Justification::centredLeft); }
    if (! installed) return;
    g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f));
    g.drawText ("name", 24, kTop + 56, 200, 14, juce::Justification::centredLeft); g.drawText ("channels", 300, kTop + 56, 80, 14, juce::Justification::centredLeft); g.drawText ("routing", 380, kTop + 56, 80, 14, juce::Justification::centredLeft);
    for (int i = 0; i < (int) devs.size(); ++i)
    {
        auto r = juce::Rectangle<float> (24.0f, (float) (kTop + 74 + i * 26), 436.0f, 24.0f);
        if (i == sel) { g.setColour (theme.fill); g.fillPath (skewRect (r, 0.2f)); }
        g.setColour (i == sel ? inkFor (theme.fill) : theme.ink); g.setFont (mono (12.0f));
        g.drawText (devs[(size_t) i].name + (devs[(size_t) i].hidden ? "  (hidden)" : ""), (int) r.getX() + 10, (int) r.getY(), 260, 24, juce::Justification::centredLeft, true);
        g.drawText (juce::String (devs[(size_t) i].channels), (int) r.getX() + 276, (int) r.getY(), 60, 24, juce::Justification::centredLeft);
        auto s = const_cast<DevicesPage*> (this)->routingSession (devs[(size_t) i], false);
        g.drawText (s.isValid() ? ((bool) s[ids::on] ? "running" : "off") : "-", (int) r.getX() + 356, (int) r.getY(), 80, 24, juce::Justification::centredLeft);
    }
    if (devs.empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("no virtual devices: + device", 24, kTop + 80, 400, 18, juce::Justification::centredLeft); }
}
void DevicesPage::resized()
{
    install.setBounds (24, kTop + 10, 170, 24); addDev.setBounds (204, kTop + 10, 90, 24);
    const int x = 500, w = std::min (520, getWidth() - x - 24);
    name->setBounds (x, kTop + 74, w, 24); channels->setBounds (x, kTop + 106, w, 22); hidden->setBounds (x, kTop + 136, w, 22);
    applyLink.setBounds (x, kTop + 168, 70, 24); editRouting.setBounds (x + 80, kTop + 168, 130, 24); removeDev.setBounds (x + 220, kTop + 168, 80, 24);
}

// =====================================================================================================================
//  meters
// =====================================================================================================================
MetersPage::MetersPage (Ctx& c) : PageBase (c, Meters), choose ("look at...", theme), bridge (theme), vuL (theme, 0), vuR (theme, 1), loud (theme), spectrum (theme), gram (theme), gonio (theme), scope (theme)
{
    vuL.label = "left"; vuR.label = "right";
    for (auto* x : std::initializer_list<juce::Component*> { &choose, &bridge, &vuL, &vuR, &loud, &spectrum, &gram, &gonio, &scope }) addAndMakeVisible (x);
    spectrum.onFrame = [this] (const std::vector<float>& m) { gram.sampleRate = spectrum.sampleRate(); gram.push (m); };
    choose.onClick = [this]
    {
        juce::PopupMenu m; std::vector<int> idsList;
        for (auto s : ctx.model.root)
        {
            if (! s.hasType (ids::session)) continue;
            juce::PopupMenu sub;
            for (auto b : s) if (b.hasType (ids::block)) { idsList.push_back (b[ids::id]); sub.addItem ((int) idsList.size(), b[ids::name].toString() + "  (" + typeInfo (typeFromId (b[ids::type])).name + ")", true, (int) b[ids::id] == watched()); }
            m.addSubMenu (s[ids::name].toString() + ((bool) s[ids::on] ? "" : "  (off)"), sub);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&choose), [this, idsList] (int r) { if (r > 0) watch (idsList[(size_t) r - 1]); });
    };
}
void MetersPage::watch (int id)
{
    if (auto* old = ctx.engine.runtime (watched())) if (old->type != BlockType::Meter) old->analysed = false;
    for (auto* w : { &bridge.src, &vuL.src, &vuR.src, &loud.src, &spectrum.src, &gonio.src, &scope.src }) *w = { &ctx.engine, id };
    if (auto* rt = ctx.engine.runtime (id)) { rt->analysed = true; rt->loudness.reset(); }
    repaint();
}
void MetersPage::visibilityChanged() { if (auto* rt = ctx.engine.runtime (watched())) rt->analysed = isVisible() || rt->type == BlockType::Meter; }
void MetersPage::tick()
{
    if (auto* rt = ctx.engine.runtime (watched())) rt->analysed = true;   // (a rebuilt plan makes a new runtime)
    bridge.tick(); vuL.tick(); vuR.tick(); loud.tick(); spectrum.tick(); gonio.tick(); scope.tick();
    repaint (0, 0, getWidth(), 100);
}
void MetersPage::paint (juce::Graphics& g)
{
    juce::String what = "choose a block to look at (any block of a session that is running)";
    for (auto s : ctx.model.root) if (auto b = Model::blockById (s, watched()); b.isValid()) what = s[ids::name].toString() + "  >  " + b[ids::name].toString() + ((bool) s[ids::on] ? "" : "  (the session is off)");
    paintHeader (g, "meters", what);
}
void MetersPage::resized()
{
    choose.setBounds (24, kTop + 8, 120, 24);
    auto r = getLocalBounds().withTrimmedTop (kTop + 40).reduced (24, 0).withTrimmedBottom (20);
    auto left = r.removeFromLeft (std::max (260, r.getWidth() / 4)); r.removeFromLeft (16);
    bridge.setBounds (left.removeFromTop (left.getHeight() / 2)); left.removeFromTop (12);
    auto vus = left.removeFromTop (left.getHeight() / 2); vuL.setBounds (vus.removeFromLeft (vus.getWidth() / 2).reduced (2)); vuR.setBounds (vus.reduced (2));
    left.removeFromTop (8); loud.setBounds (left);
    auto top = r.removeFromTop (r.getHeight() * 55 / 100);
    spectrum.setBounds (top.removeFromTop (top.getHeight() * 58 / 100)); top.removeFromTop (6); gram.setBounds (top);
    r.removeFromTop (12);
    gonio.setBounds (r.removeFromLeft (r.getHeight() + 20)); r.removeFromLeft (16); scope.setBounds (r);
}

// =====================================================================================================================
//  recordings
// =====================================================================================================================
RecordingsPage::RecordingsPage (Ctx& c) : PageBase (c, Recordings), reveal ("show in finder", theme), open ("open", theme), trash ("move to the bin", theme), rescan ("look again", theme)
{
    formats.registerBasicFormats(); thumb.addChangeListener (this);
    for (auto* l : { &reveal, &open, &trash, &rescan }) addAndMakeVisible (l);
    rescan.onClick = [this] { scan(); };
    reveal.onClick = [this] { if (sel >= 0 && sel < (int) items.size()) items[(size_t) sel].file.revealToUser(); };
    open.onClick = [this] { if (sel >= 0 && sel < (int) items.size()) items[(size_t) sel].file.startAsProcess(); };
    trash.onClick = [this] { if (sel >= 0 && sel < (int) items.size()) { items[(size_t) sel].file.moveToTrash(); thumb.clear(); sel = -1; scan(); } };
}
void RecordingsPage::scan()
{
    std::set<juce::String> folders { juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("umleitung").getFullPathName() };
    std::function<void (const juce::ValueTree&)> walk = [&] (const juce::ValueTree& t) { if (t.hasType (ids::block) && typeFromId (t[ids::type]) == BlockType::Recorder) folders.insert (Engine::recordingFolder (t).getFullPathName()); for (auto c : t) walk (c); };
    walk (ctx.model.root);
    items.clear();
    for (auto& f : folders)
        for (auto& file : juce::File (f).findChildFiles (juce::File::findFiles, false, "*.wav;*.aiff;*.aif;*.flac"))
        {
            Item it; it.file = file; it.when = file.getLastModificationTime();
            if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (file) }) { it.rate = r->sampleRate; it.channels = (int) r->numChannels; it.bits = (int) r->bitsPerSample; it.seconds = r->sampleRate > 0 ? (double) r->lengthInSamples / r->sampleRate : 0; }
            items.push_back (it);
        }
    std::sort (items.begin(), items.end(), [] (auto& a, auto& b) { return a.when > b.when; });
    repaint();
}
void RecordingsPage::mouseDown (const juce::MouseEvent& e)
{
    const int i = (e.y - (kTop + 60) + scroll) / 24;
    if (e.y > kTop + 60 && e.y < getHeight() - 200 && i >= 0 && i < (int) items.size())
    {
        sel = i; thumb.setSource (new juce::FileInputSource (items[(size_t) i].file));
        if (e.getNumberOfClicks() > 1) items[(size_t) i].file.startAsProcess();
        repaint();
    }
}
void RecordingsPage::paint (juce::Graphics& g)
{
    paintHeader (g, "recordings", juce::String (items.size()) + juce::String (juce::CharPointer_UTF8 (" files in the recorders' folders  ·  double-click: open")));
    g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f));
    const int y0 = kTop + 44;
    g.drawText ("name", 24, y0, 300, 14, juce::Justification::centredLeft); g.drawText ("when", 470, y0, 160, 14, juce::Justification::centredLeft);
    g.drawText ("length", 640, y0, 80, 14, juce::Justification::centredLeft); g.drawText ("format", 730, y0, 200, 14, juce::Justification::centredLeft); g.drawText ("size", 940, y0, 80, 14, juce::Justification::centredLeft);
    g.saveState(); g.reduceClipRegion (0, kTop + 60, getWidth(), getHeight() - 200 - (kTop + 60));
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        auto r = juce::Rectangle<float> (24.0f, (float) (kTop + 60 + i * 24 - scroll), (float) getWidth() - 48.0f, 22.0f);
        if (i == sel) { g.setColour (theme.fill); g.fillPath (skewRect (r, 0.15f)); }
        g.setColour (theme.ink); g.setFont (mono (11.0f));
        g.drawText (it.file.getFileName(), (int) r.getX() + 8, (int) r.getY(), 430, 22, juce::Justification::centredLeft, true);
        g.drawText (it.when.formatted ("%Y-%m-%d %H:%M"), 470, (int) r.getY(), 160, 22, juce::Justification::centredLeft);
        g.drawText (timeText (it.seconds), 640, (int) r.getY(), 80, 22, juce::Justification::centredLeft);
        g.drawText (juce::String (it.channels) + " ch  " + juce::String (it.bits) + " bit  " + juce::String (it.rate / 1000.0, 1) + " khz", 730, (int) r.getY(), 200, 22, juce::Justification::centredLeft);
        g.drawText (juce::File::descriptionOfSizeInBytes (it.file.getSize()), 940, (int) r.getY(), 100, 22, juce::Justification::centredLeft);
    }
    g.restoreState();
    auto wave = juce::Rectangle<int> (24, getHeight() - 186, getWidth() - 48, 150);
    g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (wave);
    if (sel >= 0 && thumb.getTotalLength() > 0)
    {
        const int n = std::max (1, thumb.getNumChannels()); const int h = wave.getHeight() / n;
        for (int c = 0; c < n; ++c) { g.setColour (theme.fill.darker (0.2f)); thumb.drawChannel (g, wave.withY (wave.getY() + c * h).withHeight (h), 0.0, thumb.getTotalLength(), c, 1.0f); }
    }
    else { g.setColour (theme.ink.withAlpha (0.5f)); g.drawText ("click a recording to see it", wave, juce::Justification::centred); }
}
void RecordingsPage::resized()
{
    int x = 24; for (auto* l : { &rescan, &reveal, &open, &trash }) { l->setBounds (x, kTop + 10, l == &trash ? 140 : 120, 22); x += (l == &trash ? 140 : 120) + 8; }
}

// =====================================================================================================================
//  plug-ins
// =====================================================================================================================
PluginsPage::PluginsPage (Ctx& c) : PageBase (c, Plugins), juce::Thread ("plug-in scan"), scan ("scan for plug-ins", theme), forget ("forget the left-out list", theme)
{
    addAndMakeVisible (scan); addAndMakeVisible (forget);
    scan.onClick = [this] { if (! isThreadRunning()) startThread(); };
    forget.onClick = [this] { ctx.plugins.blacklistFile.deleteFile(); ctx.status ("the plug-ins left out will be looked at again on the next scan"); repaint(); };
}
void PluginsPage::run()
{
    found = ctx.plugins.scan ([this] (float p, const juce::String& f) { progress = p; const juce::ScopedLock l (lock); current = f; }, [this] { return threadShouldExit(); });
    done = true;
    if (ctx.plugins.scannerBroken) juce::MessageManager::callAsync ([this, e = ctx.plugins.lastError] { ctx.status ("the scan stopped: " + e); });
    else juce::MessageManager::callAsync ([this, n = found] { ctx.status (juce::String (n) + " new plug-ins found"); });
}
void PluginsPage::paint (juce::Graphics& g)
{
    const auto types = ctx.plugins.known.getTypes();
    paintHeader (g, "plug-ins", juce::String (types.size()) + juce::String (juce::CharPointer_UTF8 (" effects and instruments (vst3, audio units) to put in a session's path  ·  scans run in a separate process, so a plug-in that crashes is left out")));
    g.setFont (mono (11.0f)); g.setColour (theme.ink);
    if (isThreadRunning()) { const juce::ScopedLock l (lock); g.drawText ("scanning " + juce::String (juce::roundToInt (progress * 100)) + " %  " + juce::File (current).getFileName(), 420, kTop + 12, getWidth() - 440, 18, juce::Justification::centredLeft, true); }
    const auto black = ctx.plugins.blacklisted();
    if (! black.isEmpty()) { g.setColour (theme.ink.withAlpha (0.62f)); g.drawText ("left out (crashed or hung while scanned): " + juce::String (black.size()), 420, kTop + 12, 400, 18, juce::Justification::centredLeft); }
    juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (0, kTop + 44, getWidth(), getHeight() - kTop - 44);
    for (int i = 0; i < types.size(); ++i)
    {
        const int y = kTop + 48 + i * 22 - scroll;
        g.setColour (theme.ink); g.drawText (types[i].name, 24, y, 320, 20, juce::Justification::centredLeft, true);
        g.setColour (theme.ink.withAlpha (0.7f)); g.drawText (types[i].manufacturerName, 350, y, 220, 20, juce::Justification::centredLeft, true);
        g.drawText (types[i].pluginFormatName.toLowerCase() + (types[i].isInstrument ? "  instrument" : "  effect") + "  " + juce::String (types[i].numInputChannels) + " in / " + juce::String (types[i].numOutputChannels) + " out", 580, y, 400, 20, juce::Justification::centredLeft, true);
    }
}
void PluginsPage::resized() { scan.setBounds (24, kTop + 10, 170, 22); forget.setBounds (204, kTop + 10, 200, 22); }

// =====================================================================================================================
//  settings
// =====================================================================================================================
SettingsPage::SettingsPage (Ctx& c) : PageBase (c, Settings) { build(); }
void SettingsPage::build()
{
    rows.clear(); owned.clear();
    auto st = ctx.model.settings();
    auto addRow = [this] (std::unique_ptr<juce::Component> c, int h) { addAndMakeVisible (*c); rows.push_back ({ c.get(), h }); owned.push_back (std::move (c)); };
    juce::StringArray names { "automatic (the first output)" }, uids { "" };
    for (auto& d : CoreAudioHost::devices()) if (! d.isAggregate && ! d.isHidden) { names.add (d.name); uids.add (d.uid); }
    addRow (boundChoice (theme, st, ids::device, "clock", names, uids), 24);
    addRow (boundChoice (theme, st, "rate", "sample rate", { "the clock device's", "44.1 khz", "48 khz", "88.2 khz", "96 khz", "192 khz" }, { "0", "44100", "48000", "88200", "96000", "192000" }), 24);
    addRow (boundChoice (theme, st, "buffer", "buffer", { "64", "128", "256", "512", "1024", "2048" }, { "64", "128", "256", "512", "1024", "2048" }), 24);
    { auto t = std::make_unique<ValueToggle> (theme, "dark (faltung deep)", (bool) st["dark"]); t->onChange = [this, st] (bool v) mutable { st.setProperty ("dark", v, nullptr); if (onDark) onDark (v); }; addRow (std::move (t), 24); }
    { auto t = std::make_unique<ValueToggle> (theme, "in the menu bar", (bool) st.getProperty ("menubar", true)); t->onChange = [this, st] (bool v) mutable { st.setProperty ("menubar", v, nullptr); if (onMenuBar) onMenuBar (v); }; addRow (std::move (t), 24); }
    { auto l = std::make_unique<TextLink> ("allow capturing other apps' sound (system settings)...", theme); l->onClick = [] { juce::URL ("x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture").launchInDefaultBrowser(); }; addRow (std::move (l), 24); }
    { auto l = std::make_unique<TextLink> ("allow the microphone (system settings)...", theme); l->onClick = [] { juce::URL ("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone").launchInDefaultBrowser(); }; addRow (std::move (l), 24); }
    { auto l = std::make_unique<TextLink> ("remove the driver (virtual devices)...", theme); l->onClick = [this] { juce::String e; if (DriverControl::uninstall (e)) ctx.status ("the driver is removed"); else ctx.status (e); }; addRow (std::move (l), 24); }
    { auto l = std::make_unique<TextLink> ("show umleitung's files", theme); l->onClick = [] { juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/umleitung").revealToUser(); }; addRow (std::move (l), 24); }
    resized();
}
void SettingsPage::paint (juce::Graphics& g)
{
    paintHeader (g, "settings", "the clock and rate every session runs at, the look, permissions, the driver");
    g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f));
    juce::String info = "taps (capturing apps): " + juce::String (CoreAudioHost::tapsSupported() ? "yes" : "this mac is too old (macos 14.2 is needed)")
                      + juce::String (juce::CharPointer_UTF8 ("  ·  capture permission: ")) + (CoreAudioHost::capturePermission() == CoreAudioHost::Permission::Granted ? "allowed" : CoreAudioHost::capturePermission() == CoreAudioHost::Permission::Denied ? "not allowed" : "not asked yet")
                      + juce::String (juce::CharPointer_UTF8 ("  ·  driver: ")) + (DriverControl::installed() ? DriverControl::installedVersion() : juce::String ("not installed"))
                      + juce::String (juce::CharPointer_UTF8 ("  ·  umleitung ")) + juce::String (JUCE_APPLICATION_VERSION_STRING);
    g.drawText (info, 24, getHeight() - 26, getWidth() - 48, 16, juce::Justification::centredLeft, true);
}
void SettingsPage::resized()
{
    int y = kTop + 14;
    for (auto& [c, h] : rows)
    {
        int w = std::min (620, getWidth() - 48);
        if (auto* l = dynamic_cast<TextLink*> (c)) w = 16 + (int) (l->text.length() * 6.8f);   // (links as wide as their words)
        c->setBounds (24, y, w, h); y += h + 8;
    }
}
} // namespace um::ui
