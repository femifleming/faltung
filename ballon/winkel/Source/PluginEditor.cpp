#include "PluginEditor.h"
#include "MoreUi.h"
#include "RainboomArt.h"

using namespace juce;

//==============================================================================
// (the look: ballon/Look.h. These are the names the rest of the window knows it by.)
const BalloonColours& balloonColours (int hue)
{
    // each bunch's colour is one of the suite's strip colours; line / shade / shine follow it
    static BalloonColours c[numHues];
    const int h = jlimit (0, numHues - 1, hue);
    const auto base = bal::hue (h);
    c[h] = { base, base.darker (0.35f), base.darker (0.15f), base.brighter (0.6f) };
    return c[h];
}

BalloonShape balloonShape (Rectangle<float> b, float inflate)
{
    const float fullR = jmin (b.getWidth(), b.getHeight()) * 0.38f;
    const float s = 0.68f + 0.32f * jlimit (0.0f, 1.0f, inflate);
    BalloonShape sh;
    sh.rx = fullR * 0.90f * s;
    sh.ry = fullR * 1.06f * s;
    sh.knot = jmax (3.0f, fullR * 0.16f);
    sh.cx = b.getCentreX();
    sh.cy = b.getCentreY() - fullR * 0.10f;
    sh.lw = jmax (1.5f, fullR * 0.07f);
    return sh;
}

//==============================================================================
SadLookAndFeel::SadLookAndFeel()
{
    setColour (Label::textColourId, bal::paperInk());
    setColour (ComboBox::textColourId, bal::paperInk());
    setColour (ComboBox::arrowColourId, bal::paperInk());
    setColour (PopupMenu::backgroundColourId, bal::paper());
    setColour (PopupMenu::textColourId, bal::paperInk());
    setColour (PopupMenu::highlightedBackgroundColourId, bal::strip (5));
    setColour (PopupMenu::highlightedTextColourId, bal::ui::inkFor (bal::strip (5)));
    setColour (PopupMenu::headerTextColourId, bal::paperInk());
    setColour (TextEditor::textColourId, bal::paperInk());
    setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    setColour (TextEditor::highlightColourId, bal::strip (5));
    setColour (CaretComponent::caretColourId, bal::paperInk());
}

Font SadLookAndFeel::uiFont (float h, bool bold) { return bal::mono (h, bold); }
Font SadLookAndFeel::titleFont (float h)         { return bal::sans (h, true); }
Font SadLookAndFeel::getComboBoxFont (ComboBox&) { return bal::mono (11.0f, true); }
Font SadLookAndFeel::getPopupMenuFont()          { return bal::mono (12.0f); }

PopupMenu::Options SadLookAndFeel::getOptionsForComboBoxPopupMenu (ComboBox& box, Label& label)
{
    auto o = LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label);
    if (auto* ed = box.findParentComponentOfClass<AudioProcessorEditor>())
        o = o.withParentComponent (ed);
    return o;
}

namespace
{
    inline float frac2 (float x) { return x - std::floor (x); }
}

void SadLookAndFeel::drawBalloon (Graphics& g, const BalloonShape& sh, int hue, const String& label,
                                  const String& value, bool hot, float wrinkles, float wet, float murk, float labelSize, float level) const
{
    bal::balloon (g, sh, hue, label, value, hot, wrinkles, wet, murk, labelSize, level);
}

void SadLookAndFeel::drawFrost (Graphics& g, const BalloonShape& sh, float frost, float seed) const
{
    bal::frost (g, sh, frost, seed);
}

void SadLookAndFeel::drawTinyBalloon (Graphics& g, Point<float> anchor, float r, int hue) const
{
    bal::tinyBalloon (g, anchor, r, bal::hue (hue));
}

// a setting in a list: its value in medium mono over a thin rule, as the suite's ChoiceBox
void SadLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    g.setColour (bal::paperInk().withAlpha (box.isMouseOver() ? 0.7f : 0.35f));
    g.fillRect (0.0f, (float) h - 2.0f, (float) w, 1.0f);
    g.setColour (bal::paperInk());
    g.setFont (bal::mono (10.0f));
    g.drawText ("v", Rectangle<float> ((float) w - 14.0f, 0.0f, 12.0f, (float) h - 2.0f), Justification::centred);
}

void SadLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (0, 0, box.getWidth() - 16, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void SadLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (bal::paper());
    g.setColour (bal::paperInk());
    g.drawRect (0.0f, 0.0f, (float) width, (float) height, 1.0f);
}

//==============================================================================
// Floater: anything on a string
Floater::Floater (BalloonEditor& e, float len, int anchorIndex)
    : anchor (anchorIndex), stringLength (len), owner (e)
{
    setSize (S, S);
}

float Floater::collisionRadius() const noexcept
{
    const auto sh = balloonShape ({ 0, 0, (float) S, (float) S }, inflation());
    return jmax (sh.rx, sh.ry * 0.9f) * 1.06f;   // a little personal space so names stay readable
}

Point<float> Floater::knotWorld() const noexcept
{
    const auto sh = balloonShape ({ 0, 0, (float) S, (float) S }, inflation());
    const float d = sh.knotBottom().y - (float) S * 0.5f;
    return pos + Point<float> (-d * std::sin (angle), d * std::cos (angle));
}

void Floater::place()
{
    setTransform (AffineTransform::rotation (angle, S * 0.5f, S * 0.5f).translated (pos.x - S * 0.5f, pos.y - S * 0.5f));
}

bool Floater::hitTest (int x, int y)
{
    const auto sh = balloonShape (getLocalBounds().toFloat(), inflation());
    const float dx = ((float) x - sh.cx) / (sh.rx + 3.0f), dy = ((float) y - sh.cy) / (sh.ry + 6.0f);
    return dx * dx + dy * dy <= 1.0f;
}

void Floater::beginMove (const MouseEvent& e)
{
    downPos = owner.toContent (e);
    moved = false;
    dragging = true;
    grabOffset = pos - downPos;
    dragTarget = pos;
    flickVel = {};
    lastMoveT = Time::getMillisecondCounterHiRes() * 0.001;
    lastMoveP = downPos;
    toFront (false);
}

void Floater::dragMove (const MouseEvent& e)
{
    const auto p = owner.toContent (e);
    if (p.getDistanceFrom (downPos) > 3.0f) moved = true;
    dragTarget = p + grabOffset;
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = now - lastMoveT;
    if (dt > 0.002 && dt < 0.2) flickVel = flickVel * 0.4f + ((p - lastMoveP) / (float) dt) * 0.6f;
    else if (dt >= 0.2) flickVel = {};
    if (dt > 0.002) { lastMoveT = now; lastMoveP = p; }
}

void Floater::maybeRainboom()
{
    if (moved && flung() && owner.rainboomThrowsOn()) owner.rainboomFromThrow (*this);
}

// let go while still moving fast (a flick, not a stop and then a release)
bool Floater::flung() const noexcept
{
    return Time::getMillisecondCounterHiRes() * 0.001 - lastMoveT < 0.07 && flickVel.getDistanceFromOrigin() > 2200.0f;
}

void Floater::applySquashAndStretch (Graphics& g, const BalloonShape& sh) const
{
    const float sc = 1.0f + 0.05f * dance;
    g.addTransform (AffineTransform::scale ((1.0f + squash) * sc * (1.0f - 0.035f * stretch),
                                            (1.0f - squash) * sc * (1.0f + 0.07f * stretch), sh.cx, sh.cy));
}

//==============================================================================
Balloon::Balloon (BalloonEditor& e, RangedAudioParameter& p, const String& l, int h, float len, int anchorIndex)
    : Floater (e, len, anchorIndex), hue (h), label (l), param (p),
      attachment (p, [this] (float v) { value = v; repaint(); }, nullptr)
{
    isToggle = dynamic_cast<AudioParameterBool*> (&p) != nullptr;
    isChoice = dynamic_cast<AudioParameterChoice*> (&p) != nullptr;
    isStepped = dynamic_cast<AudioParameterInt*> (&p) != nullptr;
    phase = Random (l.hashCode64()).nextFloat() * 6.2832f;
    attachment.sendInitialUpdate();
}

float Balloon::inflation() const noexcept
{
    if (fill >= 0.0f) return jmax (fill, value > 0.5f ? 0.2f : 0.0f);
    if (isChoice || isStepped) return 0.45f + 0.55f * param.convertTo0to1 (value);   // always nicely full; grows a little per step
    return isToggle ? (value > 0.5f ? 1.0f : 0.0f) : param.convertTo0to1 (value);
}

void Balloon::paint (Graphics& g)
{
    const float inf = inflation();
    const auto sh = balloonShape (getLocalBounds().toFloat(), inf);
    const bool hot = isMouseOverOrDragging() || mode == Mode::adjust;
    applySquashAndStretch (g, sh);
    String valueText, text = label;
    if (isChoice || isStepped)
    {
        text = label + "\n" + param.getCurrentValueAsText();   // wears its current option / step
        if (hot) valueText = isChoice ? "click: next" : "scroll or click";
    }
    else if (hot || drifting) valueText = isToggle ? String (value > 0.5f ? "on" : "off") : param.getCurrentValueAsText();   // a cloud changing it shows it too
    // (how far the colour has risen in it is its value; a switch is whole when on and only its edges when off)
    const float level = fill >= 0.0f ? fill : (isToggle ? 1.0f : (isChoice || isStepped ? param.convertTo0to1 (value) : inf));
    owner.lnf.drawBalloon (g, sh, hue, text, valueText, hot, isToggle ? 1.0f - inf / 0.45f : 0.0f, wet, 0.55f * faded, 10.0f, level);
    if (frost > 0.01f) owner.lnf.drawFrost (g, sh, frost, phase);
}

void Balloon::stepChoice (int direction)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (&param))
    {
        const int n = c->choices.size();
        const int idx = ((roundToInt (value) + direction) % n + n) % n;
        attachment.setValueAsCompleteGesture ((float) idx);
        squashVel += 3.0f;
        owner.confetti (pos, 6);
    }
    else if (auto* ip = dynamic_cast<AudioParameterInt*> (&param))
    {
        // whole steps: scrolling stops at the ends, clicking wraps round
        const auto r = ip->getRange();
        int v = roundToInt (value) + direction;
        if (v > r.getEnd())   v = (direction == 1 && wrapSteps) ? r.getStart() : r.getEnd();
        if (v < r.getStart()) v = r.getStart();
        attachment.setValueAsCompleteGesture ((float) v);
        squashVel += 3.0f;
        owner.confetti (pos, 6);
    }
}

void Balloon::setNorm (float n)
{
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, n)));
}

void Balloon::mouseDown (const MouseEvent& e)
{
    stopDrift();   // your hand wins over the cloud
    if (! isToggle && ! isChoice && (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isPopupMenu()))
    {
        mode = Mode::adjust;
        downPos = owner.toContent (e);
        downNorm = param.convertTo0to1 (value);
        attachment.beginGesture();
    }
    else
    {
        mode = Mode::move;
        beginMove (e);
    }
    repaint();
}

void Balloon::mouseDrag (const MouseEvent& e)
{
    if (mode == Mode::move) dragMove (e);
    else if (mode == Mode::adjust)
    {
        const auto p = owner.toContent (e);
        setNorm (downNorm - (p.y - downPos.y) / (e.mods.isAltDown() ? 700.0f : 220.0f));
    }
}

void Balloon::mouseUp (const MouseEvent&)
{
    if (mode == Mode::adjust)
        attachment.endGesture();
    else if (mode == Mode::move)
    {
        dragging = false;
        maybeRainboom();
        if (! moved)
        {
            if (isToggle)
            {
                const bool on = value < 0.5f;
                attachment.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
                if (on) owner.confetti (pos, 46);
            }
            else if (isChoice) stepChoice (1);
            else if (isStepped) { wrapSteps = true; stepChoice (1); wrapSteps = false; }
            squashVel += 4.0f;   // boop
        }
    }
    mode = Mode::none;
    repaint();
}

void Balloon::mouseDoubleClick (const MouseEvent&)
{
    if (isToggle || isChoice) return;   // (stepped balloons reset to their default like any other)
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    owner.confetti (pos, 16);
    squashVel += 3.0f;
}

void Balloon::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (isToggle) return;
    if (isChoice || isStepped)
    {
        int dir = 0;
        if (BalloonEditor::wheelStep (wheelAcc, w, dir)) stepChoice (dir);
        return;
    }
    float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (w.isReversed) d = -d;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, param.convertTo0to1 (value) + d * 0.35f)));
    squashVel += jlimit (-1.5f, 1.5f, d * 6.0f);
}

// Inside a cloud: a continuous control wanders slowly towards a random value that changes every
// few seconds (at most ~7 % of its range a second); a choice or a whole-number control takes a
// random step now and then. Switches stay as they are. It's one long gesture for the host.
void Balloon::cloudDrift (bool inside, float dt, Random& r)
{
    const bool can = inside && ! isToggle && ! dragging && mode == Mode::none && alive;
    const bool steps = isChoice || isStepped;
    if (can && ! drifting)
    {
        drifting = true;
        driftTarget = r.nextFloat();
        driftClock = steps ? 3.0f + 4.0f * r.nextFloat() : 2.0f + 3.0f * r.nextFloat();
        if (! steps) attachment.beginGesture();
    }
    else if (! can) { stopDrift(); return; }

    driftClock -= dt;
    if (steps)
    {
        if (driftClock <= 0.0f) { driftClock = 4.0f + 6.0f * r.nextFloat(); stepChoice (r.nextBool() ? 1 : -1); }
        return;
    }
    if (driftClock <= 0.0f) { driftClock = 2.0f + 3.0f * r.nextFloat(); driftTarget = r.nextFloat(); }
    const float n = param.convertTo0to1 (value);
    const float lim = 0.07f * dt;
    const float next = jlimit (0.0f, 1.0f, n + jlimit (-lim, lim, (driftTarget - n) * 0.6f * dt));
    if (std::abs (next - n) > 1.0e-6f) attachment.setValueAsPartOfGesture (param.convertFrom0to1 (next));
}

void Balloon::stopDrift()
{
    if (! drifting) return;
    drifting = false;
    if (! isChoice && ! isStepped) attachment.endGesture();
}

//==============================================================================
SoundBalloon::SoundBalloon (BalloonEditor& e, int s, float sz, float st, int h)
    : Floater (e, 150.0f + Random().nextFloat() * 150.0f, 1), slot (s), hue (h), size (sz), semis (st)
{
    phase = Random().nextFloat() * 6.2832f;
}

void SoundBalloon::paint (Graphics& g)
{
    const auto sh = balloonShape (getLocalBounds().toFloat(), size);
    const bool hot = isMouseOverOrDragging();
    applySquashAndStretch (g, sh);
    const float murk = (1.0f - std::pow (size, 0.85f)) * 0.85f;   // the cutoff it was frozen at
    owner.lnf.drawBalloon (g, sh, hue, BalloonEditor::semisText (semis), hot ? "click to retune" : "",
                           hot, 0.0f, wet, murk, 14.0f, -1.0f);
    if (frost > 0.01f) owner.lnf.drawFrost (g, sh, frost, phase);

    // the sound inside: a little trace that moves with what it's playing
    const float lvl = jlimit (0.0f, 1.0f, owner.proc.balloonFX.voice[slot].level.load() * 3.0f);
    Path wave;
    const float w = sh.rx * 1.1f, y0 = sh.cy + sh.ry * 0.42f, t = owner.lnf.time;
    for (int i = 0; i <= 24; ++i)
    {
        const float u = (float) i / 24.0f;
        const float y = y0 + std::sin (u * 14.0f + t * 9.0f + phase) * sh.ry * 0.12f * lvl * std::sin (u * MathConstants<float>::pi);
        if (i == 0) wave.startNewSubPath (sh.cx - w * 0.5f, y); else wave.lineTo (sh.cx - w * 0.5f + u * w, y);
    }
    g.setColour (bal::ui::inkFor (bal::hue (hue)).withAlpha (0.3f + 0.6f * lvl));
    g.strokePath (wave, PathStrokeType (1.0f));
}

void SoundBalloon::mouseUp (const MouseEvent&)
{
    dragging = false;
    if (! moved)
    {
        squashVel += 4.0f;
        owner.showPitchBubble (this);   // type an exact interval
    }
}

void SoundBalloon::mouseDoubleClick (const MouseEvent&)
{
    owner.proc.balloonFX.voice[slot].mode = 0;
    owner.pop (pos, size, hue);
    alive = false;
    setVisible (false);
}

void SoundBalloon::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    int dir = 0;
    if (! BalloonEditor::wheelStep (wheelAcc, w, dir)) return;
    semis = owner.stepSemis (semis, dir);
    owner.proc.balloonFX.voice[slot].semis = semis;
    squashVel += dir > 0 ? 1.5f : -1.5f;
    repaint();
}

//==============================================================================
BalloonEditor::BalloonEditor (BalloonProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    addAndMakeVisible (overlay);   // (the chalkboard and the library go over even this)

    // four bows on the floor, each holding its own bunch
    const float floorY = (float) designH - 30.0f;
    anchors[0] = { { 600.0f, floorY }, {}, {}, {}, false, balloonPink };    // the loop (main feedback), on the floor
    anchors[1] = { { 860.0f, floorY }, {}, {}, {}, false, balloonTeal };    // caught sounds, on the floor
    // the effects, standing on the shelf
    anchors[2] = { { 200.0f, shelfY }, {}, {}, {}, false, balloonBlue };    // spectral smear
    anchors[5] = { { 350.0f, shelfY }, {}, {}, {}, false, balloonYellow };  // pitch
    anchors[3] = { { 500.0f, shelfY }, {}, {}, {}, false, balloonMint };    // grains
    anchors[6] = { { 650.0f, shelfY }, {}, {}, {}, false, balloonLilac };   // reverb
    anchors[4] = { { 800.0f, shelfY }, {}, {}, {}, false, balloonOrange };  // the gumball machine (stutter): bought at the store
    for (int i : { 2, 3, 4, 5, 6 }) anchors[i].onShelf = true;
    anchors[4].box = true;      // it comes in a gift box like the store's other effects
    anchors[4].hidden = true;   // (until you buy it)

    struct Def { const char* id; const char* label; int hue; float len; int anchor; };
    const Def defs[] = {
        // spectral smear (blue bow)
        { ids::sgMix,     "spectral\nsmear",  balloonBlue,   240, 2 },
        { ids::sgFreeze,  "spectral\nfreeze", balloonBlue,   110, 2 },
        { ids::sgTilt,    "spectral\ntilt",   balloonBlue,   175, 2 },
        // pitch, reverb, the loop (pink bow)
        { ids::psShift,   "shift",           balloonYellow, 280, 5 }, { ids::psFeedback, "pitch\nfeedback", balloonYellow, 220, 5 },
        { ids::psMix,     "pitch\nmix",      balloonYellow, 160, 5 }, { ids::rvDecay,   "decay",           balloonLilac,  250, 6 },
        { ids::rvMix,     "verb\nmix",       balloonLilac,  180, 6 }, { ids::masterFb,  "main\nfeedback",  balloonPink,   230, 0 },
        // grains (mint bow)
        { ids::grMix,     "grain\nmix",      balloonMint,   310, 3 }, { ids::grSize,    "grain\nsize",     balloonMint,   260, 3 },
        { ids::grDensity, "density",         balloonMint,   210, 3 }, { ids::grShape,   "shape",           balloonMint,   290, 3 },
        { ids::grPan,     "random\npan",     balloonMint,   170, 3 }, { ids::grReverse, "reverse",         balloonMint,   240, 3 },
        { ids::grOctave,  "octave",          balloonMint,   140, 3 }, { ids::grFade,    "freeze\nfade",    balloonMint,   190, 3 },
        { ids::grFreeze,  "freeze",          balloonMint,   110, 3 }, { ids::grRandOct, "random\noctave",  balloonMint,    90, 3 },
        { ids::grPos,     "position",        balloonMint,   330, 3 }, { ids::grPosRand, "position\nrandom", balloonMint,   270, 3 },
        // the caught sounds' own bow (teal): how loud the frozen balloons are
        { ids::grainsLevel, "frozen\nvolume", balloonTeal,  210, 1 },
        // the gumball machine (orange bow)
        { ids::stAmount,  "stutter",         balloonOrange, 230, 4 }, { ids::stBounce,  "bounce",          balloonOrange, 180, 4 },
        { ids::stSway,    "sway",            balloonOrange, 130, 4 }, { ids::stCutup,   "cut-up",          balloonOrange, 90, 4 },
    };
    int perBow[7] {};
    for (auto& d : defs) ++perBow[d.anchor];
    int seen[7] {};
    for (auto& d : defs)
        if (auto* prm = p.apvts.getParameter (d.id))
        {
            auto* b = new Balloon (*this, *prm, d.label, d.hue, d.len, d.anchor);
            floaters.add (b);
            if (String (d.id) == ids::grFreeze) freezeBalloon = b;
            if (String (d.id) == ids::sgFreeze) spectralFreezeBalloon = b;
            // the gumball machine's four balloons belong to the store: hidden until you buy it
            {
                const char* stIds[] = { ids::stAmount, ids::stBounce, ids::stSway, ids::stCutup };
                for (int k = 0; k < 4; ++k)
                    if (String (d.id) == stIds[k])
                    {
                        storeBalloons[stutterItem][k] = b;
                        b->alive = false;
                        b->bypassable = true;
                    }
            }
            // the effects' own controls dim while bypassed; the grain controls still shape the frozen balloons
            for (auto* fxId : { ids::sgMix, ids::sgFreeze, ids::sgTilt, ids::psShift, ids::psFeedback, ids::psMix, ids::rvDecay,
                                ids::rvMix, ids::masterFb, ids::grMix, ids::grFreeze })
                if (String (d.id) == fxId) b->bypassable = true;
            // each bunch fans out from its own bow
            const int k = seen[d.anchor]++;
            const int n = jmax (2, perBow[d.anchor]);
            b->lean = ((float) ((k * 5) % n) / (float) (n - 1)) * 1.4f - 0.7f;
            if (d.anchor == 2) b->lean = 0.1f + ((float) ((k * 5) % n) / (float) (n - 1)) * 0.9f;   // keep clear of the bubble
            // they all start bunched in their bow and float up when the window opens
            b->pos = anchors[d.anchor].pos + Point<float> (rnd.nextFloat() * 80.0f - 40.0f, -20.0f - rnd.nextFloat() * 40.0f);
            b->vel = { rnd.nextFloat() * 160.0f - 80.0f, -rnd.nextFloat() * 120.0f };
            content.addChildComponent (b);
            b->setVisible (b->alive);
            b->place();
        }

    // the store's balloons: made now, hidden until you buy them
    {
        static const int hues[] = { balloonLilac, balloonOrange, balloonPink, balloonTeal, balloonYellow,
                                    balloonBlue, balloonMint, balloonOrange, balloonPink, balloonLilac, balloonTeal };
        for (int e = 0; e < fp::StoreFX::kNum; ++e)
        {
            anchors[firstBox + e] = { { 150.0f + 70.0f * (float) e, shelfY }, {}, {}, {}, false, hues[e], false, true, true, true };
            const auto& in = fp::StoreFX::infos()[(size_t) e];
            const String sn (in.shortName);
            const String labels[4] = { sn + "\n" + in.c1.name, sn + "\n" + in.c2.name, sn + "\nlevel", sn + "\nwidth" };
            const ids::StorePart parts[4] = { ids::storeA, ids::storeB, ids::storeLevel, ids::storeWidth };
            for (int k = 0; k < 4; ++k)
                if (auto* prm = p.apvts.getParameter (ids::store (e, parts[k])))
                {
                    // each effect's four balloons are tied to its own gift box
                    auto* b = new Balloon (*this, *prm, labels[k], hues[e], 70.0f + 30.0f * (float) k, firstBox + e);
                    b->lean = 0.35f * ((float) k - 1.5f) / 1.5f;   // fanned out over the box
                    b->alive = false;
                    b->bypassable = true;
                    b->setVisible (false);
                    floaters.add (b);
                    content.addChildComponent (b);
                    storeBalloons[e][k] = b;
                }
        }
    }
    numParamBalloons = floaters.size();
    syncStore();
    layoutShelf (shelfItems());
    for (auto& a : anchors) if (a.onShelf) a.pos.x = a.slotX;   // open with everything already in its place

    if (auto* r = p.apvts.getParameter (ids::rain))
    {
        rainAtt = std::make_unique<ParameterAttachment> (*r, [this] (float v) { rainValue = v; });
        rainAtt->sendInitialUpdate();
    }
    if (auto* hl = p.apvts.getParameter (ids::helium))
    {
        heliumAtt = std::make_unique<ParameterAttachment> (*hl, [this] (float v) { heliumValue = v; });
        heliumAtt->sendInitialUpdate();
    }
    if (auto* c = p.apvts.getParameter (ids::cloudy))
    {
        cloudyAtt = std::make_unique<ParameterAttachment> (*c, [this] (float v) { cloudyValue = v; });
        cloudyAtt->sendInitialUpdate();
    }
    if (auto* sn = p.apvts.getParameter (ids::snow))
    {
        snowAtt = std::make_unique<ParameterAttachment> (*sn, [this] (float v) { snowValue = v; });
        snowAtt->sendInitialUpdate();
    }
    if (auto* h = p.apvts.getParameter (ids::heat))
    {
        heatAtt = std::make_unique<ParameterAttachment> (*h, [this] (float v) { heatValue = v; });
        heatAtt->sendInitialUpdate();
    }
    {
        const char* skyIds[4] = { ids::fog, ids::wind, ids::hail, ids::aurora };
        for (int i = 0; i < 4; ++i)
            if (auto* prm = p.apvts.getParameter (skyIds[i]))
            {
                skyAtt[i] = std::make_unique<ParameterAttachment> (*prm, [this, i] (float v) { skyValue[i] = v; });
                skyAtt[i]->sendInitialUpdate();
            }
        skyArt = std::make_unique<SkyArt>();
    }
    if (auto* aw = p.apvts.getParameter (ids::autoWeather))
    {
        autoAtt = std::make_unique<ParameterAttachment> (*aw, [this] (float v) { autoValue = v; });
        autoAtt->sendInitialUpdate();
    }
    if (auto* bp = p.apvts.getParameter (ids::fxBypass))
    {
        bypassAtt = std::make_unique<ParameterAttachment> (*bp, [this] (float v) { bypassValue = v; });
        bypassAtt->sendInitialUpdate();
    }
    // the clouds, waiting off to the sides until the weather turns
    for (int i = 0; i < 4; ++i)
    {
        const float dir = i % 2 == 0 ? 1.0f : -1.0f;
        skyClouds.push_back ({ { 150.0f + 250.0f * (float) i, 150.0f + 52.0f * (float) ((i * 3) % 4) },
                               dir * (9.0f + 7.0f * rnd.nextFloat()), 1.45f + 0.5f * rnd.nextFloat(), rnd.nextFloat() * 6.2832f });
    }
    seenLightning = p.lightningCount.load();

    // the pumps: pop balloons on the left, sound balloons on the right
    popPump.area = { 28.0f, 380.0f, 96.0f, 220.0f };
    popPump.nozzle = { 92.0f, (float) designH - 80.0f };
    popPump.hue = balloonOrange;
    popPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
    soundPump.area = { 948.0f, 380.0f, 112.0f, 220.0f };
    soundPump.nozzle = { 1020.0f, (float) designH - 80.0f };
    soundPump.hue = balloonTeal;
    soundPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
    nextBadge = { 940.0f, (float) designH - 62.0f, 56.0f, 20.0f };
    cloudButton = { 394.0f, 4.0f, 96.0f, 62.0f };   // rain, next to cloudy in the weather row
    tankArea = { railRight, (float) designH - 102.0f, 46.0f, 76.0f };
    if (auto* th = p.apvts.getParameter (ids::grainsThru))
    {
        thruAtt = std::make_unique<ParameterAttachment> (*th, [this] (float v)
        {
            thruValue = v;
            if (! tankDragging) tankArea.setX (railRight - v * (railRight - railLeft));
        });
        thruAtt->sendInitialUpdate();
    }

    // the pin lives in a pincushion on the floor
    pin = std::make_unique<PinTool> (*this);
    pin->home = { 798.0f, (float) designH - 60.0f };
    pin->pos = pin->home;
    content.addAndMakeVisible (*pin);
    pin->place();

    // the chalkboard stands on the floor by the pin cushion
    chalkboard = std::make_unique<Chalkboard> (*this);
    addChildComponent (*chalkboard);   // on the editor itself, so nothing in the room (clouds, rain) draws over it
    library = std::make_unique<Library> (*this);
    addChildComponent (*library);
    gumballPage = std::make_unique<GumballPage>();
    gumballPage->getRows = [this]
    {
        // every control balloon in the room right now, a row for each bow or gift box
        static const char* const names[] = { "loop", "caught", "spectral", "grains", "stutter", "pitch", "reverb" };
        static const int place[] = { 4, 5, 0, 2, 6, 1, 3 };   // where each bow's row goes: spectral, pitch, grains, reverb, loop, caught, stutter
        std::vector<GumballPage::Row> rows;
        for (auto* f : floaters)
        {
            auto* b = dynamic_cast<Balloon*> (f);
            if (b == nullptr || ! b->alive) continue;
            const int key = b->anchor < firstBox ? place[jlimit (0, 6, b->anchor)] : 100 + (b->anchor - firstBox);
            auto it = std::find_if (rows.begin(), rows.end(), [key] (const GumballPage::Row& r) { return r.key == key; });
            if (it == rows.end())
            {
                GumballPage::Row r;
                r.key = key;
                r.hue = b->hue;
                r.name = b->anchor < firstBox ? String (names[jlimit (0, 6, b->anchor)]) : storeItemName (b->anchor - firstBox).toLowerCase();
                rows.push_back (std::move (r));
                it = rows.end() - 1;
            }
            // the balloon's name, without the effect's own name in front where the row already says it
            auto lines = StringArray::fromLines (b->label);
            lines.removeEmptyStrings();
            String label = lines.joinIntoString (" ");
            if (lines.size() == 2 && (it->name.contains (lines[0].toLowerCase()) || lines[0].toLowerCase().contains (it->name))) label = lines[1];
            it->knobs.push_back ({ &b->parameter(), label, b->isToggle, b->isChoice, b->isStepped });
        }
        std::sort (rows.begin(), rows.end(), [] (const GumballPage::Row& a, const GumballPage::Row& b) { return a.key < b.key; });
        return rows;
    };
    gumballPage->onActivity = [this] { noteActivity(); };
    addChildComponent (*gumballPage);
    storeShelves = std::make_unique<StoreShelves> (*this);
    overlay.addChildComponent (*storeShelves);
    rainboomLayer = std::make_unique<RainboomLayer> (*this);
    rainboomLayer->setBounds (0, 0, designW, designH);
    content.addChildComponent (*rainboomLayer);
    weatherBar = std::make_unique<WeatherBar> (*this);
    content.addAndMakeVisible (*weatherBar);
    orderRow = std::make_unique<OrderRow> (*this);
    overlay.addAndMakeVisible (*orderRow);
    presetStrip = std::make_unique<PresetStrip> (*this);
    overlay.addChildComponent (*presetStrip);
    fadePopup = std::make_unique<FadePopup> (*this);
    overlay.addChildComponent (*fadePopup);
    const float H = (float) designH;
    if (auto* ps = p.apvts.getParameter (ids::balloonSfx))
    {
        popsAtt = std::make_unique<ParameterAttachment> (*ps, [this] (float v) { popsValue = v; });
        popsAtt->sendInitialUpdate();
    }
    addPill (ids::catchFrom,  "catch",      { 912.0f, H - 116.0f }, balloonTeal,   iconCatch);
    pills.getLast()->phase = 2.3f;

    for (int i = 0; i < 60; ++i) dust.push_back ({ rnd.nextFloat() * designW, rnd.nextFloat() * designH });

    scaleBox.addItemList (StringArray ([] { StringArray a; for (auto& s : fp::getScales()) a.add (s.name); return a; }()), 1);
    scaleBox.setJustificationType (Justification::centredLeft);
    cardPanel = std::make_unique<CardPanel> (*this);
    overlay.addChildComponent (*cardPanel);
    cardPanel->addAndMakeVisible (scaleBox);
    scaleAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::psScale, scaleBox);

    // presets, grouped
    {
        String group;
        for (int i = 0; i < (int) presets::list().size(); ++i)
        {
            const auto& pr = presets::list()[(size_t) i];
            if (group != pr.group) { group = pr.group; if (i > 0) presetBox.addSectionHeading (group); }
            presetBox.addItem (pr.name, i + 1);
        }
        presetBox.setJustificationType (Justification::centredLeft);
        presetBox.setTextWhenNothingSelected ("presets");
        presetBox.setLookAndFeel (&storeLook);   // the same sweet-shop menu as the balloon store
        presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
        presetBox.onOpen = [this] { if (presetStrip != nullptr) { if (presetStrip->isOpen()) presetStrip->close(); else presetStrip->open(); } };
        cardPanel->addAndMakeVisible (presetBox);
    }
    placeCard();   // (it starts tucked away above the window)
    followPreset (0.0f);
    lastActivity = clock;

    setResizable (true, true);
    setResizeLimits ((int) (designW * 0.6), (int) (designH * 0.6), designW * 2, designH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setSize ((int) (designW * 0.9f), (int) (designH * 0.9f));

    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

BalloonEditor::~BalloonEditor()
{
    stopTimer();
    // don't leave a balloon recording or the pump squeaking when the window closes
    proc.balloonFX.pumping = false;
    if (soundPump.slot >= 0) proc.balloonFX.voice[soundPump.slot].mode = 0;
    presetBox.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void BalloonEditor::paint (Graphics& g) { g.fillAll (bal::ground()); }

void BalloonEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content.setBounds (0, 0, designW, designH);
    content.setTransform (AffineTransform::scale (s));
    overlay.setBounds (0, 0, designW, designH);
    overlay.setTransform (AffineTransform::scale (s));
    for (Component* c : { (Component*) chalkboard.get(), (Component*) library.get(), (Component*) gumballPage.get() })
        if (c != nullptr) c->setTransform (AffineTransform::scale (s));
}

//==============================================================================
String BalloonEditor::semisText (float semis)
{
    const int s = roundToInt (semis);
    return s == 0 ? String ("0") : (s > 0 ? "+" : String (CharPointer_UTF8 ("\xe2\x88\x92"))) + String (std::abs (s));
}

float BalloonEditor::stepSemis (float semis, int direction) const
{
    // the notes of the current scale, in semitones, across two octaves each way
    const auto& scales = fp::getScales();
    const auto& sc = scales[(size_t) jlimit (0, (int) scales.size() - 1, (int) proc.apvts.getRawParameterValue (ids::psScale)->load())];
    std::vector<int> notes;
    for (int o = -2; o <= 2; ++o)
        for (auto& d : sc.degrees) notes.push_back (o * 12 + roundToInt (d.cents / 100.0));
    notes.push_back (24);
    std::sort (notes.begin(), notes.end());
    const int cur = roundToInt (semis);
    if (direction > 0) { for (int n : notes) if (n > cur) return (float) n; return (float) notes.back(); }
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) if (*it < cur) return (float) *it;
    return (float) notes.front();
}

