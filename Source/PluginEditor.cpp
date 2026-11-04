#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
static float raw (SeaSickProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
static bool isOn (SeaSickProcessor& p, const char* id) { return raw (p, id) > 0.5f; }
static const String dot = String::fromUTF8 (" \xc2\xb7 ");

static void setParam (RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}


void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour)
{
    const float r = b.getHeight() * 0.5f;
    if (on) { g.setColour (onColour); g.fillRoundedRectangle (b, r); }
    else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawText (text, b, Justification::centred, false);
}

// =====================================================================================
// Flip card
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills and chips
// =====================================================================================
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size)
    : labels (std::move (l)), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { selected = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const int n = labels.size();
    const float gap = 8.0f, w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i) pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position)) { attachment->setValueAsCompleteGesture ((float) i); return; }
}

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, const String& tOn, float size, Colour c)
    : text (t), onText (tOn.isEmpty() ? t : tOn), textSize (size), onColour (c)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g) { pill (g, getLocalBounds().toFloat().reduced (1.0f), on ? onText : text, on, textSize, onColour); }
void ToggleChip::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

// =====================================================================================
// Look and feel
// =====================================================================================
void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 7.0f, right = (float) (x + w) - 7.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);
    g.setColour (ff::ink.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.85f));
    g.fillRoundedRectangle (left, cy - 3.0f, jmax (0.0f, pos - left), 6.0f, 3.0f);
    g.setColour (ff::ink);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    if (s.getProperties()["hidden"]) return;   // the hero dials draw themselves
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        g.drawLine ({ { c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f },
                      { c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f } }, 2.0f);
    }

    const bool red = s.getProperties()["red"];
    g.setColour (red ? ff::red : ff::ink);
    const float a = start + pos * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

static void setupRotary (Slider& s)
{
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    s.setMouseCursor (MouseCursor::PointingHandCursor);
}

// =====================================================================================
// Dials and sliders
// =====================================================================================
Dial::Dial (SeaSickProcessor& p, const String& paramId, const String& c, const String& h, bool redHand, int size)
    : caption (c), hint (h), knobSize (size)
{
    param = p.apvts.getParameter (paramId);
    setupRotary (slider);
    slider.getProperties().set ("red", redHand);
    slider.onValueChange = [this] { repaint(); };
    slider.onDragStart = [&p] { p.focusHere(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (38).withSizeKeepingCentre (knobSize, knobSize));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred, ff::dim);
    ff::label (g, hint.toUpperCase(), r.removeFromBottom (14), Justification::centred, ff::dim, 10.5f);
    const String value = valueText ? valueText() : param->getCurrentValueAsText();
    ff::label (g, value.toUpperCase(), r.removeFromBottom (22), Justification::centred, ff::ink, 15.0f);
}

LabelledSlider::LabelledSlider (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}

void LabelledSlider::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }

void LabelledSlider::paint (Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (16);
    ff::label (g, caption.toUpperCase(), top, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), top, Justification::centredRight, ff::ink, 11.0f);
}

// =====================================================================================
// Buttons
// =====================================================================================
void HoldButton::set (const String& t, bool l, Colour c)
{
    if (t == text && l == lit && c == litColour) return;
    text = t; lit = l; litColour = c;
    repaint();
}

void HoldButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (lit ? litColour : (pressed ? ff::cardHi : ff::card));
    g.fillRoundedRectangle (b, 10.0f);
    ff::label (g, text, getLocalBounds(), Justification::centred, lit ? ff::bg : ff::ink, 14.0f);
}

