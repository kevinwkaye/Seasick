#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/SeaEngine.h"

// Every parameter lives in this one list, the pedal's controls, the footswitch jobs and
// Keeper included, so a host, an automation lane or a remote controller can read and set
// everything the panel and the footswitches can.
namespace ParamIDs
{
// the pedal's six knobs
inline constexpr const char* rate      = "rate";
inline constexpr const char* damp      = "damp";
inline constexpr const char* depth     = "depth";
inline constexpr const char* lpg       = "lpg";
inline constexpr const char* mix       = "mix";
inline constexpr const char* volume    = "volume";
inline constexpr const char* bypass    = "bypass";
// inside the pedal
inline constexpr const char* floor     = "floor";
inline constexpr const char* input     = "input";
// bass and extras
inline constexpr const char* split     = "split";
inline constexpr const char* ceiling   = "ceiling";
inline constexpr const char* punch     = "punch";
inline constexpr const char* clean     = "clean";
inline constexpr const char* freezeMode = "freezeMode";
// footswitch jobs
inline constexpr const char* seasick   = "seasick";
inline constexpr const char* swell     = "swell";
inline constexpr const char* freeze    = "freeze";
inline constexpr const char* pluck     = "pluck";
// expression
inline constexpr const char* expTarget = "expTarget";
inline constexpr const char* expMin    = "expMin";
inline constexpr const char* expMax    = "expMax";
inline constexpr const char* expSmooth = "expSmooth";
// actions for a remote controller: set to 1 to press, 0 to release
inline constexpr const char* foot[4]   = { "footA", "footB", "footC", "footD" };
inline constexpr const char* keeper    = "keeper";
inline constexpr const char* calm      = "calm";
inline constexpr const char* reroll    = "reroll";
} // namespace ParamIDs

namespace names
{
struct ExpTarget { const char* id; const char* label; };
inline const std::vector<ExpTarget>& expTargets()
{
    static const std::vector<ExpTarget> t {
        { ParamIDs::depth, "Depth" }, { ParamIDs::mix, "Mix" }, { ParamIDs::lpg, "LPG" },
        { ParamIDs::rate, "Rate" }, { ParamIDs::damp, "Damp" }, { ParamIDs::volume, "Volume" } };
    return t;
}
enum { ExpDepth = 0, ExpMix = 1, ExpLpg = 2 };

inline juce::String seaState (float wobbleCents)
{
    if (wobbleCents < 1.0f) return "STILL WATER";
    if (wobbleCents < 3.5f) return "CHORUS DRIFT";
    if (wobbleCents < 7.0f) return "WARBLE";
    if (wobbleCents < 14.0f) return "WARPED TAPE";
    return "SEASICK";
}
} // namespace names

// Heel to toe on Depth, shaped so most of the travel is the subtle zone the manual says the
// good sounds live in: the first 60% covers 0 to 40% Depth, then deeper, and the last fifth
// runs into seasick.
inline float expressionToDepth (float x)
{
    x = juce::jlimit (0.0f, 1.0f, x);
    if (x < 0.6f) return x / 0.6f * 0.4f;
    if (x < 0.8f) return 0.4f + (x - 0.6f) / 0.2f * 0.4f;
    return 0.8f + (x - 0.8f) / 0.2f * 0.2f;
}
inline float depthToExpression (float d)
{
    if (d < 0.4f) return d / 0.4f * 0.6f;
    if (d < 0.8f) return 0.6f + (d - 0.4f) / 0.4f * 0.2f;
    return 0.8f + (d - 0.8f) / 0.2f * 0.2f;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&layout] (auto p) { layout.add (std::move (p)); };
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
    auto flt = [&add] (const char* id, const char* name, float lo, float hi, float def, std::function<String (float, int)> text)
    {
        add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def,
                                                    AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    auto secs = [] (float s) { return s >= 1.0f ? String (s, 1) + " s" : String (roundToInt (s * 1000.0f)) + " ms"; };

    flt (ParamIDs::rate, "Rate", 0.0f, 1.0f, 0.4f, [secs] (float v, int)
    {
        const float t = ss::stepPeriodSeconds (v);
        return t >= 1.0f ? "every " + secs (t) : String (1.0f / t, 1) + " / s";
    });
    flt (ParamIDs::damp, "Damp", 0.0f, 1.0f, 0.55f, [secs] (float v, int) { return secs (ss::dampSeconds (v)); });
    flt (ParamIDs::depth, "Depth", 0.0f, 1.0f, 0.5f, [] (float v, int)
    {
        return String::fromUTF8 ("\xc2\xb1") + String (ss::depthMs (v), 1) + " ms";
    });
    flt (ParamIDs::lpg, "LPG", 0.0f, 1.0f, 0.62f, pct);
    flt (ParamIDs::mix, "Mix", 0.0f, 1.0f, 0.55f, [] (float v, int) { return String (roundToInt (v * 100.0f)) + "% wet"; });
    flt (ParamIDs::volume, "Volume", 0.0f, 1.0f, 0.45f, [] (float v, int)
    {
        const float db = ss::volumeDb (v);
        return (db >= 0.05f ? "+" : "") + String (db, 1) + " dB";
    });
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    flt (ParamIDs::floor, "LPG Floor", 0.0f, 1.0f, 0.48f, [] (float v, int) { return String (roundToInt (ss::floorHz (v))) + " Hz"; });
    add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::input, 1 }, "Input", StringArray { "Guitar", "Bass", "Line" }, 0));
    flt (ParamIDs::split, "Bass Split", 60.0f, 200.0f, 110.0f, [] (float v, int) { return String (roundToInt (v)) + " Hz"; });
    flt (ParamIDs::ceiling, "Wobble Ceiling", 10.0f, 80.0f, 30.0f, [] (float v, int) { return String (roundToInt (v)) + " cents"; });
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::punch, 1 }, "Punch", true));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::clean, 1 }, "Clean", false));
    add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::freezeMode, 1 }, "Freeze Mode", StringArray { "Settle", "Hold bend" }, 0));

    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::seasick, 1 }, "Seasick", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::swell, 1 }, "Swell", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pluck, 1 }, "Pluck", false));

    StringArray targets;
    for (const auto& t : names::expTargets()) targets.add (t.label);
    add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::expTarget, 1 }, "Expression Target", targets, names::ExpDepth));
    flt (ParamIDs::expMin, "Expression Min", 0.0f, 1.0f, 0.0f, pct);
    flt (ParamIDs::expMax, "Expression Max", 0.0f, 1.0f, 1.0f, pct);
    flt (ParamIDs::expSmooth, "Expression Smoothing", 0.0f, 1.0f, 0.25f, pct);

    const char* footNames[4] = { "Footswitch A", "Footswitch B", "Footswitch C", "Footswitch D" };
    for (int i = 0; i < 4; ++i)
        add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::foot[i], 1 }, footNames[i], false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::keeper, 1 }, "Keeper", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::calm, 1 }, "Calm", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::reroll, 1 }, "Re-roll", false));
    return layout;
}
