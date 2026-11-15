#include "PluginEditor.h"

using namespace juce;
namespace ui = fal::ui;

namespace
{
    inline float frac (float x) { return x - std::floor (x); }
    const String sep (CharPointer_UTF8 ("  \xc2\xb7  "));
    const String loopGlyph (CharPointer_UTF8 ("\xe2\x86\xba"));
}

//==============================================================================
// themes
const CfTheme& CfTheme::forMode (int m)
{
    static const CfTheme t[3] = {
        // day: pale ground, Oval's blues and pinks
        { 0, Colour (0xfff6f7f4), Colour (0xff1d1c15), Colour (0xff2f7fd1), Colour (0xff1d1c15).withAlpha (0.18f), Colour (0xff2f7fd1), Colour (0xfff6c590),
          { Colour (0xff2f7fd1), Colour (0xffe9a0a8), Colour (0xff8ec3e6), Colour (0xff8f6bb8), Colour (0xffe06a2e) } },
        // dusk: warm pale ground, oranges, pinks and lilac
        { 1, Colour (0xfff4ece4), Colour (0xff2a1f24), Colour (0xffe06a2e), Colour (0xff2a1f24).withAlpha (0.18f), Colour (0xff6e80c8), Colour (0xffe06a2e),
          { Colour (0xffe06a2e), Colour (0xffe9a0a8), Colour (0xff8f6bb8), Colour (0xfff6c590), Colour (0xff6e80c8) } },
        // night: the series' dark ground, pale lines and a moon
        { 2, Colour (0xff1a242c), Colour (0xfff2f0ea), Colour (0xff5aaad8), Colour (0xfff2f0ea).withAlpha (0.16f), Colour (0xff5aaad8), Colour (0xfff0e6c0),
          { Colour (0xff8ec3e6), Colour (0xffb7a6e0), Colour (0xffe9a0a8), Colour (0xff5aaad8), Colour (0xfff0e6c0) } } };
    return t[jlimit (0, 2, m)];
}

Colour stageColour (int stage)
{
    // snd's pastel paper strips
    static const uint32 c[fp::numStages] = { 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xff8dbb7a, 0xfff6c590, 0xff97adc4, 0xff5e8f6a };
    return Colour (c[(size_t) jlimit (0, fp::numStages - 1, stage)]);
}

const char* stageName (int stage)
{
    static const char* n[fp::numStages] = { "smear", "pitch", "formant", "grains", "delay", "reverb", "comp" };
    return n[(size_t) jlimit (0, fp::numStages - 1, stage)];
}

//==============================================================================
void CfLook::setTheme (const CfTheme& t)
{
    setColour (PopupMenu::backgroundColourId, t.ground);
    setColour (PopupMenu::textColourId, t.ink);
    setColour (PopupMenu::headerTextColourId, t.ink.withAlpha (0.6f));
    setColour (PopupMenu::highlightedBackgroundColourId, t.fill);
    setColour (PopupMenu::highlightedTextColourId, ui::inkFor (t.fill));
    setColour (TooltipWindow::backgroundColourId, t.ink);
    setColour (TooltipWindow::textColourId, t.ground);
}

void CfLook::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (findColour (TooltipWindow::backgroundColourId));
    g.setColour (findColour (TooltipWindow::textColourId));
    g.setFont (ui::mono (11.0f));
    g.drawFittedText (text, Rectangle<int> (w, h).reduced (8, 4), Justification::centredLeft, 3);
}

Rectangle<int> CfLook::getTooltipBounds (const String& text, Point<int> pos, Rectangle<int> parent)
{
    const int w = jmin (360, GlyphArrangement::getStringWidthInt (ui::mono (11.0f), text) + 18), h = 22;
    return Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y + 16, w, h).constrainedWithin (parent);
}

//==============================================================================
DriftBar::DriftBar (RangedAudioParameter& p, ui::Theme t)
    : param (p), theme (t), attach (p, [this] (float v) { value01 = param.convertTo0to1 (v); repaint(); })
{
    attach.sendInitialUpdate();
    setTooltip ("drift: how far the random voltage moves this control (drag; double-click: none)");
    setMouseCursor (MouseCursor::LeftRightResizeCursor);
}

void DriftBar::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f, 0.0f);
    const float cy = b.getCentreY(), cx = b.getCentreX();
    // a wavy hairline (the random voltage) with a centre notch
    g.setColour (theme.ink.withAlpha (0.3f));
    Path wave;
    for (int i = 0; i <= 24; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / 24.0f;
        const float y = cy + 1.5f * std::sin ((float) i * 0.9f);
        if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.strokePath (wave, PathStrokeType (0.8f));
    g.fillRect (cx - 0.5f, cy - 4.0f, 1.0f, 8.0f);
    const float x = b.getX() + b.getWidth() * value01;
    if (std::abs (value01 - 0.5f) > 0.004f)
    {
        g.setColour (theme.fill);
        g.fillPath (ui::skewRect ({ jmin (cx, x), cy - 3.0f, std::abs (x - cx), 6.0f }, 0.4f));
    }
    g.setColour (theme.edge == Colours::white ? theme.ink : theme.edge);
    g.fillRect (x - 0.75f, cy - 5.0f, 1.5f, 10.0f);
}

void DriftBar::mouseDown (const MouseEvent&) { start = value01; attach.beginGesture(); }
void DriftBar::mouseDrag (const MouseEvent& e)
{
    const float sens = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const float d = ((float) e.getDistanceFromDragStartX() - 0.6f * (float) e.getDistanceFromDragStartY()) / jmax (20.0f, (float) getWidth()) * sens;
    attach.setValueAsPartOfGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, start + d)));
}
void DriftBar::mouseUp (const MouseEvent&) { attach.endGesture(); }
void DriftBar::mouseDoubleClick (const MouseEvent&) { attach.setValueAsCompleteGesture (param.convertFrom0to1 (0.5f)); }