// =====================================================================================
// The water and the gate, drawn from the engine's own history
// =====================================================================================
void WaterScope::paint (Graphics& g)
{
    auto& eng = proc.getEngine();
    const auto b = getLocalBounds().toFloat();
    g.setColour (ff::ring);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (ff::line);
    for (int i = 1; i < 6; ++i) g.fillRect (b.getX() + b.getWidth() * (float) i / 6.0f, b.getY() + 6.0f, 1.0f, b.getHeight() - 12.0f);
    g.fillRect (b.getX(), b.getCentreY(), b.getWidth(), 1.0f);

    constexpr int points = 6 * ss::SeaEngine::kHistRate;
    const int pos = eng.histPos.load (std::memory_order_acquire);
    const float amp = b.getHeight() * 0.5f - 8.0f;
    auto trace = [&] (const std::array<std::atomic<float>, ss::SeaEngine::kHist>& h)
    {
        Path p;
        for (int i = 0; i < points; ++i)
        {
            const int k = (pos - points + i + ss::SeaEngine::kHist * 2) % ss::SeaEngine::kHist;
            const float v = jlimit (-1.0f, 1.0f, h[(size_t) k].load (std::memory_order_relaxed) * 1.8f);
            const float x = b.getX() + b.getWidth() * (float) i / (float) (points - 1), y = b.getCentreY() - v * amp;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (ff::faint);
    g.strokePath (trace (eng.histStep), PathStrokeType (1.5f));
    const bool frozen = eng.uiFrozen.load(), sick = eng.uiSeasick.load() > 0.3f;
    g.setColour (frozen ? ff::teal : (sick ? ff::sea : ff::ink));
    g.strokePath (trace (eng.histDelay), PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void GateMeter::paint (Graphics& g)
{
    auto& eng = proc.getEngine();
    const auto b = getLocalBounds().toFloat();
    g.setColour (ff::ring);
    g.fillRoundedRectangle (b, 8.0f);
    constexpr int points = 4 * ss::SeaEngine::kHistRate;
    const int pos = eng.histPos.load (std::memory_order_acquire);
    Path line, fill;
    fill.startNewSubPath (b.getX(), b.getBottom());
    for (int i = 0; i < points; ++i)
    {
        const int k = (pos - points + i + ss::SeaEngine::kHist * 2) % ss::SeaEngine::kHist;
        const float v = jlimit (0.0f, 1.0f, eng.histGate[(size_t) k].load (std::memory_order_relaxed));
        const float x = b.getX() + b.getWidth() * (float) i / (float) (points - 1), y = b.getBottom() - 6.0f - v * (b.getHeight() - 14.0f);
        if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (b.getRight(), b.getBottom());
    fill.closeSubPath();
    g.setColour (ff::coral.withAlpha (0.28f));
    g.fillPath (fill);
    g.setColour (ff::coral);
    g.strokePath (line, PathStrokeType (2.5f));
}

// =====================================================================================
// The footswitch strip
// =====================================================================================
void FootButton::tick()
{
    const bool on = ! isOn (proc, ParamIDs::bypass);
    String b, s;
    Colour l = ff::pin;
    bool lt = false;
    switch (sw)
    {
        case 0:
            b = on ? "ON" : "BYPASSED"; s = "TAP BYPASS" + dot + "HOLD MOMENTARY";
            lt = on; if (on) l = ff::red;
            break;
        case 1:
        {
            const bool latch = isOn (proc, ParamIDs::seasick), swell = isOn (proc, ParamIDs::swell);
            b = swell ? "SWELLING" : (latch ? "SEASICK" : "SEASICK OFF"); s = "TAP SEASICK" + dot + "HOLD SWELL";
            lt = latch || swell; if (lt) l = ff::sea;
            break;
        }
        case 2:
        {
            const bool fr = isOn (proc, ParamIDs::freeze);
            const bool bend = roundToInt (raw (proc, ParamIDs::freezeMode)) == ss::HoldBend;
            b = fr ? (bend ? "BEND HELD" : "FROZEN") : "FREEZE"; s = "TAP FREEZE" + dot + "HOLD MOMENTARY";
            lt = fr; if (fr) l = ff::teal;
            break;
        }
        default:
        {
            const int r = proc.getEngine().uiRerolls.load();
            if (r != lastRerolls) { lastRerolls = r; rerolledAt = nowSeconds(); }
            const bool pl = isOn (proc, ParamIDs::pluck), fresh = nowSeconds() - rerolledAt < 0.5;
            b = pl ? "PLUCKING" : (fresh ? "RE-ROLLED" : "RE-ROLL"); s = "TAP RE-ROLL" + dot + "HOLD PLUCK";
            lt = pl || fresh; if (lt) l = ff::coral;
            break;
        }
    }
    const bool d = (proc.uiFootDown.load() >> sw) & 1;
    if (b != big || s != small || l != led || lt != lit || d != down)
    {
        big = b; small = s; led = l; lit = lt; down = d;
        repaint();
    }
}

ExpressionBar::ExpressionBar (SeaSickProcessor& p)
    : proc (p),
      depthChip ("DEPTH", [this] { setParam (target, (float) names::ExpDepth); }, 11.0f),
      mixChip ("MIX", [this] { setParam (target, (float) names::ExpMix); }, 11.0f),
      lpgChip ("LPG", [this] { setParam (target, (float) names::ExpLpg); }, 11.0f)
{
    target = p.apvts.getParameter (ParamIDs::expTarget);
    depthChip.setColour (ff::sea);
    lpgChip.setColour (ff::coral);
    addAndMakeVisible (depthChip);
    addAndMakeVisible (mixChip);
    addAndMakeVisible (lpgChip);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void ExpressionBar::resized()
{
    depthChip.setBounds (getWidth() - 186, 0, 62, 24);
    mixChip.setBounds (getWidth() - 120, 0, 56, 24);
    lpgChip.setBounds (getWidth() - 60, 0, 60, 24);
}

void ExpressionBar::tick()
{
    const int t = roundToInt (raw (proc, ParamIDs::expTarget));
    depthChip.setLit (t == names::ExpDepth);
    mixChip.setLit (t == names::ExpMix);
    lpgChip.setLit (t == names::ExpLpg);
    const float v = proc.expressionLevel();
    if (t != lastTarget || std::abs (v - level) > 0.002f) { lastTarget = t; level = v; repaint(); }
    else if (proc.isCalibrating() || proc.isLearning()) repaint();
}

void ExpressionBar::paint (Graphics& g)
{
    const auto& t = names::expTargets();
    const int which = jlimit (0, (int) t.size() - 1, roundToInt (raw (proc, ParamIDs::expTarget)));
    String head = "EXPRESSION";
    if (which > names::ExpLpg) head += dot + String (t[(size_t) which].label).toUpperCase();
    head += "  v";
    if (proc.isCalibrating()) head = "ROCK HEEL TO TOE";
    else if (proc.learningWhat() == SeaSickProcessor::LearnExpression) head = "MOVE THE PEDAL";
    ff::label (g, head, { 0, 0, getWidth() - 190, 24 }, Justification::centredLeft, proc.isCalibrating() ? ff::ink : ff::dim, 11.0f);

    const float x0 = 26.0f, w = (float) getWidth() - 52.0f, y = 42.0f;
    g.setColour (ff::card);
    g.fillRoundedRectangle (x0, y - 4.0f, w, 8.0f, 4.0f);

    auto tick = [&] (float pos, float h, const String& text)
    {
        g.setColour (ff::dim);
        g.fillRect (x0 + w * pos - 1.0f, y - h * 0.5f, 2.0f, h);
        if (text.isNotEmpty())
            ff::label (g, text, Rectangle<float> (64.0f, 14.0f).withCentre ({ x0 + w * pos, y + 18.0f }).toNearestInt(), Justification::centred, ff::dim, 10.0f);
    };
    if (which == names::ExpDepth)
    {
        g.setColour (ff::sea.withAlpha (0.35f));
        g.fillRoundedRectangle (x0 + w * 0.8f, y - 4.0f, w * 0.2f, 8.0f, 4.0f);   // the seasick fifth
        tick (0.3f, 14.0f, "SUBTLE");
        tick (depthToExpression (0.4f), 14.0f, "");
        tick (0.7f, 14.0f, "DEEPER");
        tick (0.8f, 22.0f, "");
        tick (0.9f, 14.0f, "SEASICK");
    }

    const bool fresh = nowSeconds() - proc.lastExpressionMove() < 1.5;
    const Colour knob = which == names::ExpDepth ? ff::sea : (which == names::ExpLpg ? ff::coral : ff::ink);
    g.setColour (fresh ? knob : ff::dim);
    g.fillEllipse (x0 + w * level - 10.0f, y - 10.0f, 20.0f, 20.0f);
    g.setColour (ff::face);
    g.drawEllipse (x0 + w * level - 10.0f, y - 10.0f, 20.0f, 20.0f, 3.0f);

    ff::label (g, "HEEL", { 0, getHeight() - 14, 60, 14 }, Justification::centredLeft, ff::dim, 10.0f);
    ff::label (g, "TOE", { getWidth() - 60, getHeight() - 14, 60, 14 }, Justification::centredRight, ff::dim, 10.0f);
    const String note = which == names::ExpDepth ? "MOST OF THE TRAVEL IS THE SUBTLE ZONE"
                      : which == names::ExpLpg ? "HEEL DARK AND PLUCKY" + dot + "TOE BLOOMS" : "CLICK FOR CALIBRATE AND LEARN";
    ff::label (g, note, { 60, getHeight() - 14, getWidth() - 120, 14 }, Justification::centred, ff::dim, 10.0f);
}

void ExpressionBar::mouseDown (const MouseEvent&)
{
    proc.focusHere();
    PopupMenu targets;
    const auto& t = names::expTargets();
    const int cur = roundToInt (raw (proc, ParamIDs::expTarget));
    for (int i = 0; i < (int) t.size(); ++i) targets.addItem (100 + i, t[(size_t) i].label, true, i == cur);
    PopupMenu curves;
    const char* const curveNames[3] = { "Linear", "Slow start", "Fast start" };
    for (int i = 0; i < 3; ++i) curves.addItem (200 + i, curveNames[i], true, proc.pedals().curve == i);

    PopupMenu m;
    m.addSubMenu ("Expression moves", targets);
    m.addItem (1, "Calibrate heel and toe");
    m.addItem (2, "Learn expression pedal");
    m.addSubMenu ("Curve", curves);
    m.addItem (3, "Min, max and smoothing on the Pedal page");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) proc.calibrateExpression();
        else if (r == 2) proc.learnExpression();
        else if (r == 3) { if (showPedalPage) showPedalPage(); }
        else if (r >= 100 && r < 200) setParam (target, (float) (r - 100));
        else if (r >= 200 && r < 300) proc.setCurve (r - 200);
        repaint();
    });
}

void FootButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? ff::ring : ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (lit ? led : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> p (r.getX() + 24.0f, r.getCentreY());
    if (lit)
    {
        g.setColour (led.withAlpha (0.22f));
        g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
    }
    g.setColour (lit ? led : ff::pin);
    g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);

    auto text = r.withTrimmedLeft (46.0f).reduced (0.0f, 8.0f).toNearestInt();
    ff::label (g, String::charToString ((juce_wchar) ('A' + sw)), text.removeFromTop (14), Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, big, text.removeFromTop (24), Justification::centredLeft, lit ? ff::ink : ff::dim, 19.0f);
    ff::label (g, small, text, Justification::centredLeft, ff::dim, 10.0f);
}

void FootInputLabel::paint (Graphics& g)
{
    g.setColour (proc.pedalsDirect() ? ff::green : ff::pin);
    g.fillEllipse (0.0f, (float) getHeight() * 0.5f - 5.0f, 10.0f, 10.0f);
    ff::label (g, proc.pedalsDirect() ? "FOOTSWITCHES READY  v" : "FOOTSWITCHES  v", getLocalBounds().withTrimmedLeft (16), Justification::centredLeft, ff::dim, 12.0f);
}

void FootInputLabel::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    m.addSectionHeader ("Footswitch input");
    m.addItem (-1, proc.footInputStatus(), false, false);
    const auto chosen = proc.footDevice();
    m.addItem (10, "Find my footswitch automatically", true, chosen.isEmpty());
    const auto devices = DirectMidiInput::deviceNames();
    for (int i = 0; i < devices.size() && i < 40; ++i)
        m.addItem (100 + i, "Use " + devices[i], true, chosen == devices[i]);
    m.addItem (11, "Only from a MIDI track in Ableton", true, chosen == "none");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, devices] (int r)
    {
        if (r == 10) proc.setFootDevice ({});
        else if (r == 11) proc.setFootDevice ("none");
        else if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
        repaint();
    });
}