void BalloonEditor::confetti (Point<float> at, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi;
        const float sp = 120.0f + rnd.nextFloat() * 320.0f;
        particles.push_back ({ at, { std::cos (a) * sp, std::sin (a) * sp - 180.0f },
                               rnd.nextFloat() * 6.28f, (rnd.nextFloat() - 0.5f) * 14.0f, 1.0f + rnd.nextFloat() * 0.6f,
                               rnd.nextInt (6) == 0 ? bal::white() : bal::strip (rnd.nextInt (8)), rnd.nextFloat() < 0.3f ? 1 : 0 });
    }
}

void BalloonEditor::pop (Point<float> at, float size, int hue)
{
    proc.balloonFX.popSize = size;
    ++proc.balloonFX.popRequests;
    bursts.push_back ({ at, 0.0f, 0.6f + 0.6f * size, hue });
    if (pinkieHop <= 0.5f) pinkieHopVel = 150.0f + 120.0f * jmin (1.0f, size);   // startled
    scaredUntil = clock + 1.4;
    scaredAlt = ! scaredAlt;   // she cowers one time, stands frozen the next
    noteActivity();
    // bits of rubber fly off
    const auto c = balloonColours (hue).fill;
    for (int i = 0; i < 18; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi;
        const float sp = 160.0f + rnd.nextFloat() * 380.0f * (0.5f + size);
        particles.push_back ({ at, { std::cos (a) * sp, std::sin (a) * sp - 120.0f }, rnd.nextFloat() * 6.28f,
                               (rnd.nextFloat() - 0.5f) * 20.0f, 0.9f + rnd.nextFloat() * 0.5f, c.withMultipliedBrightness (0.8f + 0.4f * rnd.nextFloat()), 2 });
    }
    confetti (at, 14);
    // the shock wave shoves the balloons nearby
    for (auto* f : floaters)
    {
        const auto d = f->pos - at;
        const float dist = d.getDistanceFromOrigin();
        if (dist < 1.0f || dist > 230.0f) continue;
        f->vel += d / dist * (420.0f * (1.0f - dist / 230.0f) * (0.5f + size));
        f->squashVel += 3.0f * (1.0f - dist / 230.0f);
    }
}

void BalloonEditor::addPill (const char* id, const String& name, Point<float> centre, int hue, int icon)
{
    auto* prm = proc.apvts.getParameter (id);
    if (prm == nullptr) return;
    auto* pl = pills.add (new Pill());
    pl->c = centre; pl->home = centre; pl->icon = icon; pl->param = prm; pl->name = name; pl->hue = hue;
    pl->r = Rectangle<float> (2.0f * pl->rad + 10.0f, 2.0f * pl->rad + 10.0f).withCentre (centre);   // the click target
    pl->continuous = dynamic_cast<AudioParameterBool*> (prm) == nullptr && dynamic_cast<AudioParameterChoice*> (prm) == nullptr;
    pl->att = std::make_unique<ParameterAttachment> (*prm, [pl] (float v) { pl->value = v; }, nullptr);
    pl->att->sendInitialUpdate();
}

void BalloonEditor::clickPill (Pill& pl, int direction)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (pl.param))
    {
        const int n = c->choices.size();
        pl.att->setValueAsCompleteGesture ((float) (((roundToInt (pl.value) + direction) % n + n) % n));
    }
    else
        pl.att->setValueAsCompleteGesture (pl.value > 0.5f ? 0.0f : 1.0f);
    pl.bounceVel += 7.0f;                 // boing
    confetti (pl.c, 6);
}

// One round, glossy toy token with an animated picture of what it controls, and a caption.
// One token: a parallelogram with the control's name on it, filled as far as its value (a switch: filled
// when on; a setting: always), and what it is set to underneath.
void BalloonEditor::drawToken (Graphics& g, const Pill& pl) const
{
    const bool isChoice = dynamic_cast<AudioParameterChoice*> (pl.param) != nullptr;
    const bool isSwitch = dynamic_cast<AudioParameterBool*> (pl.param) != nullptr;
    const bool lit = isChoice || pl.continuous || pl.value > 0.5f;
    const float R = pl.rad;
    const auto c = pl.c;
    const auto base = bal::hue (pl.hue);
    const bool hover = content.getMouseXYRelative().toFloat().getDistanceFrom (c) < R + 5.0f;

    Graphics::ScopedSaveState ss (g);
    g.addTransform (AffineTransform::scale (1.0f + pl.bounce, 1.0f - pl.bounce, c.x, c.y + R));   // (it squashes when it boings)
    const auto r = Rectangle<float> (2.5f * R, 0.86f * R).withCentre (c);
    const Path p = bal::ui::skewRect (r.translated (-r.getHeight() * 0.18f, 0.0f), 0.35f);
    float filled = lit ? 1.0f : 0.0f;
    if (pl.continuous) filled = pl.param->convertTo0to1 (pl.value);
    if (auto* ip = dynamic_cast<AudioParameterInt*> (pl.param)) filled = ip->convertTo0to1 (pl.value);
    if (filled > 0.001f)
    {
        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (p);
        g.setColour (base);
        g.fillRect (r.expanded (r.getHeight(), 0.0f).withWidth ((r.getWidth() + 2.0f * r.getHeight()) * (0.12f + 0.88f * filled)));
    }
    g.setColour (hover ? bal::white() : bal::ink().withAlpha (0.8f));
    g.strokePath (p, PathStrokeType (hover ? 1.6f : 1.0f));
    bal::label (g, pl.name, r, filled > 0.5f ? bal::ui::inkFor (base) : bal::ink(), 9.5f, Justification::centred, true);

    String cap = pl.param->getCurrentValueAsText();
    if (isSwitch) cap = pl.value > 0.5f ? "on" : "off";
    bal::label (g, cap, Rectangle<float> (110.0f, 12.0f).withCentre ({ c.x, c.y + R * 0.43f + 10.0f }), bal::ink().withAlpha (lit ? 0.95f : 0.6f), 9.0f);
}

//==============================================================================
// The tokens bob gently where they sit.
void BalloonEditor::pillStep (float)
{
    const float t = (float) clock;
    for (auto* pl : pills)
    {
        pl->c = pl->home + Point<float> (2.0f * std::sin (t * 0.7f + pl->phase), 3.0f * std::sin (t * 0.9f + pl->phase));
        pl->r = pl->r.withCentre (pl->c);
    }
}

// Right-click the pop sounds token: switch them on or off, or feed them into the effects (then the
// hiss and the pops go through the chain with your sound, instead of straight out).
// Right-click the pop sounds switch: turn them on or off, or feed them into the effects (then the
// hiss and the pops go through the chain with your sound, instead of straight out).
void BalloonEditor::openPopsMenu()
{
    auto* on = proc.apvts.getParameter (ids::balloonSfx);
    auto* thru = proc.apvts.getParameter (ids::popsThru);
    if (on == nullptr || thru == nullptr) return;
    PopupMenu m;
    m.setLookAndFeel (&storeLook);
    storeLook.time = (float) clock;
    m.addSectionHeader ("pop sounds");
    PopupMenu::Item a ("pop sounds on");
    a.itemID = 1; a.isTicked = on->getValue() > 0.5f; a.colour = balloonColours (balloonOrange).fill;
    a.shortcutKeyDescription = "the air hiss and the pops";
    m.addItem (a);
    PopupMenu::Item b ("feed into the effects");
    b.itemID = 2; b.isTicked = thru->getValue() > 0.5f; b.colour = balloonColours (balloonLilac).fill;
    b.shortcutKeyDescription = "through the chain, not straight out";
    m.addItem (b);
    const auto area = popsButton;
    const auto screen = content.localAreaToGlobal (area.getSmallestIntegerContainer());
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (screen).withMinimumWidth (330).withParentComponent (this),
                     [safe = Component::SafePointer<BalloonEditor> (this), on, thru] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         auto* prm = r == 1 ? on : thru;
                         prm->beginChangeGesture();
                         prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                         prm->endChangeGesture();
                         safe->confetti (safe->popsButton.getCentre(), 8);
                     });
}

// The watcher: a wire solid standing by the pop pump. It turns slowly, hops when something pops, puts a
// canopy up in the rain, lights every face in helium, sinks flat in the heat, ices over in the snow and
// settles when the window is left alone. (What happens to it is what happened to the original's mascot.)
void BalloonEditor::drawPinkie (Graphics& g) const
{
    const float baseY = (float) designH - 12.0f;
    const float t = (float) clock;
    const float breatheRate = 1.6f - 0.8f * sleepVis;   // slower, deeper breaths asleep
    const float breathe = 1.0f + (0.014f + 0.01f * sleepVis) * std::sin (t * breatheRate);
    const float squash = pinkieHop > 0.0f ? 1.0f + 0.0006f * pinkieHopVel : 1.0f;   // stretch going up, squash coming down
    const float shiver = (scaredVis + 0.6f * coldVis * snowyVis) * 1.6f * std::sin (t * 70.0f);

    // which mood: startled beats asleep beats soaked (rain in a heatwave) beats the canopy beats just standing
    const float wow = wowVis;
    const float scared = scaredVis * (1.0f - wow);
    const float asleep = sleepVis * (1.0f - scared) * (1.0f - wow);
    const float wet = wetVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wow);
    const float umbrella = umbrellaVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet) * (1.0f - wow);
    const float crystal = crystalVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet);
    const float snowy = snowyVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet);
    const float cold = snowy * coldVis;
    const float melted = meltVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet) * (1.0f - snowy);

    const float flat = jlimit (0.0f, 1.0f, 0.62f * melted + 0.66f * wet + 0.42f * asleep + (scaredAlt ? 0.35f : 0.0f) * scared);
    const float R = 30.0f;
    const float sy = R * breathe * squash * (1.0f - flat) * (1.0f + (scaredAlt ? 0.0f : 0.25f) * scared);
    const float sx = R * (1.0f + 0.45f * flat) / std::sqrt (breathe * squash);
    const Point<float> c (pinkieX + shiver, baseY - sy * 1.25f - pinkieHop);
    const float turn = t * 0.35f * (1.0f - asleep) * (1.0f - 0.9f * snowy) * (1.0f - melted) + 0.6f;

    // where it stands: a short rule, shorter while it is off the floor
    const float sh = jlimit (0.4f, 1.0f, 1.0f - pinkieHop / 120.0f);
    g.setColour (bal::ink().withAlpha (0.5f * sh));
    g.fillRect (Rectangle<float> ((70.0f + 40.0f * flat) * sh, 1.0f).withCentre ({ pinkieX + 2.0f, baseY - 1.0f }));

    const auto& s = bal::solid (3);
    const bal::View v { c, sx, sy, turn, 0.35f };
    const Colour lineC = bal::line().interpolatedWith (bal::strip (7), cold).interpolatedWith (bal::white(), jmax (crystal, wow));
    bal::wireSolid (g, s, v, lineC, 1.0f + 0.4f * jmax (crystal, wow, scared));
    // its lit faces: one, or all of them in helium (and when the boom amazes it)
    const int lit = 1 + (int) std::lround (jmax (crystal, wow) * 7.0f) + (scared > 0.5f ? 2 : 0);
    for (int i = 0; i < lit; ++i)
    {
        const int f = (int) (t * 0.6f * (1.0f - asleep) * (1.0f - snowy)) + i * 3;
        bal::face (g, bal::facePath (s, v, f), (0.25f + 0.4f * jmax (crystal, wow)) * (1.0f - 0.6f * asleep), 1.6f);
    }
    if (crystal > 0.001f)
    {
        // it glitters: small white marks winking on and off around it
        for (int i = 0; i < 9; ++i)
        {
            const float ph = t * (1.3f + 0.17f * (float) i) + (float) i * 2.1f;
            const float tw = jmax (0.0f, std::sin (ph));
            const Point<float> q (c.x - sx * 1.5f + sx * 3.0f * std::fmod ((float) i * 0.618f, 1.0f), c.y - sy * 1.6f + sy * 3.0f * std::fmod ((float) i * 0.382f + 0.2f, 1.0f));
            g.setColour (Colours::white.withAlpha (crystal * tw * 0.9f));
            g.fillPath (bal::ui::skewRect (Rectangle<float> (2.0f + 6.0f * tw, 1.0f + 3.0f * tw).withCentre (q)));
        }
    }
    if (snowy > 0.01f)
    {
        // snow lying on it, and ice along its edges
        g.setColour (Colours::white.withAlpha (0.9f * snowy));
        g.fillPath (bal::poly ({ { c.x - sx * 0.7f, c.y - sy * 1.02f }, { c.x + sx * 0.3f, c.y - sy * 1.34f }, { c.x + sx * 0.85f, c.y - sy * 0.98f }, { c.x + sx * 0.1f, c.y - sy * 0.86f } }));
        bal::wireSolid (g, s, v, Colours::white.withAlpha (0.5f * snowy), 0.8f + 1.2f * cold);
    }
    if (wet > 0.01f || melted > 0.01f)
    {
        // a puddle's edge round it in the heat
        Path pud;
        pud.addEllipse (Rectangle<float> (sx * 3.4f, 9.0f).withCentre ({ c.x, baseY - 3.0f }));
        g.setColour ((wet > melted ? bal::strip (2) : bal::line()).withAlpha (0.8f * jmax (wet, melted)));
        g.strokePath (pud, PathStrokeType (1.0f));
    }
    if (umbrella > 0.01f)
    {
        // the canopy: half a faceted ring on a thin handle (the rain bounces off it: see umbrellaHit)
        const auto a = umbrellaImageArea();
        const Point<float> uc (a.getX() + a.getWidth() * 0.408f, a.getY() + a.getHeight() * 0.32f);
        const float rx = a.getWidth() * 0.40f, ry = a.getHeight() * 0.20f;
        Path canopy;
        for (int i = 0; i <= 6; ++i)
        {
            const float an = MathConstants<float>::pi * (1.0f + (float) i / 6.0f);
            const Point<float> q (uc.x + rx * std::cos (an), uc.y + ry * std::sin (an));
            if (i == 0) canopy.startNewSubPath (q); else canopy.lineTo (q);
        }
        canopy.closeSubPath();
        bal::face (g, canopy, 0.25f * umbrella, 1.6f, bal::white().withAlpha (umbrella));
        g.setColour (bal::white().withAlpha (umbrella));
        for (int i = 1; i < 6; ++i)
        {
            const float an = MathConstants<float>::pi * (1.0f + (float) i / 6.0f);
            g.drawLine (uc.x, uc.y - ry, uc.x + rx * std::cos (an), uc.y, 0.8f);
        }
        g.drawLine (uc.x, uc.y, uc.x + 6.0f, c.y + sy * 0.4f, 1.2f);
    }

    // z z z
    if (asleep > 0.05f)
    {
        for (int i = 0; i < 3; ++i)
        {
            const float u = std::fmod (t * 0.35f + (float) i / 3.0f, 1.0f);
            bal::label (g, "z", Rectangle<float> (pinkieX + 44.0f + 18.0f * u + 6.0f * std::sin (t + (float) i), baseY - 70.0f - 60.0f * u, 30.0f, 22.0f),
                        bal::ink().withAlpha (asleep * (1.0f - u) * 0.9f), 10.0f + 6.0f * u);
        }
    }
}


//==============================================================================
// The balloon store
// The store
void BalloonEditor::openStore()
{
    if (storeShelves != nullptr)
    {
        if (storeShelves->isOpen()) storeShelves->close();
        else storeShelves->open (storeSign);
        return;
    }
    PopupMenu m;
    m.setLookAndFeel (&storeLook);
    storeLook.time = (float) clock;
    m.addSectionHeader ("store");
    for (int e = 0; e < kStoreItems; ++e)
    {
        const bool owned = bought (e);
        PopupMenu::Item it (storeItemName (e));
        it.itemID = e + 1;
        it.isTicked = owned;
        it.colour = storeBalloons[e][0] != nullptr ? balloonColours (storeBalloons[e][0]->hue).fill : bal::strip (4);
        it.shortcutKeyDescription = owned ? String ("yours: tap to pop")
                                  : ((lentShown >> e) & 1u) != 0 ? String ("from the lightning: tap to keep")
                                                                  : storeItemWhat (e);
        m.addItem (it);
    }
    m.addSeparator();
    PopupMenu::Item all ("pop everything you bought");
    all.itemID = 100;
    all.isEnabled = std::any_of (std::begin (storeShown), std::end (storeShown), [] (bool b) { return b; });   // (lent ones too)
    all.colour = bal::strip (4);
    m.addItem (all);
    const auto screen = content.localAreaToGlobal (storeSign.getSmallestIntegerContainer());
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (screen).withMinimumWidth (240).withParentComponent (this),
                     [safe = Component::SafePointer<BalloonEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         if (r == 100)
                         {
                             safe->proc.popStruckRequest.fetch_or ((1u << fp::StoreFX::kNum) - 1u);
                             for (int e = 0; e < kStoreItems; ++e) safe->setBought (e, false);
                             return;
                         }
                         safe->setBought (r - 1, ! safe->bought (r - 1));
                     });
}

String BalloonEditor::ownedId (int item)
{
    return item == stutterItem ? String (ids::stOwned) : ids::store (item, ids::storeOwned);
}

String BalloonEditor::storeItemName (int item)
{
    return item == stutterItem ? String ("stutter") : String (fp::StoreFX::infos()[(size_t) item].name).toLowerCase();
}

String BalloonEditor::storeItemWhat (int item)
{
    if (item == stutterItem) return "stutter + bounce + sway + cut-up";
    const auto& in = fp::StoreFX::infos()[(size_t) item];
    return String (in.c1.name) + " + " + in.c2.name;
}

void BalloonEditor::setBought (int fx, bool bought)
{
    if (auto* prm = proc.apvts.getParameter (ownedId (fx)))
        prm->setValueNotifyingHost (bought ? 1.0f : 0.0f);
    syncStore();
}

bool BalloonEditor::bought (int fx) const
{
    return proc.apvts.getRawParameterValue (ownedId (fx))->load() > 0.5f;
}

// follow the "bought" parameters (they also change from the host, presets and undo), and what the
// lightning has lent
void BalloonEditor::syncStore()
{
    const uint32_t struck = proc.struckMask.load();
    for (int e = 0; e < kStoreItems; ++e)
    {
        const bool isBought = bought (e), isLent = e != stutterItem && ((struck >> e) & 1u) != 0;   // (the lightning never lends the stutter)
        const bool owned = isBought || isLent;
        if (owned == storeShown[e])
        {
            if (owned) lentShown = isBought ? (lentShown & ~(1u << e)) : (lentShown | (1u << e));   // kept: it's yours now
            continue;
        }
        storeShown[e] = owned;
        const bool wasLent = ((lentShown >> e) & 1u) != 0;
        {
            // its gift box: out of the shop (or down with the lightning) to a spot on the floor; gone when it pops
            auto& box = anchors[storeAnchor (e)];
            box.dragging = false;
            if (owned)
            {
                // out of the shop door (or down with the lightning, or out of Trixie's bolt), onto its place on the shelf
                const bool fromTrixie = ((trixieGifts >> e) & 1u) != 0;
                trixieGifts &= ~(1u << e);
                box.pos = fromTrixie ? trixieGiftAt : (isBought ? storeSign.getCentre().translated (0.0f, 24.0f) : strikeAt);
                box.vel = { 0.0f, -120.0f };
                box.hidden = false;
            }
            else
            {
                confetti (box.pos.translated (0.0f, 10.0f), 12);
                box.hidden = true;
            }
            layoutShelf (shelfItems());
        }
        lentShown = (owned && ! isBought) ? (lentShown | (1u << e)) : (lentShown & ~(1u << e));
        for (int k = 0; k < 4; ++k)
        {
            auto* b = storeBalloons[e][k];
            if (b == nullptr) continue;
            if (owned && ! isBought)
            {
                // dropped in by the lightning, where it struck
                b->alive = true;
                b->pos = strikeAt + Point<float> ((float) k * 22.0f - 33.0f, -10.0f + rnd.nextFloat() * 20.0f);
                b->vel = { (rnd.nextFloat() - 0.5f) * 300.0f, 420.0f + rnd.nextFloat() * 200.0f };
                b->wet = 0.6f;
                b->setVisible (true);
                b->place();
            }
            else if (owned)
            {
                // out with its box (from the shop door, or wherever Trixie's bolt landed)
                b->alive = true;
                b->pos = anchors[storeAnchor (e)].pos + Point<float> ((float) k * 14.0f - 21.0f, 6.0f);
                b->vel = { -260.0f - rnd.nextFloat() * 160.0f, 60.0f + rnd.nextFloat() * 80.0f };
                b->wet = 0.0f;
                b->setVisible (true);
                b->place();
            }
            else
            {
                // a lent effect pops out balloon by balloon; a bought one pops once and leaves confetti
                if (k == 0 || wasLent) pop (b->pos, wasLent ? 0.35f : 0.5f, b->hue);
                else confetti (b->pos, 10);
                b->stopDrift();
                b->alive = false;
                b->dragging = false;
                b->setVisible (false);
            }
        }
        if (owned) confetti (isBought ? storeSign.getCentre() : strikeAt, 24);
    }
}

void BalloonEditor::drawStoreSign (Graphics& g) const
{
    const float t = (float) clock;
    const bool open = storeShelves != nullptr && storeShelves->isOpen();
    const bool hover = storeSign.contains (content.getMouseXYRelative().toFloat());
    bal::chip (g, storeSign.reduced (0.0f, 7.0f), "   store", open, hover, bal::line(), bal::ink(), 11.0f);
    // a balloon on the sign, bobbing
    bal::tinyBalloon (g, { storeSign.getX() + 24.0f, storeSign.getCentreY() + 9.0f + 1.5f * std::sin (t * 1.7f) }, 5.0f, bal::hue (balloonLilac));
}

// where the umbrella picture sits, and its canopy as half an ellipse (measured from the picture)
// where the watcher's canopy sits: this box, and the canopy in it as half an ellipse
Rectangle<float> BalloonEditor::umbrellaImageArea() const noexcept
{
    const float w = 128.0f, h = 150.0f;
    return { pinkieX + 6.0f - w * 0.5f, (float) designH - 12.0f - h - pinkieHop, w, h };
}

bool BalloonEditor::umbrellaHit (Point<float> p, Point<float>& normal) const noexcept
{
    const auto a = umbrellaImageArea();
    if (a.isEmpty()) return false;
    const Point<float> c (a.getX() + a.getWidth() * 0.408f, a.getY() + a.getHeight() * 0.32f);
    const float rx = a.getWidth() * 0.40f, ry = a.getHeight() * 0.20f;
    const float dx = (p.x - c.x) / rx, dy = (p.y - c.y) / ry;
    if (dy > 0.0f || dx * dx + dy * dy > 1.0f) return false;
    normal = Point<float> (dx / rx, dy / ry);
    normal /= jmax (1.0e-6f, normal.getDistanceFromOrigin());
    return true;
}


//==============================================================================
// The store's menu: a sweet-shop window
// The store's menu, and the other lists that open from the window: paper, mono, a balloon mark per line
StoreMenuLook::StoreMenuLook()
{
}

void StoreMenuLook::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    SadLookAndFeel::drawPopupMenuBackground (g, width, height);
}

void StoreMenuLook::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    bal::title (g, name, area.toFloat().reduced (14.0f, 0.0f).translated (0.0f, 2.0f), bal::paperInk(), 17.0f, Justification::centredLeft, true);
    g.setColour (bal::paperLine());
    g.fillRect ((float) area.getX() + 14.0f, (float) area.getBottom() - 1.0f, (float) area.getWidth() - 28.0f, 1.0f);
}

void StoreMenuLook::getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardHeight, int& w, int& h)
{
    if (isSeparator) { w = 60; h = 12; return; }
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, w, h);
    h = (text == "store" || text == "pop sounds" || text == "sonic boom") ? 40 : 30;
    w = jmax (w + 120, 300);
}

void StoreMenuLook::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                       bool isTicked, bool, const String& text, const String& shortcutKeyText, const Drawable*, const Colour* textColour)
{
    auto r = area.toFloat().reduced (8.0f, 2.0f);
    if (isSeparator)
    {
        g.setColour (bal::paperLine());
        g.fillRect (r.getX() + 6.0f, r.getCentreY(), r.getWidth() - 12.0f, 1.0f);
        return;
    }
    const auto hue = textColour != nullptr ? *textColour : bal::hue ((int) ((uint32) text.hashCode() % (uint32) numHues));
    if (isHighlighted && isActive)
    {
        g.setColour (hue.withAlpha (0.45f));
        g.fillPath (bal::ui::skewRect (r.reduced (4.0f, 1.0f), 0.25f));
    }
    // the balloon, bobbing a little when you point at it
    const float bob = isHighlighted ? 1.5f * std::sin (time * 6.0f) : 0.0f;
    bal::tinyBalloon (g, { r.getX() + 16.0f, r.getBottom() - 2.0f + bob }, 6.5f, hue);

    auto textArea = r.withTrimmedLeft (34.0f);
    bal::label (g, text, textArea, bal::paperInk().withAlpha (isActive ? 1.0f : 0.45f), 12.0f, Justification::centredLeft, true);
    if (shortcutKeyText.isNotEmpty())
        bal::label (g, shortcutKeyText, textArea.withTrimmedRight (isTicked ? 34.0f : 6.0f), bal::paperInk().withAlpha (0.6f), 10.0f, Justification::centredRight);
    if (isTicked)   // yours: the house mark, filled
    {
        const auto m = Rectangle<float> (18.0f, 10.0f).withCentre ({ r.getRight() - 18.0f, r.getCentreY() });
        g.setColour (hue); g.fillPath (bal::ui::skewRect (m));
        g.setColour (bal::paperInk()); g.strokePath (bal::ui::skewRect (m), PathStrokeType (1.0f));
    }
}

//==============================================================================
// The chalkboard
namespace
{
    constexpr float boardW = 640.0f, boardH = 430.0f;   // board space: the easel, drawn at any size
    float ease (float x) { x = jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }
    const char* chalkButtons[] = { "sine", "bell", "spiky", "smooth", "clear" };
    const char* eqButtons[]    = { "flat", "warm", "bright", "smile", "smooth" };
}

void Chalkboard::bringForward()
{
    setBounds (0, 0, BalloonEditor::designW, BalloonEditor::designH);
    setVisible (true);
    toFront (false);
    animTarget = 1.0f;
}

void Chalkboard::putBack() { animTarget = 0.0f; drawing = false; }

bool Chalkboard::step (float dt)
{
    if (anim == animTarget) return isVisible();
    anim = animTarget > anim ? jmin (animTarget, anim + dt * 3.2f) : jmax (animTarget, anim - dt * 3.2f);
    if (anim <= 0.0f) setVisible (false);
    repaint();
    return isVisible();
}

// put away, it's a little easel standing on the floor; brought forward it grows to fill the room
Rectangle<float> Chalkboard::restArea() const
{
    constexpr float k = 0.16f;   // the floor easel is this fraction of full size
    return { floorSpot.x - boardW * k * 0.5f, floorSpot.y - boardH * k, boardW * k, boardH * k };
}

Rectangle<float> Chalkboard::boardArea() const
{
    // big: to the right of centre, leaving room for the teacher on the left
    const auto big = Rectangle<float> (boardW, boardH).withCentre ({ BalloonEditor::designW * 0.5f + 96.0f, BalloonEditor::designH * 0.5f + 6.0f });
    const auto rest = restArea();
    const float e = ease (anim);
    auto mix = [e] (float a, float b) { return a + (b - a) * e; };
    return { mix (rest.getX(), big.getX()), mix (rest.getY(), big.getY()), mix (rest.getWidth(), big.getWidth()), mix (rest.getHeight(), big.getHeight()) };
}