//==============================================================================
ShapeEditor::ShapeEditor (CloudfinProcessor& p) : proc (p)
{
    proc.getGrainShape (table.data());
    seenVersion = proc.getGrainShapeVersion();
    const char* names[] = { "saw", "bell", "tri", "rev", "pulse", "smooth" };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = presets.add (new ui::TextLink (names[i], th.ui()));
        b->onClick = [this, i] { applyPreset (i); };
        addAndMakeVisible (b);
    }
}

void ShapeEditor::setTheme (const CfTheme& t)
{
    th = t;
    for (auto* b : presets) { b->theme = t.ui(); b->repaint(); }
    repaint();
}

void ShapeEditor::resized()
{
    auto r = getLocalBounds();
    auto rowR = r.removeFromBottom (18);
    plot = r.reduced (2, 4).toFloat().withTrimmedBottom (4.0f);
    const int bw = rowR.getWidth() / 6;
    for (auto* b : presets) b->setBounds (rowR.removeFromLeft (bw));
}

void ShapeEditor::applyPreset (int which)
{
    if (which < fp::GrainShifter::numShapes)
        fp::GrainShifter::makeShape (which, table.data());
    else
        for (int pass = 0; pass < 4; ++pass)
        {
            auto src = table;
            for (int i = 1; i + 1 < (int) table.size(); ++i)
                table[(size_t) i] = 0.25f * src[(size_t) i - 1] + 0.5f * src[(size_t) i] + 0.25f * src[(size_t) i + 1];
        }
    push();
}

void ShapeEditor::push()
{
    proc.setGrainShape (table.data());
    seenVersion = proc.getGrainShapeVersion();
    repaint();
}

void ShapeEditor::drawAt (Point<float> p)
{
    constexpr int N = CloudfinProcessor::kShapeN;
    auto idx = [&] (Point<float> q) { return jlimit (0, N - 1, roundToInt ((q.x - plot.getX()) / plot.getWidth() * (float) (N - 1))); };
    auto val = [&] (Point<float> q) { return jlimit (0.0f, 1.0f, 1.0f - (q.y - plot.getY()) / plot.getHeight()); };
    const int i0 = idx (last), i1 = idx (p);
    const float v0 = val (last), v1 = val (p);
    if (i0 == i1) { table[(size_t) i1] = v1; return; }
    const int stp = i1 > i0 ? 1 : -1;
    for (int i = i0; i != i1 + stp; i += stp)
        table[(size_t) i] = v0 + (v1 - v0) * (float) (i - i0) / (float) (i1 - i0);
}

void ShapeEditor::mouseDown (const MouseEvent& e)
{
    if (! plot.expanded (6.0f).contains (e.position)) return;
    drawing = true;
    last = e.position;
    drawAt (e.position);
    push();
}

void ShapeEditor::mouseDrag (const MouseEvent& e)
{
    if (! drawing) return;
    drawAt (e.position);
    last = e.position;
    push();
}

void ShapeEditor::mouseUp (const MouseEvent&) { drawing = false; }

void ShapeEditor::poll (float tt, float grainSeconds)
{
    const float dt = jlimit (0.0f, 0.1f, tt - time);
    time = tt;
    if (! drawing && proc.getGrainShapeVersion() != seenVersion)
    {
        proc.getGrainShape (table.data());
        seenVersion = proc.getGrainShapeVersion();
    }
    riderPos = frac (riderPos + dt / jlimit (0.7f, 4.0f, grainSeconds * 6.0f));
    repaint();
}