// =====================================================================================
// Pedal page
// =====================================================================================
FootswitchCard::FootswitchCard (SeaSickProcessor& p, int s)
    : proc (p), sw (s),
      learn ("LEARN", [this] { proc.learnFootswitch (sw); }),
      reset ("DEFAULTS", [this] { proc.resetFootswitches(); })
{
    letter.setTextHeight (54.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    addAndMakeVisible (letter);
    addAndMakeVisible (learn);
    addAndMakeVisible (reset);
}

void FootswitchCard::resized()
{
    letter.setBounds (16, 40, getWidth() - 32, 90);
    learn.setBounds (16, getHeight() - 50, (getWidth() - 40) / 2, 32);
    reset.setBounds (24 + (getWidth() - 40) / 2, getHeight() - 50, (getWidth() - 40) / 2, 32);
}

void FootswitchCard::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    ff::label (g, "FOOTSWITCH", { 16, 12, 200, 18 });

    static const char* const taps[4] = { "BYPASS", "SEASICK", "FREEZE", "RE-ROLL" };
    static const char* const holds[4] = { "MOMENTARY", "SWELL", "WHILE HELD", "PLUCK" };
    static const char* const notes[4] = { "ACTS ON THE PRESS", "DEEPER, FASTER, SHARPER", "SETTLE OR HOLD THE BEND", "A FRESH LURCH NOW" };
    static const char* const combos[4] = { "A+B  CALM", "A+B  CALM", "C+D  NEXT COPY", "C+D HELD 1 S  KEEPER" };
    struct Row { const char* k; String v; };
    const Row rows[3] = { { "TAP", taps[sw] }, { "HOLD", holds[sw] }, { "LISTENS FOR", sends } };
    const int w = getWidth() - 32;
    int y = 146;
    for (const auto& r : rows)
    {
        g.setColour (ff::ring);
        g.fillRect (16, y, w, 2);
        ff::label (g, r.k, { 16, y + 4, 100, 30 }, Justification::centredLeft, ff::dim, 12.0f);
        ff::label (g, r.v, { 16, y + 4, w, 30 }, Justification::centredRight, ff::ink, 13.0f);
        y += 38;
    }
    g.setColour (ff::ring);
    g.fillRect (16, y, w, 2);
    ff::label (g, notes[sw], { 16, y + 8, w, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, combos[sw], { 16, y + 30, w, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    if (sw >= 2) ff::label (g, sw == 2 ? "C+D HELD 1 S  KEEPER" : "C+D  NEXT COPY", { 16, y + 50, w, 16 }, Justification::centredLeft, ff::dim, 11.0f);
}

void FootswitchCard::tick (double dt)
{
    const auto& s = proc.pedals();
    const String t = "CH " + String (s.fsChannel[(size_t) sw]) + dot + "CC " + String (s.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    learn.setLit (proc.learningWhat() == sw);
    learn.setText (proc.learningWhat() == sw ? "PRESS IT" : "LEARN");
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    letter.setAlpha (down ? 0.6f : 1.0f);
    letter.advance (dt);
}

// =====================================================================================
// Pebble: floats on the water drawn from the real modulation. Rocks and tilts with it, goes
// faintly green when it gets seasick, bobs up as each note opens the gate, holds still with
// its eyes shut on Freeze, lurches on Re-roll, dozes when bypassed.
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    auto& eng = proc.getEngine();
    const float fdt = (float) dt;
    asleep = isOn (proc, ParamIDs::bypass);
    frozen = ! asleep && isOn (proc, ParamIDs::freeze);
    bending = frozen && roundToInt (raw (proc, ParamIDs::freezeMode)) == ss::HoldBend;
    plucking = ! asleep && isOn (proc, ParamIDs::pluck);
    depthNorm = raw (proc, ParamIDs::depth);
    gate = asleep ? 0.0f : eng.uiGate.load();

    // the water line: the last three seconds of the delay's movement
    const int pos = eng.histPos.load (std::memory_order_acquire);
    for (int i = 0; i < (int) water.size(); ++i)
    {
        const int k = (pos - 3 * ss::SeaEngine::kHistRate + i * 3 * ss::SeaEngine::kHistRate / ((int) water.size() - 1) + ss::SeaEngine::kHist * 2) % ss::SeaEngine::kHist;
        water[(size_t) i] = jlimit (-1.0f, 1.0f, eng.histDelay[(size_t) k].load (std::memory_order_relaxed) * 1.8f);
    }

    const float sickTarget = asleep ? 0.0f : jlimit (0.0f, 1.0f, eng.uiSeasick.load() * 0.8f + jmax (0.0f, eng.uiWobble.load() - 7.0f) / 12.0f);
    sick += (sickTarget - sick) * jmin (1.0f, fdt * 3.0f);

    const float last = water.back(), slope = (water.back() - water[water.size() - 3]) * 2.0f;
    const float leanTarget = t - proc.lastExpressionMove() < 1.5 ? (proc.expressionLevel() - 0.5f) * 0.3f : 0.0f;
    lean += (leanTarget - lean) * jmin (1.0f, fdt * 6.0f);
    const float tiltTarget = asleep ? 0.08f : jlimit (-0.5f, 0.5f, last * 0.25f * (0.5f + depthNorm) + slope
                                                       + 0.05f * sick * std::sin ((float) t * 1.7f)) + lean
                                                + lurch * 0.35f * std::sin ((float) t * 30.0f);
    tilt += (tiltTarget - tilt) * jmin (1.0f, fdt / 0.12f);

    // a spring: each note's gate opening throws Pebble up, the water brings it back
    bobV += (gate - bob) * fdt * 60.0f - bobV * fdt * 9.0f;
    bob = jlimit (-0.3f, 1.2f, bob + bobV * fdt);

    if (proc.uiFootPulse.load() != lastFoot) { lastFoot = proc.uiFootPulse.load(); hop = 1.0f; }
    if (proc.uiKeepPulse.load() != lastKeep) { lastKeep = proc.uiKeepPulse.load(); nod = 1.0f; }
    if (proc.uiCalmPulse.load() != lastCalm) { lastCalm = proc.uiCalmPulse.load(); calm = 1.0f; }
    if (eng.uiRerolls.load() != lastReroll) { lastReroll = eng.uiRerolls.load(); lurch = 1.0f; }
    hop = jmax (0.0f, hop - fdt * 4.0f);
    nod = jmax (0.0f, nod - fdt * 1.6f);
    calm = jmax (0.0f, calm - fdt * 0.6f);
    lurch = jmax (0.0f, lurch - fdt * 2.2f);

    if (t > nextBlink) { blinking = true; blinkUntil = t + 0.14; nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 4.0; }
    if (blinking && t > blinkUntil) blinking = false;
    repaint();
}

String Pebble::mood() const
{
    const double ago = nowSeconds() - proc.uiMessageAt;
    if (ago < 2.5 && proc.uiMessage.isNotEmpty()) return proc.uiMessage;
    if (asleep) return "DOZING";
    if (calm > 0.3f) return "CALM";
    if (bending) return "HOLDING THE BEND";
    if (frozen) return "HOLDING STILL";
    if (plucking) return "PLUCKING";
    if (proc.getEngine().uiSeasick.load() > 0.5f) return "SEASICK";
    if (sick > 0.3f) return "QUEASY";
    if (gate > 0.6f) return "BLOOMING";
    return "CALM WATER";
}

String Pebble::hint() const
{
    if (asleep) return "TAP A TO WAKE PEBBLE";
    if (calm > 0.3f) return "BACK TO THE KNOBS";
    if (bending) return "THE BEND DRIFTS HOME" + dot + "LET GO OF C";
    if (frozen) return "THE WATER STOPPED" + dot + "TAP C TO LET IT FLOW";
    if (plucking) return "LET GO OF D TO OPEN THE GATE";
    if (proc.getEngine().uiSeasick.load() > 0.5f) return "TAP B OR A+B TO CALM DOWN";
    return "ROCKS WITH THE WATER" + dot + "BOBS WITH THE GATE";
}

void Pebble::paint (Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight(), t = (float) now;
    const float wy = h * 0.74f, waveAmp = 24.0f * (0.4f + depthNorm);

    auto waterPath = [&]
    {
        Path p;
        for (int i = 0; i < (int) water.size(); ++i)
        {
            const float x = w * (float) i / (float) (water.size() - 1), y = wy - water[(size_t) i] * waveAmp;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    // the body, floating
    const float cx = w * 0.5f, cy = h * 0.58f - bob * 30.0f - hop * 10.0f + (asleep ? 10.0f : 0.0f);
    const float squash = hop * 0.08f + nod * 0.05f * std::sin (nod * 9.0f);
    const float breathe = asleep ? 1.0f + 0.025f * std::sin (t * 1.6f) : 1.0f;
    const auto body = AffineTransform::scale ((1.0f + squash) * breathe, (1.0f - squash) / breathe)
                          .rotated (tilt).translated (cx, cy);
    auto ell = [&] (float x, float y, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (x - rx, y - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, body);
    };
    auto stroke = [&] (Path& p, float width)
    {
        g.setColour (ff::faceInk);
        g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded), body);
    };

    ell (7, 9, 62, 52, ff::blobShade.interpolatedWith (Colour (0xff1f6b4a), sick * 0.55f));
    ell (0, 0, 62, 52, ff::blob.interpolatedWith (ff::sea, sick * 0.55f));

    const bool closed = asleep || frozen || blinking;
    for (float ex : { -22.0f, 22.0f })
    {
        if (closed)
        {
            Path lid; lid.startNewSubPath (ex - 11, -6); lid.quadraticTo (ex, 2, ex + 11, -6);
            stroke (lid, 5.0f);
        }
        else
        {
            ell (ex, -8, 15, 15 - sick * 3, ff::eye);
            const float swirl = sick > 0.5f ? 3.0f * std::sin (t * 3.0f + ex) : 0.0f;
            const float look = 1.5f * std::sin (t * 1.3f) + tilt * 10.0f;
            ell (ex + look + swirl, -6 + std::cos (t * 3.0f + ex) * swirl, 7, 7, ff::faceInk);
        }
    }

    if (asleep) { Path m; m.startNewSubPath (-8, 24); m.lineTo (8, 24); stroke (m, 6.0f); }
    else if (sick > 0.45f)
    {
        Path m; m.startNewSubPath (-18, 24);
        for (int k = 1; k <= 6; ++k) m.lineTo (-18.0f + (float) k * 6.0f, 24.0f + ((k % 2) ? -4.0f : 4.0f));
        stroke (m, 5.0f);
    }
    else if (lurch > 0.3f) ell (0, 24, 7, 8, ff::faceInk);
    else if (frozen) { Path m; m.startNewSubPath (-10, 24); m.lineTo (10, 24); stroke (m, 6.0f); }
    else { Path m; m.startNewSubPath (-12, 20); m.quadraticTo (0, 32 + gate * 4.0f, 12, 20); stroke (m, 6.0f); }

    // the water over Pebble's belly, so it floats in it
    Path sea = waterPath();
    Path fill (sea);
    fill.lineTo (w, h); fill.lineTo (0, h); fill.closeSubPath();
    g.setColour (Colour (0xd205282c));
    g.fillPath (fill);
    g.setColour (frozen ? ff::teal : ff::teal.withAlpha (0.7f));
    g.strokePath (sea, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    if (asleep)
    {
        g.setFont (ff::font (16.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (w - 50.0f + ph * 10.0f, 40.0f - ph * 22.0f, 16.0f, 16.0f), Justification::centred, false);
        }
    }
}

// =====================================================================================
// Editor
// =====================================================================================
SeaSickEditor::SeaSickEditor (SeaSickProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      input (p.apvts, ParamIDs::input, { "GUITAR", "BASS", "LINE" }, 13.0f),
      mainTab ("MAIN", [this] { setPage (0); }),
      pedalTab ("PEDAL", [this] { setPage (1); }),
      keepButton ("KEEP", [this] { proc.keepNow(); }),
      keepMenu ("v", [this] { keeperMenu(); }),
      footInput (p),
      rerollChip ("RE-ROLL", [this] { proc.focusHere(); proc.reroll(); }, 12.0f),
      scope (p),
      rate (p, ParamIDs::rate, "Rate", "Rare to nervous"),
      damp (p, ParamIDs::damp, "Damp", "Sudden to slow"),
      depth (p, ParamIDs::depth, "Depth", "Less is more", true),
      lpg (p, ParamIDs::lpg, "LPG", "Pluck to bloom"),
      floorDial (p, ParamIDs::lpgFloor, "Floor", "Inside: darkest"),
      gateMeter (p),
      punch (p.apvts, ParamIDs::punch, "PUNCH OFF", "PUNCH", 12.0f),
      clean (p.apvts, ParamIDs::clean, "HISS", "CLEAN", 12.0f),
      mix (p, ParamIDs::mix, "Mix", "Vibrato at full"),
      volume (p, ParamIDs::volume, "Volume", "Breaks up high"),
      split (p.apvts, ParamIDs::split, "Low split stays in tune"),
      ceiling (p.apvts, ParamIDs::ceiling, "Wobble ceiling"),
      freezeMode (p.apvts, ParamIDs::freezeMode, { "SETTLE", "HOLD BEND" }, 11.0f),
      pebble (p),
      expression (p),
      expMin (p.apvts, ParamIDs::expMin, "Heel sets"),
      expMax (p.apvts, ParamIDs::expMax, "Toe sets"),
      expSmooth (p.apvts, ParamIDs::expSmooth, "Smoothing"),
      calibrate ("CALIBRATE", [this] { proc.calibrateExpression(); }),
      learnExp ("LEARN", [this] { proc.learnExpression(); }),
      curve ("CURVE", [this] { proc.setCurve ((proc.pedals().curve + 1) % 3); }),
      liveChip ("STUDIO", [this] { proc.setKeeperLive (! proc.keeperLive()); }),
      learnKeep ("LEARN SWITCH", [this] { proc.learnKeeper(); }),
      exportKeep ("EXPORT", [this] { proc.exportKeepers(); }),
      folderKeep ("FOLDER", [this] { proc.openKeepersFolder(); })
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<Component*> { &input, &mainTab, &pedalTab, &keepButton, &keepMenu, &footInput,
                                                       &rerollChip, &seaCard, &centCard, &scope, &rate, &damp, &depth,
                                                       &lpg, &floorDial, &gateMeter, &gateCard, &punch, &clean,
                                                       &mix, &volume, &split, &ceiling, &freezeMode, &seasickButton, &freezeButton,
                                                       &pebble, &moodCard, &expression, &expMin, &expMax, &expSmooth,
                                                       &calibrate, &learnExp, &curve, &liveChip, &learnKeep, &exportKeep, &folderKeep })
        addAndMakeVisible (c);

    seaCard.setTextHeight (26.0f);
    centCard.setTextHeight (26.0f);
    gateCard.setTextHeight (24.0f);
    moodCard.setTextHeight (22.0f);
    rate.valueText = [this]
    {
        const float t = ss::stepPeriodSeconds (raw (proc, ParamIDs::rate));
        return t >= 1.0f ? "EVERY " + String (t, 1) + " S" : String (1.0f / t, 1) + " / S";
    };

    // the big buttons are footswitches B and C, taps, holds and all
    seasickButton.onDown = [this] { proc.footDown (1); };
    seasickButton.onUp = [this] { proc.footUp (1); };
    freezeButton.onDown = [this] { proc.footDown (2); };
    freezeButton.onUp = [this] { proc.footUp (2); };
    expression.showPedalPage = [this] { setPage (1); };

    for (int i = 0; i < 4; ++i)
    {
        feet[(size_t) i] = std::make_unique<FootButton> (p, i);
        addAndMakeVisible (*feet[(size_t) i]);
        cards[(size_t) i] = std::make_unique<FootswitchCard> (p, i);
        addChildComponent (*cards[(size_t) i]);
    }
    const auto& t = names::expTargets();
    for (int i = 0; i < 6; ++i)
    {
        targetChips[(size_t) i] = std::make_unique<ActionChip> (String (t[(size_t) i].label).toUpperCase(),
            [this, i] { setParam (proc.apvts.getParameter (ParamIDs::expTarget), (float) i); }, 11.0f);
        addChildComponent (*targetChips[(size_t) i]);
    }

    noteEditor.setFont (ff::font (14.0f));
    noteEditor.setColour (TextEditor::backgroundColourId, ff::card);
    noteEditor.setColour (TextEditor::textColourId, ff::ink);
    noteEditor.setColour (TextEditor::outlineColourId, ff::line);
    noteEditor.setTextToShowWhenEmpty ("A NOTE FOR THIS KEEPER, THEN ENTER", ff::dim);
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    addChildComponent (noteEditor);

    setPage (0);
    setSize (1260, 660);
    start = lastTick = nowSeconds();
    startTimerHz (30);
}

SeaSickEditor::~SeaSickEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SeaSickEditor::setPage (int p)
{
    page = p;
    const bool main = p == 0;
    for (auto* c : std::initializer_list<Component*> { &rerollChip, &seaCard, &centCard, &scope, &rate, &damp, &depth,
                                                       &lpg, &floorDial, &gateMeter, &gateCard, &punch, &clean,
                                                       &mix, &volume, &split, &ceiling, &freezeMode, &seasickButton, &freezeButton,
                                                       &pebble, &moodCard })
        c->setVisible (main);
    for (auto& c : cards) c->setVisible (! main);
    for (auto& c : targetChips) c->setVisible (! main);
    for (auto* c : std::initializer_list<Component*> { &expMin, &expMax, &expSmooth, &calibrate, &learnExp, &curve,
                                                       &liveChip, &learnKeep, &exportKeep, &folderKeep })
        c->setVisible (! main);
    mainTab.setLit (main);
    pedalTab.setLit (! main);
    repaint();
}

void SeaSickEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (5, "Live", true, proc.keeperLive());
    m.addItem (6, "Studio", true, ! proc.keeperLive());
    m.addSeparator();
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepMenu), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = nowSeconds() + 20.0;
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
        else if (r == 5 || r == 6) proc.setKeeperLive (r == 5);
    });
}