Rectangle<float> Chalkboard::plotArea() const
{
    const auto b = boardArea();
    const float k = b.getWidth() / boardW;
    return { b.getX() + 80.0f * k, b.getY() + 78.0f * k, 480.0f * k, 190.0f * k };
}

Rectangle<float> Chalkboard::buttonArea (int i) const
{
    const auto b = boardArea();
    const float k = b.getWidth() / boardW;
    return { b.getX() + (56.0f + 108.0f * (float) i) * k, b.getY() + 284.0f * k, 96.0f * k, 26.0f * k };
}

Rectangle<float> Chalkboard::tabArea (int i) const
{
    const auto b = boardArea();
    const float k = b.getWidth() / boardW;
    return { b.getX() + (148.0f + 176.0f * (float) i) * k, b.getY() + 42.0f * k, 168.0f * k, 30.0f * k };
}

// a drawn curve: one firm coloured line and two thin ones riding beside it (Oval's coloured lines)
void Chalkboard::drawChalkCurve (Graphics& g, const std::function<float (float)>& at, Rectangle<float> a, float th, float alpha)
{
    static const int cols[3] = { 4, 3, 5 };
    for (int pass = 2; pass >= 0; --pass)
    {
        Path p;
        const float oy = (float) pass * th * 1.6f;
        for (int i = 0; i <= 96; ++i)
        {
            const float t = (float) i / 96.0f;
            const Point<float> pt (a.getX() + t * a.getWidth(), a.getBottom() - at (t) * (a.getHeight() - oy));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (bal::strip (cols[pass]).withAlpha (alpha * (pass == 0 ? 1.0f : 0.7f)));
        g.strokePath (p, PathStrokeType (pass == 0 ? jmax (1.2f, th * 0.75f) : jmax (0.8f, th * 0.3f)));
    }
}

void Chalkboard::paint (Graphics& g)
{
    const float e = ease (anim);
    g.fillAll (bal::ground().withAlpha (0.82f * e));   // the room steps back while it's out

    const auto b = boardArea();
    drawEasel (g, b, e);

    // where the pen is while you're over the board: a small cross
    if (hovering && e > 0.99f)
    {
        g.setColour (bal::paperInk());
        g.drawLine (hover.x - 7.0f, hover.y, hover.x + 7.0f, hover.y, 1.0f);
        g.drawLine (hover.x, hover.y - 7.0f, hover.x, hover.y + 7.0f, 1.0f);
        g.setColour (bal::strip (4));
        g.fillPath (bal::ui::skewRect (Rectangle<float> (8.0f, 5.0f).withCentre (hover.translated (9.0f, -8.0f))));
    }
}

// The board: a sheet of paper on two thin legs. Small, it stands on the floor showing the grain shape;
// brought forward it has two pages, the grain shape and the master eq.
void Chalkboard::drawEasel (Graphics& g, Rectangle<float> b, float e) const
{
    const float k = b.getWidth() / boardW;
    auto at = [&] (float x, float y, float w, float h) { return Rectangle<float> (b.getX() + x * k, b.getY() + y * k, w * k, h * k); };
    const Colour inkC = bal::paperInk();

    // the legs
    g.setColour (e > 0.5f ? inkC.withAlpha (0.0f) : bal::ink());
    g.drawLine (b.getX() + 70.0f * k, b.getY() + 320.0f * k, b.getX() + 40.0f * k, b.getY() + 428.0f * k, 1.0f);
    g.drawLine (b.getX() + 570.0f * k, b.getY() + 320.0f * k, b.getX() + 600.0f * k, b.getY() + 428.0f * k, 1.0f);
    // the sheet, one corner folded
    const auto sheet = at (20, 20, 600, 300 + 100.0f * e);
    const float fold = 26.0f * k;
    g.setColour (bal::paper());
    g.fillPath (bal::poly ({ sheet.getTopLeft(), { sheet.getRight() - fold, sheet.getY() }, { sheet.getRight(), sheet.getY() + fold }, sheet.getBottomRight(), sheet.getBottomLeft() }));
    g.setColour (bal::paper().darker (0.18f));
    g.fillPath (bal::poly ({ { sheet.getRight() - fold, sheet.getY() }, { sheet.getRight(), sheet.getY() + fold }, { sheet.getRight() - fold, sheet.getY() + fold } }));
    g.setColour (inkC.withAlpha (0.8f));
    g.strokePath (bal::poly ({ sheet.getTopLeft(), { sheet.getRight() - fold, sheet.getY() }, { sheet.getRight(), sheet.getY() + fold }, sheet.getBottomRight(), sheet.getBottomLeft() }), PathStrokeType (1.0f));

    const Rectangle<float> plot { b.getX() + 80.0f * k, b.getY() + 78.0f * k, 480.0f * k, 190.0f * k };
    const float chalkA = 0.6f + 0.4f * e;
    const auto& eqc = owner.proc.eq;
    if (e > 0.5f)
    {
        // two pages, as tabs along the top: the grain shape and the master eq
        const char* tabs[] = { "grain shape", "master eq" };
        for (int i = 0; i < 2; ++i)
        {
            const auto r = Rectangle<float> (b.getX() + (148.0f + 176.0f * (float) i) * k, b.getY() + 42.0f * k, 168.0f * k, 30.0f * k);
            if (page == i) bal::foldStrip (g, r.reduced (10.0f * k, 4.0f * k), bal::strip (i == 0 ? 1 : 2), tabs[i], false, 0.0f, 13.0f * k);
            else bal::title (g, tabs[i], r, inkC.withAlpha (0.6f * chalkA), 13.0f * k, Justification::centred);
        }
        bal::label (g, "click outside the board to put it away", at (36, 22, 540, 16), inkC.withAlpha (0.5f * chalkA), 9.5f * k);
    }

    if (page == 0 || e <= 0.5f)
    {
        // axes
        g.setColour (inkC.withAlpha (0.45f * chalkA));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getBottom(), 1.0f);
        g.drawLine (plot.getX(), plot.getY(), plot.getX(), plot.getBottom(), 1.0f);
        if (e > 0.3f)
        {
            bal::label (g, "start", Rectangle<float> (plot.getX() - 20.0f * k, plot.getBottom() + 2.0f * k, 60.0f * k, 14.0f * k), inkC.withAlpha (0.7f * chalkA), 10.0f * k, Justification::centredLeft);
            bal::label (g, "end", Rectangle<float> (plot.getRight() - 40.0f * k, plot.getBottom() + 2.0f * k, 40.0f * k, 14.0f * k), inkC.withAlpha (0.7f * chalkA), 10.0f * k, Justification::centredRight);
            bal::label (g, "loud", Rectangle<float> (plot.getX() - 46.0f * k, plot.getY(), 40.0f * k, 14.0f * k), inkC.withAlpha (0.7f * chalkA), 10.0f * k, Justification::centredRight);
        }
        drawChalkCurve (g, [this] (float t) { return owner.proc.chalk.at (t); }, plot, 3.0f * k, chalkA);
    }
    else
    {
        // the eq: 0 db across the middle, octaves marked along the bottom, boost up and cut down
        g.setColour (inkC.withAlpha (0.14f * chalkA));
        for (float db : { -6.0f, 6.0f })
        {
            const float y = plot.getCentreY() - db / fp::EqCurve::range * plot.getHeight() * 0.5f;
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);
        }
        const float marks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        for (float f : marks)
        {
            const float x = plot.getX() + (float) fp::EqCurve::tOf (f) * plot.getWidth();
            g.setColour (inkC.withAlpha (0.14f * chalkA));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);
            bal::label (g, f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)),
                        Rectangle<float> (40.0f * k, 14.0f * k).withCentre ({ x, plot.getBottom() - 8.0f * k }), inkC.withAlpha (0.6f * chalkA), 9.5f * k);
        }
        g.setColour (inkC.withAlpha (0.5f * chalkA));
        float dashes[] = { 6.0f * k, 5.0f * k };
        g.drawDashedLine (Line<float> (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY()), dashes, 2, 1.0f);
        bal::label (g, "boost", Rectangle<float> (plot.getX() - 62.0f * k, plot.getY(), 56.0f * k, 14.0f * k), inkC.withAlpha (0.7f * chalkA), 10.0f * k, Justification::centredRight);
        bal::label (g, "0 db", Rectangle<float> (plot.getX() - 62.0f * k, plot.getCentreY() - 7.0f * k, 56.0f * k, 14.0f * k), inkC.withAlpha (0.7f * chalkA), 10.0f * k, Justification::centredRight);
        bal::label (g, "cut", Rectangle<float> (plot.getX() - 62.0f * k, plot.getBottom() - 14.0f * k, 56.0f * k, 14.0f * k), inkC.withAlpha (0.7f * chalkA), 10.0f * k, Justification::centredRight);

        // what the eq really does (the blue line), under what you drew
        if (! eqc.isFlat())
        {
            Path real;
            for (int i = 0; i <= 120; ++i)
            {
                const float t = (float) i / 120.0f;
                const float db = jlimit (-fp::EqCurve::range * 1.1f, fp::EqCurve::range * 1.1f, (float) eqc.actualDb (fp::EqCurve::freqAt (t)));
                const Point<float> pt (plot.getX() + t * plot.getWidth(), plot.getCentreY() - db / fp::EqCurve::range * plot.getHeight() * 0.5f);
                if (i == 0) real.startNewSubPath (pt); else real.lineTo (pt);
            }
            g.setColour (bal::strip (3).withAlpha (chalkA));
            g.strokePath (real, PathStrokeType (1.6f * k));
        }
        {
            // (one line only here: the thin riders of the grain shape would read as other bands)
            Path drawn;
            for (int i = 0; i <= 96; ++i)
            {
                const float t = (float) i / 96.0f;
                const Point<float> pt (plot.getX() + t * plot.getWidth(), plot.getBottom() - eqc.at (t) * plot.getHeight());
                if (i == 0) drawn.startNewSubPath (pt); else drawn.lineTo (pt);
            }
            g.setColour (bal::strip (4).withAlpha (chalkA));
            g.strokePath (drawn, PathStrokeType (jmax (1.2f, 2.2f * k)));
        }

        // where the pen is: frequency and gain
        if (hovering && e > 0.99f && plot.expanded (6.0f).contains (hover))
        {
            const double f = fp::EqCurve::freqAt ((hover.x - plot.getX()) / plot.getWidth());
            const float db = (plot.getCentreY() - hover.y) / (plot.getHeight() * 0.5f) * fp::EqCurve::range;
            const String txt = (f >= 1000.0 ? String (f / 1000.0, 1) + " khz" : String (roundToInt (f)) + " hz") + "   "
                             + (db >= 0.0f ? "+" : "") + String (jlimit (-fp::EqCurve::range, fp::EqCurve::range, db), 1) + " db";
            bal::label (g, txt, Rectangle<float> (170.0f * k, 16.0f * k).withCentre (hover.translated (0.0f, -26.0f * k)), inkC, 10.0f * k, Justification::centred, true);
        }
    }

    // the buttons
    if (e > 0.3f)
    {
        const bool using_ = (int) owner.proc.apvts.getRawParameterValue (ids::grShape)->load() == 6;
        for (int i = 0; i < 5; ++i)
        {
            const Rectangle<float> r { b.getX() + (56.0f + 108.0f * (float) i) * k, b.getY() + 284.0f * k, 96.0f * k, 26.0f * k };
            const bool over = hovering == false && r.contains (hover);
            bal::chip (g, r, page == 0 ? chalkButtons[i] : eqButtons[i], false, over, bal::strip (5), inkC.withAlpha (e), 11.0f * k);
        }
        if (page == 0)
            bal::label (g, using_ ? "the grains are using this shape" : "draw on it to use it for the grains",
                        at (36, 316, 568, 18), inkC.withAlpha (0.8f * e), 10.0f * k);
        else
        {
            const bool flat = eqc.isFlat();
            bal::label (g, flat ? "flat: draw above the line to boost, below to cut" : "the whole output follows this curve (blue: what you hear)",
                        at (36, 316, 568, 18), inkC.withAlpha (0.8f * e), 10.0f * k);
        }
    }
}

void Chalkboard::drawAt (Point<float> p)
{
    const auto a = plotArea();
    if (page == 1)
    {
        auto& c = owner.proc.eq;
        const int N = fp::EqCurve::N;
        const int idx = jlimit (0, N - 1, roundToInt ((p.x - a.getX()) / a.getWidth() * (float) (N - 1)));
        float h = jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
        if (std::abs (h - 0.5f) < 0.012f) h = 0.5f;   // the 0 dB line is a little sticky, so you can draw back to flat
        if (lastIndex < 0) c.set (idx, h);
        else
        {
            const int lo = jmin (idx, lastIndex), hi = jmax (idx, lastIndex);
            for (int i = lo; i <= hi; ++i)
            {
                const float u = hi == lo ? 1.0f : (float) (i - lastIndex) / (float) (idx - lastIndex);
                c.set (i, lastHeight + (h - lastHeight) * u);
            }
        }
        lastIndex = idx; lastHeight = h;
        c.fit();
        repaint();
        return;
    }
    auto& c = owner.proc.chalk;
    const int idx = jlimit (0, fp::ChalkShape::N - 1, roundToInt ((p.x - a.getX()) / a.getWidth() * (float) (fp::ChalkShape::N - 1)));
    const float h = jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
    if (lastIndex < 0) c.set (idx, h);
    else
    {
        const int lo = jmin (idx, lastIndex), hi = jmax (idx, lastIndex);
        for (int i = lo; i <= hi; ++i)
        {
            const float u = hi == lo ? 1.0f : (float) (i - lastIndex) / (float) (idx - lastIndex);
            c.set (i, lastHeight + (h - lastHeight) * u);
        }
    }
    lastIndex = idx; lastHeight = h;
    c.updatePower();
    if (auto* prm = owner.proc.apvts.getParameter (ids::grShape))
        if (roundToInt (owner.proc.apvts.getRawParameterValue (ids::grShape)->load()) != 6)
            prm->setValueNotifyingHost (prm->convertTo0to1 (6.0f));
    repaint();
}

void Chalkboard::mouseDown (const MouseEvent& e)
{
    owner.noteActivity();
    const auto p = e.position;
    if (anim < 0.99f) return;
    if (! boardArea().contains (p) && ! plotArea().contains (p)) { putBack(); return; }
    for (int i = 0; i < 2; ++i)
        if (tabArea (i).contains (p)) { setPage (i); return; }
    if (page == 1)
        for (int i = 0; i < 5; ++i)
            if (buttonArea (i).contains (p)) { eqButton (i); return; }
    for (int i = 0; i < 5; ++i)
        if (buttonArea (i).contains (p))
        {
            auto& c = owner.proc.chalk;
            const int N = fp::ChalkShape::N;
            Random rr;
            std::vector<float> v ((size_t) N);
            for (int k = 0; k < N; ++k) v[(size_t) k] = c.at ((float) k / (float) (N - 1));
            for (int k = 0; k < N; ++k)
            {
                const float t = (float) k / (float) (N - 1);
                if (i == 0) v[(size_t) k] = std::sin (MathConstants<float>::pi * t);
                if (i == 1) v[(size_t) k] = std::exp (-std::pow ((t - 0.5f) / 0.18f, 2.0f));
                if (i == 2) v[(size_t) k] = (k % 8 < 2 ? 1.0f : 0.25f) * std::sin (MathConstants<float>::pi * t);
                if (i == 4) v[(size_t) k] = 0.0f;
            }
            if (i == 3)
                for (int pass = 0; pass < 3; ++pass)
                    for (int k = 1; k < N - 1; ++k) v[(size_t) k] = 0.25f * v[(size_t) k - 1] + 0.5f * v[(size_t) k] + 0.25f * v[(size_t) k + 1];
            for (int k = 0; k < N; ++k) c.set (k, v[(size_t) k]);
            c.updatePower();
            if (i != 4)
                if (auto* prm = owner.proc.apvts.getParameter (ids::grShape)) prm->setValueNotifyingHost (prm->convertTo0to1 (6.0f));
            repaint();
            return;
        }
    if (plotArea().expanded (8.0f).contains (p)) { drawing = true; lastIndex = -1; drawAt (p); }
}

// the EQ's ready-made curves (in dB, over 0..1 of the board), and smoothing what's drawn
void Chalkboard::eqButton (int i)
{
    auto& c = owner.proc.eq;
    const int N = fp::EqCurve::N;
    std::vector<float> v ((size_t) N);
    for (int k = 0; k < N; ++k) v[(size_t) k] = c.at ((float) k / (float) (N - 1));
    auto bump = [] (float t, float centre, float width) { return std::exp (-std::pow ((t - centre) / width, 2.0f)); };
    for (int k = 0; k < N; ++k)
    {
        const float t = (float) k / (float) (N - 1);
        float db = 0.0f;
        if (i == 1) db = 8.0f * bump (t, 0.1f, 0.22f) - 7.0f * bump (t, 1.0f, 0.25f);    // warm: fuller lows, softer top
        if (i == 2) db = 9.0f * bump (t, 1.0f, 0.3f) - 3.0f * bump (t, 0.05f, 0.15f);    // bright: air on top
        if (i == 3) db = 10.0f * bump (t, 0.0f, 0.22f) + 10.0f * bump (t, 1.0f, 0.22f) - 5.0f * bump (t, 0.5f, 0.2f);   // smile
        if (i != 4) v[(size_t) k] = 0.5f + db / (2.0f * fp::EqCurve::range);
    }
    if (i == 4)
        for (int pass = 0; pass < 3; ++pass)
            for (int k = 1; k < N - 1; ++k) v[(size_t) k] = 0.25f * v[(size_t) k - 1] + 0.5f * v[(size_t) k] + 0.25f * v[(size_t) k + 1];
    for (int k = 0; k < N; ++k) c.set (k, v[(size_t) k]);
    c.fit();
    repaint();
}

void Chalkboard::mouseDrag (const MouseEvent& e) { mouseMove (e); if (drawing) drawAt (e.position); }
void Chalkboard::mouseUp (const MouseEvent&)     { drawing = false; lastIndex = -1; }

void Chalkboard::mouseMove (const MouseEvent& e)
{
    hover = e.position;
    const bool over = anim > 0.99f && plotArea().expanded (14.0f).contains (hover);
    if (over != hovering) setMouseCursor (over ? MouseCursor::NoCursor : MouseCursor::NormalCursor);
    hovering = over;
    repaint();
}
void Chalkboard::mouseExit (const MouseEvent&) { hovering = false; setMouseCursor (MouseCursor::NormalCursor); repaint(); }

//==============================================================================
// The help
namespace
{
    struct HelpChapter { const char* title; const char* body; };
    const HelpChapter helpChapters[] = {
        { "welcome",
          "Every balloon in the shop is a control. How far the colour has risen in it shows its value, and it grows a little as it fills.\n\n"
          "In short: drag to move, scroll to fill, shift-drag for fine changes, double-click to reset, click a caught sound to type its pitch, drag the pin to pop them.\n\n"
          "Drag a balloon to move it, and let go to watch it float back up on its thread. Scroll or swipe over it to fill it or let some out. "
          "Hold shift and drag up or down to set it by hand (hold alt as well for finer steps). Double-click to put it back how it started.\n\n"
          "Balloons with a setting written on them, like shape or octave, step to the next one when you click. Switches like freeze turn on and off with a click: one that is off is only its edges.\n\n"
          "The folded strips hold each bunch. Drag a strip to move its whole bunch. The next page shows which colour belongs to which effect." },
        { "colours",
          "Each bunch has its own colour, and so does its strip." },
        { "the effects",
          "Your sound runs through four effects in a row.\n\n"
          "First the spectral smear (the sky-blue balloons), which blurs the sound across time. Spectral freeze holds it still. "
          "Spectral tilt leans the smear from dark to bright: up lifts the highs and sinks the lows, down does the opposite, and the middle is flat. The smear stays about as loud either way.\n\n"
          "Then the pitch shifter (peach). Shift picks the interval, snapped to the scale chosen on the card at the top left, and pitch feedback sends it round again so it keeps climbing.\n\n"
          "Then the grains (green), and last a reverb (periwinkle) that keeps the low end clean. That's the order they start in: the order chapter says how to change it.\n\n"
          "Each effect has a mix balloon. When a mix balloon is empty, that effect is out of the way.\n\n"
          "Main feedback takes the end of the chain back to the start, so the whole shop can ring on by itself. It is tamed so it sings without blowing up." },
        { "grains",
          "The green bunch chops your sound into small grains. Grain size sets how long each one is and density sets how many overlap.\n\n"
          "Shape is the envelope of each grain. The last shape, chalk, is one you draw yourself on the board.\n\n"
          "Random pan scatters the grains across the stereo field and reverse flips some of them backwards. Octave moves them up or down, or let random octave choose for each grain.\n\n"
          "Position picks where in the last four seconds the grains read from, from right now to four seconds back, and position random lets them wander around that spot. "
          "Click freeze to hold the sound in place. Freeze fade sets how slowly it slides in and out of the freeze.\n\n"
          "These balloons also shape every frozen balloon you catch with the sound pump, even when the effects are bypassed." },
        { "the stutter",
          "The stutter is sold at the store. Buy it and it comes out as a coral box with four balloons, and takes its place in the row like the other effects; give it back (or pop one of its balloons) and the sound goes straight past it again.\n\n"
          "The coral bunch plays with the whole sound in time with your track (or at its own steady tempo when the track is stopped). "
          "On each sixteenth note it may grab the moment and do something with it.\n\n"
          "Stutter sets how often that happens. Bounce leans it from plain rolls and flams towards bouncing-ball repeats that speed up until they become a pitch, "
          "slowing ones that do the opposite, and frozen buzzing grains. Cut-up lets it pull its sound from other steps of the last bar, so the bar gets rearranged like a breakbeat. "
          "Sway holds back every second sixteenth, from a gentle swing to a stumble.\n\n"
          "These ideas come from Alexander Mesker's study of Autechre's music: the bouncing ball, the event types, restarting patterns and the swing table.\n\n"
          "Now and then a pluck from the pop pump bounces too." },
        { "the order",
          "The sound goes through the effects from left to right, from in to out: the spectral smear (sky blue), pitch (peach), grains (green), reverb (periwinkle) and a box for each effect from the store, the stutter (coral) among them.\n\n"
          "The order list waits just off the left edge of the window. Move the cursor to the left edge and it slides out, with the effects as folded strips from top (in) to bottom (out). Drag one up or down to move it: the others make room, and when you let go the strips in the room hop to their new places and the sound follows with a tiny dip so it never clicks. Shuffle, at the top, rolls a new order. The order is saved with your session.\n\n"
          "The loop (main feedback) and the caught sounds stay on the floor: the loop wraps round all the effects." },
        { "the pop pump",
          "Hold the pump on the left to blow up a balloon.\n\n"
          "While it fills, short plucked saws play on random notes of the scale, slowly and unevenly. Every pluck is folded and panned at random, "
          "and its attack is sometimes snappy and sometimes slow. The plucks go through the effects.\n\n"
          "Somewhere between 5 and 12 seconds the balloon pops, and every pop is different.\n\n"
          "The pop sounds switch at the top right, above help and reset, turns the hiss and the pops on or off. Right-click it to feed them into the effects instead of straight out, so the pops go through the chain with your sound." },
        { "catching sounds",
          "Hold the pump on the right to catch a sound in a balloon. It catches what comes out of the effects, or your plain input if you click the catch token beside it. "
          "The balloon starts muffled and opens up as it fills. Let go and it floats away, playing what it caught as frozen grains.\n\n"
          "You can keep six at once, and the deep green frozen volume balloon sets how loud they all are. The mark by the pump shows the pitch of the next one. Click it, or click any caught balloon, to type an interval like +7 or -5, then press return. "
          "Scrolling over a caught balloon retunes it along the scale.\n\n"
          "To pop one, double-click it, or drag the pin from its stand onto it." },
        { "helium",
          "Click the helium canister to fill the room with helium. It adds a gentle highpass to the output, and the balloons stretch and float higher.\n\n"
          "Slide the canister along its rule to choose where the frozen balloons go. To the right they come straight out. "
          "To the left they go back through the effects, so the pitch shifter and the reverb play with them too." },
        { "weather",
          "The weather switches are one row along the top of the window, all the same size: cloudy, rain, snow, heatwave, fog, wind, hail, aurora, twister and auto weather. Click one to turn it on and again to turn it off. One that is on sits on a filled parallelogram. Rain and clouds together make a storm.\n\n"
          "Rain eases a gentle lowpass onto the output and wets the balloons.\n\n"
          "When it's cloudy, clouds drift through the bunches, and any balloon inside one slowly wanders to new values, showing where it has got to. Clouds bring thunder too, which pulls the rain's lowpass down in slow rolls.\n\n"
          "In a storm, lightning strikes now and then and drops a random effect from the store into the room for a minute or so. If you like it, pick it in the store and it stays.\n\n"
          "Snow freezes everything over the fade-in time: the sound slows until it stops completely and holds, and the balloons ice over and hang still. It stays frozen until you click snow again, then thaws over the fade-out time. Right-click snow to set both times, anywhere from 5 seconds to 5 minutes: drag along a bar or scroll on it, and double-click puts it back to 5 seconds. The solid by the pump ices over after a minute out in it, and thaws once the snow has gone.\n\n"
          "Snow and a heatwave never come together: turning one on sends the other away.\n\n"
          "A heatwave melts the sound into a slow smear over its fade-in time (right-click heatwave to set its fade in and fade out, like the snow's): whatever you play is spread across many seconds, and the held sound keeps drifting towards whatever comes next, like a stretched recording that never quite stops. The top end softens and swells as the hot air wobbles, gently, nothing like the rain. The solid sinks flat in it.\n\n"
          "With auto weather on, the sky looks after itself. Every few minutes the weather turns, the way it does outside: clear skies cloud over, the cloud brings rain or a storm or snow, a long hot spell might end in a summer storm. Each spell lasts somewhere between a minute and twelve, and the name under the auto switch shows what the sky is doing. You can still flip the switches yourself; auto carries on from there." },
        { "more weather",
          "Fog, wind, hail, aurora and twister come after the heatwave in the weather row.\n\n"
          "Fog blurs every attack. The sound goes through six short echoes too close together to hear apart, so a hit loses its edge without growing a tail, and the highs sink. Pale bars drift through the shop.\n\n"
          "Wind comes in gusts. The sound is read through a delay that keeps drifting, so its pitch wavers a few cents sharp and flat, up to about a fifth of a semitone in a strong gust. Each gust swells the sound a little and leans it to one side, and the balloons lean over with it. The wind blows the fog away: turning one on sends the other off.\n\n"
          "Hail throws hailstones. Each one is a tiny slice of the last moment of your sound, hit hard and then bouncing, quicker and quieter, like a ball dropped on a table. Every stone lands somewhere else between left and right.\n\n"
          "The aurora plays your sound an octave up into a long reverb. It hangs in four curtains of thin lines across the stereo field, and each curtain swells and fades by itself.\n\n"
          "Twister starts a whirlwind that lasts between 10 and 20 seconds. The pitch climbs a fifth and falls back while the sound whirls from side to side, faster and faster, and the balloons are swept round with it. With wind and clouds both on, one comes by itself every few minutes.\n\n"
          "The rainbow has no switch. When the rain stops under a clear sky (no clouds, no snow) after at least 20 seconds of rain, a rainbow stands over the shop for about 25 seconds. It is your sound in seven coloured lines, from red, the lowest, on the left to violet, the highest, on the right, each colour arriving a little after the one before.\n\n"
          "Auto weather knows all of these. Fog comes after cloud, rain or snow. Wind can blow up a storm. Hail falls out of a storm, and an aurora follows a clear spell." },
        { "the sonic boom",
          "The dart with seven lines behind it, under help and reset, is the sonic boom. Click it and a white dart dives in from the sky, faster and faster, seven coloured lines streaming out behind it, until it breaks the sound barrier: a boom and a rainbow at the same moment.\n\n"
          "On the way in your sound is squeezed up in pitch, the way a sound rushing towards you would be, with a rush of air under it. The boom has the real shape of a sonic boom, a double crack. "
          "Then the rainbow: your sound split into seven colours, red the lowest and violet the highest. They arrive one after another, so they sweep up from the lows to the highs, and each one keeps ringing for the rainbow's length while the rainbow fades from the sky.\n\n"
          "The shockwave knocks the balloons back, and the ones nearest it are nudged to new values. The solid by the pump jumps, every face lit.\n\n"
          "Fling a balloon hard enough and it breaks the barrier itself: a white cone grows behind it as you get close. Right-click the dart for what else sets one off, such as a hit in your sound or the rain stopping after a storm. "
          "The same menu sets how loud it is and how long the rainbow lasts."
          "\n\nIn stereo the colours fan out as the ring opens, red to the left and violet to the right." },
        { "the store",
          "Click store at the top to open the store. Pick an effect and a little box drops out with four balloons tied to it: its two controls, then its level and its width. "
          "The box takes a place among the other effects, and the order list moves it about like the rest. Width spreads the effect from mono in the middle to wide stereo.\n\n"
          "Pick a bought effect again to give it back, or pop one of its balloons with the pin. Pop everything sends them all back at once.\n\n"
          "The tuned effects in the store follow the scale as well. The ring mod multiplies the sound by a tone, and with follow turned up the tone rises as the sound gets louder." },
        { "the sliders",
          "Click sliders, under the boom's dart at the top right, to open the sliders page. It shows every control in the shop again as a slider that stays where it is, for when you'd rather not chase a balloon. There is one row for each effect, on a strip in its balloons' colour.\n\n"
          "Each one is the suite's slider: a thin rule, a slanted fill and a white edge, with its value underneath. Drag it sideways or up and down (hold shift for fine changes), or scroll over it. Click a switch to flip it. Click a setting like shape to step to the next one, or right-click for the one before. Double-click puts a slider back how it started. Point at one to read its full name and value.\n\n"
          "The sliders and the balloons are the same controls. Move one and the other follows, and the balloons still work just as they did.\n\n"
          "When an effect's balloons come into the room, bought at the store or dropped by the lightning, its row of sliders comes in here. When they pop, the row goes. With many effects bought, short rows share a line and the page scrolls.\n\n"
          "Click close or press escape to go back." },
        { "the board",
          "Click the little sheet standing on the floor to bring the board forward. It has two pages, named along the top.\n\n"
          "On grain shape, draw across the board to make the life of one grain, from start on the left to end on the right, with loud at the top. Drawing switches the grains to your shape.\n\n"
          "On master eq, draw the tone of the whole output. Above the dashed line boosts and below it cuts, with the lows on the left. "
          "The blue line shows what you actually hear. The buttons underneath give you ready-made curves, smooth what you drew, or set it flat again.\n\n"
          "On master eq the board goes from a deep cut at the bottom to a big boost at the top, 24 db each way.\n\n"
          "Click anywhere outside the board to put it away." },
        { "the card",
          "The card holds the presets and the scale. It waits under the name at the top left of the window: move the cursor up there and it slides down. Every preset changes the wire figure behind the shop as well as the sound.\n\n"
          "The scale decides which notes the pitch shifter lands on. Everything tuned in the shop follows it.\n\n"
          "Reset, under help in the top right corner, brings everything back to normal: the first preset, a flat eq, the plain grain shape and the effects in their first order. "
          "Caught sounds stay where they are.\n\n"
          "The switch underneath the card bypasses the effects, the store's ones included. Only your input and the frozen balloons are left, and the grain balloons still shape them." },
        { "the solid",
          "A wire solid stands by the pop pump, turning slowly, one face lit.\n\n"
          "Pops make it jump. When it rains it puts a canopy up (unless it's a heatwave too: then it lies flat in the puddle), and helium lights every face.\n\n"
          "Leave the shop alone for a while and it settles down to sleep.\n\n"
          "Every so often, about once an hour, a visitor comes in at the right: a white tetrahedron. It strikes balloons with thin lines from its point, each one jumping to a new value, "
          "then sets everything it can reach at random with one flash and leaves. Some visits it also plays a strange synthesised noise into the shop's input, which goes through all the effects, with a chirp for every strike. "
          "Sometimes it strikes a few effects from the store into the room too, and those stay. It leaves the bypass and the volume alone, and never touches the snow, the heat or auto weather. "
          "If you don't like what it did, reset puts everything back to normal." },
    };
}