void ShapeEditor::paint (Graphics& g)
{
    constexpr int N = CloudfinProcessor::kShapeN;
    const auto p = plot;
    // the frame: a hairline floor and two ticks, like the faltung step grid
    g.setColour (th.rule);
    g.fillRect (p.getX(), p.getBottom(), p.getWidth(), 1.0f);
    g.fillRect (p.getX(), p.getY(), 1.0f, p.getHeight());
    g.fillRect (p.getRight() - 1.0f, p.getY(), 1.0f, p.getHeight());
    // the envelope as Oval lines: the shape, then two fainter echoes under it
    auto curve = [&] (float scale, float dy)
    {
        Path c;
        for (int i = 0; i < N; ++i)
        {
            const float x = p.getX() + p.getWidth() * (float) i / (float) (N - 1);
            const float y = p.getBottom() - p.getHeight() * table[(size_t) i] * scale + dy;
            if (i == 0) c.startNewSubPath (x, y); else c.lineTo (x, y);
        }
        return c;
    };
    g.setColour (th.lines[1].withAlpha (0.8f));
    g.strokePath (curve (0.82f, 0.0f), PathStrokeType (1.5f));
    g.setColour (th.lines[2].withAlpha (0.8f));
    g.strokePath (curve (0.64f, 0.0f), PathStrokeType (1.0f));
    g.setColour (th.fill);
    g.strokePath (curve (1.0f, 0.0f), PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    // a grain riding along it
    const int ri = jlimit (0, N - 1, (int) (riderPos * (float) (N - 1)));
    const float rx = p.getX() + p.getWidth() * riderPos, ry = p.getBottom() - p.getHeight() * table[(size_t) ri];
    g.setColour (th.ink);
    g.fillEllipse (rx - 3.0f, ry - 3.0f, 6.0f, 6.0f);
    g.drawLine (rx, ry, rx, p.getBottom(), 1.0f);
}

//==============================================================================
ChainStrip::ChainStrip (CloudfinProcessor& p) : proc (p)
{
    order = proc.getOrder();
    for (int i = 0; i < fp::numStages; ++i) chipX[order[(size_t) i]] = slotX (i);
    setMouseCursor (MouseCursor::DraggingHandCursor);
}

int ChainStrip::stageAt (Point<float> pos) const
{
    for (int i = 0; i < fp::numStages; ++i)
    {
        const int st = order[(size_t) i];
        if (Rectangle<float> (chipX[st], 0.0f, chipW, (float) getHeight()).contains (pos)) return st;
    }
    return -1;
}

void ChainStrip::mouseDown (const MouseEvent& e)
{
    dragStage = stageAt (e.position);
    if (dragStage >= 0) { grabOffset = e.position.x - chipX[dragStage]; dragX = chipX[dragStage]; }
}

void ChainStrip::mouseDrag (const MouseEvent& e)
{
    if (dragStage < 0) return;
    dragX = jlimit (slotX (0), slotX (fp::numStages - 1), e.position.x - grabOffset);
    chipX[dragStage] = dragX;
    const int want = jlimit (0, fp::numStages - 1, roundToInt ((dragX - endW) / (chipW + gap)));
    int cur = 0;
    for (int i = 0; i < fp::numStages; ++i) if (order[(size_t) i] == dragStage) cur = i;
    if (want != cur)
    {
        auto o = order;
        const int st = o[(size_t) cur];
        if (want > cur) for (int i = cur; i < want; ++i) o[(size_t) i] = o[(size_t) i + 1];
        else            for (int i = cur; i > want; --i) o[(size_t) i] = o[(size_t) i - 1];
        o[(size_t) want] = st;
        order = o;
    }
    repaint();
}

void ChainStrip::mouseUp (const MouseEvent&)
{
    if (dragStage >= 0) { dragStage = -1; proc.setOrder (order); }   // applied with a 40 ms dip, so it never clicks
}

void ChainStrip::mouseDoubleClick (const MouseEvent&)
{
    order = fp::defaultOrder();
    proc.setOrder (order);
}

void ChainStrip::shuffle()
{
    const auto cur = proc.getOrder();
    auto o = cur;
    for (int tries = 0; tries < 8 && o == cur; ++tries)
        for (int i = fp::numStages - 1; i > 0; --i)
            std::swap (o[(size_t) i], o[(size_t) rnd.nextInt (i + 1)]);
    order = o;
    proc.setOrder (o);
}

void ChainStrip::poll (float dt)
{
    if (dragStage < 0) order = proc.getOrder();
    const float k = jmin (1.0f, dt * 12.0f);
    bool moved = false;
    for (int i = 0; i < fp::numStages; ++i)
    {
        const int st = order[(size_t) i];
        if (st != dragStage)
        {
            const float d = (slotX (i) - chipX[st]) * k;
            moved |= std::abs (d) > 0.05f;
            chipX[st] += d;
        }
    }
    if (moved) repaint();
}

void ChainStrip::paint (Graphics& g)
{
    if (theme == nullptr) return;
    const float h = (float) getHeight(), depth = 13.0f, face = h - depth - 4.0f;
    const float y = depth + 2.0f;
    const Colour ink = theme->ink;
    // the signal path: in → the strips → out, and the loop back under them
    g.setColour (ink.withAlpha (0.55f));
    g.fillRect (0.0f, y + face * 0.5f, (float) getWidth(), 1.0f);
    g.setFont (ui::mono (10.0f));
    g.drawText ("in", Rectangle<float> (0.0f, y, endW - 8.0f, face), Justification::centredLeft);
    g.drawText ("out", Rectangle<float> ((float) getWidth() - endW + 8.0f, y, endW - 8.0f, face), Justification::centredRight);

    auto draw = [&] (int st, bool lifted)
    {
        const float x = chipX[st], yy = y - (lifted ? 5.0f : 0.0f);
        const Colour c = stageColour (st);
        // the face (a parallelogram), and the paper turning back along its top: snd's fold
        Path fold;
        fold.startNewSubPath (x + 8.0f, yy); fold.lineTo (x + chipW, yy); fold.lineTo (x + chipW + depth * 0.62f, yy - depth * 0.78f);
        fold.lineTo (x + 8.0f + depth * 0.62f, yy - depth * 0.78f); fold.closeSubPath();
        g.setColour (c.darker (0.28f));
        g.fillPath (fold);
        g.setColour (c.interpolatedWith (Colours::white, lifted ? 0.25f : 0.0f));
        g.fillPath (ui::skewRect ({ x, yy, chipW, face }, 0.3f));
        if (lifted) { g.setColour (Colours::white); g.strokePath (ui::skewRect ({ x, yy, chipW, face }, 0.3f), PathStrokeType (1.5f)); }
        g.setColour (Colour (0xff1e2a20));
        g.setFont (ui::sans (13.0f));
        const int pos = [&] { for (int i = 0; i < fp::numStages; ++i) if (order[(size_t) i] == st) return i + 1; return 0; }();
        juce::ignoreUnused (pos);
        g.drawText (stageName (st), Rectangle<float> (x + 6.0f, yy, chipW - 8.0f, face), Justification::centred);
    };
    for (int st = 0; st < fp::numStages; ++st) if (st != dragStage) draw (st, false);
    if (dragStage >= 0) draw (dragStage, true);
}

//==============================================================================
void CfPresets::poll()
{
    const int pr = proc.currentPreset.load();
    if (pr != shownProgram && ! user) { shownProgram = pr; shownName = proc.getProgramName (jmax (0, pr)); repaint(); }
    else if (pr != shownProgram && user) { user = false; shownProgram = -2; poll(); }
}

void CfPresets::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (theme.ink);
    g.setFont (ui::mono (11.0f));
    g.drawText ("<", b.withWidth (18.0f), Justification::centredLeft);
    g.drawText (">", b.withTrimmedLeft (b.getWidth() - 18.0f), Justification::centredRight);
    g.setFont (ui::mono (11.0f, true));
    g.drawText (shownName, b.reduced (20.0f, 0.0f), Justification::centred, true);
}

void CfPresets::mouseDown (const MouseEvent& e)
{
    if (e.position.x < 22.0f) step (-1);
    else if (e.position.x > (float) getWidth() - 22.0f) step (1);
    else menu();
}

void CfPresets::step (int d)
{
    const int n = proc.getNumPrograms();
    user = false;
    proc.setCurrentProgram ((jmax (0, proc.getCurrentProgram()) + d + n) % n);
    shownProgram = -2; poll();
}