void SeaSickEditor::closeNote (bool save)
{
    if (save) proc.addKeeperNote (noteNumber, noteEditor.getText());
    if (save && noteEditor.getText().trim().isNotEmpty())
    {
        proc.uiMessage = "NOTED";
        proc.uiMessageAt = nowSeconds();
    }
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
}

void SeaSickEditor::section (Graphics& g, Rectangle<int> r) const
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);
}

void SeaSickEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.06f));
    g.drawText ("SEASICK", 20, 20, 120, 40, Justification::centredLeft, false);
    ff::label (g, "RANDOM WATER, WARPED TAPE, BLOOMING NOTES", { 134, 20, 340, 40 });
    ff::label (g, "INPUT", { 500, 20, 50, 40 });

    section (g, { 20, 532, 1220, 112 });
    ff::label (g, "FOOTSWITCHES" + dot + "TAP OR HOLD, JUST LIKE THE REAL ONES", { 36, 538, 420, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, "A+B CALM" + dot + "C+D NEXT COPY" + dot + "HOLD C+D 1 S KEEPER", { 440, 538, 396, 18 }, Justification::centredRight, ff::dim, 11.0f);

    if (page == 0)
    {
        section (g, { 20, 76, 400, 440 });
        section (g, { 440, 76, 250, 440 });
        section (g, { 710, 76, 250, 440 });
        section (g, { 980, 76, 260, 440 });
        ff::label (g, "THE WATER", { 36, 88, 200, 18 });
        ff::label (g, "LOW PASS GATE", { 456, 88, 200, 18 });
        ff::label (g, "MIX + OUTPUT", { 726, 88, 200, 18 });
        ff::label (g, "PEBBLE", { 996, 88, 200, 18 });

        auto& eng = proc.getEngine();
        ff::label (g, "DELAY TIME, LAST 6 S", { 36, 318, 200, 16 }, Justification::centredLeft, ff::dim, 11.0f);
        ff::label (g, "THIN: STEPS" + dot + "THICK: AFTER DAMP", { 204, 318, 200, 16 }, Justification::centredRight, ff::faint, 11.0f);

        const bool bass = roundToInt (raw (proc, ParamIDs::input)) == ss::Bass;
        const float r = raw (proc, ParamIDs::rate), d = raw (proc, ParamIDs::damp), m = raw (proc, ParamIDs::mix);
        String waterHint;
        if (r > 0.7f && d > 0.65f) waterHint = "FAST STEPS CAN'T GET THROUGH HIGH DAMP";
        else if (d > 0.75f && m > 0.85f) waterHint = "HIGH DAMP, FULL WET: ADD SOME DRY TO HEAR IT";
        else if (bass) waterHint = "BASS" + dot + "BENDS CAPPED AT " + String (roundToInt (raw (proc, ParamIDs::ceiling))) + " CENTS";
        ff::label (g, waterHint, { 36, 488, 368, 18 }, Justification::centred, ff::dim, 11.0f);

        ff::label (g, "GATE OPENING", { 456, 360, 120, 16 }, Justification::centredLeft, ff::dim, 11.0f);
        const float cut = eng.uiCutoff.load();
        ff::label (g, cut >= 1000.0f ? String (cut / 1000.0f, 1) + " KHZ" : String (roundToInt (cut)) + " HZ",
                   { 574, 360, 100, 16 }, Justification::centredRight, ff::ink, 11.0f);

        const String mixHint = m > 0.9f ? "FULL WET" + dot + "VIBRATO" : (m < 0.15f ? "MOSTLY DRY" : "CHORUS" + dot + "DRY HEARS THE DRIFT");
        ff::label (g, mixHint, { 726, 256, 218, 16 }, Justification::centred, ff::dim, 11.0f);
        ff::label (g, "BASS", { 726, 278, 100, 16 }, Justification::centredLeft, bass ? ff::ink : ff::dim, 13.0f);
        ff::label (g, bass ? "FUNDAMENTAL DRY AND IN TUNE" : "CHOOSE BASS INPUT TO USE THESE", { 776, 278, 168, 16 },
                   Justification::centredRight, bass ? ff::dim : ff::faint, 10.0f);
        ff::label (g, "FREEZE", { 726, 384, 100, 16 }, Justification::centredLeft, ff::dim, 11.0f);

        ff::label (g, pebble.hint(), { 990, 456, 240, 18 }, Justification::centred, ff::dim, 11.0f);
    }
    else
    {
        section (g, { 940, 76, 300, 280 });
        section (g, { 940, 366, 300, 150 });
        ff::label (g, "EXPRESSION MOVES", { 956, 88, 260, 18 });
        ff::label (g, "KEEPER", { 956, 378, 120, 18 });
        ff::label (g, "LAST No. " + String (proc.lastKeeper()), { 1080, 378, 144, 18 }, Justification::centredRight, ff::ink, 12.0f);
        ff::label (g, "HOLD C+D FOR A SECOND, OR PRESS KEEP", { 956, 488, 270, 16 }, Justification::centredLeft, ff::dim, 10.5f);
        ff::label (g, "SET ONCE, SHARED BY EVERY FLIP FELLA PLUGIN", { 956, 336, 270, 14 }, Justification::centredLeft, ff::dim, 10.0f);
    }
}