Library::Library (BalloonEditor& e) : owner (e)
{
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (true);
}

int Library::numChapters() const noexcept { return (int) std::size (helpChapters); }
// (for the manual: the same chapters, read out)
int manualChapterCount() { return (int) std::size (helpChapters); }
const char* manualChapterTitle (int i) { return helpChapters[i].title; }
const char* manualChapterBody (int i) { return helpChapters[i].body; }
void Library::showChapter (int i)
{
    i = jlimit (0, numChapters() - 1, i);
    if (i == chapter) return;
    flipFrom = chapter;
    flipDir = i > chapter ? 1 : -1;
    flipT = 0.0f;
    chapter = i;
    repaint();
}

void Library::open()
{
    setBounds (0, 0, BalloonEditor::designW, BalloonEditor::designH);
    setVisible (true);
    toFront (true);
    animTarget = 1.0f;
}

void Library::close() { animTarget = 0.0f; }

bool Library::step (float dt)
{
    t += dt;
    if (flipT < 1.0f) flipT = jmin (1.0f, flipT + dt / 0.6f);   // a page takes 0.6 s to turn
    if (anim != animTarget)
    {
        anim = animTarget > anim ? jmin (animTarget, anim + dt * 3.5f) : jmax (animTarget, anim - dt * 3.5f);
        if (anim <= 0.0f) setVisible (false);
    }
    if (isVisible()) repaint();
    return isVisible();
}

Rectangle<float> Library::bookArea() const
{
    const float e = ease (anim);
    return Rectangle<float> (744.0f, 486.0f).withCentre ({ 618.0f, 312.0f + 30.0f * (1.0f - e) }).withSizeKeepingCentre (744.0f * (0.92f + 0.08f * e), 486.0f * (0.92f + 0.08f * e));
}
Rectangle<float> Library::leftPage() const  { auto b = bookArea().reduced (14.0f); return b.withWidth (b.getWidth() * 0.5f); }
Rectangle<float> Library::rightPage() const { auto b = bookArea().reduced (14.0f); return b.withTrimmedLeft (b.getWidth() * 0.5f); }
Rectangle<float> Library::chapterRow (int i) const
{
    const auto p = leftPage().reduced (34.0f, 0.0f);
    const float rowH = jmin (29.0f, (p.getHeight() - 108.0f - 14.0f) / (float) numChapters());
    return { p.getX(), p.getY() + 108.0f + rowH * (float) i, p.getWidth(), rowH - 2.0f };
}
Rectangle<float> Library::closeTab() const  { const auto b = bookArea(); return { b.getRight() - 178.0f, b.getY() - 22.0f, 150.0f, 34.0f }; }
Rectangle<float> Library::pageButton (int dir) const
{
    const auto p = rightPage().reduced (30.0f, 18.0f);
    return dir < 0 ? Rectangle<float> (p.getX(), p.getBottom() - 24.0f, 80.0f, 24.0f) : Rectangle<float> (p.getRight() - 80.0f, p.getBottom() - 24.0f, 80.0f, 24.0f);
}

// The help: a sheet of paper over the room. Its chapters are a track list down the left (the one being read on
// a folded strip), the chapter itself is on the right; a wire solid turns slowly in the corner.
void Library::paint (Graphics& g)
{
    const float e = ease (anim);
    const auto full = getLocalBounds().toFloat();
    if (e <= 0.001f) return;
    g.beginTransparencyLayer (e);

    g.fillAll (bal::ground());
    // the solid in the corner, reading along
    bal::drawSolid (g, 2, { 118.0f, (float) BalloonEditor::designH - 120.0f }, 62.0f, t * 0.25f, 0.5f, bal::line(), 1.0f, (int) (t * 0.4f), 0.3f);
    bal::title (g, bal::product().name, Rectangle<float> (28.0f, 20.0f, 220.0f, 30.0f), bal::ink(), 24.0f, Justification::centredLeft, true);
    bal::label (g, "help", Rectangle<float> (28.0f, 50.0f, 220.0f, 14.0f), bal::ink(), 11.0f, Justification::centredLeft);

    const auto b = bookArea();
    g.setColour (bal::paper());
    g.fillRect (b);
    g.setColour (bal::paperInk());
    g.drawRect (b, 1.0f);
    g.setColour (bal::paperLine());
    g.fillRect (rightPage().getX(), b.getY() + 22.0f, 1.0f, b.getHeight() - 44.0f);

    drawContents (g);
    // (a new chapter comes up through the paper in the time a page took to turn)
    if (flipT < 1.0f)
    {
        g.beginTransparencyLayer (flipT * flipT * (3.0f - 2.0f * flipT));
        drawChapter (g, chapter);
        g.endTransparencyLayer();
    }
    else drawChapter (g, chapter);

    // back to the shop
    const bool overClose = closeTab().contains (getMouseXYRelative().toFloat());
    bal::chip (g, closeTab().reduced (20.0f, 6.0f), "back", overClose, overClose, bal::line(), bal::ink(), 11.0f);
    g.endTransparencyLayer();
}

void Library::drawParchment (Graphics& g, Rectangle<float> pg) const
{
    g.setColour (bal::paper());
    g.fillRect (pg);
}

void Library::drawContents (Graphics& g) const
{
    // left page: the chapters, as a track list
    const auto lp = leftPage().reduced (34.0f, 26.0f);
    bal::title (g, "help", lp.withHeight (40.0f), bal::paperInk(), 26.0f, Justification::centredLeft, true);
    bal::label (g, String ("how to play in ") + bal::product().name, lp.withTrimmedTop (44.0f).withHeight (18.0f), bal::paperInk().withAlpha (0.7f), 10.5f, Justification::centredLeft);
    for (int i = 0; i < numChapters(); ++i)
    {
        const auto r = chapterRow (i);
        const bool on = i == chapter;
        if (on) bal::foldStrip (g, r.reduced (0.0f, 2.0f).withTrimmedRight (20.0f), bal::strip (i), {}, false, 0.0f);
        else if (i == hoverRow)
        {
            g.setColour (bal::paperInk().withAlpha (0.5f));
            g.fillRect (r.getX() + 8.0f, r.getBottom() - 3.0f, r.getWidth() - 40.0f, 1.0f);
        }
        const Colour c = on ? bal::ui::inkFor (bal::strip (i)) : bal::paperInk();
        bal::label (g, String (i + 1).paddedLeft ('0', 2), r.withTrimmedLeft (14.0f).withWidth (24.0f), c.withAlpha (on ? 1.0f : 0.6f), jmin (10.5f, r.getHeight() * 0.5f), Justification::centredLeft);
        bal::label (g, helpChapters[i].title, r.withTrimmedLeft (44.0f), c, jmin (11.5f, r.getHeight() * 0.55f), Justification::centredLeft, on);
    }
}

void Library::drawChapter (Graphics& g, int which) const
{
    // right page: the chapter
    const auto rp = rightPage().reduced (30.0f, 18.0f);
    const Colour inkC = bal::paperInk();
    bal::title (g, helpChapters[which].title, rp.withHeight (40.0f), inkC, 22.0f, Justification::centredLeft, true);
    g.setColour (bal::strip (which));
    g.fillPath (bal::ui::skewRect ({ rp.getX(), rp.getY() + 44.0f, 110.0f, 5.0f }));
    {
        const auto textArea = rp.withTrimmedTop (60.0f).withTrimmedBottom (34.0f);
        TextLayout layout;
        for (float size = 13.0f; size >= 9.5f; size -= 0.5f)   // as big as fits
        {
            AttributedString as;
            as.setLineSpacing (size * 0.3f);
            as.append (helpChapters[which].body, bal::sans (size), inkC);
            layout.createLayout (as, textArea.getWidth());
            if (layout.getHeight() <= textArea.getHeight()) break;
        }
        layout.draw (g, textArea);
        if (String (helpChapters[which].title) == "colours")
        {
            struct Key { int hue; const char* code; const char* name; };
            const Key keys[] = { { balloonBlue, "sm1", "the spectral smear" }, { balloonYellow, "pt2", "the pitch shifter" }, { balloonMint, "gr3", "the grains" },
                                 { balloonLilac, "rv4", "the reverb" }, { balloonOrange, "st5", "the stutter (from the store)" }, { balloonPink, "lp0", "the loop (main feedback)" },
                                 { balloonTeal, "ct6", "caught sounds" } };
            float y = textArea.getY() + layout.getHeight() + 18.0f;
            for (auto& k : keys)
            {
                bal::tinyBalloon (g, { textArea.getX() + 12.0f, y + 22.0f }, 7.0f, bal::hue (k.hue));
                bal::foldStrip (g, { textArea.getX() + 34.0f, y + 4.0f, 46.0f, 14.0f }, bal::hue (k.hue), k.code, false, 0.0f, 10.0f);
                bal::label (g, k.name, Rectangle<float> (textArea.getX() + 100.0f, y, textArea.getWidth() - 100.0f, 21.0f), inkC, 11.0f, Justification::centredLeft);
                y += 31.0f;
            }
        }
    }
    if (which > 0)                 bal::label (g, "< back", pageButton (-1), inkC, 11.0f, Justification::centredLeft, true);
    if (which < numChapters() - 1) bal::label (g, "next >", pageButton (1), inkC, 11.0f, Justification::centredRight, true);
    bal::label (g, String (which + 1) + " / " + String (numChapters()), rp.withTop (rp.getBottom() - 24.0f), inkC.withAlpha (0.6f), 10.0f);
}

void Library::mouseDown (const MouseEvent& e)
{
    owner.noteActivity();
    if (anim < 0.5f) return;
    const auto p = e.position;
    if (closeTab().contains (p) || ! bookArea().expanded (8.0f, 30.0f).contains (p)) { close(); return; }
    for (int i = 0; i < numChapters(); ++i)
        if (chapterRow (i).contains (p)) { showChapter (i); return; }
    if (chapter > 0 && pageButton (-1).contains (p)) { showChapter (chapter - 1); return; }
    if (chapter < numChapters() - 1 && pageButton (1).contains (p)) { showChapter (chapter + 1); return; }
}

void Library::mouseMove (const MouseEvent& e)
{
    int row = -1;
    for (int i = 0; i < numChapters(); ++i) if (chapterRow (i).contains (e.position)) row = i;
    const bool overButton = row >= 0 || closeTab().contains (e.position)
                         || (chapter > 0 && pageButton (-1).contains (e.position)) || (chapter < numChapters() - 1 && pageButton (1).contains (e.position));
    setMouseCursor (overButton ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    if (row != hoverRow) { hoverRow = row; repaint(); }
}

bool Library::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    if (k == KeyPress::leftKey || k == KeyPress::upKey)    { showChapter (chapter - 1); return true; }
    if (k == KeyPress::rightKey || k == KeyPress::downKey) { showChapter (chapter + 1); return true; }
    return false;
}

// the title, in the show's own lettering (the "Magic is Friendship" fan font, built in): a dark pink
// edge, a white rim, a glossy pink fill and a shine
// the name, where every faltung window has it: top left, lowercase (the card slides down over it)
void BalloonEditor::drawTitle (Graphics& g) const
{
    const float a = 1.0f - jlimit (0.0f, 1.0f, cardShown * 1.4f);
    if (a <= 0.01f) return;
    bal::title (g, bal::product().name, Rectangle<float> (28.0f, 16.0f, 240.0f, 30.0f), bal::ink().withAlpha (a), 24.0f, Justification::centredLeft, true);
    bal::label (g, bal::product().blurb, Rectangle<float> (28.0f, 47.0f, 250.0f, 13.0f), bal::ink().withAlpha (0.9f * a), 9.5f, Justification::centredLeft);
    bal::label (g, "v   presets, scale, bypass", Rectangle<float> (28.0f, 62.0f, 250.0f, 13.0f), bal::ink().withAlpha (0.55f * a), 9.5f, Justification::centredLeft);
}

// the reset button: a little circular arrow
// the buttons in the top right corner: the house mark and a name
void BalloonEditor::drawResetButton (Graphics& g) const
{
    const auto r = resetButton;
    const bool hover = r.contains (content.getMouseXYRelative().toFloat());
    const auto m = Rectangle<float> (r.getX() + 6.0f, r.getCentreY() - 5.0f, 18.0f, 10.0f);
    g.setColour (bal::ink().withAlpha (hover ? 1.0f : 0.8f));
    g.strokePath (bal::ui::skewRect (m), PathStrokeType (hover ? 1.4f : 1.0f));
    g.drawLine (m.getX() + 4.0f, m.getBottom() - 2.5f, m.getRight() + 1.5f, m.getY() + 2.5f, 1.0f);
    bal::label (g, "reset", r.withTrimmedLeft (34.0f), bal::ink(), 10.5f, Justification::centredLeft);
}

void BalloonEditor::resetEverything()
{
    proc.resetToNormal();
    layoutShelf (shelfItems());
    confetti (resetButton.getCentre(), 30);
    for (auto* f : floaters) f->squashVel += 3.0f;
    if (chalkboard != nullptr) chalkboard->repaint();
}

//==============================================================================
// Trixie
// The visitor
Rectangle<float> BalloonEditor::trixieArea() const
{
    const float h = 230.0f, w = 190.0f;
    const auto& v = trixieVisit;
    // in from the right (1 s), on the spot while it works, out again to the right
    float x = 720.0f;
    if (v.t < 1.0f) x = 720.0f + ((float) designW + 140.0f - 720.0f) * std::pow (1.0f - v.t, 2.0f);
    if (v.t > 5.8f) x = 720.0f + ((float) designW + 140.0f - 720.0f) * fp::smoothstep ((v.t - 5.8f) / 1.2f);
    const float hop = v.t < 1.0f || v.t > 5.8f ? std::abs (std::sin (v.t * 9.0f)) * 10.0f : 0.0f;
    return { x - w * 0.5f, (float) designH - 4.0f - h - hop, w, h };
}

Point<float> BalloonEditor::trixieAmulet() const
{
    const auto a = trixieArea();
    return { a.getX() + a.getWidth() * 0.37f, a.getY() + a.getHeight() * 0.585f };
}

Path BalloonEditor::makeBolt (Point<float> from, Point<float> to, float jag)
{
    Path p;
    p.startNewSubPath (from);
    const auto d = to - from;
    const float len = jmax (1.0f, d.getDistanceFromOrigin());
    const Point<float> perp (-d.y / len, d.x / len);
    const int segs = jmax (4, (int) (len / 40.0f));
    for (int i = 1; i <= segs; ++i)
    {
        const float u = (float) i / (float) segs;
        p.lineTo (from + d * u + perp * (i == segs ? 0.0f : (rnd.nextFloat() - 0.5f) * jag));
    }
    return p;
}

void BalloonEditor::startTrixie()
{
    if (trixieVisit.on) return;
    trixieVisit = {};
    trixieVisit.on = true;
    trixieVisit.nextZap = 1.2f;
    // sometimes she plays something strange into the input while she's here, and sometimes she doesn't
    trixieVisit.noisy = rnd.nextBool();
    trixieVisit.gifts = rnd.nextInt (4);   // and she may bring a few effects from the store along
    if (trixieVisit.noisy) proc.trixieNoise.request ((uint32_t) rnd.nextInt(), 6.6f);
}

void BalloonEditor::trixieStep (float dt)
{
    // once a minute, a small chance she drops by (not while you're reading or drawing)
    if (! trixieVisit.on && clock >= nextTrixieRoll)
    {
        nextTrixieRoll = clock + 60.0;
        const bool busy = (library != nullptr && library->isVisible()) || (chalkboard != nullptr && chalkboard->isVisible());
        if (! busy && rnd.nextFloat() < trixieChancePerMinute) startTrixie();
    }
    auto& v = trixieVisit;
    v.boltAge += dt;
    v.flash = jmax (0.0f, v.flash - dt * 1.5f);
    if (! v.on) return;
    v.t += dt;
    noteActivity();

    // zap! a balloon she can reach jumps to a random value
    if (v.t >= v.nextZap && v.t < 5.0f)
    {
        v.nextZap += 0.3f;
        // ...or, now and then, an effect from the store appears on the shelf where the bolt lands
        if (v.gifts > 0 && (rnd.nextFloat() < 0.3f || v.t > 4.4f))
        {
            std::vector<int> notYet;
            for (int e = 0; e < kStoreItems; ++e) if (! storeShown[e]) notYet.push_back (e);
            if (! notYet.empty())
            {
                const int e = notYet[(size_t) rnd.nextInt ((int) notYet.size())];
                trixieGiftAt = { shelfLeft + rnd.nextFloat() * (shelfRight - shelfLeft), shelfY - 60.0f };
                v.bolt = makeBolt (trixieAmulet(), trixieGiftAt, 34.0f);
                v.boltAge = 0.0f;
                trixieGifts |= 1u << e;
                setBought (e, true);
                if (v.noisy) proc.trixieNoise.zap();
                confetti (trixieGiftAt, 24);
                --v.gifts;
                ++v.zaps;
                return;
            }
            v.gifts = 0;
        }
        std::vector<Balloon*> targets;
        for (auto* f : floaters)
            if (auto* b = dynamic_cast<Balloon*> (f))
                if (b->alive && BalloonProcessor::trixieMayTouch (b->parameter().getParameterID())) targets.push_back (b);
        if (! targets.empty())
        {
            auto* b = targets[(size_t) rnd.nextInt ((int) targets.size())];
            v.bolt = makeBolt (trixieAmulet(), b->pos, 34.0f);
            v.boltAge = 0.0f;
            BalloonProcessor::randomise (b->parameter(), rnd);
            if (v.noisy) proc.trixieNoise.zap();
            b->squashVel += 6.0f;
            b->vel += { (rnd.nextFloat() - 0.5f) * 500.0f, -300.0f };
            for (int i = 0; i < 14; ++i)
            {
                static const Colour stars[] = { Colour (0xff7fe8ff), Colour (0xfff7e36b), Colour (0xffb48cff), Colours::white };
                particles.push_back ({ b->pos, { (rnd.nextFloat() - 0.5f) * 300.0f, -rnd.nextFloat() * 280.0f },
                                       rnd.nextFloat() * 6.0f, (rnd.nextFloat() - 0.5f) * 14.0f, 1.0f, stars[rnd.nextInt (4)], 0 });
            }
            ++v.zaps;
        }
    }
    // and the grand finale: everything at once
    if (! v.finale && v.t >= 5.0f)
    {
        v.finale = true;
        v.flash = 1.0f;
        proc.trixieShuffle (rnd);
        scaredUntil = jmax (scaredUntil, clock + 1.2);
        for (auto* f : floaters) { f->squashVel += 5.0f; f->vel += { (rnd.nextFloat() - 0.5f) * 400.0f, -250.0f }; }
        confetti (trixieAmulet(), 60);
    }
    if (v.t >= 7.0f) v.on = false;
}

// The visitor: a white wire tetrahedron that comes in at the right, strikes balloons with thin lines from its
// point, flashes once and leaves.
void BalloonEditor::drawTrixie (Graphics& g) const
{
    const auto& v = trixieVisit;
    if (v.flash > 0.0f)
    {
        g.setColour (bal::white().withAlpha (0.45f * v.flash * v.flash));
        g.fillRect (Rectangle<float> (0, 0, (float) designW, (float) designH));
    }
    if (! v.on) return;
    const auto a = trixieArea();
    const auto am = trixieAmulet();
    const bool working = v.t > 1.0f && v.t < 5.4f;
    const float glow = working ? 0.6f + 0.4f * std::sin (v.t * 18.0f) : 0.0f;
    {
        const auto& s = bal::solid (0);
        const bal::View view { { a.getCentreX(), a.getY() + a.getHeight() * 0.62f }, 62.0f, 62.0f, v.t * 1.7f, 0.5f };
        bal::wireSolid (g, s, view, bal::white(), 1.6f);
        bal::face (g, bal::facePath (s, view, (int) (v.t * 3.0f)), 0.2f + 0.5f * glow, 2.0f);
        // where it stands
        g.setColour (bal::ink().withAlpha (0.5f));
        g.fillRect (Rectangle<float> (110.0f, 1.0f).withCentre ({ a.getCentreX(), (float) designH - 12.0f }));
        // the point it strikes from
        g.setColour (bal::white().withAlpha (0.4f + 0.6f * glow));
        g.fillPath (bal::ui::skewRect (Rectangle<float> (12.0f + 6.0f * glow, 7.0f + 3.0f * glow).withCentre (am)));
    }
    if (v.boltAge < 0.18f)
    {
        const float k = 1.0f - v.boltAge / 0.18f;
        g.setColour (bal::strip (7).withAlpha (0.8f * k));
        g.strokePath (v.bolt, PathStrokeType (3.0f, PathStrokeType::mitered));
        g.setColour (Colours::white.withAlpha (k));
        g.strokePath (v.bolt, PathStrokeType (1.2f, PathStrokeType::mitered));
    }
    // what it plays drifts up from it as marks
    if (v.noisy && v.t > 0.4f && v.t < 6.6f)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float u = std::fmod (v.t * 0.7f + (float) i * 0.21f, 1.0f);
            const Point<float> p0 (a.getX() + a.getWidth() * 0.18f, a.getY() + a.getHeight() * 0.42f);
            const Point<float> p (p0.x - 30.0f * u + 14.0f * std::sin (u * 9.0f + (float) i), p0.y - 120.0f * u);
            g.setColour (bal::strip (2 + i).withAlpha (1.0f - u));
            g.fillPath (bal::ui::skewRect (Rectangle<float> (12.0f, 6.0f).withCentre (p)));
        }
    }
    // what it is
    const String line = v.finale ? "everything, at once" : (v.noisy ? "a visitor, playing" : "a visitor");
    if (v.t > 0.6f && v.t < 6.4f)
    {
        auto tag = Rectangle<float> (160.0f, 18.0f).withCentre ({ a.getCentreX(), a.getY() + 30.0f });
        tag = tag.withX (jlimit (8.0f, (float) designW - 168.0f, tag.getX()));
        bal::label (g, line, tag, bal::white(), 10.5f, Justification::centred, true);
    }
}

// the way in: a little closed book, top right
void BalloonEditor::drawHelpButton (Graphics& g) const
{
    const auto r = helpButton;
    const bool hover = r.contains (content.getMouseXYRelative().toFloat());
    const auto m = Rectangle<float> (r.getX() + 6.0f, r.getCentreY() - 5.0f, 18.0f, 10.0f);
    g.setColour (bal::ink().withAlpha (hover ? 1.0f : 0.8f));
    g.strokePath (bal::ui::skewRect (m), PathStrokeType (hover ? 1.4f : 1.0f));
    bal::label (g, "?", m.translated (2.5f, 0.0f), bal::ink(), 9.0f, Justification::centred, true);
    bal::label (g, "help", r.withTrimmedLeft (34.0f), bal::ink(), 10.5f, Justification::centredLeft);
}

// the pop sounds switch, in the same style as help and reset: a little party blower, unrolled when they're on
// the pop sounds switch (right-click: feed them into the effects)
void BalloonEditor::drawPopsButton (Graphics& g) const
{
    const auto r = popsButton;
    const bool on = popsValue > 0.5f;
    const bool hover = r.contains (content.getMouseXYRelative().toFloat());
    bal::toggle (g, r.withTrimmedLeft (6.0f), "pop sounds", on, hover, bal::line(), bal::ink(), 10.5f);
}

//==============================================================================
// Presets: pictures, colours
// (every preset has the product's own colours; what changes with the preset is the solid drawn behind)
BalloonEditor::Theme BalloonEditor::themeOf (int)
{
    return { bal::ground(), bal::line(), bal::ink() };
}

void BalloonEditor::followPreset (float dt)
{
    const int idx = proc.currentPreset.load();
    if (idx != shownPreset)
    {
        const int bg = isPositiveAndBelow (idx, (int) presets::list().size()) ? presets::list()[(size_t) idx].background : -1;
        if (shownPreset == -2) { bgFrom = bgTo = bg; bgFade = 1.0f; themeFrom = themeTo = theme = themeOf (bg); }
        else { bgFrom = bgTo; themeFrom = theme; bgTo = bg; themeTo = themeOf (bg); bgFade = 0.0f; }
        shownPreset = idx;
        presetBox.setSelectedId (idx + 1, dontSendNotification);
    }
    if (bgFade < 1.0f)
    {
        bgFade = jmin (1.0f, bgFade + dt / 1.4f);   // a slow cross-fade
        const float u = ease (bgFade);
        theme = { themeFrom.deep.interpolatedWith (themeTo.deep, u), themeFrom.accent.interpolatedWith (themeTo.accent, u),
                  themeFrom.text.interpolatedWith (themeTo.text, u) };
    }
}

void BalloonEditor::applyTheme()
{
}

String BalloonEditor::pillText (const Pill& pl) const
{
    if (pl.continuous) return pl.name + ": " + pl.param->getCurrentValueAsText();
    if (auto* c = dynamic_cast<AudioParameterChoice*> (pl.param))
        return pl.name + ": " + c->choices[jlimit (0, c->choices.size() - 1, roundToInt (pl.value))];
    return pl.name + (pl.value > 0.5f ? ": on" : ": off");
}

void BalloonEditor::popPumpBalloon (Pump& pmp, bool sound)
{
    const auto sh = pumpBalloonShape (pmp);
    pop ({ sh.cx, sh.cy }, jmin (1.0f, pmp.size), pmp.hue);
    if (sound)
    {
        if (pmp.slot >= 0) proc.balloonFX.voice[pmp.slot].mode = 0;   // the capture is gone
        pmp.spent = true;
        pmp.slot = -1;
    }
    else
    {
        static const int hues[] = { balloonOrange, balloonPink, balloonBlue, balloonYellow, balloonLilac, balloonMint };
        pmp.hue = hues[(size_t) rnd.nextInt (6)];
    }
    pmp.holdTime = 0.0f; pmp.size = 0.0f; pmp.strain = 0.0f;
    pmp.popAt = 5.0f + rnd.nextFloat() * 7.0f;
}

void BalloonEditor::pinStep (float dt)
{
    if (pin == nullptr) return;
    if (! pin->dragging)
    {
        pin->pos += (pin->home - pin->pos) * jmin (1.0f, dt * 9.0f);   // back to the cushion
        pin->place();
        return;
    }
    const auto t = pin->tip();
    for (auto* f : floaters)
    {
        if (! f->alive || t.getDistanceFrom (f->pos) > f->collisionRadius() * 0.95f) continue;
        if (auto* sb = dynamic_cast<SoundBalloon*> (f))
        {
            proc.balloonFX.voice[sb->slot].mode = 0;    // pop: its frozen sound stops
            pop (sb->pos, sb->size, sb->hue);
            sb->alive = false;
            sb->setVisible (false);
            continue;
        }
        // an effect from the store pops too (with the rest of its bunch: the effect goes back on the shelf)
        int bought = -1;
        for (int e = 0; e < kStoreItems && bought < 0; ++e)
            for (auto* b : storeBalloons[e]) if (b == f) bought = e;
        if (bought >= 0)
        {
            if (bought != stutterItem) proc.popStruckRequest.fetch_or (1u << bought);
            setBought (bought, false);
            continue;
        }
        else
        {
            // control balloons don't pop: they flinch away
            auto away = f->pos - t;
            const float d = jmax (1.0f, away.getDistanceFromOrigin());
            f->vel += away / d * 520.0f;
            f->squashVel += 4.0f;
        }
    }
    auto inside = [&t, this] (const Pump& pmp)
    {
        if (pmp.size < 0.03f) return false;
        const auto sh = pumpBalloonShape (pmp);
        const float dx = (t.x - sh.cx) / sh.rx, dy = (t.y - sh.cy) / sh.ry;
        return dx * dx + dy * dy < 1.0f;
    };
    if (inside (popPump)) popPumpBalloon (popPump, false);
    if (soundPump.slot >= 0 && ! soundPump.spent && inside (soundPump)) popPumpBalloon (soundPump, true);
}

int BalloonEditor::freeSlot() const
{
    for (int i = 0; i < fp::BalloonFX::kVoices; ++i)
    {
        if (soundPump.slot == i) continue;
        if (proc.balloonFX.voice[i].mode.load() != 0 || proc.balloonFX.voice[i].audioMode.load() != 0) continue;
        bool used = false;
        for (int k = numParamBalloons; k < floaters.size(); ++k)
            if (auto* s = dynamic_cast<SoundBalloon*> (floaters[k])) used |= (s->alive && s->slot == i);
        if (! used) return i;
    }
    return -1;
}

void BalloonEditor::startSoundPump()
{
    const int slot = freeSlot();
    if (slot < 0) { fullFlash = 1.0f; return; }
    auto& v = proc.balloonFX.voice[slot];
    v.semis = nextSemis;
    v.size = 0.0f;
    v.mode = 1;                                    // start recording into this balloon
    soundPump.slot = slot;
    soundPump.holding = true;
    soundPump.spent = false;
    soundPump.holdTime = 0.0f;
    soundPump.size = 0.0f;
    soundPump.strain = 0.0f;
    soundPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
}

void BalloonEditor::releaseSoundPump()
{
    auto& pmp = soundPump;
    if (pmp.slot >= 0 && ! pmp.spent)
    {
        auto& v = proc.balloonFX.voice[pmp.slot];
        if (pmp.holdTime < 0.15f) v.mode = 0;      // too quick to catch anything
        else
        {
            v.mode = 2;                            // tie it off: freeze and play
            const auto sh = pumpBalloonShape (pmp);
            auto* b = new SoundBalloon (*this, pmp.slot, pmp.size, nextSemis, pmp.hue);
            b->pos = { sh.cx, sh.cy };
            b->vel = { -80.0f - rnd.nextFloat() * 60.0f, -260.0f };
            b->lean = (rnd.nextFloat() - 0.5f) * 0.6f;
            floaters.add (b);
            content.addAndMakeVisible (b);
            b->place();
            if (pin != nullptr) pin->toFront (false);
            static const int hues[] = { balloonTeal, balloonOrange, balloonBlue, balloonLilac, balloonYellow, balloonMint, balloonPink };
            pmp.hue = hues[(size_t) (++nextHue % 7)];
        }
    }
    pmp.slot = -1;
    pmp.holding = false;
    pmp.spent = false;
    pmp.holdTime = 0.0f;
    pmp.size = 0.0f;
    pmp.strain = 0.0f;
}

