#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour teal      { 0xff3fd6c8 };
const juce::Colour coral     { 0xffff7a59 };
const juce::Colour amber     { 0xfff2c230 };
const juce::Colour green     { 0xff4fcf6a };
const juce::Colour violet    { 0xffb06bff };
const juce::Colour sea       { 0xff57d18f };   // the seasick green
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };


juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
void pill (juce::Graphics&, juce::Rectangle<float> b, const juce::String& text, bool on, float size, juce::Colour onColour = ff::ink);

// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 34.0f;
};

// Rounded pill buttons bound to a choice parameter.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A single pill that turns ink-white when its switch is on.
class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                const juce::String& onText = {}, float textSize = 13.0f, juce::Colour onColour = ff::ink);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String text, onText;
    float textSize;
    juce::Colour onColour;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A plain pill button that runs a function.
class ActionChip : public juce::Component
{
public:
    ActionChip (const juce::String& t, std::function<void()> fn, float textSize = 12.0f)
        : text (t), action (std::move (fn)), size (textSize) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { pill (g, getLocalBounds().toFloat().reduced (1.0f), text, lit, size, colour); }
    void mouseDown (const juce::MouseEvent&) override { if (action) action(); }
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void setColour (juce::Colour c) { colour = c; repaint(); }

private:
    juce::String text;
    std::function<void()> action;
    float size;
    bool lit = false;
    juce::Colour colour = ff::ink;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A knob drawn as a small clock face, with its name above and its value below.
class Dial : public juce::Component
{
public:
    Dial (SeaSickProcessor&, const juce::String& paramId, const juce::String& caption, const juce::String& hint,
          bool redHand = false, int knobSize = 80);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<juce::String()> valueText;   // overrides the parameter's own text
    juce::Slider slider;

private:
    juce::RangedAudioParameter* param;
    juce::String caption, hint;
    int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The water: the actual modulation scrolling past, the random steps thin and the damped
// result thick, over the last six seconds.
class WaterScope : public juce::Component
{
public:
    explicit WaterScope (SeaSickProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SeaSickProcessor& proc;
};

// How far the low pass gate is open, over the last four seconds.
class GateMeter : public juce::Component
{
public:
    explicit GateMeter (SeaSickProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SeaSickProcessor& proc;
};

// A big press-and-hold button that runs a function on press and on release.
class HoldButton : public juce::Component
{
public:
    std::function<void()> onDown, onUp;
    void set (const juce::String& text, bool lit, juce::Colour litColour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); if (onDown) onDown(); }
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); if (onUp) onUp(); }

private:
    juce::String text;
    bool lit = false, pressed = false;
    juce::Colour litColour = ff::coral;
};

// One footswitch in the bottom strip. Works exactly like the real one: tap, hold, pairs.
class FootButton : public juce::Component
{
public:
    FootButton (SeaSickProcessor& p, int s) : proc (p), sw (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (sw); }
    void tick();

private:
    SeaSickProcessor& proc;
    int sw;
    juce::String big, small;
    juce::Colour led = ff::pin;
    bool lit = false, down = false;
    int lastRerolls = 0;
    double rerolledAt = -10.0;
};

// The expression pedal: live position, Depth, Mix or LPG chips, and a menu for the rest.
class ExpressionBar : public juce::Component
{
public:
    explicit ExpressionBar (SeaSickProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();
    std::function<void()> showPedalPage;

private:
    SeaSickProcessor& proc;
    ActionChip depthChip, mixChip, lpgChip;
    juce::RangedAudioParameter* target;
    float level = 0.0f;
    int lastTarget = -1;
};

// "FOOTSWITCHES": a dot that lights when the footswitch is heard directly, and the input menu.
class FootInputLabel : public juce::Component
{
public:
    explicit FootInputLabel (SeaSickProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    SeaSickProcessor& proc;
};

// One footswitch on the Pedal page: its jobs, what it listens for, Learn and Defaults.
class FootswitchCard : public juce::Component
{
public:
    FootswitchCard (SeaSickProcessor&, int sw);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double dt);

private:
    SeaSickProcessor& proc;
    int sw;
    FlipCard letter;
    ActionChip learn, reset;
    juce::String sends;
};

// Pebble floats on the water: rocks and tilts with the modulation, goes a little green when
// seasick, bobs up as each note opens the gate, shuts its eyes on Freeze, lurches on Re-roll.
class Pebble : public juce::Component
{
public:
    explicit Pebble (SeaSickProcessor& p) : proc (p) {}
    void update (double t, double dt);
    void paint (juce::Graphics&) override;
    juce::String mood() const;
    juce::String hint() const;

private:
    SeaSickProcessor& proc;
    double now = 0.0;
    float tilt = 0.0f, bob = 0.0f, bobV = 0.0f, sick = 0.0f, gate = 0.0f, lean = 0.0f, depthNorm = 0.0f;
    float hop = 0.0f, nod = 0.0f, calm = 0.0f, lurch = 0.0f;
    bool asleep = false, frozen = false, bending = false, plucking = false, blinking = false;
    double nextBlink = 2.0, blinkUntil = 0.0;
    int lastFoot = 0, lastKeep = 0, lastCalm = 0, lastReroll = 0;
    std::array<float, 61> water {};
};

class SeaSickEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SeaSickEditor (SeaSickProcessor&);
    ~SeaSickEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { proc.focusHere(); }
    void setPage (int p);

private:
    void timerCallback() override;
    void keeperMenu();
    void closeNote (bool save);
    void section (juce::Graphics&, juce::Rectangle<int>) const;

    SeaSickProcessor& proc;
    FlipLookAndFeel lnf;
    int page = 0;   // 0 Main, 1 Pedal

    // header
    Pills input;
    ActionChip mainTab, pedalTab, keepButton, keepMenu;
    FootInputLabel footInput;

    // the water
    ActionChip rerollChip;
    FlipCard seaCard, centCard;
    WaterScope scope;
    Dial rate, damp, depth;

    // the gate
    Dial lpg, floorDial;
    GateMeter gateMeter;
    FlipCard gateCard;
    ToggleChip punch, clean;

    // mix and output
    Dial mix, volume;
    LabelledSlider split, ceiling;
    Pills freezeMode;
    HoldButton seasickButton, freezeButton;

    // Pebble
    Pebble pebble;
    FlipCard moodCard;

    // strip
    std::array<std::unique_ptr<FootButton>, 4> feet;
    ExpressionBar expression;

    // pedal page
    std::array<std::unique_ptr<FootswitchCard>, 4> cards;
    std::array<std::unique_ptr<ActionChip>, 6> targetChips;
    LabelledSlider expMin, expMax, expSmooth;
    ActionChip calibrate, learnExp, curve, liveChip, learnKeep, exportKeep, folderKeep;

    juce::TextEditor noteEditor;
    int noteNumber = 0;
    double noteUntil = 0.0;

    double lastTick = 0.0, start = 0.0;
};
} // namespace ui

using SeaSickEditor = ui::SeaSickEditor;