void CfPresets::menu()
{
    PopupMenu m;
    String group;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
    {
        if (proc.getProgramGroup (i) != group) { group = proc.getProgramGroup (i); m.addSectionHeader (group); }
        m.addItem (i + 1, proc.getProgramName (i), true, ! user && i == proc.getCurrentProgram());
    }
    const auto files = proc.userPresets();
    if (! files.isEmpty()) m.addSectionHeader ("yours");
    for (int i = 0; i < files.size(); ++i) m.addItem (1001 + i, files[i].getFileNameWithoutExtension());
    m.addSeparator();
    m.addItem (2000, "save this as a preset...");
    m.addItem (2001, "show the presets folder");
    SafePointer<CfPresets> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe, files] (int id)
    {
        if (safe == nullptr || id == 0) return;
        auto& self = *safe;
        if (id <= 1000) { self.user = false; self.proc.setCurrentProgram (id - 1); self.shownProgram = -2; self.poll(); }
        else if (id < 2000)
        {
            const auto f = files[id - 1001];
            if (self.proc.loadUserPreset (f)) { self.user = true; self.shownProgram = self.proc.currentPreset.load(); self.shownName = f.getFileNameWithoutExtension(); self.repaint(); }
        }
        else if (id == 2000)
        {
            self.proc.userPresetFolder().createDirectory();
            self.chooser = std::make_unique<FileChooser> ("Save this as a preset",
                self.proc.userPresetFolder().getChildFile ("my preset" + self.proc.presetExtension()), "*" + self.proc.presetExtension());
            self.chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                                       [safe] (const FileChooser& fc)
                                       {
                                           if (safe == nullptr) return;
                                           const auto f = fc.getResult();
                                           if (f != File() && safe->proc.saveUserPreset (f))
                                           { safe->user = true; safe->shownProgram = safe->proc.currentPreset.load(); safe->shownName = f.getFileNameWithoutExtension(); safe->repaint(); }
                                       });
        }
        else { self.proc.userPresetFolder().createDirectory(); self.proc.userPresetFolder().revealToUser(); }
    });
}

//==============================================================================
CloudfinEditor::CloudfinEditor (CloudfinProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    th = CfTheme::forMode (proc.themeMode.load());
    const auto u = th.ui();
    auto P = [this] (const char* id) -> RangedAudioParameter& { return *dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter (id)); };

    sgS = section ("spectral smear", fp::stGate);
    for (auto [id, l] : { std::pair { ids::sgCutoff, "cutoff" }, { ids::sgAttack, "attack" }, { ids::sgSmear, "smear" }, { ids::sgKnee, "knee" },
                          { ids::sgBlur, "blur" }, { ids::sgScatter, "scatter" }, { ids::sgTilt, "tilt" }, { ids::sgMix, "mix" } })
        sgS->rows.push_back (row (id, l));
    psS = section ("pitch", fp::stPitch);
    for (auto [id, l] : { std::pair { ids::psShift, "shift" }, { ids::psGrain, "grain" }, { ids::psFeedback, "feedback" }, { ids::psMix, "mix" } })
        psS->rows.push_back (row (id, l));
    fbS = section ("feedback", -1);
    fbS->rows.push_back (row (ids::masterFb, "amount"));
    grS = section ("grains", fp::stGrains);
    for (auto [id, l] : { std::pair { ids::grSize, "size" }, { ids::grDensity, "density" }, { ids::grPosition, "position" }, { ids::grSpray, "spray" },
                          { ids::grWidth, "rand pan" }, { ids::grReverse, "reverse" }, { ids::grOctave, "octave" }, { ids::grFade, "freeze fade" }, { ids::grMix, "mix" } })
        grS->rows.push_back (row (id, l));
    shS = section ("grain shape", -1);
    drS = section ("drift", -1);
    drS->rows.push_back (row (ids::modRate, "rate"));
    drS->rows.push_back (row (ids::modSlew, "slew"));
    dlS = section ("ji delay", fp::stDelay);
    for (auto [id, l] : { std::pair { ids::dlTime, "time" }, { ids::dlFeedback, "feedback" }, { ids::dlTone, "tone" }, { ids::dlMix, "mix" } })
        dlS->rows.push_back (row (id, l));
    rvS = section ("reverb", fp::stReverb);
    for (auto [id, l] : { std::pair { ids::rvDecay, "decay" }, { ids::rvTone, "tone" }, { ids::rvMix, "mix" } })
        rvS->rows.push_back (row (id, l));
    cpS = section ("compander", fp::stComp);
    for (auto [id, l] : { std::pair { ids::cpThresh, "threshold" }, { ids::cpRatio, "ratio" }, { ids::cpBelow, "below" }, { ids::cpAttack, "attack" },
                          { ids::cpRelease, "release" }, { ids::cpMakeup, "makeup" }, { ids::cpMix, "mix" } })
        cpS->rows.push_back (row (id, l));
    fmS = section ("formant", fp::stFormant);
    for (auto [id, l] : { std::pair { ids::fmVowel, "vowel" }, { ids::fmShift, "shift" }, { ids::fmRes, "resonance" }, { ids::fmMix, "mix" } })
        fmS->rows.push_back (row (id, l));

    scaleBox = std::make_unique<ui::ChoiceBox> (P (ids::psScale), u, "scale");
    scaleBox->labelW = 78;
    freeze = std::make_unique<ui::ToggleBox> (P (ids::grFreeze), u, "freeze");
    randOct = std::make_unique<ui::ToggleBox> (P (ids::grRandOct), u, "random octave");
    specFreeze = std::make_unique<ui::ToggleBox> (P (ids::sgFreeze), u, "freeze");
    gravity = std::make_unique<ui::ToggleBox> (P (ids::gravity), u, "gravity");
    gravity->setTooltip ("the clouds fall into the water; the water sloshes in the sound");
    hydroLevel = std::make_unique<ui::ParamSlider> (P (ids::hydroLevel), u, "hydrophone");
    hydroLevel->setLabelWidth (84);
    hydroLevel->setTooltip ("a hydrophone recording of moving water (double-click: none)");
    hydroInto = std::make_unique<ui::ChoiceBox> (P (ids::hydroInto), u, "into");
    hydroInto->labelW = 34;
    for (Component* c : std::initializer_list<Component*> { scaleBox.get(), freeze.get(), randOct.get(), specFreeze.get(), gravity.get(), hydroLevel.get(),
                                                           hydroInto.get(), &shapeEditor, &chain, &presets, &shuffleBtn, &randomBtn })
        content.addAndMakeVisible (c);

    const char* modeNames[3] = { "day", "dusk", "night" };
    for (int i = 0; i < 3; ++i)
    {
        modeLinks[(size_t) i] = std::make_unique<ui::TextLink> (modeNames[i], u);
        modeLinks[(size_t) i]->onClick = [this, i] { setThemeMode (i); };
        content.addAndMakeVisible (*modeLinks[(size_t) i]);
    }
    shuffleBtn.onClick = [this] { chain.shuffle(); };
    randomBtn.onClick = [this] { proc.randomize(); };
    randomBtn.setTooltip ("a random patch: every control somewhere new");
    shuffleBtn.setTooltip ("shuffle the order of the effects");
    chain.setTooltip ("the seven effects in order: drag a strip to move it, double-click for the default order");

    buildClouds();
    waveH.assign (124, 0.0f);
    waveV.assign (124, 0.0f);
    layoutContent();
    setThemeMode (th.mode);

    setResizable (true, true);
    setResizeLimits (designW / 2, designH / 2, (int) (designW * 1.6), (int) (designH * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setSize ((int) (designW * 0.85), (int) (designH * 0.85));

    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    timerCallback();
    startTimerHz (30);
}

CloudfinEditor::~CloudfinEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

CloudfinEditor::Row* CloudfinEditor::row (const char* id, const String& label)
{
    auto r = std::make_unique<Row>();
    auto& prm = *dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter (id));
    r->slider = std::make_unique<ui::ParamSlider> (prm, th.ui(), label);
    r->slider->setLabelWidth (78);
    content.addAndMakeVisible (*r->slider);
    r->modIndex = modIndexOf (id);
    if (r->modIndex >= 0)
        if (auto* a = dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter (ids::att (id))))
        {
            r->drift = std::make_unique<DriftBar> (*a, th.ui());
            content.addAndMakeVisible (*r->drift);
        }
    rows.push_back (std::move (r));
    return rows.back().get();
}