void BalloonEditor::contentMouse (const MouseEvent& e, int phase)
{
    const auto p = toContent (e);
    noteActivity();
    if (phase == 0 && chalkboard != nullptr && chalkboard->restArea().contains (p) && ! chalkboard->isVisible())
    {
        chalkboard->bringForward();
        return;
    }
    if (phase == 0 && presetStrip != nullptr && presetStrip->isOpen())
    {
        presetStrip->close();   // a click anywhere else puts the filmstrip away
        return;
    }
    if (phase == 0 && storeShelves != nullptr && storeShelves->isOpen() && ! storeSign.contains (p))
    {
        storeShelves->close();   // a click anywhere else puts the store away
        return;
    }
    if (phase == 0 && fadePopup != nullptr && fadePopup->isOpen())
    {
        fadePopup->close();   // a click anywhere else puts the fade times away
        return;
    }
    if (phase == 0)
    {
        for (auto* pl : pills)
            if (pl->r.contains (p))
            {
                if (e.mods.isPopupMenu()) return;
                if (pl->continuous)
                {
                    dragPill = pl;
                    dragStartNorm = pl->param->convertTo0to1 (pl->value);
                    dragStartY = p.y;
                    pl->att->beginGesture();
                }
                else clickPill (*pl, 1);
                return;
            }
        if (helpButton.contains (p) && library != nullptr) { library->open(); library->grabKeyboardFocus(); }
        else if (gumballButton.contains (p)) openGumballs();
        else if (resetButton.contains (p)) resetEverything();
        else if (rainboomButton.contains (p))
        {
            if (e.mods.isPopupMenu()) openRainboomMenu();
            else { proc.requestRainboom (0.7f); confetti (rainboomButton.getCentre(), 10); }
        }
        else if (popsButton.contains (p) && popsAtt != nullptr)
        {
            if (e.mods.isPopupMenu()) openPopsMenu();
            else { popsAtt->setValueAsCompleteGesture (popsValue > 0.5f ? 0.0f : 1.0f); confetti (popsButton.getCentre(), 8); }
        }
        else if (storeSign.contains (p)) openStore();
        else if (tankArea.contains (p))
        {
            // a click turns the helium on and off; a drag slides the canister along its rail
            tankDragging = true; tankMoved = false;
            tankGrabX = p.x - tankArea.getX(); tankDownX = p.x;
        }
        else if (nextBadge.contains (p)) showPitchBubble (nullptr);
        else if (popPump.area.contains (p)) { popPump.holding = true; }
        else if (soundPump.area.contains (p)) startSoundPump();
        else
        {
            bool grabbed = false;
            for (auto& a : anchors)
                if (! grabbed && ! a.fixed && ! a.hidden && ! a.onShelf && p.getDistanceFrom (a.pos + Point<float> (0.0f, a.box ? 12.0f : 0.0f)) < 30.0f)
                    { a.dragging = true; a.grab = a.pos - p; a.target = a.pos; grabbed = true; }
            if (! grabbed) confetti (p, 7);                // a little sparkle wherever you click the sky
        }
    }
    else if (phase == 1 && tankDragging)
    {
        if (! tankMoved && std::abs (p.x - tankDownX) > 4.0f)
        {
            tankMoved = true;
            if (thruAtt != nullptr) thruAtt->beginGesture();
        }
        if (tankMoved)
        {
            tankArea.setX (jlimit (railLeft, railRight, p.x - tankGrabX));
            if (thruAtt != nullptr) thruAtt->setValueAsPartOfGesture ((railRight - tankArea.getX()) / (railRight - railLeft));
        }
    }
    else if (phase == 1 && dragPill != nullptr)
    {
        const float n = jlimit (0.0f, 1.0f, dragStartNorm - (p.y - dragStartY) / 120.0f);   // up = more, like a knob
        dragPill->att->setValueAsPartOfGesture (dragPill->param->convertFrom0to1 (n));
    }
    else if (phase == 1)
    {
        for (auto& a : anchors) if (a.dragging) a.target = p + a.grab;
    }
    else
    {
        if (dragPill != nullptr) { dragPill->att->endGesture(); dragPill = nullptr; }
        if (tankDragging)
        {
            tankDragging = false;
            if (tankMoved) { if (thruAtt != nullptr) thruAtt->endGesture(); }
            else if (heliumAtt != nullptr)
            {
                heliumAtt->setValueAsCompleteGesture (heliumOn() ? 0.0f : 1.0f);
                if (heliumOn())
                {
                    for (auto* f : floaters) f->squashVel -= 2.5f;   // whoosh
                    confetti ({ pinkieX, (float) designH - 70.0f }, 30);   // and Pinkie turns to crystal in a burst of sparkles
                }
            }
        }
        popPump.holding = false;
        if (soundPump.holding) releaseSoundPump();
        bool movedShelf = false;
        for (auto& a : anchors) { movedShelf |= a.dragging && a.onShelf; a.dragging = false; }
        if (movedShelf) commitShelf();
    }
}

void BalloonEditor::contentWheel (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto p = toContent (e);
    for (auto* pl : pills)
        if (pl->r.contains (p))
        {
            int dir = 0;
            if (pl->continuous)
            {
                float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
                if (w.isReversed) d = -d;
                const float n = jlimit (0.0f, 1.0f, pl->param->convertTo0to1 (pl->value) + d * 0.3f);
                pl->att->setValueAsCompleteGesture (pl->param->convertFrom0to1 (n));
            }
            else if (dynamic_cast<AudioParameterChoice*> (pl->param) != nullptr && wheelStep (nextWheelAcc, w, dir)) clickPill (*pl, dir);
            return;
        }
    if (! soundPump.area.expanded (40.0f, 0.0f).contains (p) && ! nextBadge.contains (p)) return;
    int dir = 0;
    if (wheelStep (nextWheelAcc, w, dir)) nextSemis = stepSemis (nextSemis, dir);
}

// Trackpads send lots of tiny deltas: collect them and move one note per deliberate flick.
bool BalloonEditor::wheelStep (float& acc, const MouseWheelDetails& w, int& direction)
{
    float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (w.isReversed) d = -d;
    if (std::abs (d) >= 0.2f && ! w.isSmooth) { acc = 0.0f; direction = d > 0 ? 1 : -1; return true; }  // a mouse wheel notch
    acc += d;
    if (std::abs (acc) < 0.3f) return false;
    direction = acc > 0 ? 1 : -1;
    acc = 0.0f;
    return true;
}

// Pop up a speech bubble at the balloon (or the pump's "next" badge) to type an exact interval.
void BalloonEditor::showPitchBubble (SoundBalloon* target)
{
    if (pitchBubble == nullptr)
    {
        pitchBubble = std::make_unique<PitchBubble>();
        content.addChildComponent (*pitchBubble);
    }
    Component::SafePointer<SoundBalloon> safe (target);
    const bool forNext = target == nullptr;
    pitchBubble->onCommit = [this, safe, forNext] (float st)
    {
        if (forNext) { nextSemis = st; confetti (nextBadge.getCentre(), 8); return; }
        if (auto* b = safe.getComponent())
        {
            b->semis = st;
            proc.balloonFX.voice[b->slot].semis = st;
            b->squashVel += 3.0f;
            confetti (b->pos, 10);
            b->repaint();
        }
    };
    pitchBubble->onClose = [this] { if (pitchBubble != nullptr) pitchBubble->setVisible (false); };

    const auto bounds = Rectangle<float> (0, 0, (float) designW, (float) designH);
    if (forNext)
        pitchBubble->open ({ nextBadge.getCentreX(), nextBadge.getY() - 2.0f }, nextSemis, "next balloon's pitch", bounds);
    else
    {
        const auto sh = balloonShape ({ 0, 0, (float) Floater::S, (float) Floater::S }, target->size);
        pitchBubble->open ({ target->pos.x, target->pos.y - sh.ry - 4.0f }, target->semis, "this balloon's pitch", bounds);
    }
}

//==============================================================================
// the push pin: a red glass head and a steel shaft, tip at the bottom left
// the pin: a thin white line with the house mark for a head, tip at the bottom left
void PinTool::paint (Graphics& g)
{
    const Point<float> head (30.0f, 13.0f), tipP (8.0f, 58.0f);
    g.setColour (bal::white());
    g.drawLine (Line<float> (head, tipP), dragging ? 1.8f : 1.2f);
    const auto m = Rectangle<float> (18.0f, 10.0f).withCentre (head.translated (-2.0f, 0.0f));
    g.setColour (bal::strip (4));
    g.fillPath (bal::ui::skewRect (m));
    g.setColour (bal::white());
    g.strokePath (bal::ui::skewRect (m), PathStrokeType (1.2f));
}

bool PinTool::hitTest (int x, int y)
{
    // grab it by the head or the shaft
    const Point<float> p ((float) x, (float) y), head (30.0f, 13.0f), tipP (8.0f, 58.0f);
    if (p.getDistanceFrom (head) < 14.0f) return true;
    Point<float> nearest;
    return Line<float> (head, tipP).getDistanceFromPoint (p, nearest) < 6.0f;
}

void PinTool::mouseDown (const MouseEvent& e)
{
    dragging = true;
    grab = pos - owner.toContent (e);
    toFront (false);
}

void PinTool::mouseDrag (const MouseEvent& e)
{
    pos = owner.toContent (e) + grab;
    place();
}

//==============================================================================
PitchBubble::PitchBubble()
{
    field.setFont (bal::mono (22.0f, true));
    field.setJustification (Justification::centred);
    field.setInputRestrictions (3, "+-0123456789");
    field.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    field.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    field.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    field.setColour (TextEditor::textColourId, bal::paperInk());
    field.setColour (TextEditor::highlightColourId, bal::strip (5));
    field.setColour (CaretComponent::caretColourId, bal::paperInk());
    field.setTextToShowWhenEmpty ("+7", bal::paperInk().withAlpha (0.35f));
    field.addListener (this);
    addAndMakeVisible (field);
    setSize (170, 92);
}

void PitchBubble::open (Point<float> tip, float current, const String& t, Rectangle<float> within)
{
    title = t;
    closing = false;
    // sit above what we point at, or below it if there's no room
    tailDown = tip.y - (float) getHeight() - 6.0f > within.getY();
    auto r = Rectangle<float> ((float) getWidth(), (float) getHeight())
                 .withCentre ({ tip.x, tailDown ? tip.y - (float) getHeight() * 0.5f - 2.0f : tip.y + (float) getHeight() * 0.5f + 2.0f });
    r = r.constrainedWithin (within.reduced (4.0f));
    tailX = jlimit (24.0f, (float) getWidth() - 24.0f, tip.x - r.getX());
    setBounds (r.toNearestInt());
    const int st = roundToInt (current);
    field.setText (st == 0 ? "0" : (st > 0 ? "+" : "-") + String (std::abs (st)), false);
    setVisible (true);
    toFront (true);
    field.grabKeyboardFocus();
    field.selectAll();
    repaint();
}

void PitchBubble::resized()
{
    auto body = getLocalBounds().reduced (6).withTrimmedTop (tailDown ? 0 : 12).withTrimmedBottom (tailDown ? 12 : 0);
    field.setBounds (body.withTrimmedTop (22).reduced (14, 4));
}

// a slip of paper to type a pitch on, with a corner pointing at what it is for
void PitchBubble::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f).withTrimmedTop (tailDown ? 0.0f : 12.0f).withTrimmedBottom (tailDown ? 12.0f : 0.0f);
    Path slip;
    slip.addRectangle (b);
    if (tailDown) slip.addTriangle (tailX - 9.0f, b.getBottom() - 1.0f, tailX + 9.0f, b.getBottom() - 1.0f, tailX + 2.0f, b.getBottom() + 12.0f);
    else          slip.addTriangle (tailX - 9.0f, b.getY() + 1.0f, tailX + 9.0f, b.getY() + 1.0f, tailX + 2.0f, b.getY() - 12.0f);
    g.setColour (bal::paper());
    g.fillPath (slip);
    g.setColour (bal::paperInk());
    g.drawRect (b, 1.0f);
    bal::label (g, title, b.withHeight (24.0f).translated (0.0f, 3.0f), bal::paperInk(), 10.0f, Justification::centred, true);
    bal::label (g, "type +7 or -5, then enter", b.removeFromBottom (16.0f), bal::paperInk().withAlpha (0.6f), 8.5f);
}

void PitchBubble::commit()
{
    if (closing) return;
    auto t = field.getText().trim();
    const bool neg = t.startsWithChar ('-');
    t = t.removeCharacters ("+-");
    if (t.isNotEmpty() && t.containsOnly ("0123456789") && onCommit)
        onCommit ((float) jlimit (-24, 24, (neg ? -1 : 1) * t.getIntValue()));
    close();
}

void PitchBubble::close()
{
    if (closing) return;
    closing = true;
    if (onClose) onClose();
}

//==============================================================================
// Physics: buoyant balloons on strings tied to heavy anchors. Strings only pull (they go
// slack when you push a balloon in), balloons bump and jiggle, anchors rest on the floor.
void BalloonEditor::step (float h)
{
    const float W = (float) designW, H = (float) designH;
    const float floorY = H - 30.0f;
    const float t = (float) clock;
    constexpr float buoyancy = -520.0f, damping = 1.6f, maxV = 2600.0f;

    for (auto& a : anchors)
    {
        if (a.hidden) continue;
        if (a.fixed) { a.pos = pinkieHoof(); a.vel = {}; continue; }
        if (a.dragging)
        {
            const auto np = Point<float> (jlimit (30.0f, W - 30.0f, a.target.x), jlimit (20.0f, floorY, a.target.y));
            a.vel = (np - a.pos) / h;
            a.pos = np;
        }
        else
        {
            // things on the shelf stand on it and glide to their place in the order
            const float ground = a.onShelf ? shelfY : floorY;
            if (a.onShelf) a.vel.x += (a.slotX - a.pos.x) * 90.0f * h - a.vel.x * 12.0f * h;
            a.vel.y += 1800.0f * h;
            a.vel *= 1.0f - 2.0f * h;
            a.pos += a.vel * h;
            if (a.pos.y >= ground - 0.5f)
            {
                a.pos.y = ground; a.vel.y = jmin (0.0f, a.vel.y) * -0.25f;
                if (! a.onShelf) a.vel.x *= jmax (0.0f, 1.0f - 40.0f * h);   // a bow on the floor has grip: strings don't drag it around
            }
            a.pos.x = jlimit (30.0f, W - 30.0f, a.pos.x);
        }
    }

    for (auto* b : floaters)
    {
        if (! b->alive) continue;
        if (b->dragging)
        {
            auto np = b->dragTarget;
            b->vel = (np - b->pos) / h;
            b->vel.x = jlimit (-maxV, maxV, b->vel.x);
            b->vel.y = jlimit (-maxV, maxV, b->vel.y);
            b->pos = np;
            continue;
        }
        const float lift = (1.0f + 0.8f * heliumVis) * (1.0f - 0.25f * rainVis - 0.20f * b->wet);
        const Point<float> breeze (26.0f * std::sin (t * 0.37f + b->phase) + 330.0f * b->lean * lift,   // the fan scales with the lift
                                   10.0f * std::sin (t * 0.23f + b->phase * 1.7f));
        const float thaw = 1.0f - 0.92f * b->frost;   // frozen balloons barely drift
        b->vel.y += buoyancy * lift * h * (b->frost > 0.5f ? thaw : 1.0f);
        b->vel += breeze * h * thaw;
        if (skyArt != nullptr) b->vel += skyArt->pushAt (b->pos) * h * thaw;   // the wind leans them over; a twister whirls them
        b->vel *= 1.0f - jmin (1.0f, (damping + 9.0f * b->frost) * h);
        b->pos += b->vel * h;
    }

    for (int iter = 0; iter < 3; ++iter)
    {
        for (auto* b : floaters)
        {
            if (! b->alive) continue;
            auto& a = anchors[b->anchor];
            const float wBow = a.dragging || a.fixed ? 0.0f : 0.08f;
            const float wb = b->dragging ? 0.0f : 1.0f;
            const float ws = wb + wBow;
            if (ws <= 0.0f) continue;
            const auto d = b->pos - a.pos;
            const float L = b->stringLength + 36.0f;
            const float dist = d.getDistanceFromOrigin();
            if (dist <= L || dist < 1.0e-3f) continue;
            const auto n = d / dist;
            const float corr = dist - L;
            b->pos -= n * (corr * wb / ws);
            a.pos += n * (corr * wBow / ws);
            const float rel = (b->vel - a.vel).getDotProduct (n);
            if (rel > 0.0f) { b->vel -= n * (rel * wb / ws); a.vel += n * (rel * wBow / ws); }
        }

        for (int i = 0; i < floaters.size(); ++i)
            for (int j = i + 1; j < floaters.size(); ++j)
            {
                auto* a = floaters[i];
                auto* c = floaters[j];
                if (! a->alive || ! c->alive) continue;
                const auto d = c->pos - a->pos;
                const float R = a->collisionRadius() + c->collisionRadius();
                const float dist2 = d.x * d.x + d.y * d.y;
                if (dist2 >= R * R || dist2 < 1.0e-6f) continue;
                const float wa = a->dragging ? 0.0f : 1.0f, wc = c->dragging ? 0.0f : 1.0f;
                const float ws = wa + wc;
                if (ws <= 0.0f) continue;
                const float dist = std::sqrt (dist2);
                const auto n = d / dist;
                const float overlap = R - dist;
                a->pos -= n * (overlap * wa / ws);
                c->pos += n * (overlap * wc / ws);
                const float rel = (c->vel - a->vel).getDotProduct (n);
                if (rel < 0.0f)
                {
                    const float imp = -(1.0f + 0.45f) * rel / ws;
                    a->vel -= n * (imp * wa);
                    c->vel += n * (imp * wc);
                    const float j2 = jmin (6.0f, -rel * 0.012f);   // squish!
                    a->squashVel += j2; c->squashVel += j2;
                }
            }

        for (auto* b : floaters)
        {
            const float r = b->collisionRadius();
            if (b->pos.x < r)     { b->pos.x = r;     b->vel.x =  std::abs (b->vel.x) * 0.5f; b->squashVel -= 1.0f; }
            if (b->pos.x > W - r) { b->pos.x = W - r; b->vel.x = -std::abs (b->vel.x) * 0.5f; b->squashVel -= 1.0f; }
            if (b->pos.y < r)     { b->pos.y = r;     b->vel.y =  std::abs (b->vel.y) * 0.5f; b->squashVel += 1.0f; }
            if (b->pos.y > H - r) { b->pos.y = H - r; b->vel.y = -std::abs (b->vel.y) * 0.5f; b->squashVel += 1.0f; }
        }
    }

    for (auto* b : floaters)
    {
        const auto d = b->pos - anchors[b->anchor].pos;
        const float dist = d.getDistanceFromOrigin();
        const bool taut = dist > b->stringLength + 30.0f;
        float target = std::atan2 (d.x, -d.y);
        if (! taut) target = target * 0.35f + 0.08f * std::sin (t * 0.9f + b->phase);
        b->angle += (target - b->angle) * jmin (1.0f, (taut ? 7.0f : 2.5f) * h);
        b->squashVel += (-260.0f * b->squash - 7.0f * b->squashVel) * h;
        b->squash = jlimit (-0.22f, 0.22f, b->squash + b->squashVel * h);
    }
}

//==============================================================================
// The pumps: hold to blow a balloon up. Somewhere between 5 and 12 seconds of
// inflating (you never know when) it bursts.
void BalloonEditor::pumpStep (float dt)
{
    auto grow = [] (Pump& p)
    {
        p.size = std::pow (jmin (1.0f, p.holdTime / 5.0f), 0.7f);
        p.strain = jmax (0.0f, (p.holdTime - 5.0f) / 7.0f);
    };

    // pop balloons
    if (popPump.holding)
    {
        popPump.holdTime += dt;
        popPump.handle += dt * 9.0f;
        grow (popPump);
        if (popPump.holdTime >= popPump.popAt)
        {
            const auto sh = pumpBalloonShape (popPump);
            pop ({ sh.cx, sh.cy }, jmin (1.0f, popPump.size), popPump.hue);
            popPump.holdTime = 0.0f; popPump.size = 0.0f; popPump.strain = 0.0f;
            popPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
            static const int hues[] = { balloonOrange, balloonPink, balloonBlue, balloonYellow, balloonLilac, balloonMint };
            popPump.hue = hues[(size_t) rnd.nextInt (6)];
        }
    }
    proc.balloonFX.pumping = popPump.holding;
    proc.balloonFX.pumpSize = popPump.size + 0.3f * popPump.strain;

    // sound balloons
    if (soundPump.holding && soundPump.slot >= 0 && ! soundPump.spent)
    {
        soundPump.holdTime += dt;
        soundPump.handle += dt * 9.0f;
        grow (soundPump);
        proc.balloonFX.voice[soundPump.slot].size = soundPump.size;
        soundPump.strain = 0.0f;   // a sound balloon never bursts by itself: it only goes when you pop it
    }
    fullFlash = jmax (0.0f, fullFlash - dt * 0.6f);
}

BalloonShape BalloonEditor::pumpBalloonShape (const Pump& p) const
{
    const float t = (float) clock;
    const float tremble = p.strain * 3.5f * (std::sin (t * 53.0f) + 0.6f * std::sin (t * 37.0f + 1.3f));
    BalloonShape sh;
    sh.rx = 6.0f + 30.0f * p.size * (1.0f + 0.22f * p.strain);
    sh.ry = sh.rx * 1.12f;
    sh.knot = 4.0f;
    sh.cx = p.nozzle.x + tremble;
    sh.cy = p.nozzle.y - sh.knot - sh.ry + p.strain * 1.5f * std::sin (t * 41.0f);
    sh.lw = 1.5f;
    return sh;
}

//==============================================================================
void BalloonEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) jlimit (0.0, 0.05, now - lastTick);
    lastTick = now;
    clock += dt;
    lnf.time = (float) clock;

    // the look follows the switches straight away (the audio filters ease in on their own)
    auto follow = [dt] (float& v, float target) { v += (target - v) * jmin (1.0f, dt * 1.4f); if (target == 0.0f && v < 0.002f) v = 0.0f; };
    follow (rainVis, rainOn() ? 1.0f : 0.0f);
    follow (heliumVis, heliumOn() ? 1.0f : 0.0f);
    follow (cloudVis, cloudyOn() ? 1.0f : 0.0f);
    follow (snowVis, snowOn() ? 1.0f : 0.0f);
    follow (heatVis, heatOn() ? 1.0f : 0.0f);
    follow (autoVis, autoOn() ? 1.0f : 0.0f);
    frozen = proc.snowAmount.load();   // the ice follows the sound's own freeze
    follow (bypassVis, bypassOn() ? 1.0f : 0.0f);
    {
        const int lc = proc.lightningCount.load();   // read before the store sync, so a lent effect drops in where it struck
        if (lc != seenLightning) { seenLightning = lc; strike(); }
    }
    syncStore();
    {
        // dragging something along the shelf: the others make room as it passes
        bool draggingShelf = false;
        for (auto& a : anchors) draggingShelf |= a.dragging && a.onShelf && ! a.hidden;
        if (draggingShelf)
        {
            auto visible = shelfItems();
            std::stable_sort (visible.begin(), visible.end(), [this] (int a, int b) { return anchors[anchorOfSlot (a)].pos.x < anchors[anchorOfSlot (b)].pos.x; });
            layoutShelf (visible);
        }
    }
    {
        // the gumball machine's balloons hop on every stutter
        const int ev = proc.stutter.eventCount.load();
        if (ev != seenStutter)
        {
            seenStutter = ev;
            for (int i = 0; i < numParamBalloons; ++i)
                if (auto* f = floaters[i]; f->alive && f->anchor == 4) { f->squashVel += 3.5f; f->vel.y -= 70.0f; }
        }
    }
    followPreset (dt);
    if (chalkboard != nullptr) chalkboard->step (dt);
    if (library != nullptr) library->step (dt);
    if (gumballPage != nullptr) gumballPage->step (dt);
    skyStep (dt);
    if (storeShelves != nullptr) storeShelves->step (dt);
    if (weatherBar != nullptr)
    {
        weatherBar->step (dt);
        revealStep (dt);
        if (cardPanel != nullptr && cardPanel->isVisible()) cardPanel->repaint();
        if (fadePopup != nullptr && fadePopup->isVisible()) fadePopup->repaint();
        if (presetStrip != nullptr) presetStrip->step (dt);
        // keep the barometer above the balloons (they're added as they're caught), and the store above that
        const int n = content.getNumChildComponents();
        auto* topmost = n > 0 ? content.getChildComponent (n - 1) : nullptr;
        if (topmost != weatherBar.get()) weatherBar->toFront (false);   // (the card, lists and menus are on the overlay, above all of it)
    }
    {
        const bool scared = clock < scaredUntil;
        const bool wow = clock < wowUntil;   // a rainboom: she jumps, amazed
        // how long she's been out in the snow: a minute and she's got a cold (too poorly to doze off) until it goes
        if (snowVis > 0.3f) { if (snowSince < 0.0) snowSince = clock; }
        else snowSince = -1.0;
        const bool hasCold = snowSince >= 0.0 && clock - snowSince > coldAfter;
        const bool asleep = ! scared && ! wow && ! hasCold && clock - lastActivity > sleepAfter;
        auto toward = [dt] (float& v, bool on, float speed) { v += ((on ? 1.0f : 0.0f) - v) * jmin (1.0f, dt * speed); };
        toward (scaredVis, scared, 14.0f);
        toward (wowVis, wow, 14.0f);
        toward (sleepVis, asleep, 1.5f);
        const bool hotRain = rainVis > 0.3f && heatVis > 0.3f;   // rain in a heatwave: no umbrella, she's soaked and flat out
        toward (wetVis, ! scared && ! asleep && hotRain, 1.5f);
        toward (umbrellaVis, ! scared && ! asleep && rainVis > 0.3f && ! hotRain, 3.0f);
        toward (crystalVis, ! scared && ! asleep && heliumVis > 0.3f, 4.0f);
        toward (snowyVis, ! scared && ! asleep && snowVis > 0.3f, 3.0f);
        toward (coldVis, hasCold, 0.8f);
        toward (meltVis, ! scared && ! asleep && heatVis > 0.3f, 1.2f);   // she wilts slowly
    }

    // blow on them: moving the mouse fast near a balloon pushes it
    const bool anyDrag = std::any_of (std::begin (anchors), std::end (anchors), [] (const Anchor& a) { return a.dragging; })
                      || std::any_of (floaters.begin(), floaters.end(), [] (Floater* b) { return b->dragging; });
    const auto m = content.getMouseXYRelative().toFloat();
    if (haveMouse && dt > 0.0f && ! anyDrag && getScreenBounds().contains (Desktop::getMousePosition()))
    {
        const auto mv = (m - lastMouse) / dt;
        for (auto* b : floaters)
        {
            const float d = b->pos.getDistanceFrom (m);
            if (d < 150.0f) b->vel += mv * (0.07f * (1.0f - d / 150.0f));
        }
    }
    if (haveMouse && isShowing() && m.getDistanceFrom (lastMouse) > 0.5f && getLocalBounds().contains (getLocalPoint (&content, m.toInt()))) noteActivity();
    lastMouse = m;
    haveMouse = true;

    // dance with the sound (sound balloons dance with their own voice)
    const float lvl = jlimit (0.0f, 1.0f, proc.outputLevel.load() * 1.6f);
    for (auto* b : floaters)
    {
        float l = lvl;
        if (auto* s = dynamic_cast<SoundBalloon*> (b)) l = jlimit (0.0f, 1.0f, proc.balloonFX.voice[s->slot].level.load() * 3.0f);
        b->dance += (l * (0.5f + 0.5f * std::sin ((float) clock * 7.0f + b->phase * 3.0f)) - b->dance) * jmin (1.0f, dt * 12.0f);
        if (auto* pb = dynamic_cast<Balloon*> (b)) pb->faded = pb->bypassable ? bypassVis : 0.0f;
        if (l > 0.25f && rnd.nextFloat() < dt * 3.0f) b->vel += { (rnd.nextFloat() - 0.5f) * 140.0f * l, -60.0f * l };
    }

    const float f = proc.freezeAmount.load();
    if (freezeBalloon != nullptr)
    {
        freezeBalloon->fill = f;
        if (lastFreeze < 1.0f && f >= 1.0f) confetti (freezeBalloon->pos, 24);
    }
    lastFreeze = f;
    if (spectralFreezeBalloon != nullptr) spectralFreezeBalloon->fill = proc.spectralFreezeAmount.load();
    freezeText = f <= 0.0f ? "" : f >= 1.0f ? "frozen" : "freezing...";

    weatherStep (dt);
    cloudStep (dt);
    rainboomStep (dt);
    snowStep (dt);
    trixieStep (dt);
    pumpStep (dt);
    pinStep (dt);
    pillStep (dt);
    pinkieHopVel -= 900.0f * dt;
    pinkieHop += pinkieHopVel * dt;
    if (pinkieHop <= 0.0f) { pinkieHop = 0.0f; pinkieHopVel = 0.0f; }
    for (auto* pl : pills)
    {
        pl->bounceVel += (-320.0f * pl->bounce - 9.0f * pl->bounceVel) * dt;
        pl->bounce = jlimit (-0.3f, 0.3f, pl->bounce + pl->bounceVel * dt);
    }
    step (dt * 0.5f);
    step (dt * 0.5f);

    // tidy up popped sound balloons
    for (int i = floaters.size(); --i >= numParamBalloons;)
        if (! floaters[i]->alive && ! floaters[i]->isMouseButtonDown())
        {
            content.removeChildComponent (floaters[i]);
            floaters.remove (i);
        }
    for (auto* b : floaters) { b->place(); b->repaint(); }

    for (auto& pt : particles)
    {
        pt.v.y += (pt.kind == 2 ? 700.0f : 520.0f) * dt;
        pt.v *= 1.0f - 0.9f * dt;
        pt.p += pt.v * dt;
        pt.rot += pt.spin * dt;
        pt.life -= dt * (pt.kind == 1 ? 1.4f : 0.55f);
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& q) { return q.life <= 0.0f; }), particles.end());
    for (auto& b : bursts) b.age += dt;
    bursts.erase (std::remove_if (bursts.begin(), bursts.end(), [] (const Burst& b) { return b.age > 0.5f; }), bursts.end());
    for (auto& d : dust) { d.y -= dt * 4.0f; if (d.y < 0) d.y += designH; }


    const auto& scales = fp::getScales();
    const int si = jlimit (0, (int) scales.size() - 1, (int) proc.apvts.getRawParameterValue (ids::psScale)->load());
    const auto& sc = scales[(size_t) si];
    const auto q = fp::quantise (sc, proc.apvts.getRawParameterValue (ids::psShift)->load() * 100.0);
    String periodText;
    if (q.period != 0) periodText = (q.period > 0 ? " +" : " ") + String (q.period) + " oct";
    const int st = roundToInt (q.cents / 100.0);
    intervalText = String ("shift lands on ") + sc.degrees[(size_t) q.degreeIndex].label + periodText
                   + "  (" + (st >= 0 ? "+" : "") + String (st) + " st)";

    content.repaint();
}