void SeaSickEditor::resized()
{
    // header
    input.setBounds (552, 24, 220, 32);
    mainTab.setBounds (790, 24, 74, 32);
    pedalTab.setBounds (870, 24, 76, 32);
    keepButton.setBounds (960, 24, 72, 32);
    keepMenu.setBounds (1036, 24, 32, 32);
    footInput.setBounds (1080, 20, 160, 40);

    // the water
    rerollChip.setBounds (316, 84, 88, 26);
    seaCard.setBounds (36, 116, 226, 60);
    centCard.setBounds (270, 116, 134, 60);
    scope.setBounds (36, 188, 368, 128);
    rate.setBounds (36, 336, 110, 150);
    damp.setBounds (157, 336, 110, 150);
    depth.setBounds (278, 336, 126, 150);

    // the gate
    lpg.setBounds (452, 104, 113, 150);
    floorDial.setBounds (565, 104, 113, 150);
    gateMeter.setBounds (456, 262, 218, 96);
    gateCard.setBounds (456, 384, 218, 56);
    punch.setBounds (456, 456, 105, 30);
    clean.setBounds (569, 456, 105, 30);

    // mix and output
    mix.setBounds (722, 104, 113, 150);
    volume.setBounds (835, 104, 113, 150);
    split.setBounds (726, 300, 218, 34);
    ceiling.setBounds (726, 340, 218, 34);
    freezeMode.setBounds (776, 380, 168, 24);
    seasickButton.setBounds (726, 432, 105, 60);
    freezeButton.setBounds (839, 432, 105, 60);

    // pebble
    pebble.setBounds (990, 110, 240, 260);
    moodCard.setBounds (996, 380, 228, 60);
    noteEditor.setBounds (996, 482, 228, 28);

    // strip
    for (int i = 0; i < 4; ++i) feet[(size_t) i]->setBounds (36 + i * 202, 560, 196, 72);
    expression.setBounds (856, 540, 368, 96);

    // pedal page
    for (int i = 0; i < 4; ++i) cards[(size_t) i]->setBounds (20 + i * 230, 76, 220, 440);
    for (int i = 0; i < 6; ++i) targetChips[(size_t) i]->setBounds (956 + (i % 3) * 90, 112 + (i / 3) * 34, 84, 28);
    expMin.setBounds (956, 184, 268, 32);
    expMax.setBounds (956, 220, 268, 32);
    expSmooth.setBounds (956, 256, 268, 32);
    calibrate.setBounds (956, 296, 86, 30);
    learnExp.setBounds (1047, 296, 86, 30);
    curve.setBounds (1138, 296, 86, 30);
    liveChip.setBounds (956, 404, 130, 32);
    learnKeep.setBounds (1094, 404, 130, 32);
    exportKeep.setBounds (956, 444, 130, 32);
    folderKeep.setBounds (1094, 444, 130, 32);
}

