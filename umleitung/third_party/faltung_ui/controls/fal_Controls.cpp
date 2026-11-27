namespace fal::ui
{
// ---- draw -----------------------------------------------------------------------------------------
namespace draw
{
void slider (juce::Graphics& g, juce::Rectangle<float> b, const Theme& theme, const juce::String& label, const juce::String& valueText,
             float value01, float zero, float ghost, bool dragging, int labelW)
{
    const float cy = b.getCentreY();
    g.setColour (theme.ink);
    g.setFont (mono (11.0f));
    g.drawText (label, juce::Rectangle<float> (b.getX(), b.getY(), (float) labelW, b.getHeight()), juce::Justification::centredLeft, true);
    g.drawText (valueText, juce::Rectangle<float> (b.getRight() - 70.0f, b.getY(), 70.0f, b.getHeight()), juce::Justification::centredRight, true);

    auto t = b;
    t.removeFromLeft ((float) labelW);
    t.removeFromRight (70.0f);
    t = t.reduced (4.0f, 0.0f);
    g.setColour (theme.ink.withAlpha (0.35f));
    g.fillRect (t.getX(), cy, t.getWidth(), 1.0f);

    const float a = t.getX() + t.getWidth() * std::min (zero, value01);
    const float e = t.getX() + t.getWidth() * std::max (zero, value01);
    const float h = 9.0f;
    if (e - a > 0.5f)
    {
        g.setColour (theme.fill.withAlpha (dragging ? 1.0f : 0.92f));
        g.fillPath (skewRect ({ a, cy - h * 0.5f, e - a, h }));
    }
    const float x = t.getX() + t.getWidth() * value01;
    if (ghost >= 0.0f && std::abs (ghost - value01) > 0.004f)
    {
        const float gx = t.getX() + t.getWidth() * juce::jlimit (0.0f, 1.0f, ghost);
        g.setColour (theme.ink.withAlpha (0.55f));
        g.drawRect (juce::Rectangle<float> (gx - 1.5f, cy - 6.5f, 3.0f, 13.0f), 1.0f);
    }
    g.setColour (theme.edge);
    g.fillRect (x - 1.0f, cy - 5.5f, 2.0f, 11.0f);
}

void choice (juce::Graphics& g, juce::Rectangle<float> b, const Theme& theme, const juce::String& label, const juce::String& valueText, int labelW)
{
    g.setFont (mono (11.0f));
    g.setColour (theme.ink);
    const float lw = label.isEmpty() ? 0.0f : (float) labelW;
    g.drawText (label, b.removeFromLeft (lw), juce::Justification::centredLeft);
    g.setFont (mono (11.0f, true));
    g.drawText (valueText, b, juce::Justification::centredLeft);
    g.setColour (theme.ink.withAlpha (0.35f));
    g.fillRect (b.getX(), b.getBottom() - 2.0f, std::min (b.getWidth(), 90.0f), 1.0f);
}

void tabs (juce::Graphics& g, juce::Rectangle<float> b, const Theme& theme, const juce::String& label, const juce::StringArray& items, int index)
{
    g.setFont (mono (11.0f));
    if (label.trim().isNotEmpty())
    {
        g.setColour (theme.ink);
        g.drawText (label, b.removeFromLeft (64.0f), juce::Justification::centredLeft);
    }
    const int n = std::max (1, items.size());
    const float w = b.getWidth() / (float) n;
    for (int i = 0; i < items.size(); ++i)
    {
        auto r = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (3.0f, 2.0f);
        if (i == index)
        {
            g.setColour (theme.fill);
            g.fillPath (skewRect (r.reduced (2.0f, 0.0f), 0.35f));
        }
        g.setColour (theme.ink);
        g.drawText (items[i], r, juce::Justification::centred);
    }
}

int tabAt (juce::Rectangle<float> b, const juce::String& label, int n, float x)
{
    if (label.trim().isNotEmpty()) b.removeFromLeft (64.0f);
    return (int) ((x - b.getX()) / (b.getWidth() / (float) std::max (1, n)));
}

void toggle (juce::Graphics& g, juce::Rectangle<float> b, const Theme& theme, const juce::String& label, bool on)
{
    const auto r = juce::Rectangle<float> (b.getX(), b.getCentreY() - 5.0f, 18.0f, 10.0f);
    auto p = skewRect (r);
    if (on) { g.setColour (theme.fill); g.fillPath (p); }
    g.setColour (theme.ink);
    g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setFont (mono (11.0f));
    g.drawText (label, b.withTrimmedLeft (30.0f), juce::Justification::centredLeft);
}
} // namespace draw

// ---- ParamSlider ----------------------------------------------------------------------------------
ParamSlider::ParamSlider (juce::RangedAudioParameter& p, Theme t, juce::String l)
    : param (p), theme (t), label (l.isEmpty() ? p.getName (32) : l),
      attach (p, [this] (float v) { value01 = param.convertTo0to1 (v); repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (p.getName (64) + U ("  (drag · shift = fine · double-click = default)"));
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}
juce::Rectangle<float> ParamSlider::track() const
{
    auto b = getLocalBounds().toFloat();
    b.removeFromLeft ((float) labelW);
    b.removeFromRight (70.0f);
    return b.reduced (4.0f, 0.0f);
}
void ParamSlider::paint (juce::Graphics& g)
{
    const auto range = param.getNormalisableRange();
    const bool bipolar = range.start < 0.0f && range.end > 0.0f;
    draw::slider (g, getLocalBounds().toFloat(), theme, label, param.getCurrentValueAsText(), value01,
                  bipolar ? range.convertTo0to1 (0.0f) : 0.0f, ghost, dragging, labelW);
}
void ParamSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    dragging = true; dragStart = value01;
    attach.beginGesture();
    if (onTouch) onTouch();
    repaint();
}
void ParamSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float w = std::max (40.0f, track().getWidth());
    const float sens = e.mods.isShiftDown() ? 0.12f : 1.0f;
    const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY() * 0.6f) / w * sens;
    attach.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, dragStart + d)));
}
void ParamSlider::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attach.endGesture();
    repaint();
}
void ParamSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}
void ParamSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY + w.deltaX >= 0 ? 1.0f : -1.0f);
    attach.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value01 + step)));
}