// Rain falls (and really hits the balloons); helium hisses from the hose.
void BalloonEditor::weatherStep (float dt)
{
    const float W = (float) designW, H = (float) designH;

    dropBudget += rainVis * 420.0f * dt;
    while (dropBudget >= 1.0f)
    {
        dropBudget -= 1.0f;
        drops.push_back ({ { rnd.nextFloat() * (W + 120.0f) - 100.0f, -10.0f - rnd.nextFloat() * 30.0f },
                           { 70.0f + rnd.nextFloat() * 30.0f, 780.0f + rnd.nextFloat() * 260.0f } });
    }
    for (auto& d : drops)
    {
        if (d.bounced) d.v.y += 1500.0f * dt;     // a bounced drop arcs back down
        d.p += d.v * dt;
        bool gone = false;
        Point<float> nrm;
        if (! d.bounced && umbrellaVis > 0.5f && d.v.y > 0.0f && umbrellaHit (d.p, nrm))
        {
            const float vn = d.v.getDotProduct (nrm);
            d.v -= nrm * ((1.0f + 0.35f) * vn);                       // off the canopy, losing most of its speed
            d.v = d.v * 0.55f + Point<float> ((rnd.nextFloat() - 0.5f) * 80.0f, -40.0f * rnd.nextFloat());
            d.bounced = true;
            if (rnd.nextFloat() < 0.4f)
                particles.push_back ({ d.p, { (rnd.nextFloat() - 0.5f) * 90.0f, -50.0f - rnd.nextFloat() * 60.0f },
                                       0.0f, 0.0f, 0.25f, Colour (0xffcfe6ff), 1 });
        }
        for (auto* b : floaters)
        {
            if (! b->alive || d.p.getDistanceFrom (b->pos) > b->collisionRadius()) continue;
            for (int k = 0; k < 3; ++k)
                particles.push_back ({ d.p, { (rnd.nextFloat() - 0.5f) * 160.0f, -60.0f - rnd.nextFloat() * 90.0f },
                                       0.0f, 0.0f, 0.35f, Colour (0xffcfe6ff), 1 });
            b->vel += { d.v.x * 0.01f, 10.0f };
            b->squashVel += 0.35f;
            b->wet = jmin (1.0f, b->wet + 0.04f);
            gone = true;
            break;
        }
        if (! gone && d.p.y > H - 6.0f)
        {
            ripples.push_back ({ { d.p.x, H - 5.0f - rnd.nextFloat() * 4.0f }, 0.0f });
            gone = true;
        }
        if (gone) d.p.y = 1.0e9f;
    }
    drops.erase (std::remove_if (drops.begin(), drops.end(), [&] (const Drop& d) { return d.p.y > H + 50.0f || d.p.x > W + 60.0f; }), drops.end());

    for (auto& r : ripples) r.age += dt * 2.2f;
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.age >= 1.0f; }), ripples.end());

    puddle = jlimit (0.0f, 1.0f, puddle + (rainVis > 0.05f ? rainVis * 0.03f : -0.02f) * dt);
    for (auto* b : floaters)
    {
        b->wet = jmax (0.0f, b->wet - dt * (0.01f + 0.12f * (1.0f - rainVis)));
        b->stretch = heliumVis;
    }

    valveAngle += dt * 9.0f * heliumVis;
    puffBudget += heliumVis * 30.0f * dt;
    while (puffBudget >= 1.0f)
    {
        puffBudget -= 1.0f;
        puffs.push_back ({ anchors[0].pos + Point<float> (-8.0f, -2.0f), { (rnd.nextFloat() - 0.5f) * 40.0f, -40.0f - rnd.nextFloat() * 60.0f },
                           0.0f, 0.0f, 1.0f, Colours::white, 1 });
    }
    for (auto& pf : puffs) { pf.p += pf.v * dt; pf.v.x *= 1.0f - dt; pf.life -= dt * 0.9f; }
    puffs.erase (std::remove_if (puffs.begin(), puffs.end(), [] (const Particle& q) { return q.life <= 0.0f; }), puffs.end());
}

//==============================================================================
// Cloudy weather: the clouds come down from the sky and drift slowly through the bouquet.
Point<float> BalloonEditor::cloudAt (const SkyCloud& c) const
{
    const float t = (float) clock, th = proc.thunderAmount.load();
    return { c.p.x + 3.0f * th * std::sin (t * 37.0f + c.phase),
             c.p.y + 6.0f * std::sin (t * 0.4f + c.phase) + 2.5f * th * std::sin (t * 29.0f + c.phase * 2.0f)
                   - 340.0f * (1.0f - fp::smoothstep (cloudVis)) };
}

bool BalloonEditor::insideCloud (const Floater& f) const
{
    const float r = f.collisionRadius();
    for (auto& c : skyClouds)
    {
        const auto at = cloudAt (c);
        const float dx = (f.pos.x - at.x) / (44.0f * c.s + 0.5f * r), dy = (f.pos.y - (at.y - 4.0f * c.s)) / (24.0f * c.s + 0.5f * r);
        if (dx * dx + dy * dy < 1.0f) return true;
    }
    return false;
}

void BalloonEditor::cloudStep (float dt)
{
    const float W = (float) designW;
    for (auto& c : skyClouds)
    {
        c.p.x += c.vx * dt * (1.0f - 0.9f * frozen);
        const float half = 60.0f * c.s;
        if (c.vx > 0.0f && c.p.x > W + half) c.p.x = -half;
        if (c.vx < 0.0f && c.p.x < -half)    c.p.x = W + half;
    }
    flash = jmax (0.0f, flash - dt * 2.5f);

    // a balloon in a cloud slows down (it's thick in there) and its value wanders
    const bool active = cloudVis > 0.6f && cloudyOn();   // (switching cloudy off stops the wandering at once)
    for (auto* f : floaters)
    {
        if (! f->alive) continue;
        const bool in = active && insideCloud (*f);
        if (in) f->vel *= 1.0f - jmin (1.0f, 0.9f * dt);
        if (auto* b = dynamic_cast<Balloon*> (f)) b->cloudDrift (in, dt * (1.0f - frozen), rnd);   // ice slows the wandering too
    }
}

// a lightning strike: a white flash, a jagged bolt from one of the clouds, and the balloons nearby jump
void BalloonEditor::strike()
{
    if (skyClouds.empty()) return;
    std::vector<int> onScreen;
    for (int i = 0; i < (int) skyClouds.size(); ++i)
        if (skyClouds[(size_t) i].p.x > 120.0f && skyClouds[(size_t) i].p.x < (float) designW - 120.0f) onScreen.push_back (i);
    const int ci = onScreen.empty() ? rnd.nextInt ((int) skyClouds.size()) : onScreen[(size_t) rnd.nextInt ((int) onScreen.size())];
    const auto& c = skyClouds[(size_t) ci];
    const auto from = cloudAt (c) + Point<float> (rnd.nextFloat() * 40.0f - 20.0f, 18.0f * c.s);
    auto to = from + Point<float> (rnd.nextFloat() * 140.0f - 70.0f, 150.0f + rnd.nextFloat() * 90.0f);
    to.x = jlimit (90.0f, (float) designW - 90.0f, to.x);
    to.y = jlimit (120.0f, (float) designH - 110.0f, to.y);
    strikeAt = to;

    bolt.clear();
    bolt.startNewSubPath (from);
    const int segs = 9;
    const auto dir = to - from;
    const Point<float> perp (-dir.y, dir.x);
    const float len = jmax (1.0f, dir.getDistanceFromOrigin());
    Point<float> branchFrom;
    for (int i = 1; i <= segs; ++i)
    {
        const float u = (float) i / (float) segs;
        const float j = i == segs ? 0.0f : (rnd.nextFloat() - 0.5f) * 36.0f;
        const auto q = from + dir * u + perp * (j / len);
        bolt.lineTo (q);
        if (i == 4) branchFrom = q;
    }
    bolt.startNewSubPath (branchFrom);   // a little fork
    bolt.lineTo (branchFrom + Point<float> ((rnd.nextBool() ? 1.0f : -1.0f) * (24.0f + 20.0f * rnd.nextFloat()), 30.0f));
    bolt.lineTo (branchFrom + Point<float> ((rnd.nextBool() ? 1.0f : -1.0f) * 14.0f, 58.0f));

    flash = 1.0f;
    scaredUntil = jmax (scaredUntil, clock + 0.9);
    for (auto* f : floaters)
    {
        if (! f->alive) continue;
        const auto away = f->pos - strikeAt;
        const float d = away.getDistanceFromOrigin();
        if (d > 180.0f) continue;
        f->vel += away / jmax (1.0f, d) * 360.0f * (1.0f - d / 180.0f);
        f->squashVel += 3.0f;
    }
}

void BalloonEditor::drawSkyClouds (Graphics& g) const
{
    if (cloudVis <= 0.001f) return;
    const float dark = (0.15f + 0.6f * rainVis + 0.25f * proc.thunderAmount.load()) * (1.0f - 0.8f * flash);
    g.beginTransparencyLayer (0.9f * fp::smoothstep (cloudVis));
    for (auto& c : skyClouds) drawCloud (g, cloudAt (c), c.s, jlimit (0.0f, 1.0f, dark));
    g.endTransparencyLayer();

    // over the cloud: a slip on each balloon it's changing, so you can watch the value move
    const Font f = bal::mono (10.0f, true);
    std::vector<Rectangle<float>> placed;   // slips step aside so they never sit on each other
    for (auto* fl : floaters)
    {
        auto* b = dynamic_cast<Balloon*> (fl);
        if (b == nullptr || ! b->alive || ! b->isDrifting()) continue;
        const String text = b->label.replace ("\n", " ") + "  " + b->currentText();
        const float w = GlyphArrangement::getStringWidth (f, text) + 18.0f;
        auto r = Rectangle<float> (w, 16.0f).withCentre (b->pos.translated (0.0f, -b->collisionRadius() - 6.0f));
        r = r.withX (jlimit (4.0f, (float) designW - w - 4.0f, r.getX())).withY (jmax (4.0f, r.getY()));
        const auto home = r;
        for (int tries = 0; tries < 12; ++tries)
        {
            bool clash = false;
            for (auto& q : placed) clash |= q.expanded (2.0f).intersects (r);
            if (! clash) break;
            r = home.translated (0.0f, (tries % 2 == 0 ? -1.0f : 1.0f) * 19.0f * (float) (tries / 2 + 1));   // above, below, further above...
        }
        placed.push_back (r);
        if (const auto c = r.getCentre(); c.getDistanceFrom (b->pos) > b->collisionRadius() + 16.0f)
        {
            g.setColour (balloonColours (b->hue).fill);   // a thread back to its balloon
            g.drawLine (Line<float> (c, b->pos), 1.0f);
        }
        g.setColour (bal::paper());
        g.fillRect (r);
        g.setColour (balloonColours (b->hue).fill);
        g.fillRect (r.withWidth (4.0f));
        bal::label (g, text, r.withTrimmedLeft (4.0f), bal::paperInk(), 10.0f, Justification::centred, true);
    }
}

// Snow: flakes drift down, a few rest on the balloons' tops and melt, the rest pile up on the floor
void BalloonEditor::snowStep (float dt)
{
    const float W = (float) designW, H = (float) designH;
    flakeBudget += snowVis * 70.0f * dt;
    while (flakeBudget >= 1.0f)
    {
        flakeBudget -= 1.0f;
        flakes.push_back ({ { rnd.nextFloat() * W, -8.0f }, 28.0f + rnd.nextFloat() * 40.0f, 2.0f + rnd.nextFloat() * 4.5f, rnd.nextFloat() * 6.2832f });
    }
    const float t = (float) clock;
    for (auto& f : flakes)
    {
        f.p.y += f.vy * dt;
        f.p.x += (14.0f * std::sin (t * 0.9f + f.phase) + 8.0f) * dt;
        bool gone = f.p.y > H - 8.0f - 14.0f * snowDepth;
        if (gone) snowDepth = jmin (1.0f, snowDepth + 0.0009f);
        for (auto* b : floaters)
            if (! gone && b->alive && f.p.getDistanceFrom (b->pos) < b->collisionRadius() * 0.8f)
            {
                gone = true;
                b->frost = jmax (b->frost, jmin (1.0f, frozen + 0.05f));   // a touch of ice where it lands
            }
        if (gone) f.p.y = 1.0e9f;
    }
    flakes.erase (std::remove_if (flakes.begin(), flakes.end(), [&] (const Flake& f) { return f.p.y > H + 20.0f || f.p.x > W + 20.0f; }), flakes.end());
    if (snowVis < 0.05f) snowDepth = jmax (0.0f, snowDepth - dt * 0.08f);   // it melts away once the snow stops
    for (auto* b : floaters) b->frost += (frozen - b->frost) * jmin (1.0f, dt * 2.0f);
}

void BalloonEditor::drawSnowbank (Graphics& g) const
{
    if (snowDepth <= 0.002f) return;
    const float H = (float) designH, top = H - 4.0f - 22.0f * snowDepth;
    Path bank;
    bank.startNewSubPath (0.0f, H);
    for (int i = 0; i <= 14; ++i)   // a faceted drift
    {
        const float x = (float) designW * (float) i / 14.0f;
        bank.lineTo (x, top + 5.0f * std::sin ((float) i * 1.7f) + 3.0f * std::sin ((float) i * 0.6f));
    }
    bank.lineTo ((float) designW, H);
    bank.closeSubPath();
    g.setColour (Colours::white.withAlpha (0.92f));
    g.fillPath (bank);
}

// the snow switch: a snowflake that spins slowly (faster, and glowing, while it snows)
// the heatwave switch: a hot sun with the air wobbling underneath it
// the shop in a heatwave: warm light, hotter near the floor, and the air rippling upwards
// the shop in a heatwave: thin lines of hot air rising from the floor, each one fading as it climbs
void BalloonEditor::drawHeatShimmer (Graphics& g, Rectangle<float> full) const
{
    const float u = heatVis, t = (float) clock;
    g.setColour (bal::strip (4).withAlpha (0.08f * u));
    g.fillRect (full);
    for (int k = 0; k < 9; ++k)
    {
        const float life = std::fmod (t * 0.07f + (float) k / 9.0f, 1.0f);
        const float y = full.getBottom() - 30.0f - life * full.getHeight() * 0.75f;
        const float a = u * 0.7f * std::sin (life * MathConstants<float>::pi);
        if (a <= 0.01f) continue;
        Path w;
        const float amp = 3.0f + 4.0f * life, len = 120.0f + 30.0f * (float) (k % 3);
        for (float x = full.getX() - 20.0f; x <= full.getRight() + 20.0f; x += 8.0f)
        {
            const float yy = y + amp * std::sin (x / len * MathConstants<float>::twoPi + t * 1.3f + (float) k * 1.7f);
            if (x <= full.getX() - 20.0f) w.startNewSubPath (x, yy); else w.lineTo (x, yy);
        }
        g.setColour (bal::strip (k % 2 == 0 ? 5 : 4).withAlpha (a));
        g.strokePath (w, PathStrokeType (1.0f));
    }
}

// the auto weather switch: when it's on it says what the sky is doing
// the weather switch: a sun that hides behind clouds
// a little slide switch on the info card: the effects on, or stepped aside for the frozen balloons
// a switch on the card: the effects on, or stepped aside for the frozen balloons
void BalloonEditor::drawBypassSwitch (Graphics& g) const
{
    const auto r = bypassButton;
    bal::toggle (g, r.withTrimmedLeft (6.0f), bypassOn() ? "effects bypassed: frozen balloons only" : "effects on (click to bypass)",
                 bypassVis > 0.5f, false, bal::strip (4), bal::paperInk(), 10.0f);
}

//==============================================================================
// what a bunch is tied to: a folded strip in its colour, with its code
void BalloonEditor::drawBow (Graphics& g, Point<float> c, int hue) const
{
    static const char* const codes[numHues] = { "sm1", "pt2", "lp0", "gr3", "rv4", "st5", "ct6" };
    bal::knotStrip (g, c, bal::hue (hue), codes[jlimit (0, (int) numHues - 1, hue)]);
}

//==============================================================================
// The shelf: the effects stand on it in the order the sound goes through them
int BalloonEditor::anchorOfSlot (int slot) noexcept
{
    static const int fixedSlots[] = { 2, 5, 3, 6, 4 };   // spectral, pitch, grains, reverb, stutter
    return slot < fp::Engine::slotStore ? fixedSlots[slot] : firstBox + (slot - fp::Engine::slotStore);
}

std::vector<int> BalloonEditor::shelfItems() const
{
    std::vector<int> v;
    for (int slot : proc.getShelfOrder())
        if (slot >= 0 && ! anchors[anchorOfSlot (slot)].hidden) v.push_back (slot);
    return v;
}

void BalloonEditor::layoutShelf (const std::vector<int>& visibleOrder)
{
    const float span = shelfRight - shelfLeft, gap = span / (float) jmax (1, (int) visibleOrder.size());
    for (size_t i = 0; i < visibleOrder.size(); ++i)
        anchors[anchorOfSlot (visibleOrder[i])].slotX = shelfLeft + gap * ((float) i + 0.5f);
}

// the order, read off the shelf left to right; anything not standing on it keeps its place after them
void BalloonEditor::commitShelf()
{
    auto visible = shelfItems();
    std::stable_sort (visible.begin(), visible.end(), [this] (int a, int b) { return anchors[anchorOfSlot (a)].pos.x < anchors[anchorOfSlot (b)].pos.x; });
    const auto old = proc.getShelfOrder();
    if (! std::equal (visible.begin(), visible.end(), shelfItems().begin())) confetti ({ (shelfLeft + shelfRight) * 0.5f, shelfY + 20.0f }, 14);
    BalloonProcessor::Order o; o.fill (-1);
    size_t n = 0;
    for (int slot : visible) o[n++] = slot;
    for (int slot : old) if (std::find (visible.begin(), visible.end(), slot) == visible.end()) o[n++] = slot;
    proc.setShelfOrder (o);
    layoutShelf (visible);
}

// the row the effects stand in: a thin rule from in to out, their places numbered along it
void BalloonEditor::drawShelf (Graphics& g) const
{
    const float y = shelfY + 27.0f;
    const float x0 = shelfLeft - 10.0f, x1 = shelfRight + 10.0f;
    g.setColour (bal::ink().withAlpha (0.45f));
    g.fillRect (x0, y, x1 - x0, 1.0f);
    bal::label (g, "in", Rectangle<float> (40.0f, 12.0f).withCentre ({ x0 - 16.0f, y }), bal::ink(), 9.5f);
    bal::label (g, "out", Rectangle<float> (40.0f, 12.0f).withCentre ({ x1 + 18.0f, y }), bal::ink(), 9.5f);
    const auto items = shelfItems();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& a = anchors[anchorOfSlot (items[i])];
        const float x = a.slotX;
        g.setColour (bal::white());
        g.fillRect (x - 1.0f, y - 4.0f, 2.0f, 9.0f);
        bal::label (g, String ((int) i + 1), Rectangle<float> (16.0f, 10.0f).withCentre ({ x, y + 11.0f }), bal::ink().withAlpha (0.8f), 8.5f);
        if (i + 1 < items.size())
        {
            // on to the next one
            const float nx = anchors[anchorOfSlot (items[i + 1])].slotX, mx = (x + nx) * 0.5f;
            g.setColour (bal::ink().withAlpha (0.6f));
            g.drawLine (mx - 3.0f, y - 3.0f, mx + 2.0f, y + 0.5f, 1.0f);
            g.drawLine (mx - 3.0f, y + 4.0f, mx + 2.0f, y + 0.5f, 1.0f);
        }
    }
}

// a little wrapped present in the effect's colour; the strings tie onto the bow on its lid
// a store effect's box: a small wire cube in the effect's colour, its lid lit; the threads tie on at the top
void BalloonEditor::drawGiftBox (Graphics& g, Point<float> knot, int hue) const
{
    bal::box (g, knot, bal::hue (hue), (float) clock + (float) hue);
}

// a flat cartoon cloud: overlapping puffs, darker silhouette line, a lighter top. dark 0..1 = storm.
// a cloud: stacked arcs of thin line. dark 0..1 = storm.
void BalloonEditor::drawCloud (Graphics& g, Point<float> c, float s, float dark) const
{
    bal::cloud (g, c, s, dark, bal::white().interpolatedWith (bal::ink(), 0.5f * dark), bal::white().interpolatedWith (bal::ink(), dark));
}

// the helium tank on the floor, with its hose to the bow
// the helium canister on the floor (a wire box, its valve the house mark), on its rule
void BalloonEditor::drawTank (Graphics& g) const
{
    // its rule: slide it left and the frozen grains go back through the effects, right and they go straight out
    {
        const float y = (float) designH - 9.0f, x0 = railLeft + 8.0f, x1 = railRight + tankArea.getWidth() - 8.0f;
        const float k = thruValue;
        bal::bar (g, { x0, y - 6.0f, x1 - x0, 12.0f }, jlimit (0.0f, 1.0f, (tankArea.getCentreX() - x0) / (x1 - x0)), bal::line(), bal::ink(), bal::white(), 1.0f);
        bal::label (g, "< thru fx", Rectangle<float> (x0 - 62.0f, y - 6.0f, 58.0f, 12.0f), bal::ink().withAlpha (0.45f + 0.5f * k), 8.5f, Justification::centredRight);
        bal::label (g, "out >", Rectangle<float> (x1 + 5.0f, y - 6.0f, 40.0f, 12.0f), bal::ink().withAlpha (0.45f + 0.5f * (1.0f - k)), 8.5f, Justification::centredLeft);
    }
    const auto& bow = anchors[0].pos;
    const auto body = Rectangle<float> (tankArea.getX() + 4.0f, tankArea.getY() + 20.0f, 38.0f, 54.0f);
    const Point<float> nozzle (body.getCentreX() + 12.0f, body.getY() - 10.0f);

    // the hose to the loop's strip
    Path hose;
    hose.startNewSubPath (nozzle);
    hose.cubicTo (nozzle.x + 30.0f, nozzle.y - 30.0f, bow.x - 60.0f, bow.y + 30.0f, bow.x - 6.0f, bow.y + 4.0f);
    g.setColour ((heliumOn() ? bal::white() : bal::ink()).withAlpha (0.5f + 0.4f * heliumVis));
    g.strokePath (hose, PathStrokeType (1.0f));

    // the canister: a tall box seen from a little above
    const float d = 9.0f;
    const auto front = body.withTrimmedTop (d).withTrimmedRight (d);
    g.setColour (bal::line());
    g.drawRect (front, 1.0f);
    const Path lid = bal::poly ({ front.getTopLeft(), front.getTopLeft().translated (d, -d), front.getTopRight().translated (d, -d), front.getTopRight() });
    g.strokePath (lid, PathStrokeType (1.0f));
    g.drawLine ({ front.getTopRight().translated (d, -d), front.getBottomRight().translated (d, -d) }, 1.0f);
    g.drawLine ({ front.getBottomRight(), front.getBottomRight().translated (d, -d) }, 1.0f);
    bal::face (g, lid, 0.2f + 0.5f * heliumVis, 1.4f);
    bal::label (g, "he", front.withHeight (20.0f).translated (0.0f, 8.0f), bal::ink(), 11.0f, Justification::centred, true);
    // the valve: the house mark, filled while the helium is on, turning as it opens
    {
        const Point<float> vc (body.getCentreX(), body.getY() - 14.0f);
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::rotation (0.25f * std::sin (valveAngle), vc.x, vc.y));
        const auto m = Rectangle<float> (18.0f, 10.0f).withCentre (vc);
        if (heliumOn()) { g.setColour (bal::line()); g.fillPath (bal::ui::skewRect (m)); }
        g.setColour (bal::ink());
        g.strokePath (bal::ui::skewRect (m), PathStrokeType (1.0f));
    }
    g.setColour (bal::ink().withAlpha (0.8f));
    g.drawLine (body.getCentreX(), body.getY() - 9.0f, body.getCentreX(), front.getY() - d * 0.5f, 1.0f);
    g.drawLine (body.getCentreX() + 5.0f, body.getY() - 5.0f, nozzle.x, nozzle.y, 1.0f);
    bal::label (g, heliumOn() ? "helium: on" : "helium", Rectangle<float> (tankArea.getX() - 30.0f, tankArea.getBottom() + 1.0f, tankArea.getWidth() + 60.0f, 12.0f),
                bal::ink().withAlpha (heliumOn() ? 1.0f : 0.75f), 9.5f, Justification::centred, heliumOn());
}

// a hand pump: cylinder, a T-handle that pumps while you hold it, a spout with the balloon on it
// a hand pump: a thin cylinder, a T-handle that pumps while you hold it, a spout with the balloon on it
void BalloonEditor::drawPump (Graphics& g, const Pump& p, bool sound) const
{
    const float baseX = p.nozzle.x - 22.0f;
    const float floorY = (float) designH - 6.0f;
    const auto body = Rectangle<float> (baseX - 12.0f, p.nozzle.y + 8.0f, 24.0f, floorY - p.nozzle.y - 14.0f);
    const auto col = sound ? bal::hue (balloonTeal) : bal::line();
    const bool hover = p.area.contains (content.getMouseXYRelative().toFloat());

    // the foot
    g.setColour (bal::ink().withAlpha (0.7f));
    g.fillRect (Rectangle<float> (46.0f, 1.0f).withCentre ({ baseX, floorY - 1.0f }));
    // handle rod + T bar, going up and down while held
    const float pumpY = p.holding ? 8.0f * (0.5f + 0.5f * std::sin (p.handle)) : 0.0f;
    const float rodTop = body.getY() - 22.0f + pumpY;
    g.setColour (bal::ink());
    g.drawLine (baseX, rodTop, baseX, body.getY(), 1.0f);
    g.fillPath (bal::ui::skewRect (Rectangle<float> (30.0f, 5.0f).withCentre ({ baseX - 1.0f, rodTop })));

    // the cylinder: its outline in the pump's colour, a lit panel on it (brighter while it works)
    g.setColour (col.withAlpha (p.holding ? 0.35f : 0.0f));
    g.fillRect (body);
    g.setColour (col);
    g.drawRect (body, hover || p.holding ? 1.6f : 1.0f);
    bal::face (g, bal::poly ({ { body.getX() + 5.0f, body.getY() + 12.0f }, { body.getRight() - 5.0f, body.getY() + 6.0f },
                               { body.getRight() - 5.0f, body.getY() + 30.0f }, { body.getX() + 5.0f, body.getY() + 36.0f } }),
               p.holding ? 0.6f : 0.25f, 1.4f);
    if (sound)   // the pump that listens: three short lines
        for (int i = -1; i <= 1; ++i)
        {
            g.setColour (bal::ink());
            g.drawLine (body.getX() + 6.0f, body.getY() + 50.0f + (float) i * 4.0f, body.getRight() - 6.0f, body.getY() + 50.0f + (float) i * 4.0f, 1.0f);
        }
    // spout to the nozzle
    g.setColour (bal::ink());
    g.drawLine (body.getRight(), body.getY() + 6.0f, p.nozzle.x, body.getY() + 6.0f, 1.0f);
    g.drawLine (p.nozzle.x, body.getY() + 6.0f, p.nozzle.x, p.nozzle.y + 2.0f, 1.0f);
    bal::label (g, sound ? "catch" : "pop", Rectangle<float> (60.0f, 10.0f).withCentre ({ baseX, body.getBottom() - 9.0f }), bal::ink().withAlpha (0.8f), 8.5f);
}

void BalloonEditor::paintContent (Graphics& g)
{
    const auto full = Rectangle<float> (0, 0, (float) designW, (float) designH);

    // the field, and the preset's figure on it (the last one's fading out as the new one comes)
    g.setColour (bal::ground());
    g.fillRect (full);
    {
        const float u = ease (bgFade);
        const float level = jlimit (0.0f, 1.0f, proc.outputLevel.load() * 1.6f);
        const auto area = Rectangle<float> (150.0f, 80.0f, (float) designW - 330.0f, 330.0f);
        if (bgFrom != bgTo) bal::backdrop (g, area, bgFrom + 2, level, (float) clock, 0.55f * (1.0f - u), bal::line(), bal::white());
        bal::backdrop (g, area, bgTo + 2, level, (float) clock, 0.55f * (bgFrom == bgTo ? 1.0f : u), bal::line(), bal::white());
    }
    // the floor
    g.setColour (bal::ink().withAlpha (0.3f));
    g.fillRect (0.0f, (float) designH - 5.0f, (float) designW, 1.0f);
    if (skyArt != nullptr) skyArt->paintBehind (g);   // the aurora and the rainbow, in the sky behind everything
    drawTitle (g);

    if (rainVis > 0.0f)
    {
        g.setColour (bal::ink().withAlpha (0.12f * rainVis));
        g.fillRect (full);
        const float drop = -60.0f + 100.0f * fp::smoothstep (rainVis);
        const float xs[] = { 90.0f, 260.0f, 440.0f, 620.0f, 800.0f, 980.0f };
        for (int i = 0; i < 6; ++i)
            drawCloud (g, { xs[i] + 14.0f * std::sin (lnf.time * 0.12f + (float) i), drop + (float) ((i * 37) % 23) },
                       1.4f + 0.25f * (float) (i % 3), 1.0f);
    }
    if (cloudVis > 0.0f)
    {
        g.setColour (bal::ink().withAlpha (0.06f * cloudVis));
        g.fillRect (full);
    }
    if (snowVis > 0.0f || frozen > 0.0f)
    {
        // a cold, pale light that deepens as everything freezes
        g.setColour (Colours::white.withAlpha (0.06f * snowVis + 0.12f * frozen));
        g.fillRect (full);
    }
    if (heatVis > 0.0f) drawHeatShimmer (g, full);
    if (heliumVis > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (0.05f * heliumVis));
        g.fillRect (full);
    }

    drawShelf (g);
    drawTank (g);
    drawPump (g, popPump, false);
    drawPump (g, soundPump, true);

    // the balloons on the pumps
    if (popPump.size > 0.0f || popPump.holding)
        lnf.drawBalloon (g, pumpBalloonShape (popPump), popPump.hue,
                         popPump.holding ? semisText (proc.balloonFX.toneSemis.load()) : String(), "", false, 0.0f, 0.0f, 0.0f, 11.0f, -1.0f);
    if (soundPump.slot >= 0)
    {
        const float murk = (1.0f - std::pow (soundPump.size, 0.85f)) * 0.85f;   // the filter opens as it fills
        const auto sh = pumpBalloonShape (soundPump);
        lnf.drawBalloon (g, sh, soundPump.hue, semisText (nextSemis), "", false, 0.0f, 0.0f, murk, 12.0f, -1.0f);
        // recording
        g.setColour (bal::strip (4).withAlpha (0.6f + 0.4f * std::sin ((float) clock * 8.0f)));
        g.fillPath (bal::ui::skewRect (Rectangle<float> (9.0f, 6.0f).withCentre ({ sh.cx + sh.rx * 0.75f, sh.cy - sh.ry * 0.75f })));
    }
    // next pitch
    {
        const auto r = nextBadge;
        const bool hover = r.contains (content.getMouseXYRelative().toFloat());
        bal::chip (g, r, "next " + semisText (nextSemis), false, hover, bal::hue (balloonTeal), bal::ink(), 9.5f);
        if (fullFlash > 0.0f)
            bal::label (g, "6 max: pop one", r.translated (0.0f, -22.0f).withWidth (110.0f), bal::white().withAlpha (fullFlash), 9.5f, Justification::centredLeft, true);
    }

    // puddle + ripples
    if (puddle > 0.01f)
    {
        const auto& bow = anchors[0].pos;
        const float w = 60.0f + 420.0f * puddle, ht = 8.0f + 10.0f * puddle;
        auto pr = Rectangle<float> (w, ht).withCentre ({ bow.x, (float) designH - 6.0f });
        g.setColour (bal::strip (2).withAlpha (0.25f));
        g.fillEllipse (pr);
        g.setColour (bal::strip (7).withAlpha (0.8f));
        g.drawEllipse (pr, 1.0f);
    }
    for (auto& r : ripples)
    {
        const float a = 1.0f - r.age;
        g.setColour (bal::strip (7).withAlpha (0.8f * a));
        g.drawEllipse (Rectangle<float> (6.0f + 26.0f * r.age, 2.0f + 6.0f * r.age).withCentre (r.p), 1.0f);
    }

    drawSnowbank (g);

    // threads: taut ones are straight, slack ones droop to the floor
    for (auto* b : floaters)
    {
        if (! b->alive) continue;
        const auto& a = anchors[b->anchor].pos;
        const auto k = b->knotWorld();
        const float dist = k.getDistanceFrom (a);
        const float slack = jmax (0.0f, b->stringLength - dist);
        auto mid = (k + a) * 0.5f + Point<float> (0.0f, std::sqrt (slack * (b->stringLength + dist)) * 0.5f);
        mid.y = jmin (mid.y, (float) designH - 3.0f);
        Path str;
        str.startNewSubPath (k);
        str.quadraticTo (mid, a);
        g.setColour (bal::ink().withAlpha (0.5f));
        g.strokePath (str, PathStrokeType (0.8f));
    }
    for (auto& a : anchors)
    {
        if (a.fixed || a.hidden) continue;
        if (a.box) drawGiftBox (g, a.pos, a.hue);
        else drawBow (g, a.pos, a.hue);
    }

    // where the pin stands: a small wire block
    if (pin != nullptr)
    {
        const auto c = pin->home + Point<float> (-14.0f, 30.0f);
        bal::drawSolid (g, 1, c.translated (0.0f, 2.0f), 11.0f, 0.5f, 0.4f, bal::line(), 1.0f, 2, 0.3f);
        bal::label (g, "pin", Rectangle<float> (60.0f, 12.0f).withCentre ({ c.x, c.y + 24.0f }), bal::ink().withAlpha (0.8f), 9.0f);
    }

    drawPinkie (g);

    // the board, standing on the floor (click it to draw a grain shape or the eq)
    if (chalkboard != nullptr && ! chalkboard->isVisible())
    {
        chalkboard->drawEasel (g, chalkboard->restArea(), 0.0f);
        bal::label (g, "grains + eq", chalkboard->restArea().withY (chalkboard->restArea().getBottom() - 2.0f).withHeight (12.0f).expanded (20.0f, 0.0f),
                    bal::ink().withAlpha (0.8f), 8.5f);
    }

    // the tokens
    for (auto* pl : pills) drawToken (g, *pl);

    drawStoreSign (g);
    drawHelpButton (g);
    drawResetButton (g);
    drawRainboomButton (g);
    drawGumballButton (g);
    drawPopsButton (g);
    // (the weather switches are drawn by the weather row, on its own layer above the balloons)
}