CloudfinEditor::Section* CloudfinEditor::section (const String& title, int stage)
{
    auto s = std::make_unique<Section>();
    s->title = title; s->stage = stage;
    sections.push_back (std::move (s));
    return sections.back().get();
}

void CloudfinEditor::setThemeMode (int mode)
{
    proc.themeMode.store (jlimit (0, 2, mode));
    th = CfTheme::forMode (mode);
    look.setTheme (th);
    const auto u = th.ui();
    for (auto& r : rows) { r->slider->setTheme (u); if (r->drift) r->drift->setTheme (u); }
    scaleBox->setTheme (u); hydroInto->setTheme (u); hydroLevel->setTheme (u);
    for (auto* tb : { freeze.get(), randOct.get(), specFreeze.get(), gravity.get() }) tb->setTheme (u);
    for (auto* l : { &shuffleBtn, &randomBtn }) { l->theme = u; l->repaint(); }
    for (int i = 0; i < 3; ++i) { modeLinks[(size_t) i]->theme = u; modeLinks[(size_t) i]->setLit (i == mode); }
    presets.theme = u; presets.repaint();
    shapeEditor.setTheme (th);
    chain.theme = &th;
    chain.repaint();
    content.repaint();
}

//==============================================================================
// layout: the picture band on top (title, presets, the strips), a control line, then three columns
void CloudfinEditor::layoutContent()
{
    constexpr int colW = 368, gapX = 34, x0 = 36, top = 344, rowH = 19, headH = 28, secGap = 12;
    const int cols[3] = { x0, x0 + colW + gapX, x0 + 2 * (colW + gapX) };

    presets.setBounds (designW - 36 - 250, 24, 250, 20);
    for (int i = 0; i < 3; ++i) modeLinks[(size_t) i]->setBounds (designW - 36 - 250 + 64 + i * 62, 50, 58, 18);
    {
        const int w = (int) ChainStrip::width();
        chain.setBounds (designW / 2 - w / 2, 246, w, 40);
    }
    // the control line under the picture
    const int ly = skyH + 10;
    hydroLevel->setBounds (x0, ly, 300, 20);
    hydroInto->setBounds (x0 + 316, ly, 120, 20);
    gravity->setBounds (cols[1] + 150, ly, 100, 20);
    randomBtn.setBounds (designW - 36 - 206, ly, 100, 20);
    shuffleBtn.setBounds (designW - 36 - 96, ly, 96, 20);

    auto place = [&] (Section* s, int x, int y, int extraRows = 0, int extraH = 0)
    {
        int yy = y + headH;
        for (auto* r : s->rows)
        {
            r->slider->setBounds (x, yy, colW - (r->drift ? 52 : 0), rowH - 1);
            if (r->drift) r->drift->setBounds (x + colW - 46, yy, 46, rowH - 1);
            yy += rowH;
        }
        yy += extraRows * rowH + extraH;
        s->r = { x, y, colW, yy - y };
        return yy + secGap;
    };
    int y = top;
    // column 1: spectral smear, pitch, feedback
    y = place (sgS, cols[0], y);
    specFreeze->setBounds (cols[0] + colW - 80, sgS->r.getY() + 2, 80, 18);
    y = place (psS, cols[0], y, 1);
    scaleBox->setBounds (cols[0], psS->r.getBottom() - rowH, colW - 52, rowH - 1);
    y = place (fbS, cols[0], y);
    // column 2: grains, the grain shape, drift
    y = place (grS, cols[1], top, 1);
    freeze->setBounds (cols[1], grS->r.getBottom() - rowH, 90, rowH - 1);
    randOct->setBounds (cols[1] + 120, grS->r.getBottom() - rowH, 150, rowH - 1);
    y = place (shS, cols[1], y, 0, 110);
    shapeEditor.setBounds (cols[1], shS->r.getY() + headH, colW, 108);
    y = place (drS, cols[1], y);
    // column 3: ji delay, reverb, compander, formant
    y = place (dlS, cols[2], top);
    y = place (rvS, cols[2], y);
    y = place (cpS, cols[2], y);
    y = place (fmS, cols[2], y);
}