// ---- ValueSlider ----------------------------------------------------------------------------------
ValueSlider::ValueSlider (Theme t, juce::String l, juce::NormalisableRange<float> r, float v, float def)
    : theme (t), label (std::move (l)), range (r), value (r.snapToLegalValue (v)), defaultValue (def)
{
    setTooltip (label + U ("  (drag · shift = fine · double-click = default)"));
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}
juce::Rectangle<float> ValueSlider::track() const
{
    auto b = getLocalBounds().toFloat();
    b.removeFromLeft ((float) labelW);
    b.removeFromRight (70.0f);
    return b.reduced (4.0f, 0.0f);
}
void ValueSlider::setValue (float v, juce::NotificationType n)
{
    v = range.snapToLegalValue (juce::jlimit (range.start, range.end, v));
    if (juce::exactlyEqual (v, value)) return;
    value = v;
    repaint();
    if (n != juce::dontSendNotification && onChange) onChange (value);
}
void ValueSlider::setFrom01 (float v01) { setValue (range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, v01))); }
void ValueSlider::paint (juce::Graphics& g)
{
    const bool bipolar = range.start < 0.0f && range.end > 0.0f;
    const juce::String text = textFromValue ? textFromValue (value) : juce::String (value, 2);
    draw::slider (g, getLocalBounds().toFloat(), theme, label, text, range.convertTo0to1 (value),
                  bipolar ? range.convertTo0to1 (0.0f) : 0.0f, ghost, dragging, labelW);
}
void ValueSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    dragging = true; dragStart = range.convertTo0to1 (value);
    if (onDragStart) onDragStart();
    repaint();
}
void ValueSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float w = std::max (40.0f, track().getWidth());
    const float sens = e.mods.isShiftDown() ? 0.12f : 1.0f;
    const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY() * 0.6f) / w * sens;
    setFrom01 (dragStart + d);
}
void ValueSlider::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if (onDragEnd) onDragEnd();
    repaint();
}
void ValueSlider::mouseDoubleClick (const juce::MouseEvent&) { setValue (defaultValue); }
void ValueSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY + w.deltaX >= 0 ? 1.0f : -1.0f);
    setFrom01 (range.convertTo0to1 (value) + step);
}

// ---- ChoiceBox ------------------------------------------------------------------------------------
ChoiceBox::ChoiceBox (juce::RangedAudioParameter& p, Theme t, juce::String l)
    : param (p), theme (t), label (l.isEmpty() ? p.getName (32) : l),
      attach (p, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (p.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
int ChoiceBox::numItems() const
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param)) return c->choices.size();
    return std::max (2, param.getNumSteps());
}
void ChoiceBox::set (int i)
{
    i = juce::jlimit (0, numItems() - 1, i);
    attach.setValueAsCompleteGesture ((float) i);
}
void ChoiceBox::paint (juce::Graphics& g)
{
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param);
    if (asTabs && c) draw::tabs (g, getLocalBounds().toFloat(), theme, label, c->choices, index);
    else             draw::choice (g, getLocalBounds().toFloat(), theme, label, param.getCurrentValueAsText(), labelW);
}
void ChoiceBox::mouseDown (const juce::MouseEvent& e)
{
    moved = false; dragStartIndex = index;
    if (asTabs)
    {
        set (draw::tabAt (getLocalBounds().toFloat(), label, numItems(), e.position.x));
        moved = true;
    }
}
void ChoiceBox::mouseDrag (const juce::MouseEvent& e)
{
    if (asTabs) return;
    const int d = -e.getDistanceFromDragStartY() / 12;
    if (d != 0) moved = true;
    if (moved) set (dragStartIndex + d);
}
void ChoiceBox::mouseUp (const juce::MouseEvent&)
{
    if (moved) return;
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param);
    if (! c) { set ((index + 1) % numItems()); return; }
    juce::PopupMenu m;
    for (int i = 0; i < c->choices.size(); ++i) m.addItem (i + 1, c->choices[i], true, i == index);
    juce::Component::SafePointer<ChoiceBox> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) { if (safe && r > 0) safe->set (r - 1); });
}