// over the balloons: rain, splashes, helium puffs, confetti, rubber shreds, POP!
// over the balloons: rain, splashes, helium, confetti, shreds, pop
void BalloonEditor::paintOver (Graphics& g)
{
    drawSkyClouds (g);   // over the balloons: the ones inside peek through
    if (skyArt != nullptr) skyArt->paintOver (g);   // fog, the wind's streaks, hailstones, the twister
    for (auto& f : flakes)
    {
        const float tw = 0.7f + 0.3f * std::sin ((float) clock * 3.0f + f.phase);
        g.setColour (Colours::white.withAlpha (0.9f * tw));
        if (f.size > 4.5f)   // the big ones are three crossed lines
            for (int a = 0; a < 3; ++a)
            {
                const float ang = (float) a * MathConstants<float>::pi / 3.0f + f.phase;
                g.drawLine (f.p.x - std::cos (ang) * f.size, f.p.y - std::sin (ang) * f.size, f.p.x + std::cos (ang) * f.size, f.p.y + std::sin (ang) * f.size, 0.8f);
            }
        else g.fillRect (Rectangle<float> (f.size * 0.6f, f.size * 0.6f).withCentre (f.p));
    }
    for (auto& d : drops)
    {
        g.setColour (bal::white().withAlpha (0.8f));
        g.drawLine (Line<float> (d.p, d.p - d.v * (d.bounced ? 0.012f : 0.028f)), 1.0f);
    }
    for (auto& pf : puffs)
    {
        const float a = jlimit (0.0f, 1.0f, pf.life);
        g.setColour (Colours::white.withAlpha (0.6f * a));
        g.strokePath (bal::ui::skewRect (Rectangle<float> (4.0f + 10.0f * (1.0f - a), 3.0f + 6.0f * (1.0f - a)).withCentre (pf.p)), PathStrokeType (0.8f));
    }
    for (auto& pt : particles)
    {
        g.setColour (pt.c.withAlpha (jlimit (0.0f, 1.0f, pt.life * 1.5f)));
        if (pt.kind == 1)
            g.fillRect (Rectangle<float> (3.0f, 3.0f).withCentre (pt.p));
        else if (pt.kind == 2)
        {
            Path sh;
            sh.startNewSubPath (-6.0f, -2.0f); sh.lineTo (5.0f, -4.0f); sh.lineTo (3.0f, 4.0f); sh.closeSubPath();
            g.fillPath (sh, AffineTransform::rotation (pt.rot).translated (pt.p));
        }
        else
            g.fillPath (bal::ui::skewRect ({ -4.0f, -1.8f, 8.0f, 3.6f }), AffineTransform::rotation (pt.rot).scaled (1.0f, 0.6f + 0.4f * std::abs (std::sin (pt.rot * 1.3f))).translated (pt.p));
    }
    for (auto& b : bursts)
    {
        // a pop: the balloon's facets flying apart as a white wire star, and the word
        const float u = b.age / 0.5f;
        const float a = 1.0f - u;
        const float R = 30.0f * b.size * (1.0f + 1.5f * u);
        Path star;
        for (int i = 0; i < 20; ++i)
        {
            const float ang = (float) i * MathConstants<float>::twoPi / 20.0f;
            const float rr = (i % 2 == 0 ? R : R * 0.55f);
            const Point<float> q (b.p.x + std::cos (ang) * rr, b.p.y + std::sin (ang) * rr);
            if (i == 0) star.startNewSubPath (q); else star.lineTo (q);
        }
        star.closeSubPath();
        bal::face (g, star, 0.25f * a, 1.6f, Colours::white.withAlpha (a));
        bal::label (g, "pop", Rectangle<float> (R * 2.0f, R).withCentre (b.p), balloonColours (b.hue).fill.withAlpha (a).interpolatedWith (Colours::white.withAlpha (a), 0.5f), 11.0f + 7.0f * b.size, Justification::centred, true);
    }
    drawTrixie (g);
    if (flash > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (0.3f * flash * flash));
        g.fillRect (Rectangle<float> (0, 0, (float) designW, (float) designH));
        const float a = jmin (1.0f, flash * 1.6f);
        g.setColour (bal::strip (7).withAlpha (0.7f * a));
        g.strokePath (bolt, PathStrokeType (3.0f, PathStrokeType::mitered));
        g.setColour (Colours::white.withAlpha (a));
        g.strokePath (bolt, PathStrokeType (1.4f, PathStrokeType::mitered));
    }
}

//==============================================================================
// the order dice
void BalloonEditor::rollOrder()
{
    auto visible = shelfItems();
    if (visible.size() < 2) return;
    const auto before = visible;
    for (int tries = 0; tries < 8 && visible == before; ++tries)
        for (int i = (int) visible.size() - 1; i > 0; --i) std::swap (visible[(size_t) i], visible[(size_t) rnd.nextInt (i + 1)]);
    setVisibleOrder (visible);
}

// the effects in this order (the ones on the shelf; anything hidden keeps its place after them)
void BalloonEditor::setVisibleOrder (const std::vector<int>& visible)
{
    if (visible == shelfItems()) return;
    confetti ({ (shelfLeft + shelfRight) * 0.5f, shelfY + 20.0f }, 14);
    const auto old = proc.getShelfOrder();
    BalloonProcessor::Order o; o.fill (-1);
    size_t n = 0;
    for (int slot : visible) o[n++] = slot;
    for (int slot : old) if (std::find (visible.begin(), visible.end(), slot) == visible.end()) o[n++] = slot;
    proc.setShelfOrder (o);
    layoutShelf (visible);   // the bows and boxes hop to their new places
    for (int slot : visible) anchors[anchorOfSlot (slot)].vel.y -= 160.0f;
    noteActivity();
}

// The card waits above the window and the order list off the left edge; each slides in while the
// cursor is near it (and stays while you're using it).
void BalloonEditor::revealStep (float dt)
{
    const auto m = content.getMouseXYRelative().toFloat();
    const bool over = isShowing() && getScreenBounds().contains (Desktop::getMousePosition())
                      && (chalkboard == nullptr || ! chalkboard->isVisible()) && (library == nullptr || ! library->isVisible());
    // the card: near its corner of the window, or anywhere on it once it's out
    const auto home = cardHome.toFloat();
    const bool nearCard = over && (cardShown > 0.5f ? home.expanded (18.0f).contains (m)
                                                    : m.y < home.getBottom() && m.x < home.getRight() + 10.0f);
    const bool wantCard = nearCard || scaleBox.isPopupActive();
    const float before = cardShown;
    cardShown = jlimit (0.0f, 1.0f, cardShown + (wantCard ? dt * 6.0f : -dt * 3.0f));
    if (cardShown != before) placeCard();

    // the order list: the cursor at the left edge, or over the list once it's out
    if (orderRow != nullptr)
    {
        const auto list = orderRow->listArea();
        const bool nearList = over && (orderRow->shownAmount() > 0.5f ? list.expanded (20.0f).contains (m)
                                                                      : m.x < 28.0f && m.y > list.getY() - 10.0f && m.y < list.getBottom() + 10.0f);
        orderRow->step (dt, nearList || orderRow->isDragging());
    }
}

// the card: the scale and where the shift lands, the preset (its boxes are the panel's children) and the bypass
// the card: a slip of paper with the preset, the scale and where the shift lands (its boxes are the panel's children), and the bypass
void BalloonEditor::drawCard (Graphics& g) const
{
    auto rf = infoCard.toFloat();
    g.setColour (bal::paper());
    g.fillRect (rf);
    g.setColour (bal::paperInk());
    g.drawRect (rf, 1.0f);
    bal::label (g, "preset", Rectangle<float> (rf.getX() + 8.0f, rf.getY() + 7.0f, 50.0f, 22.0f), bal::paperInk(), 10.0f, Justification::centredLeft);
    bal::label (g, "scale", Rectangle<float> (rf.getX() + 8.0f, rf.getY() + 34.0f, 50.0f, 22.0f), bal::paperInk(), 10.0f, Justification::centredLeft);
    bal::label (g, intervalText + "   " + freezeText, infoCard.toFloat().withTrimmedTop (59.0f).withHeight (20.0f).reduced (8.0f, 0.0f), bal::paperInk().withAlpha (0.75f), 9.5f, Justification::centredLeft);
    drawBypassSwitch (g);
}

void BalloonEditor::CardPanel::paint (Graphics& g)
{
    g.addTransform (AffineTransform::translation ((float) -getX(), (float) -getY()));   // drawn in the room's coordinates
    ed.drawCard (g);
}

void BalloonEditor::CardPanel::mouseDown (const MouseEvent& e)
{
    ed.noteActivity();
    if (ed.bypassButton.contains (e.position + getPosition().toFloat()) && ed.bypassAtt != nullptr)
    {
        ed.bypassAtt->setValueAsCompleteGesture (ed.bypassOn() ? 0.0f : 1.0f);
        ed.confetti (ed.bypassButton.getCentre(), 10);
    }
}

void BalloonEditor::placeCard()
{
    const float u = fp::smoothstep (cardShown);
    const int dy = -roundToInt ((float) (cardHome.getBottom() + 8) * (1.0f - u));
    infoCard = cardHome.translated (0, dy);
    bypassButton = { (float) infoCard.getX() + 8.0f, (float) infoCard.getY() + 82.0f, (float) infoCard.getWidth() - 16.0f, 20.0f };
    if (cardPanel != nullptr)
    {
        cardPanel->setBounds (infoCard);
        scaleBox.setBounds (60, 34, infoCard.getWidth() - 68, 22);
        presetBox.setBounds (60, 7, infoCard.getWidth() - 68, 22);
        cardPanel->setVisible (cardShown > 0.0f);
    }
}
//==============================================================================
StoreShelves::StoreShelves (BalloonEditor& e) : ed (e)
{
    setWantsKeyboardFocus (true);
    setSize ((int) W, (int) H);
}

Rectangle<float> StoreShelves::tileArea (int i) const noexcept
{
    const float left = 14.0f, top = 62.0f, tw = (W - 2.0f * left) / (float) cols, th = 82.0f;
    return { left + tw * (float) (i % cols), top + th * (float) (i / cols), tw, th - 12.0f };
}

int StoreShelves::tileAt (Point<float> p) const noexcept
{
    for (int i = 0; i <= BalloonEditor::kStoreItems; ++i)
        if (tileArea (i).contains (p)) return i;
    return -1;
}

void StoreShelves::open (Rectangle<float> under)
{
    setBounds ((int) (under.getRight() - W), (int) (under.getBottom() + 4.0f), (int) W, (int) H);
    opening = true;
    hover = -1;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void StoreShelves::close()
{
    opening = false;
    hover = -1;
}

void StoreShelves::step (float dt)
{
    if (! isVisible()) return;
    t += dt;
    shown += ((opening ? 1.0f : 0.0f) - shown) * jmin (1.0f, dt * 14.0f);
    if (! opening && shown < 0.02f) { shown = 0.0f; setVisible (false); }
    if (opening && shown > 0.995f) shown = 1.0f;
    // it pops open from the sign: grows from its top-right corner
    const float k = 0.85f + 0.15f * fp::smoothstep (shown);
    setTransform (AffineTransform::scale (k, k, (float) getX() + W, (float) getY()));
    setAlpha (jmin (1.0f, shown * 1.4f));
    repaint();
}

bool StoreShelves::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    return false;
}

void StoreShelves::mouseMove (const MouseEvent& e) { const int h = tileAt (e.position); if (h != hover) { hover = h; repaint(); } }
void StoreShelves::mouseExit (const MouseEvent&)   { hover = -1; repaint(); }

void StoreShelves::mouseDown (const MouseEvent& e)
{
    const int i = tileAt (e.position);
    if (i < 0 || ! opening) return;
    ed.noteActivity();
    if (i == BalloonEditor::kStoreItems)
    {
        if (! std::any_of (std::begin (ed.storeShown), std::end (ed.storeShown), [] (bool b) { return b; })) return;
        ed.proc.popStruckRequest.fetch_or ((1u << fp::StoreFX::kNum) - 1u);
        for (int fx = 0; fx < BalloonEditor::kStoreItems; ++fx) ed.setBought (fx, false);
        return;
    }
    ed.setBought (i, ! ed.bought (i));
}

// The store: a sheet of paper with a balloon for every effect, four to a row, each row on a thin rule. The
// ones you own sit on a filled parallelogram; the lightning's loans are marked with a zigzag.
void StoreShelves::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const Colour inkC = bal::paperInk();
    g.setColour (bal::paper());
    g.fillRect (r);
    g.setColour (inkC);
    g.drawRect (r, 1.0f);
    bal::title (g, "store", Rectangle<float> (18.0f, 14.0f, W - 36.0f, 30.0f), inkC, 22.0f, Justification::centredLeft, true);
    bal::label (g, "click one to buy it, again to give it back", Rectangle<float> (18.0f, 20.0f, W - 36.0f, 22.0f), inkC.withAlpha (0.6f), 9.0f, Justification::centredRight);

    const uint32 struck = ed.proc.struckMask.load();
    for (int i = 0; i <= BalloonEditor::kStoreItems; ++i)
    {
        const auto a = tileArea (i);
        const bool hov = i == hover;
        if (i % cols == 0)
        {
            // the rule this row stands on
            g.setColour (bal::paperLine());
            g.fillRect (14.0f, a.getBottom() + 3.0f, W - 28.0f, 1.0f);
        }
        if (i == BalloonEditor::kStoreItems)
        {
            // "pop everything": the pin
            const bool any = std::any_of (std::begin (ed.storeShown), std::end (ed.storeShown), [] (bool b) { return b; });
            const auto c = Point<float> (a.getCentreX(), a.getY() + 30.0f);
            g.setColour (inkC.withAlpha (any ? 1.0f : 0.35f));
            g.drawLine (c.x - 8.0f, c.y + 12.0f, c.x + 8.0f, c.y - 14.0f, hov && any ? 1.6f : 1.0f);
            g.setColour (bal::strip (4).withAlpha (any ? 1.0f : 0.35f));
            g.fillPath (bal::ui::skewRect (Rectangle<float> (16.0f, 9.0f).withCentre ({ c.x + 7.0f, c.y - 14.0f })));
            bal::label (g, "pop everything", a.withTop (a.getBottom() - 22.0f), inkC.withAlpha (any ? 1.0f : 0.45f), 9.0f, Justification::centred, hov && any);
            continue;
        }
        const bool owned = ed.bought (i), lent = ! owned && i != BalloonEditor::stutterItem && ((struck >> i) & 1u) != 0;
        const auto hue = ed.storeBalloons[i][0] != nullptr ? balloonColours (ed.storeBalloons[i][0]->hue).fill : bal::strip (4);
        if (owned)
        {
            g.setColour (hue.withAlpha (0.5f));
            g.fillPath (bal::ui::skewRect (a.reduced (10.0f, 2.0f).withTrimmedTop (a.getHeight() - 26.0f), 0.3f));
        }
        // the balloon: bobbing if you're pointing at it, swaying gently if it's yours
        const float bob = hov ? 2.5f * std::sin (t * 6.0f) : (owned ? 1.2f * std::sin (t * 2.0f + (float) i) : 0.0f);
        bal::tinyBalloon (g, { a.getCentreX(), a.getBottom() - 24.0f + bob }, 12.0f, hue, owned || hov);
        if (hov)
        {
            g.setColour (inkC);
            g.strokePath (bal::ui::skewRect (a.reduced (10.0f, 2.0f).withTrimmedTop (a.getHeight() - 26.0f), 0.3f), PathStrokeType (1.0f));
        }
        if (lent)
        {
            // from the lightning: a zigzag
            const float bx = a.getRight() - 16.0f, by = a.getY() + 6.0f;
            g.setColour (inkC);
            g.strokePath (bal::polyline ({ { bx + 3.0f, by }, { bx - 3.0f, by + 8.0f }, { bx + 3.0f, by + 8.0f }, { bx - 3.0f, by + 16.0f } }), PathStrokeType (1.2f));
        }
        bal::label (g, BalloonEditor::storeItemName (i), a.withTop (a.getBottom() - 22.0f), inkC, 9.0f, Justification::centred, owned);
    }

    // what the one you're pointing at does
    String line = "point at a balloon to see what it does";
    if (hover >= 0 && hover < BalloonEditor::kStoreItems)
    {
        const auto name = BalloonEditor::storeItemName (hover);
        line = ed.bought (hover) ? name + ": yours. click to pop it"
             : hover != BalloonEditor::stutterItem && ((struck >> hover) & 1u) != 0 ? name + ": from the lightning. click to keep it"
                                              : name + ": " + BalloonEditor::storeItemWhat (hover);
    }
    else if (hover == BalloonEditor::kStoreItems) line = "pops every effect you bought";
    bal::label (g, line, Rectangle<float> (12.0f, H - 28.0f, W - 24.0f, 20.0f), inkC.withAlpha (0.8f), 9.5f);
}

//==============================================================================
// the weather row
WeatherBar::WeatherBar (BalloonEditor& e) : ed (e)
{
    // one row, all the same size: cloudy, rain, snow, heatwave, fog, wind, hail, aurora, twister, auto
    ed.cloudyButton = SkyArt::slot (SkyArt::slotCloudy);
    ed.cloudButton  = SkyArt::slot (SkyArt::slotRain);
    ed.snowButton   = SkyArt::slot (SkyArt::slotSnow);
    ed.heatButton   = SkyArt::slot (SkyArt::slotHeat);
    ed.autoButton   = SkyArt::slot (SkyArt::slotAuto);
    setBounds ((int) X, (int) Y, (int) W, (int) H);
    setInterceptsMouseClicks (true, false);
}

bool WeatherBar::isOn (int which) const noexcept
{
    switch (which)
    {
        case 0: return ed.cloudyOn();
        case 1: return ed.rainOn();
        case 2: return ed.snowOn();
        case 3: return ed.heatOn();
        case 5: case 6: case 7: case 8: return ed.skyValue[which - 5] > 0.5f;
        case 9: return ed.proc.twisterAmount.load() > 0.0f;
        default: return ed.autoOn();
    }
}

int WeatherBar::buttonAt (Point<float> p) const noexcept
{
    const Rectangle<float> r[] = { ed.cloudyButton, ed.cloudButton, ed.snowButton, ed.heatButton, ed.autoButton };
    for (int i = 0; i < 5; ++i) if (r[i].contains (p)) return i;
    if (const int b = SkyArt::buttonAt (p); b >= 0) return 5 + b;
    return -1;
}

bool WeatherBar::hitTest (int x, int y)
{
    return buttonAt (Point<float> ((float) x + X, (float) y + Y)) >= 0;
}

void WeatherBar::toggle (int which)
{
    if (which == 9) { ed.proc.requestTwister(); ed.noteActivity(); return; }   // the twister: one starts now
    if (which >= 5 && which <= 8)
    {
        auto& att = ed.skyAtt[which - 5];
        if (att == nullptr) return;
        const bool on = ed.skyValue[which - 5] <= 0.5f;
        // the wind blows the fog away: turning one on sends the other off
        if (on && which == 5 && ed.skyValue[1] > 0.5f && ed.skyAtt[1] != nullptr) ed.skyAtt[1]->setValueAsCompleteGesture (0.0f);
        if (on && which == 6 && ed.skyValue[0] > 0.5f && ed.skyAtt[0] != nullptr) ed.skyAtt[0]->setValueAsCompleteGesture (0.0f);
        att->setValueAsCompleteGesture (on ? 1.0f : 0.0f);
        ed.noteActivity();
        return;
    }
    std::unique_ptr<ParameterAttachment>* atts[] = { &ed.cloudyAtt, &ed.rainAtt, &ed.snowAtt, &ed.heatAtt, &ed.autoAtt };
    if (which < 0 || which > 4 || *atts[which] == nullptr) return;
    const bool turningOn = ! isOn (which);
    // the snow and the heatwave never fall together: turning one on sends the other away
    if (turningOn && which == 2 && ed.heatOn() && ed.heatAtt != nullptr) ed.heatAtt->setValueAsCompleteGesture (0.0f);
    if (turningOn && which == 3 && ed.snowOn() && ed.snowAtt != nullptr) ed.snowAtt->setValueAsCompleteGesture (0.0f);
    (*atts[which])->setValueAsCompleteGesture (turningOn ? 1.0f : 0.0f);
    ed.noteActivity();
}

void WeatherBar::mouseDown (const MouseEvent& e)
{
    const auto p = e.position + Point<float> (X, Y);
    const int b = buttonAt (p);
    if (b < 0) return;
    if (e.mods.isPopupMenu())
    {
        // right-click the snow or the heat: its fade times
        if ((b == 2 || b == 3) && ed.fadePopup != nullptr)
        {
            if (ed.fadePopup->showing() == b) ed.fadePopup->close();
            else ed.fadePopup->open (b, b == 2 ? ed.snowButton : ed.heatButton);
        }
        return;
    }
    if (ed.fadePopup != nullptr && ed.fadePopup->isOpen()) ed.fadePopup->close();
    toggle (b);
}
void WeatherBar::step (float) { repaint(); }   // the sun turns, the rain falls, the hot air ripples

void WeatherBar::paint (Graphics& g)
{
    g.addTransform (AffineTransform::translation (-X, -Y));   // the switches are placed in the room's coordinates
    if (ed.skyArt == nullptr) return;
    // which switch (see toggle) sits in each place of the row
    static const int which[SkyArt::kSlots] = { 0, 1, 2, 3, 5, 6, 7, 8, 9, 4 };
    static const char* const names[SkyArt::kSlots] = { "cloudy", "rain", "snow", "heatwave", "fog", "wind", "hail", "aurora", "twister", "auto" };
    static const char* const spells[fp::AutoWeather::numSpells] = { "clear", "cloudy", "rain", "storm", "snow", "heatwave", "fog", "wind", "hail", "aurora" };
    for (int i = 0; i < SkyArt::kSlots; ++i)
    {
        const bool on = isOn (which[i]);
        String label (names[i]);
        if (i == SkyArt::slotSnow && on && ed.frozen >= 1.0f) label = "frozen";
        if (i == SkyArt::slotAuto && on)   // (auto: what the sky is doing now)
            label = spells[(int) fp::AutoWeather::spellOf ({ ed.rainOn(), ed.cloudyOn(), ed.snowOn(), ed.heatOn(), ed.skyValue[0] > 0.5f, ed.skyValue[1] > 0.5f,
                                                             ed.skyValue[2] > 0.5f, ed.skyValue[3] > 0.5f })];
        ed.skyArt->drawSlot (g, i, on, label);
    }
}

//==============================================================================
// the newer weather's pictures follow the sound: how thick the fog is, how hard it's blowing, every
// hailstone's bounce, the rainbow and the aurora fading in and out, the twister turning
void BalloonEditor::skyStep (float dt)
{
    if (skyArt == nullptr) return;
    SkyArt::In in;
    in.fog = jmax (proc.fogAmount.load(), 0.0f);
    in.windGust = proc.windGust.load();
    in.windLean = proc.windLean.load();
    in.hailAmt = proc.hailAmount.load();
    in.hailHits = proc.hailHits.load();
    in.rainbow = proc.rainbowAmount.load();
    in.aurora = proc.auroraAmount.load();
    in.twister = proc.twisterAmount.load();
    in.twisterAngle = proc.twisterAngle.load();
    for (int j = 0; j < 4; ++j) in.curtains[j] = proc.auroraCurtain[(size_t) j].load();
    skyArt->step (dt, in, (float) designW, (float) designH, rnd);
}

// the gumball machine's button: a little machine under the rainboom's cloud
// the way to the sliders page: a small slider, under the boom's dart
void BalloonEditor::drawGumballButton (Graphics& g) const
{
    const auto r = gumballButton;
    const bool hover = r.contains (content.getMouseXYRelative().toFloat());
    bal::bar (g, { r.getX() + 6.0f, r.getCentreY() - 6.0f, 20.0f, 12.0f }, 0.55f + 0.2f * std::sin ((float) clock * 0.9f), bal::line(), bal::ink(), bal::white());
    bal::label (g, "sliders", r.withTrimmedLeft (34.0f), bal::ink().withAlpha (hover ? 1.0f : 0.9f), 10.5f, Justification::centredLeft, hover);
}

void BalloonEditor::openGumballs()
{
    if (gumballPage == nullptr) return;
    gumballPage->open();
    gumballPage->grabKeyboardFocus();
    noteActivity();
}

//==============================================================================
// the order list, down the left edge
OrderRow::OrderRow (BalloonEditor& e) : ed (e)
{
    setBounds ((int) (X - W), (int) Y, (int) W, (int) H);   // (starts tucked away off the side)
}

const std::vector<OrderRow::Chip>& OrderRow::layout() const
{
    const auto items = ed.shelfItems();
    if (items != layoutFor || layoutCache.empty() != items.empty()) { layoutFor = items; layoutCache = computeLayout(); }
    return layoutCache;
}

std::vector<OrderRow::Chip> OrderRow::computeLayout() const
{
    std::vector<Chip> v;
    int i = 0;
    for (int slot : ed.shelfItems())
    {
        String name;
        switch (slot)
        {
            case fp::Engine::slotSpectral: name = "smear"; break;
            case fp::Engine::slotPitch:    name = "pitch"; break;
            case fp::Engine::slotGrains:   name = "grains"; break;
            case fp::Engine::slotReverb:   name = "reverb"; break;
            case fp::Engine::slotStutter:  name = "stutter"; break;
            default: name = fp::StoreFX::infos()[(size_t) (slot - fp::Engine::slotStore)].shortName; break;
        }
        const int hue = ed.anchors[BalloonEditor::anchorOfSlot (slot)].hue;
        const float w = jmin (W - left - 14.0f, GlyphArrangement::getStringWidth (bal::sans (11.0f), name) + 30.0f);
        v.push_back ({ slot, name, balloonColours (hue).fill, { left, top + pitch * (float) i++, jmax (74.0f, w), chipH } });
    }
    return v;
}

Rectangle<float> OrderRow::listArea() const noexcept
{
    const auto& chips = layout();
    float right = dieArea().getRight() + 60.0f;
    for (auto& c : chips) right = jmax (right, c.r.getRight());
    const float bottom = chips.empty() ? top : chips.back().r.getBottom() + 14.0f;
    return { X, Y, right + 10.0f, bottom + 4.0f };
}

// the order as it's shown: while dragging, the dragged one sits where it would land
std::vector<int> OrderRow::displayOrder() const
{
    std::vector<int> o;
    for (auto& c : layout()) o.push_back (c.slot);
    if (dragSlot < 0 || dragTo < 0) return o;
    o.erase (std::remove (o.begin(), o.end(), dragSlot), o.end());
    o.insert (o.begin() + jlimit (0, (int) o.size(), dragTo), dragSlot);
    return o;
}

int OrderRow::indexAt (float chipTop) const noexcept
{
    const int n = (int) layout().size();
    return jlimit (0, jmax (0, n - 1), roundToInt ((chipTop - top) / pitch));
}

bool OrderRow::hitTest (int x, int y)
{
    if (shown < 0.5f) return false;
    const auto p = Point<float> ((float) x, (float) y);
    if (dieArea().expanded (2.0f).contains (p)) return true;
    for (auto& c : layout()) if (c.r.expanded (4.0f, 3.0f).contains (p)) return true;
    return false;
}