void CloudfinEditor::paint (Graphics& g) { g.fillAll (th.ground); }

void CloudfinEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content.setBounds (0, 0, designW, designH);
    content.setTransform (AffineTransform::scale (s));
}

//==============================================================================
// the clouds: a few heaps of puffs per side, from far (small, near the horizon) to near
void CloudfinEditor::buildClouds()
{
    clouds.clear();
    Random rnd (11);
    auto make = [&] (float dx, float baseY, float w)
    {
        Cloud c;
        c.dx = dx; c.baseY = baseY; c.w = w;
        const int n = 5 + rnd.nextInt (3);
        for (int i = 0; i < n; ++i)
        {
            const float u = (float) i / (float) (n - 1) * 2.0f - 1.0f;
            const float hump = 1.0f - u * u;
            const float r = w * (0.10f + 0.16f * hump) * (0.8f + 0.4f * rnd.nextFloat());
            c.puffs.push_back ({ u * w * 0.40f, -r * (0.35f + 0.4f * hump * rnd.nextFloat()), r });
        }
        if (dx == 0.0f) { const auto half = c.puffs; for (auto& p : half) c.puffs.push_back ({ -p.x, p.y, p.r }); }
        clouds.push_back (c);
    };
    make (520.0f, 204.0f, 110.0f);
    make (300.0f, 200.0f, 150.0f);
    make (0.0f,   150.0f, 190.0f);
    make (330.0f, 134.0f, 140.0f);
    make (170.0f, 176.0f, 130.0f);
}

float CloudfinEditor::surfaceY (float x) const noexcept
{
    const float u = jlimit (0.0f, 1.0f, x / ((float) designW * 0.5f)) * (float) (waveH.size() - 1);
    const int i = (int) u;
    const float f = u - (float) i;
    const float h = waveH[(size_t) i] * (1.0f - f) + waveH[(size_t) jmin ((int) waveH.size() - 1, i + 1)] * f;
    return horizonY - waterRise + h;
}

void CloudfinEditor::drawSkyHalf (Graphics& g)
{
    const float cx = designW * 0.5f, w = designW * 0.5f;
    const float lv = levelSm, drift = modValue (ids::modRate);
    const float hy = horizonY - waterRise;

    // the sun (dusk: on the horizon, day: high, night: a full moon with a ring); half of it is on this side
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (0, 0, (int) cx + 2, (int) hy);
        const float r = th.mode == 1 ? 46.0f : 26.0f, sy = th.mode == 1 ? hy - 34.0f : (th.mode == 0 ? 62.0f : 70.0f);
        g.setColour (th.sun.withAlpha (0.9f));
        g.fillEllipse (cx - r, sy - r, 2 * r, 2 * r);
        if (th.mode == 2) { g.setColour (th.sun.withAlpha (0.35f)); g.drawEllipse (cx - r - 9.0f, sy - r - 9.0f, 2 * r + 18.0f, 2 * r + 18.0f, 1.0f); }
    }
    // far lines in the sky: Oval's arcs, very faint
    for (int k = 0; k < 6; ++k)
    {
        Path a;
        const float y0 = 30.0f + (float) k * 12.0f;
        a.startNewSubPath (cx * 0.42f, y0 + 26.0f);
        a.quadraticTo (cx * 0.7f, y0 - 10.0f + 4.0f * std::sin (t * 0.2f + (float) k), cx + 10.0f, y0 + 8.0f);
        g.setColour (th.lines[(size_t) (k % 5)].withAlpha (0.18f));
        g.strokePath (a, PathStrokeType (1.0f));
    }
    // the clouds, far to near. Each puff is a disc of the ground colour outlined by three tight
    // parallel lines (Oval's way of drawing), cut flat along the cloud's base; later puffs cover the
    // lines of earlier ones, so a heap reads as one outline.
    for (size_t ci = 0; ci < clouds.size(); ++ci)
    {
        const auto& c = clouds[ci];
        const float sway = 6.0f * std::sin (t * (0.07f + 0.02f * (float) ci) + (float) ci);
        for (size_t pi = 0; pi < c.puffs.size(); ++pi)
        {
            const auto& p = c.puffs[pi];
            const float breathe = 1.0f + (0.03f + 0.05f * drift) * std::sin (t * 0.5f + (float) pi * 1.7f + (float) ci);
            const float x = cx - c.dx + p.x + sway, y = c.baseY + p.y + p.fall, r = p.r * breathe;
            if (x + r < 0.0f || x - r > cx + 2.0f) continue;
            const float base = jmin (c.baseY + p.fall + 2.0f, surfaceY (jlimit (0.0f, cx, x)) + 1.0f);
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (Rectangle<int> (0, 0, (int) cx + 2, (int) jmax (0.0f, base)));
            g.setColour (th.ground);
            g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
            for (int k = 0; k < 3; ++k)
            {
                const float rr = r - (float) k * 4.0f;
                g.setColour (th.lines[(size_t) ((k + (int) pi + 2 * (int) ci) % 5)]);
                g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, k == 0 ? 2.0f : 1.1f);
            }
        }
        // the flat base: two lines under the heap
        if (c.puffs.front().fall < 1.0f)
        {
            float x0 = 1.0e6f, x1 = -1.0e6f;
            for (auto& p : c.puffs) { x0 = jmin (x0, p.x - p.r * 0.6f); x1 = jmax (x1, p.x + p.r * 0.6f); }
            const float bx = cx - c.dx + sway;
            for (int k = 0; k < 2; ++k)
            {
                g.setColour (th.lines[(size_t) ((ci + (size_t) k) % 5)].withAlpha (0.9f));
                g.fillRect (jmax (0.0f, bx + x0 + (float) k * 10.0f), c.baseY + 1.0f + (float) k * 4.0f, jmin (cx - (bx + x0), x1 - x0 - (float) k * 20.0f), 1.2f);
            }
        }
    }
    // the water: a bundle of horizontal lines, swelling with the sound
    for (int k = 0; k < 9; ++k)
    {
        Path wl;
        const float y0 = hy + 6.0f + (float) k * (k < 3 ? 5.0f : 8.0f + (float) k);
        for (int i = 0; i <= 62; ++i)
        {
            const float x = w * (float) i / 62.0f;
            const float y = y0 + (surfaceY (x) - hy) * (1.0f - (float) k * 0.08f)
                          + (1.0f + 3.5f * lv) * std::sin (x * (0.018f + 0.003f * (float) k) + t * (0.6f + 0.1f * (float) k) + (float) k);
            if (i == 0) wl.startNewSubPath (x, y); else wl.lineTo (x, y);
        }
        const float wdt = k == 0 ? 3.0f : (k % 3 == 0 ? 2.0f : 1.0f);
        g.setColour ((k == 0 ? th.water : th.lines[(size_t) (k % 5)]).withAlpha (k == 0 ? 1.0f : 0.8f - 0.05f * (float) k));
        g.strokePath (wl, PathStrokeType (wdt));
    }
    // the sun's path on the water: short dashes under it
    g.setColour (th.sun.withAlpha (0.7f));
    for (int k = 0; k < 6; ++k)
    {
        const float y = hy + 14.0f + (float) k * 9.0f, len = 30.0f - (float) k * 3.0f + 6.0f * std::sin (t * 1.3f + (float) k);
        g.fillRect (cx - len, y, len, 2.0f);
    }
    // Oval's two pastel blocks
    g.setColour (th.lines[3].withAlpha (0.45f));
    g.fillPath (ui::skewRect ({ 96.0f, hy - 64.0f, 64.0f, 30.0f }, 0.4f));
    g.setColour (th.lines[1].withAlpha (0.4f));
    g.fillRect (cx - 220.0f, 42.0f, 40.0f, 22.0f);
}