// ---- ValueChoice ----------------------------------------------------------------------------------
ValueChoice::ValueChoice (Theme t, juce::String l, juce::StringArray it, int i)
    : theme (t), label (std::move (l)), items (std::move (it)), index (juce::jlimit (0, std::max (0, items.size() - 1), i))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void ValueChoice::setIndex (int i, juce::NotificationType n)
{
    i = juce::jlimit (0, std::max (0, items.size() - 1), i);
    if (i == index) return;
    index = i;
    repaint();
    if (n != juce::dontSendNotification && onChange) onChange (index);
}
void ValueChoice::paint (juce::Graphics& g)
{
    if (asTabs) draw::tabs (g, getLocalBounds().toFloat(), theme, label, items, index);
    else        draw::choice (g, getLocalBounds().toFloat(), theme, label, items[index], labelW);
}
void ValueChoice::mouseDown (const juce::MouseEvent& e)
{
    moved = false; dragStartIndex = index;
    if (asTabs)
    {
        setIndex (draw::tabAt (getLocalBounds().toFloat(), label, items.size(), e.position.x));
        moved = true;
    }
}
void ValueChoice::mouseDrag (const juce::MouseEvent& e)
{
    if (asTabs) return;
    const int d = -e.getDistanceFromDragStartY() / 12;
    if (d != 0) moved = true;
    if (moved) setIndex (dragStartIndex + d);
}
void ValueChoice::mouseUp (const juce::MouseEvent&)
{
    if (moved || items.isEmpty()) return;
    juce::PopupMenu m;
    for (int i = 0; i < items.size(); ++i) m.addItem (i + 1, items[i], true, i == index);
    juce::Component::SafePointer<ValueChoice> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) { if (safe && r > 0) safe->setIndex (r - 1); });
}

// ---- ToggleBox ------------------------------------------------------------------------------------
ToggleBox::ToggleBox (juce::RangedAudioParameter& p, Theme t, juce::String l)
    : param (p), theme (t), label (l.isEmpty() ? p.getName (32) : l),
      attach (p, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (p.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void ToggleBox::paint (juce::Graphics& g) { draw::toggle (g, getLocalBounds().toFloat(), theme, label, on); }
void ToggleBox::mouseDown (const juce::MouseEvent&) { attach.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

// ---- ValueToggle ----------------------------------------------------------------------------------
ValueToggle::ValueToggle (Theme t, juce::String l, bool o) : theme (t), label (std::move (l)), on (o)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void ValueToggle::setOn (bool o, juce::NotificationType n)
{
    if (o == on) return;
    on = o;
    repaint();
    if (n != juce::dontSendNotification && onChange) onChange (on);
}
void ValueToggle::paint (juce::Graphics& g) { draw::toggle (g, getLocalBounds().toFloat(), theme, label, on); }

// ---- TextLink -------------------------------------------------------------------------------------
void TextLink::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (lit) { g.setColour (theme.fill); g.fillPath (skewRect (b.reduced (2.0f, 3.0f), 0.35f)); }
    g.setColour (theme.ink);
    g.setFont (mono (11.0f));
    g.drawText (text, b, juce::Justification::centred);
}

// ---- PresetBar ------------------------------------------------------------------------------------
PresetBar::PresetBar (juce::AudioProcessor& proc, Theme t) : p (proc), theme (t)
{
    startTimerHz (8);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("presets");
}
void PresetBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme.ink);
    g.setFont (mono (11.0f));
    g.drawText ("<", b.removeFromLeft (16.0f), juce::Justification::centred);
    g.drawText (">", b.removeFromRight (16.0f), juce::Justification::centred);
    g.setFont (mono (11.0f, true));
    g.drawText (p.getProgramName (p.getCurrentProgram()), b, juce::Justification::centred, true);
}
void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    const int n = p.getNumPrograms(), cur = p.getCurrentProgram();
    if (n <= 0) return;
    if (e.x < 18) { load (p, (cur - 1 + n) % n); return; }
    if (e.x > getWidth() - 18) { load (p, (cur + 1) % n); return; }
    juce::PopupMenu m;
    for (int i = 0; i < n; ++i) m.addItem (i + 1, p.getProgramName (i), true, i == cur);
    auto* proc = &p;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [proc] (int r) { if (r > 0) load (*proc, r - 1); });
}
} // namespace fal::ui