void OrderRow::mouseMove (const MouseEvent& e)
{
    const bool d = dieArea().expanded (2.0f).contains (e.position);
    int h = -1;
    if (! d) for (auto& c : layout()) if (c.r.expanded (4.0f, 3.0f).contains (e.position)) h = c.slot;
    if (d != hoverDie || h != hoverSlot) { hoverDie = d; hoverSlot = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}

void OrderRow::mouseExit (const MouseEvent&) { hoverDie = false; hoverSlot = -1; repaint(); }

void OrderRow::mouseDown (const MouseEvent& e)
{
    if (dieArea().expanded (2.0f).contains (e.position)) { spin = 1.0f; ed.rollOrder(); return; }
    for (auto& c : layout())
        if (c.r.expanded (4.0f, 3.0f).contains (e.position))
        {
            dragSlot = c.slot;
            grabDy = e.position.y - c.r.getY();
            dragY = downY = e.position.y;
            dragTo = indexAt (c.r.getY());
            moved = false;
            return;
        }
}

void OrderRow::mouseDrag (const MouseEvent& e)
{
    if (dragSlot < 0) return;
    dragY = e.position.y;
    moved |= std::abs (dragY - downY) > 3.0f;
    dragTo = indexAt (dragY - grabDy);
    repaint();
}

void OrderRow::mouseUp (const MouseEvent&)
{
    if (dragSlot < 0) return;
    const auto o = displayOrder();
    shownY[dragSlot] = dragY - grabDy;   // it settles from where you let go
    dragSlot = -1; dragTo = -1;
    if (moved) ed.setVisibleOrder (o);
    repaint();
}

void OrderRow::step (float dt, bool wanted)
{
    t += dt;
    const float before = shown;
    shown = jlimit (0.0f, 1.0f, shown + (wanted ? dt * 6.0f : -dt * 3.0f));
    if (shown != before)
        setTopLeftPosition (roundToInt (X - (listArea().getWidth() + 4.0f) * (1.0f - fp::smoothstep (shown))), (int) Y);
    if (shown <= 0.0f) return;
    spin = jmax (0.0f, spin - dt * 1.6f);
    bool moving = spin > 0.0f || dragSlot >= 0;
    const auto order = displayOrder();
    for (size_t i = 0; i < order.size(); ++i)
    {
        const float y = top + pitch * (float) i;
        auto it = shownY.find (order[i]);
        if (it == shownY.end()) { shownY[order[i]] = y; moving = true; continue; }
        const float d = y - it->second;
        if (std::abs (d) > 0.3f) { it->second += d * jmin (1.0f, dt * 12.0f); moving = true; }
        else it->second = y;
    }
    if (moving) repaint();
}

// The order: a slip of paper with the effects as folded strips, top (in) to bottom (out)
void OrderRow::paint (Graphics& g)
{
    const auto& chips = layout();
    const auto order = displayOrder();
    {
        const auto a = listArea().withPosition (0.0f, 0.0f);
        g.setColour (bal::paper());
        g.fillRect (a.withTrimmedLeft (-12.0f));
        g.setColour (bal::paperInk());
        g.drawRect (a.withTrimmedLeft (-12.0f), 1.0f);
        bal::label (g, "out", Rectangle<float> (left + 2.0f, a.getBottom() - 16.0f, 40.0f, 12.0f), bal::paperInk().withAlpha (0.8f), 9.0f, Justification::centredLeft);
    }
    auto drawChip = [&] (const Chip& c, Rectangle<float> r, bool lifted)
    {
        bal::foldStrip (g, r.withTrimmedRight (8.0f), c.colour, c.name, false, lifted || hoverSlot == c.slot ? 0.5f : 0.0f, 11.0f);
        if (lifted)
        {
            g.setColour (bal::paperInk());
            g.drawLine (r.getX(), r.getBottom() + 2.0f, r.getRight() - 8.0f, r.getBottom() + 2.0f, 1.0f);
        }
    };
    // the marks between them, down the way the sound goes
    for (size_t i = 0; i + 1 < order.size(); ++i)
    {
        const float x = left + 34.0f, y = top + pitch * (float) i + chipH + (pitch - chipH) * 0.5f;
        g.setColour (bal::paperInk().withAlpha (0.7f));
        g.drawLine (x - 3.0f, y - 1.4f, x, y + 1.4f, 1.0f);
        g.drawLine (x, y + 1.4f, x + 3.0f, y - 1.4f, 1.0f);
    }
    const Chip* lifted = nullptr;
    for (auto& c : chips)
    {
        if (c.slot == dragSlot) { lifted = &c; continue; }
        auto it = shownY.find (c.slot);
        drawChip (c, c.r.withY (it != shownY.end() ? it->second : c.r.getY()), false);
    }
    if (lifted != nullptr)   // the one in your hand, on top
        drawChip (*lifted, lifted->r.withY (jlimit (top - 6.0f, top + pitch * (float) (chips.size() - 1) + 6.0f, dragY - grabDy)).expanded (2.0f, 1.0f), true);

    // shuffle: the house mark, turning when it rolls, and "in" beside it
    const auto d = dieArea();
    const auto c = d.getCentre();
    {
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::rotation (spin * spin * 6.0f, c.x, c.y));
        const auto m = Rectangle<float> (16.0f, 9.0f).withCentre (c.translated (-1.0f, 0.0f));
        g.setColour (hoverDie ? bal::strip (5) : bal::strip (4));
        g.fillPath (bal::ui::skewRect (m));
        g.setColour (bal::paperInk());
        g.strokePath (bal::ui::skewRect (m), PathStrokeType (1.0f));
    }
    bal::label (g, "shuffle", Rectangle<float> (d.getRight() + 6.0f, 4.0f, 50.0f, 20.0f), bal::paperInk(), 9.0f, Justification::centredLeft, hoverDie);
    bal::label (g, "in", Rectangle<float> (d.getRight() + 62.0f, 4.0f, 24.0f, 20.0f), bal::paperInk().withAlpha (0.8f), 9.0f, Justification::centredLeft);
}

//==============================================================================
// the fade times of the snow or the heat, on a right-click
FadePopup::FadePopup (BalloonEditor& e) : ed (e)
{
    setSize ((int) W, (int) H);
    setWantsKeyboardFocus (true);
}

void FadePopup::open (int w, Rectangle<float> under)
{
    which = w;
    params[0] = ed.proc.apvts.getParameter (w == 2 ? ids::snowIn : ids::heatIn);
    params[1] = ed.proc.apvts.getParameter (w == 2 ? ids::snowOut : ids::heatOut);
    setTopLeftPosition (roundToInt (jlimit (6.0f, (float) BalloonEditor::designW - W - 6.0f, under.getCentreX() - W * 0.5f)),
                        roundToInt (under.getBottom() + 6.0f));
    setVisible (true);
    toFront (true);
    repaint();
}

void FadePopup::close() { dragRow = -1; setVisible (false); }

bool FadePopup::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    return false;
}

void FadePopup::setFromX (int row, float x)
{
    if (auto* prm = params[row])
    {
        const auto tr = trackArea (row);
        prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, (x - tr.getX()) / tr.getWidth()));
        repaint();
    }
}

void FadePopup::mouseDown (const MouseEvent& e)
{
    for (int row = 0; row < 2; ++row)
        if (params[row] != nullptr && rowArea (row).contains (e.position))
        {
            auto* prm = params[row];
            if (e.getNumberOfClicks() > 1)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
                prm->endChangeGesture();
                repaint();
                return;
            }
            dragRow = row;
            prm->beginChangeGesture();
            if (trackArea (row).expanded (6.0f, 8.0f).contains (e.position)) setFromX (row, e.position.x);
            return;
        }
}

void FadePopup::mouseDrag (const MouseEvent& e) { if (dragRow >= 0) setFromX (dragRow, e.position.x); }

void FadePopup::mouseUp (const MouseEvent&)
{
    if (dragRow >= 0 && params[dragRow] != nullptr) params[dragRow]->endChangeGesture();
    dragRow = -1;
}

void FadePopup::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    for (int row = 0; row < 2; ++row)
        if (params[row] != nullptr && rowArea (row).contains (e.position))
        {
            float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
            if (w.isReversed) d = -d;
            auto* prm = params[row];
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, prm->getValue() + d * 0.15f));
            prm->endChangeGesture();
            repaint();
            return;
        }
}

// the fade times of the snow or the heat: two of the suite's sliders on a slip of paper
void FadePopup::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const Colour inkC = bal::paperInk();
    g.setColour (bal::paper());
    g.fillRect (b);
    g.setColour (inkC);
    g.drawRect (b, 1.0f);
    const auto accent = which == 2 ? bal::strip (2) : bal::strip (4);
    bal::label (g, which == 2 ? "snow" : "heatwave", Rectangle<float> (12.0f, 6.0f, W - 24.0f, 18.0f), inkC, 10.5f, Justification::centredLeft, true);
    bal::label (g, "double-click: 5 s", Rectangle<float> (12.0f, 6.0f, W - 24.0f, 18.0f), inkC.withAlpha (0.6f), 9.0f, Justification::centredRight);
    const char* words[] = { "fade in", "fade out" };
    for (int row = 0; row < 2; ++row)
    {
        auto* prm = params[row];
        if (prm == nullptr) continue;
        const auto r = rowArea (row), tr = trackArea (row);
        bal::label (g, words[row], r.withWidth (tr.getX() - r.getX() - 4.0f), inkC, 10.0f, Justification::centredLeft);
        const float v = prm->getValue();
        bal::bar (g, tr, v, accent, inkC, inkC);
        bal::label (g, snowFadeText (prm->convertFrom0to1 (v)), r.withTrimmedLeft (tr.getRight() - r.getX() + 8.0f), inkC, 10.0f, Justification::centredLeft, true);
    }
}

//==============================================================================
// the preset filmstrip
const char* const PresetStrip::groups[4] = { "start", "feedback", "stretch", "useful" };

PresetStrip::PresetStrip (BalloonEditor& e) : ed (e)
{
    setWantsKeyboardFocus (true);
    setBounds (0, BalloonEditor::designH, BalloonEditor::designW, (int) H);
}

Rectangle<float> PresetStrip::chipArea (int g) const noexcept
{
    float x = left;
    for (int i = 0; i < g; ++i) x += GlyphArrangement::getStringWidth (bal::mono (10.0f, true), groups[i]) + 26.0f;
    return { x, 7.0f, GlyphArrangement::getStringWidth (bal::mono (10.0f, true), groups[g]) + 22.0f, 17.0f };
}

float PresetStrip::maxScroll() const noexcept
{
    const int n = (int) presets::list().size();
    return jmax (0.0f, left + (float) n * (frameW + gap) - gap + left - (float) getWidth());
}

int PresetStrip::frameAt (Point<float> p) const noexcept
{
    for (int i = 0; i < (int) presets::list().size(); ++i)
        if (frameArea (i).withHeight (frameH + 15.0f).contains (p)) return i;
    return -1;
}

void PresetStrip::scrollTo (int preset)
{
    scrollTarget = jlimit (0.0f, maxScroll(), left + (float) preset * (frameW + gap) + frameW * 0.5f - (float) getWidth() * 0.5f);
}

void PresetStrip::open()
{
    opening = true;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    scrollTo (jmax (0, ed.proc.currentPreset.load()));
    scroll = scrollTarget;
}

void PresetStrip::close() { opening = false; hover = -1; }

bool PresetStrip::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    if (k == KeyPress::leftKey || k == KeyPress::rightKey)
    {
        const int n = (int) presets::list().size();
        const int next = jlimit (0, n - 1, jmax (0, ed.proc.currentPreset.load()) + (k == KeyPress::leftKey ? -1 : 1));
        ed.proc.setCurrentProgram (next);
        scrollTo (next);
        return true;
    }
    return false;
}

void PresetStrip::step (float dt)
{
    if (! isVisible()) return;
    shown += ((opening ? 1.0f : 0.0f) - shown) * jmin (1.0f, dt * 11.0f);
    if (! opening && shown < 0.01f) { shown = 0.0f; setVisible (false); return; }
    if (opening && shown > 0.995f) shown = 1.0f;
    setTopLeftPosition (0, (int) std::round ((float) BalloonEditor::designH - H * fp::smoothstep (shown)));
    scroll += (scrollTarget - scroll) * jmin (1.0f, dt * 12.0f);
    repaint();
}

void PresetStrip::mouseMove (const MouseEvent& e) { const int h = frameAt (e.position); if (h != hover) { hover = h; repaint(); } }
void PresetStrip::mouseExit (const MouseEvent&)   { hover = -1; }

void PresetStrip::mouseDown (const MouseEvent& e)
{
    ed.noteActivity();
    if (closeArea().contains (e.position)) { close(); return; }
    for (int g = 0; g < 4; ++g)
        if (chipArea (g).contains (e.position))
        {
            for (int i = 0; i < (int) presets::list().size(); ++i)
                if (String (presets::list()[(size_t) i].group) == groups[g])
                {
                    scrollTarget = jlimit (0.0f, maxScroll(), left + (float) i * (frameW + gap) - left);
                    break;
                }
            return;
        }
    pressed = frameAt (e.position);
    dragged = false;
    downScroll = scrollTarget;
}

void PresetStrip::mouseDrag (const MouseEvent& e)
{
    if (std::abs (e.getDistanceFromDragStartX()) > 4) dragged = true;
    if (dragged) scroll = scrollTarget = jlimit (0.0f, maxScroll(), downScroll - (float) e.getDistanceFromDragStartX());
}

void PresetStrip::mouseUp (const MouseEvent& e)
{
    if (! dragged && pressed >= 0 && frameAt (e.position) == pressed)
    {
        ed.proc.setCurrentProgram (pressed);   // the room fades over to it
        scrollTo (pressed);
        close();
    }
    pressed = -1;
}

void PresetStrip::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    float d = std::abs (w.deltaX) >= std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
    if (w.isReversed) d = -d;
    scrollTarget = jlimit (0.0f, maxScroll(), scrollTarget + d * 420.0f);
}

// The presets: a strip of paper along the bottom, each preset a frame with its figure in it
void PresetStrip::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const Colour inkC = bal::paperInk();
    g.setColour (bal::paper());
    g.fillRect (r);
    g.setColour (inkC);
    g.fillRect (r.withHeight (1.0f));

    const int current = ed.proc.currentPreset.load();
    // which kind is in the middle of the strip, for the chips
    int middle = jlimit (0, (int) presets::list().size() - 1, (int) ((scroll + (float) getWidth() * 0.5f - left) / (frameW + gap)));
    const String midGroup = presets::list()[(size_t) middle].group;
    for (int k = 0; k < 4; ++k)
        bal::chip (g, chipArea (k), groups[k], midGroup == groups[k], false, bal::strip (5), inkC, 10.0f);
    bal::label (g, "scroll or drag along", Rectangle<float> ((float) getWidth() - 220.0f, 7.0f, 150.0f, 17.0f), inkC.withAlpha (0.6f), 9.0f, Justification::centredRight);
    bal::label (g, "close", closeArea().expanded (14.0f, 0.0f).translated (-10.0f, -1.0f), inkC, 9.5f, Justification::centred, true);

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (Rectangle<int> (0, (int) frameTop - 4, getWidth(), (int) (H - frameTop + 4)));
    for (int i = 0; i < (int) presets::list().size(); ++i)
    {
        const auto f = frameArea (i);
        if (f.getRight() < -10.0f || f.getX() > (float) getWidth() + 10.0f) continue;
        const bool on = i == current, hov = i == hover;
        {
            // the frame: the field, and the preset's figure on it
            Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (f.toNearestInt());
            g.setColour (bal::ground());
            g.fillRect (f);
            bal::backdrop (g, f.expanded (10.0f, 4.0f), presets::list()[(size_t) i].background + 2, 0.0f, 0.0f, on || hov ? 1.0f : 0.8f, bal::line(), bal::white());
        }
        if (on || hov)
        {
            g.setColour (inkC);
            g.drawRect (f.expanded (1.0f), on ? 2.0f : 1.0f);
        }
        bal::label (g, presets::list()[(size_t) i].name, Rectangle<float> (f.getX() - 4.0f, f.getBottom() + 2.0f, f.getWidth() + 8.0f, 13.0f), inkC.withAlpha (on || hov ? 1.0f : 0.8f), 9.0f, Justification::centred, on);
    }
}

// the rain switch: a little cloud
//==============================================================================
// The sonic rainboom
//==============================================================================
namespace
{
    // the rainbow, outside (red) to inside (violet)
    const Colour rainbowColours[7] = { Colour (0xffff3b47), Colour (0xffff9a1f), Colour (0xffffe03a), Colour (0xff3fd16b),
                                       Colour (0xff2f9bff), Colour (0xff5b5cf0), Colour (0xffc05cf5) };
    Point<float> bezier (Point<float> a, Point<float> c, Point<float> b, float u)
    {
        const float v = 1.0f - u;
        return a * (v * v) + c * (2.0f * v * u) + b * (u * u);
    }
}

void Balloon::nudge (float amount)
{
    if (isToggle || isChoice || isStepped || dragging || ! alive) return;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, param.convertTo0to1 (value) + amount)));
}

bool BalloonEditor::rainboomThrowsOn() const
{
    auto* p = proc.apvts.getRawParameterValue (ids::rbThrows);
    return p != nullptr && p->load() > 0.5f;
}

// a balloon flung hard enough: it carries on along its throw, and breaks the sound barrier a moment later
void BalloonEditor::rainboomFromThrow (Floater& f)
{
    rbThrown = &f;
    rbThrownAt = clock;
    f.vel = f.flickVel * 0.55f;
    proc.requestRainboom (0.35f);
}

Point<float> BalloonEditor::boomHead() const
{
    const auto& b = boomScene;
    if (rbThrown != nullptr && clock - rbThrownAt < b.diveLen + 0.5) return rbThrown->pos;
    const float u = jlimit (0.0f, 1.0f, (float) ((clock - b.diveStart) / jmax (0.05, b.diveLen)));
    // a cartoon swoop: down past where it'll break, then up into it, faster and faster
    const float side = b.at.x > b.from.x ? 1.0f : -1.0f;
    const Point<float> c1 (b.from.x * 0.25f + b.at.x * 0.75f - side * 60.0f, b.at.y + 230.0f);
    const Point<float> c2 (b.at.x - side * 170.0f, b.at.y + 150.0f);
    const float e = u * u, v = 1.0f - e;
    return b.from * (v * v * v) + c1 * (3.0f * v * v * e) + c2 * (3.0f * v * e * e) + b.at * (e * e * e);
}

void BalloonEditor::rainboomStep (float)
{
    auto& b = boomScene;
    const int dives = proc.rainboomDives.load(), booms = proc.rainboomBooms.load();
    const bool thrownNow = rbThrown != nullptr && clock - rbThrownAt < 0.4;
    if (dives != rbDivesSeen)
    {
        rbDivesSeen = dives;
        b.hasDive = true;
        b.diveStart = clock;
        b.diveLen = jmax (0.1f, proc.rainboomDiveLeft.load());
        b.trail.clear();
        if (thrownNow) { b.from = rbThrown->pos; b.at = rbThrown->pos + rbThrown->vel * (float) b.diveLen; }
        else
        {
            b.at = { 330.0f + 420.0f * rnd.nextFloat(), 120.0f + 140.0f * rnd.nextFloat() };
            b.from = { b.at.x < (float) designW * 0.5f ? -90.0f : (float) designW + 90.0f, -70.0f };
        }
    }
    if (booms != rbBoomsSeen)
    {
        const bool dived = clock - b.diveStart < b.diveLen + 0.3;
        rbBoomsSeen = booms;
        if (dived) b.at = boomHead();
        else
        {
            // (a hit in the sound: no dive, just the boom, somewhere in the sky)
            b.hasDive = false;
            b.trail.clear();
            b.at = { 330.0f + 420.0f * rnd.nextFloat(), 120.0f + 140.0f * rnd.nextFloat() };
        }
        b.boomTime = clock;
        if (auto* t = proc.apvts.getRawParameterValue (ids::rbTail)) b.tail = t->load();
        shockwave (b.at);
    }
    // the streak's path, for the rainbow it leaves
    const bool diving = clock - b.diveStart < b.diveLen;
    if (diving && b.hasDive)
    {
        const auto h = boomHead();
        if (b.trail.empty() || b.trail.back().getDistanceFrom (h) > 6.0f) b.trail.push_back (h);
    }
    // a balloon being dragged fast: its cone grows towards the barrier
    rbCharging = nullptr; rbCharge = 0.0f;
    if (rainboomThrowsOn())
        for (auto* f : floaters)
            if (f->dragging && Time::getMillisecondCounterHiRes() * 0.001 - f->lastMoveT < 0.07)
            {
                const float c = f->flickVel.getDistanceFromOrigin() / 2200.0f;
                if (c > 0.45f && c > rbCharge) { rbCharge = jmin (1.0f, c); rbCharging = f; }
            }
    const bool show = diving || clock - b.boomTime < (double) b.tail + 2.0 || rbCharging != nullptr;
    if (show != rainboomLayer->isVisible()) rainboomLayer->setVisible (show);
    if (show)
    {
        rainboomLayer->toFront (false);
        rainboomLayer->repaint();
    }
}

// the shockwave: every balloon knocked outward (more the nearer it is), and the near ones nudged to new values
void BalloonEditor::shockwave (Point<float> at)
{
    for (auto* f : floaters)
    {
        if (! f->alive) continue;
        const auto d = f->pos - at;
        const float dist = d.getDistanceFromOrigin();
        const float k = jmax (0.0f, 1.0f - dist / 700.0f);
        if (k <= 0.0f) continue;
        const auto dir = dist > 1.0f ? d / dist : Point<float> (0.0f, -1.0f);
        f->vel += dir * (900.0f * k);
        f->squashVel += 5.0f * k;
        if (auto* bl = dynamic_cast<Balloon*> (f))
            if (dist < 420.0f && BalloonProcessor::trixieMayTouch (bl->parameter().getParameterID()))
                bl->nudge ((rnd.nextFloat() - 0.5f) * 0.3f * (1.0f - dist / 420.0f));
    }
    pinkieHopVel = jmax (pinkieHopVel, 320.0f);
    wowUntil = clock + 2.8;   // amazed
    noteActivity();
}

void RainboomLayer::paint (Graphics& g)
{
    const auto& b = ed.boomScene;
    const double now = ed.clock;
    const float W = (float) BalloonEditor::designW, H = (float) BalloonEditor::designH;
    const float time = (float) now;

    // a balloon flung fast: a white cone behind it, growing as it nears the barrier
    if (ed.rbCharging != nullptr)
    {
        const auto v = ed.rbCharging->flickVel;
        const float sp = jmax (1.0f, v.getDistanceFromOrigin());
        rbart::comet (g, ed.rbCharging->pos, v / sp, 0.5f + 0.5f * ed.rbCharge, time, false);
    }

    // the dive: speed lines, the seven lines behind the dart, the dart with its cone
    const float u = (float) ((now - b.diveStart) / jmax (0.05, b.diveLen));
    const bool thrown = ed.rbThrown != nullptr && now - ed.rbThrownAt < b.diveLen + 0.5;
    if (b.hasDive && u >= 0.0f && u < 1.0f)
    {
        const auto head = ed.boomHead();
        auto pts = b.trail;
        pts.push_back (head);
        Point<float> dir (1.0f, 0.0f);
        if (pts.size() > 2) dir = head - pts[pts.size() - 3];
        dir /= jmax (1.0f, dir.getDistanceFromOrigin());
        Random r (7);
        for (int i = 0; i < 18; ++i)   // speed lines across the sky, along the dive
        {
            const Point<float> c (r.nextFloat() * W, r.nextFloat() * H);
            const float shift = std::fmod (time * 1400.0f + 300.0f * (float) i, 1600.0f) - 800.0f;
            const auto a = c + dir * shift, e = a + dir * (60.0f + 120.0f * u);
            g.setColour (Colours::white.withAlpha (0.15f + 0.4f * u * u));
            g.drawLine ({ a, e }, 1.0f);
        }
        if (thrown)
        {
            rbart::ribbon (g, pts, 14.0f + 16.0f * u, 1.0f, 0.0f, time);
            rbart::comet (g, head, dir, u, time, false);   // (the flung balloon is its own flyer)
        }
        else
        {
            // the dart out in front, the seven lines streaming from its tail
            const float dw = 64.0f;
            const auto tail = rbart::dartTail (head, dir, dw);
            rbart::ribbon (g, rbart::trailToTail (pts, head, tail, dw * 0.7f), 14.0f + 16.0f * u, 1.0f, 0.0f, time, false);
            rbart::comet (g, head, dir, u, time, false, false);
            rbart::dart (g, head, dir, dw);
        }
    }

    // the boom: the rainbow left along the path, the flash, the ring, the burst and the marks
    const float t = (float) (now - b.boomTime);
    if (t >= 0.0f && t < b.tail + 2.0f)
    {
        if (b.trail.size() > 1)
        {
            auto pts = b.trail;
            pts.push_back (b.at);
            const float a = 1.0f - rbart::smooth01 ((t - b.tail * 0.6f) / (b.tail * 0.4f + 1.0f));
            rbart::ribbon (g, pts, 30.0f + 8.0f * rbart::smooth01 (t / 1.0f), a, rbart::smooth01 ((t - 0.4f) / (b.tail + 0.6f)) * 0.9f, time);
            rbart::trailSparkles (g, pts, a, time, ed.rbBoomsSeen);
        }
        if (t < 0.35f)
        {
            g.setColour (Colours::white.withAlpha (0.5f * std::exp (-t / 0.08f)));
            g.fillAll();
        }
        if (t < 2.4f)
        {
            // flat and seen a little from above, opening out fast, red outside
            const float R = 24.0f + 1150.0f * (1.0f - std::exp (-t / 0.55f));
            const float bw = 9.0f + 12.0f * rbart::smooth01 (t / 0.8f);
            const auto at = b.at;
            rbart::donut (g, [=] (int k, float ang, float inset)
                          {
                              const float rr = jmax (1.0f, R - ((float) k + inset) * bw);
                              return at + Point<float> (std::cos (ang) * rr, std::sin (ang) * rr * 0.42f);
                          }, 1.0f - rbart::smooth01 (t / 2.4f), time);
            for (int j = 1; j <= 3; ++j)   // the shock itself: white rings inside the ring
            {
                const float rr = R * 0.22f * (float) j;
                g.setColour (Colours::white.withAlpha (0.6f * (1.0f - rbart::smooth01 (t / 1.6f))));
                g.drawEllipse (Rectangle<float> (2.0f * rr, 2.0f * rr * 0.42f).withCentre (at), 1.0f);
            }
        }
        rbart::burst (g, b.at, t);
        rbart::sparkles (g, b.at, t, ed.rbBoomsSeen);
        // and the dart is gone, out of the picture the way it was going
        if (b.hasDive && ! thrown && t < 0.7f && b.trail.size() > 2)
        {
            auto d = b.at - b.trail[b.trail.size() - 3];
            d /= jmax (1.0f, d.getDistanceFromOrigin());
            rbart::dart (g, b.at + d * (1900.0f * t), d, 64.0f, 1.0f - rbart::smooth01 ((t - 0.35f) / 0.35f));
        }
    }
}

// the button: Rainbow Dash's cloud, with a rainbow looping out of it
// the boom's button: a white dart with seven coloured lines behind it
void BalloonEditor::drawRainboomButton (Graphics& g) const
{
    const auto r = rainboomButton;
    const auto c = r.getCentre().translated (0.0f, -8.0f);
    const float t = (float) clock;
    const bool hover = r.contains (content.getMouseXYRelative().toFloat());
    const Point<float> dir (0.94f, -0.34f);
    const Point<float> nose = c + dir * (26.0f + (hover ? 3.0f * std::sin (t * 9.0f) : 0.0f));
    const auto tail = rbart::dartTail (nose, dir, 26.0f);
    const Point<float> side (-dir.y, dir.x);
    for (int k = 0; k < 7; ++k)
    {
        const float off = ((float) k - 3.0f) * 2.6f;
        const float len = 34.0f + 6.0f * std::sin (t * 1.3f + (float) k * 0.8f);
        g.setColour (bal::rainbow (k).withAlpha (hover ? 1.0f : 0.9f));
        g.drawLine ({ tail + side * off, tail + side * off - dir * len }, 1.4f);
    }
    rbart::dart (g, nose, dir, 26.0f, 1.0f);
    auto label = String ("boom");
    StringArray how;
    auto on = [this] (const char* id) { auto* v = proc.apvts.getRawParameterValue (id); return v != nullptr && v->load() > 0.5f; };
    if (on (ids::rbHits)) how.add ("hits");
    if (on (ids::rbStorms)) how.add ("storms");
    if (how.size() > 0) label << ": " << how.joinIntoString (", ");
    bal::label (g, label, r.withTop (r.getBottom() - 14.0f).expanded (10.0f, 0.0f), bal::ink().withAlpha (hover ? 1.0f : 0.9f), 9.5f, Justification::centred, hover);
}

// right-click the rainboom: what sets one off, how loud, how long the rainbow rings on
// right-click the boom: what sets one off, how loud, how long the rainbow rings on
void BalloonEditor::openRainboomMenu()
{
    auto prm = [this] (const char* id) { return proc.apvts.getParameter (id); };
    PopupMenu m;
    m.setLookAndFeel (&storeLook);
    storeLook.time = (float) clock;
    m.addSectionHeader ("sonic boom");
    PopupMenu::Item now ("boom now");
    now.itemID = 1; now.colour = bal::rainbow (0); now.shortcutKeyDescription = "a dive, then the boom";
    m.addItem (now);
    struct Sw { int id; const char* param; const char* name; const char* what; };
    const Sw sws[] = { { 2, ids::rbThrows, "on throws", "fling a balloon hard" },
                       { 3, ids::rbHits, "on hits", "a hit in your sound" },
                       { 4, ids::rbStorms, "after storms", "when the rain stops" } };
    for (int i = 0; i < 3; ++i)
    {
        PopupMenu::Item it (sws[i].name);
        it.itemID = sws[i].id; it.isTicked = prm (sws[i].param)->getValue() > 0.5f; it.colour = bal::rainbow (2 + i);
        it.shortcutKeyDescription = sws[i].what;
        m.addItem (it);
    }
    PopupMenu level, length;
    const float lv = prm (ids::rbLevel)->convertFrom0to1 (prm (ids::rbLevel)->getValue());
    for (int i = 0; i < 4; ++i)
    {
        const float v[] = { 0.25f, 0.5f, 0.7f, 1.0f };
        level.addItem (10 + i, String (roundToInt (v[i] * 100.0f)) + " %", true, std::abs (lv - v[i]) < 0.02f);
    }
    const float tl = prm (ids::rbTail)->convertFrom0to1 (prm (ids::rbTail)->getValue());
    for (int i = 0; i < 4; ++i)
    {
        const float v[] = { 2.0f, 5.0f, 8.0f, 12.0f };
        length.addItem (20 + i, String (roundToInt (v[i])) + " s", true, std::abs (tl - v[i]) < 0.05f);
    }
    m.addSubMenu ("level", level);
    m.addSubMenu ("rainbow length", length);
    const auto screen = content.localAreaToGlobal (rainboomButton.getSmallestIntegerContainer());
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (screen).withMinimumWidth (300).withParentComponent (this),
                     [safe = Component::SafePointer<BalloonEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         auto& ap = safe->proc.apvts;
                         auto setReal = [&ap] (const char* id, float v)
                         {
                             auto* p = ap.getParameter (id);
                             p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
                         };
                         if (r == 1) safe->proc.requestRainboom (0.7f);
                         else if (r >= 2 && r <= 4)
                         {
                             const char* id = r == 2 ? ids::rbThrows : r == 3 ? ids::rbHits : ids::rbStorms;
                             setReal (id, ap.getParameter (id)->getValue() > 0.5f ? 0.0f : 1.0f);
                         }
                         else if (r >= 10 && r < 14) { const float v[] = { 0.25f, 0.5f, 0.7f, 1.0f }; setReal (ids::rbLevel, v[r - 10]); }
                         else if (r >= 20 && r < 24) { const float v[] = { 2.0f, 5.0f, 8.0f, 12.0f }; setReal (ids::rbTail, v[r - 20]); }
                         safe->confetti (safe->rainboomButton.getCentre(), 8);
                     });
}

// (here, so the processor can be built and tested without its window)
juce::AudioProcessorEditor* BalloonProcessor::createEditor() { return new BalloonEditor (*this); }