void CloudfinEditor::drawSky (Graphics& g)
{
    const float cx = designW * 0.5f;
    g.setColour (th.ground);
    g.fillRect (0, 0, designW, skyH);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (0, 0, (int) cx, skyH);
        drawSkyHalf (g);
    }
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion ((int) cx, 0, designW - (int) cx, skyH);
        g.addTransform (AffineTransform::scale (-1.0f, 1.0f, cx, 0.0f));
        drawSkyHalf (g);
    }
}

void CloudfinEditor::drawSection (Graphics& g, const Section& s)
{
    const auto r = s.r.toFloat();
    // header: a strip swatch for an effect (its place in the chain), the name, a rule
    float tx = r.getX();
    if (s.stage >= 0)
    {
        g.setColour (stageColour (s.stage));
        g.fillPath (ui::skewRect ({ r.getX(), r.getY() + 5.0f, 22.0f, 11.0f }, 0.4f));
        tx += 30.0f;
        const auto o = proc.getOrder();
        int pos = 0; for (int i = 0; i < fp::numStages; ++i) if (o[(size_t) i] == s.stage) pos = i + 1;
        g.setColour (th.ink.withAlpha (0.55f));
        g.setFont (ui::mono (10.0f));
        g.drawText (String (pos) + " / " + String (fp::numStages), Rectangle<float> (r.getRight() - 140.0f, r.getY() + 1.0f, 50.0f, 18.0f), Justification::centredRight);
    }
    g.setColour (th.ink);
    g.setFont (ui::mono (12.0f, true));
    g.drawText (s.title, Rectangle<float> (tx, r.getY(), 200.0f, 20.0f), Justification::centredLeft);
    g.setColour (th.rule);
    g.fillRect (r.getX(), r.getY() + 22.0f, r.getWidth(), 1.0f);

    // live readouts in the header
    if (&s == cpS)
    {
        // the compander's gain, as a bar from the middle: left = turned down, right = up
        const float gdb = jlimit (-24.0f, 12.0f, proc.compGainDb.load());
        const float mx = r.getRight() - 80.0f, wd = 76.0f, cy = r.getY() + 10.0f;
        g.setColour (th.ink.withAlpha (0.3f));
        g.fillRect (mx, cy, wd, 1.0f);
        const float zero = mx + wd * (24.0f / 36.0f), x = mx + wd * ((gdb + 24.0f) / 36.0f);
        g.setColour (th.fill);
        g.fillRect (jmin (zero, x), cy - 3.0f, std::abs (x - zero), 7.0f);
        g.setColour (th.ink);
        g.fillRect (zero - 0.5f, cy - 5.0f, 1.0f, 11.0f);
    }
    if (&s == grS || &s == sgS)
    {
        const float amt = &s == grS ? proc.freezeAmount.load() : proc.spectralFreezeAmount.load();
        if (amt > 0.01f)
        {
            // frost: hatching that grows with the freeze
            g.setColour (th.ink.withAlpha (0.5f));
            const int n = (int) (amt * 10.0f);
            const float hx = &s == grS ? r.getRight() - 150.0f : r.getRight() - 128.0f;
            for (int i = 0; i < n; ++i) g.drawLine (hx + (float) i * 4.0f, r.getY() + 16.0f, hx + 6.0f + (float) i * 4.0f, r.getY() + 5.0f, 0.8f);
        }
    }
}