void SeaSickEditor::timerCallback()
{
    const double now = nowSeconds();
    const double dt = jlimit (0.0, 0.2, now - lastTick);
    lastTick = now;

    for (auto& f : feet) f->tick();
    expression.tick();
    footInput.repaint();

    const bool kept = proc.uiMessage == "KEPT" && now - proc.uiMessageAt < 1.5;
    keepButton.setLit (kept);
    keepButton.setText (kept ? "KEPT" : (proc.learningWhat() == SeaSickProcessor::LearnKeeper ? "PRESS IT" : "KEEP"));
    if (noteUntil > 0.0 && now > noteUntil) closeNote (true);

    if (page == 0)
    {
        auto& eng = proc.getEngine();
        const bool bypassed = isOn (proc, ParamIDs::bypass), frozen = isOn (proc, ParamIDs::freeze);
        const bool bend = roundToInt (raw (proc, ParamIDs::freezeMode)) == ss::HoldBend;
        seaCard.setValue (bypassed ? "BYPASSED" : (frozen ? (bend ? "BEND HELD" : "FROZEN") : names::seaState (eng.uiWobble.load())));
        const int cents = roundToInt (eng.uiCents.load() * 0.5f) * 2;
        centCard.setValue ((cents > 0 ? "+" : (cents < 0 ? String::fromUTF8 ("\xe2\x88\x92") : String())) + String (std::abs (cents)) + " C");
        const float l = raw (proc, ParamIDs::lpg);
        gateCard.setValue (isOn (proc, ParamIDs::pluck) ? "PLUCK" : (l > 0.55f ? "BLOOM" : (l > 0.3f ? "BOUNCE" : (l > 0.12f ? "PLUCK" : "DARK"))));
        pebble.update (now - start, dt);
        moodCard.setValue (pebble.mood());
        for (auto* c : { &seaCard, &centCard, &gateCard, &moodCard }) c->advance (dt);

        const bool sick = isOn (proc, ParamIDs::seasick), swell = isOn (proc, ParamIDs::swell);
        seasickButton.set (swell ? "SWELLING" : (sick ? "SEASICK" : "SEASICK  B"), sick || swell, ff::sea);
        freezeButton.set (frozen ? (bend ? "BEND HELD" : "FROZEN") : "FREEZE  C", frozen, ff::teal);
        split.setAlpha (roundToInt (raw (proc, ParamIDs::input)) == ss::Bass ? 1.0f : 0.45f);
        ceiling.setAlpha (split.getAlpha());
        scope.repaint();
        gateMeter.repaint();
        repaint (36, 486, 368, 22);
        repaint (456, 358, 218, 20);
        repaint (726, 254, 218, 40);
        repaint (985, 454, 250, 26);
    }
    else
    {
        for (auto& c : cards) c->tick (dt);
        const int t = roundToInt (raw (proc, ParamIDs::expTarget));
        for (int i = 0; i < 6; ++i) targetChips[(size_t) i]->setLit (i == t);
        const char* const curveNames[3] = { "LINEAR", "SLOW START", "FAST START" };
        curve.setText (curveNames[jlimit (0, 2, proc.pedals().curve)]);
        calibrate.setLit (proc.isCalibrating());
        calibrate.setText (proc.isCalibrating() ? "ROCK IT" : "CALIBRATE");
        learnExp.setLit (proc.learningWhat() == SeaSickProcessor::LearnExpression);
        liveChip.setText (proc.keeperLive() ? "LIVE" : "STUDIO");
        learnKeep.setLit (proc.learningWhat() == SeaSickProcessor::LearnKeeper);
        repaint (940, 370, 300, 30);
    }
}
} // namespace ui