void CloudfinEditor::paintContent (Graphics& g)
{
    g.fillAll (th.ground);
    drawSky (g);

    // the title block, as on the other faltung effects
    g.setColour (th.ink);
    g.setFont (ui::sans (22.0f, true));
    g.drawText ("cloudfin", 36, 24, 300, 28, Justification::centredLeft);
    g.setFont (ui::mono (11.0f));
    g.drawText ("clouds over water: seven effects in any order", 36, 54, 400, 16, Justification::centredLeft);

    // the rule under the picture, and the control line
    g.setColour (th.ink);
    g.fillRect (36.0f, (float) skyH, (float) designW - 72.0f, 1.0f);
    g.setColour (th.rule);
    g.fillRect (36.0f, (float) skyH + 38.0f, (float) designW - 72.0f, 1.0f);

    for (auto& s : sections) drawSection (g, *s);

    g.setColour (th.ink.withAlpha (0.7f));
    g.setFont (ui::mono (10.0f));
    g.drawText ("faltung cloudfin" + sep + "drag the strips to reorder, double-click for the default" + sep + "the small bars: drift from the random voltage",
                36, designH - 26, designW - 72, 14, Justification::centredLeft);
    // the resize corner, as on the others
    g.setColour (th.ink.withAlpha (0.5f));
    for (int i = 0; i < 3; ++i) g.drawLine ((float) designW - 4.0f - (float) i * 5.0f, (float) designH - 2.0f, (float) designW - 2.0f, (float) designH - 4.0f - (float) i * 5.0f, 1.0f);
}

//==============================================================================
float CloudfinEditor::modValue (const char* id) const
{
    const int i = modIndexOf (id);
    if (i >= 0 && i < (int) proc.modNorm.size()) return proc.modNorm[(size_t) i]->load();
    if (auto* prm = proc.apvts.getParameter (id)) return prm->getValue();
    return 0.0f;
}

// gravity: the puffs fall, hit the water (a splash, a wave), sink to float at its surface;
// the water rises a little. Off: they float back up. The water's motion is the slosh in the sound.
void CloudfinEditor::stepGravity (float dt)
{
    waterRise += ((gravityOn ? 26.0f : 0.0f) - waterRise) * jmin (1.0f, dt * 1.2f);
    const float cxHalf = designW * 0.5f;
    for (auto& c : clouds)
        for (auto& p : c.puffs)
        {
            const float x = cxHalf - c.dx + p.x;
            const float home = c.baseY + p.y;
            if (gravityOn)
            {
                const float surf = surfaceY (jlimit (0.0f, cxHalf, x));
                const float yNow = home + p.fall;
                if (! p.wet)
                {
                    p.vy += 900.0f * dt;
                    p.fall += p.vy * dt;
                    if (home + p.fall > surf)
                    {
                        p.wet = true;
                        const float strength = jmin (1.0f, p.vy / 600.0f) * jmin (1.0f, p.r / 40.0f);
                        splashEnergy = jmin (1.0f, splashEnergy + 0.25f * strength);
                        const int i = jlimit (0, (int) waveH.size() - 1, (int) (jlimit (0.0f, cxHalf, x) / cxHalf * (float) (waveH.size() - 1)));
                        for (int k = -3; k <= 3; ++k)
                            if (i + k >= 0 && i + k < (int) waveV.size()) waveV[(size_t) (i + k)] += 180.0f * strength * (1.0f - (float) std::abs (k) / 4.0f);
                        p.vy *= 0.2f;
                    }
                }
                else
                {
                    // afloat: bob at the surface, half under
                    const float target = surf - home + p.r * 0.1f;
                    p.vy += ((target - p.fall) * 30.0f - p.vy * 4.0f) * dt;
                    p.fall += p.vy * dt;
                }
                juce::ignoreUnused (yNow);
            }
            else
            {
                p.wet = false;
                p.vy = 0.0f;
                p.fall += (0.0f - p.fall) * jmin (1.0f, dt * 1.6f);
            }
        }
    // the surface: neighbours pull on each other, everything springs back, slowly losing energy
    const int n = (int) waveH.size();
    for (int sub = 0; sub < 4; ++sub)
    {
        const float h = dt / 4.0f;
        for (int i = 0; i < n; ++i)
        {
            const float l = waveH[(size_t) jmax (0, i - 1)], r = waveH[(size_t) jmin (n - 1, i + 1)];
            waveV[(size_t) i] += ((l + r - 2.0f * waveH[(size_t) i]) * 900.0f - waveH[(size_t) i] * 6.0f) * h;
            waveV[(size_t) i] *= 1.0f - 1.6f * h;
        }
        for (int i = 0; i < n; ++i) waveH[(size_t) i] = jlimit (-40.0f, 40.0f, waveH[(size_t) i] + waveV[(size_t) i] * h);
    }
    float motion = 0.0f;
    for (auto v : waveV) motion += std::abs (v);
    motion = jlimit (0.0f, 1.0f, motion / ((float) jmax (1, n) * 40.0f));
    splashEnergy *= std::exp (-dt * 1.5f);
    proc.sloshEnergy.store (gravityOn ? jlimit (0.0f, 1.0f, 0.5f * motion + splashEnergy) : 0.0f, std::memory_order_relaxed);
}

void CloudfinEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    t += dt;

    gravityOn = proc.apvts.getRawParameterValue (ids::gravity)->load() > 0.5f;
    stepGravity (dt);
    if (proc.themeMode.load() != th.mode) setThemeMode (proc.themeMode.load());   // a preset or session brought another light

    // the level of the sound, for the water (from the slosh, the freeze and the drift; cheap and calm)
    const float target = jlimit (0.0f, 1.0f, 0.6f * proc.freezeAmount.load() + 0.4f * modValue (ids::grMix) + 0.8f * proc.sloshAmount.load());
    levelSm += (target - levelSm) * jmin (1.0f, dt * 2.0f);

    for (auto& r : rows)
        if (r->modIndex >= 0 && r->modIndex < (int) proc.modNorm.size())
            r->slider->setGhost (proc.modNorm[(size_t) r->modIndex]->load());

    shapeEditor.poll (t, modValue (ids::grSize) * 0.001f);
    chain.poll (dt);
    presets.poll();
    // the picture band and the live headers move every frame; the rows repaint themselves
    content.repaint (0, 0, designW, skyH);
    for (auto* s : { grS, sgS, cpS }) content.repaint (s->r.withHeight (24));
    static int frame = 0;
    if (++frame % 15 == 0) for (auto& s : sections) if (s->stage >= 0) content.repaint (s->r.withHeight (24));   // their place in the chain
}
