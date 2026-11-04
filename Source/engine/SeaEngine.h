#pragma once

// SeaSick's sound: a short analogue-style delay whose time is pushed around by a random
// "K-field" generator, through an envelope low pass gate. Plain C++17, no JUCE, so it
// builds anywhere (Windows x64, macOS, Linux ARM64) and can move to a headless host.
//
// All of it is original code written from public descriptions of the pedal's behaviour.

#include "DspUtil.h"
#include <array>
#include <atomic>

namespace ss
{
enum Input { Guitar = 0, Bass = 1, Line = 2 };
enum FreezeMode { Settle = 0, HoldBend = 1 };

constexpr float kCentreMs = 7.0f;      // the delay's resting point (an estimate: no source gives it)
constexpr float kMaxSwingMs = 5.0f;    // Depth at full: plus or minus this much
constexpr float kLookaheadMs = 1.0f;   // Punch: the gate reads this far ahead of the audio

// Knob tapers, shared with the panel so it shows what you hear.
inline float stepPeriodSeconds (float rate) { return 8.0f * std::pow (0.005f, clampf (rate, 0.0f, 1.0f)); }   // 8 s to 40 ms
inline float dampSeconds (float damp) { return 0.002f * std::pow (1000.0f, clampf (damp, 0.0f, 1.0f)); }       // 2 ms to 2 s
inline float depthMs (float depth) { return kMaxSwingMs * std::pow (clampf (depth, 0.0f, 1.0f), 1.4f); }
inline float floorHz (float f) { return 150.0f * std::pow (10.0f, clampf (f, 0.0f, 1.0f)); }                   // 150 Hz to 1.5 kHz
inline float volumeDb (float v) { return (v - 0.47f) * 26.0f; }                                                 // unity just before noon

struct EngineParams
{
    // the pedal's six knobs, 0..1
    float rate = 0.4f, damp = 0.55f, depth = 0.5f, lpg = 0.62f, mix = 0.55f, volume = 0.45f;
    // inside the pedal: the LPG Adjust trimpot, and the Boost and Pad jumpers as an input choice
    float floor = 0.48f;
    int input = Guitar;
    // bass protections
    float splitHz = 110.0f, ceilingCents = 30.0f;
    bool punch = true, clean = false;
    // footswitch jobs
    bool active = true;
    bool seasick = false, swell = false;
    bool freeze = false;
    int freezeMode = Settle;
    bool pluck = false;
};

// The random modulation. Not an LFO: steps come at irregular times, each target wanders from
// the last with an occasional big jump, a little of the step before echoes back, and two
// integrators (Damp) turn the steps into curves.
class KField
{
public:
    void prepare (double sampleRate, uint32_t seed)
    {
        sr = (float) sampleRate;
        rng = Rng (seed);
        s1 = s2 = tgt = prev = prev2 = 0.0f;
        left = 0.5f;
        lastDamp = -1.0f;
    }

    void reroll (uint32_t seed)
    {
        rng = Rng (seed);
        step (true);
        left = 0.33f + uniform();   // the next one comes along in its own time
    }

    // one sample; returns the smoothed position, -1..1
    float next (float rate, float damp, bool frozen)
    {
        if (frozen) return s2;
        if (damp != lastDamp) { lastDamp = damp; k = 1.0f - std::exp (-1.0f / (dampSeconds (damp) * sr)); }
        left -= 1.0f / (stepPeriodSeconds (rate) * sr);   // in periods, so turning Rate takes effect at once
        if (left <= 0.0f)
        {
            const float u = uniform();
            left = clampf (0.33f - std::log (1.0f - 0.999f * u) * 0.67f, 0.33f, 3.0f);
            step (false);
        }
        s1 += (tgt - s1) * k;
        s2 += (s1 - s2) * k;
        return s2;
    }

    float target() const { return tgt; }
    float position() const { return s2; }
    int steps() const { return count; }

private:
    float uniform() { return 0.5f * (rng.next() + 1.0f); }
    void step (bool big)
    {
        prev2 = prev;
        prev = tgt;
        float t = tgt + 0.45f * rng.next();
        if (big || uniform() < 0.12f) t = rng.next();
        t = 0.82f * t + 0.18f * prev2;    // the past comes back a little
        tgt = clampf (t, -1.0f, 1.0f);
        ++count;
    }

    Rng rng;
    float sr = 48000.0f, s1 = 0.0f, s2 = 0.0f, tgt = 0.0f, prev = 0.0f, prev2 = 0.0f, left = 0.5f, k = 0.01f, lastDamp = -1.0f;
    int count = 0;
};

// Zavalishin state-variable low pass: smooth to sweep, which the gate does on every note.
struct SvfLP
{
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    void set (float hz, float sr, float q)
    {
        const float g = std::tan (kPi * clampf (hz, 20.0f, 0.45f * sr) / sr), k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process (float v0)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
};

class SeaEngine
{
public:
    static constexpr int kHist = 512;      // scope history, 64 points a second
    static constexpr int kHistRate = 64;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        look = std::max (1, (int) std::lround (kLookaheadMs * 0.001f * sr));
        ahead.prepare (look + 8);
        bbd.prepare ((int) (0.045f * sr) + 8);
        field.prepare (sampleRate, 0x5ea51c4u);
        noise = Rng (0x1234567u);
        bbdIn.setLowPass (7000.0f, 0.707f, sr);
        bbdIn.reset();
        lpf.reset();
        for (auto* b : { &lo1, &lo2, &hi1, &hi2 }) b->reset();
        lastSplit = -1.0f;
        delay = kCentreMs; vel = 0.0f; holdVel = 0.0f;
        fast = slow = open = lvlFast = lvlSlow = pluckEnv = 0.0f;
        seasickAmt = 0.0f; fade = 1.0f;
        depthSm.setTime (0.05f, sampleRate); depthSm.reset (0.4f);
        volSm.setTime (0.03f, sampleRate); volSm.reset (0.45f);
        mixSm.setTime (0.03f, sampleRate); mixSm.reset (0.55f);
        histEvery = std::max (1, (int) (sr / (float) kHistRate));
        histCount = 0;
        cutCount = 0;
        wasFrozen = wasBending = recovering = false;
        rerollSeed = 0x9e3779b9u;
    }

    // latency is constant (the Punch lookahead is always in the line) so toggling Punch never clicks
    int latencySamples() const { return look; }

    void reroll() { rerollPending.store (true); }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const EngineParams& p)
    {
        const bool bass = p.input == Bass;
        const float inGain = p.input == Guitar ? 1.4f : (bass ? 0.7f : 0.35f);
        const float hiss = p.clean ? 0.0f : 0.0004f;
        const bool frozen = p.freeze;
        const bool bend = frozen && p.freezeMode == HoldBend;

        if (rerollPending.exchange (false))
        {
            rerollSeed = rerollSeed * 1664525u + 1013904223u + (uint32_t) field.steps();
            field.reroll (rerollSeed);
            uiRerolls.fetch_add (1);
        }
        if (bass && std::abs (p.splitHz - lastSplit) > 0.5f)
        {
            lastSplit = p.splitHz;
            lo1.setLowPass (p.splitHz, 0.7071f, sr); lo2.setLowPass (p.splitHz, 0.7071f, sr);
            hi1.setHighPass (p.splitHz, 0.7071f, sr); hi2.setHighPass (p.splitHz, 0.7071f, sr);
        }

        // Seasick: Depth deep, Damp eased, Rate up a notch. Hold swells in over about a second.
        const float sickWant = (p.seasick || p.swell) ? 1.0f : 0.0f;
        const float sickCoef = 1.0f - std::exp (-1.0f / ((p.swell ? 0.35f : 0.2f) * sr));

        // the gate's envelope times
        const float lpgEff = p.pluck ? std::max (p.lpg, 0.5f) : p.lpg;
        const float sens = 12.0f * lpgEff * lpgEff;
        const float cFast = std::exp (-1.0f / (0.06f * sr));
        const float cSlow = std::exp (-1.0f / ((0.25f + 1.4f * p.lpg) * sr));
        const float slowScale = 0.6f;
        const float onsetUp = 1.0f - std::exp (-1.0f / (0.001f * sr)), onsetDown = 1.0f - std::exp (-1.0f / (0.02f * sr));
        const float avgUp = 1.0f - std::exp (-1.0f / (0.04f * sr)), avgDown = 1.0f - std::exp (-1.0f / (0.3f * sr));
        const float cPluck = std::exp (-1.0f / (0.04f * sr));
        const float openUp = 1.0f - std::exp (-1.0f / (0.0005f * sr)), openDown = 1.0f - std::exp (-1.0f / (0.02f * sr));
        const float fl = floorHz (p.floor), top = std::min (12000.0f, 0.42f * sr);

        // how fast the delay may move: the wobble ceiling in Bass mode, generous otherwise
        const float maxVel = bass ? 1.0f - std::exp2 (-p.ceilingCents / 1200.0f) : 0.5f;
        const float maxStepMs = maxVel * 1000.0f / sr;
        const float holdDecay = std::exp (-1.0f / (2.5f * sr));
        const float fadeCoef = 1.0f - std::exp (-1.0f / (0.008f * sr));
        const float g = std::pow (10.0f, volumeDb (p.volume) / 20.0f);

        if (bend && ! wasFrozen) holdVel = vel;   // keep bending at the speed it was going
        if (wasBending && ! bend) recovering = true;   // glide home from a held bend, no chirp
        wasFrozen = frozen;
        wasBending = bend;
        const float recoverStepMs = 0.03f * 1000.0f / sr;

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (inL[i] + inR[i]);
            ahead.write (x);
            const float xd = ahead.read ((float) (look + 1));   // exactly look samples ago

            // ---- envelope follower and gate ----
            const float a = std::abs (p.punch ? x : xd);
            fast = a > fast ? a : fast * cFast;
            slow = a * slowScale > slow ? a * slowScale : slow * cSlow;
            // Pluck listens for note onsets only: a level well above the recent average
            lvlFast += (a - lvlFast) * (a > lvlFast ? onsetUp : onsetDown);
            lvlSlow += (a - lvlSlow) * (a > lvlSlow ? avgUp : avgDown);
            if (lvlFast > 1.6f * lvlSlow + 1.0e-4f) pluckEnv = std::max (pluckEnv, lvlFast * 2.0f);
            pluckEnv *= cPluck;
            if (fast < 1.0e-9f) fast = 0.0f;
            if (slow < 1.0e-9f) slow = 0.0f;
            if (pluckEnv < 1.0e-9f) pluckEnv = 0.0f;
            if (lvlSlow < 1.0e-9f) lvlSlow = lvlFast = 0.0f;
            const float want = clampf ((p.pluck ? pluckEnv : std::max (fast, slow)) * sens, 0.0f, 1.0f);
            open += (want - open) * (want > open ? openUp : openDown);
            if (open < 1.0e-7f) open = 0.0f;
            if (--cutCount <= 0)
            {
                cutCount = 8;
                cutoff = fl * std::pow (top / fl, open);
                lpf.set (cutoff, sr, 0.65f);
            }

            // ---- the modulation ----
            seasickAmt += (sickWant - seasickAmt) * sickCoef;
            const float rate = p.rate + (std::max (p.rate, 0.6f) - p.rate) * seasickAmt;
            const float damp = p.damp + (std::min (p.damp, 0.34f) - p.damp) * seasickAmt;
            const float depth = depthSm.next (p.depth + (std::max (p.depth, 0.95f) - p.depth) * seasickAmt);
            const float pos = field.next (rate, damp, frozen);
            const float before = delay;
            if (bend)
            {
                holdVel *= holdDecay;
                if (holdVel > 0.0f) holdVel *= clampf ((40.0f - delay) / 4.0f, 0.0f, 1.0f);
                else holdVel *= clampf ((delay - 1.0f) / 1.5f, 0.0f, 1.0f);
                delay += holdVel;
            }
            else
            {
                const float target = kCentreMs + depthMs (depth) * pos;
                float lim = maxStepMs;
                if (recovering)
                {
                    if (std::abs (target - delay) < 0.05f) recovering = false;
                    else lim = std::min (lim, recoverStepMs);
                }
                delay += clampf (target - delay, -lim, lim);
            }
            vel = delay - before;

            // ---- the BBD: split, squash, hiss, delay, gate ----
            float low = 0.0f, high = xd;
            if (bass) { low = lo2.process (lo1.process (xd)); high = hi2.process (hi1.process (xd)); }
            float w = bbdIn.process (high * inGain);
            w = softClip (w) + hiss * noise.next();
            bbd.write (w);
            float wet = bbd.read (delay * 0.001f * sr);
            wet = lpf.process (wet) * (0.55f + 0.45f * open) / inGain;
            float dry = xd;
            if (bass)
            {
                wet += low * (0.5f + 0.5f * open);   // the fundamental stays put but still plucks
                dry = low + high;                    // same phase as the wet low band
            }

            const float m = mixSm.next (p.mix);
            float y = dry * std::cos (m * 0.5f * kPi) + wet * std::sin (m * 0.5f * kPi);
            y = safetyLimit (softClip (y * volSm.next (g) * 0.6f) / 0.6f);

            // true bypass, with a short fade so it never clicks
            fade += ((p.active ? 1.0f : 0.0f) - fade) * fadeCoef;
            const float out = xd + fade * (y - xd);
            outL[i] = out;
            outR[i] = out;
            peak = std::max (peak, std::abs (out));

            if (--histCount <= 0)
            {
                histCount = histEvery;
                const int h = histPos.load (std::memory_order_relaxed);
                histDelay[(size_t) h].store ((delay - kCentreMs) / kMaxSwingMs, std::memory_order_relaxed);
                histStep[(size_t) h].store (frozen ? histStep[(size_t) ((h + kHist - 1) % kHist)].load() : field.target() * depthMs (depth) / kMaxSwingMs, std::memory_order_relaxed);
                histGate[(size_t) h].store (p.active ? open : 0.0f, std::memory_order_relaxed);
                histPos.store ((h + 1) % kHist, std::memory_order_release);
            }
        }

        // ---- what the panel and Pebble show ----
        const float velSec = vel * 0.001f * sr;
        const float cents = p.active ? 1200.0f * std::log2 (std::max (0.05f, 1.0f - velSec)) : 0.0f;
        uiCents.store (cents);
        const float blockSec = (float) n / sr;
        wobble2 += (cents * cents - wobble2) * (1.0f - std::exp (-blockSec / 0.8f));
        uiWobble.store (std::sqrt (wobble2));
        uiGate.store (p.active ? open : 0.0f);
        uiCutoff.store (cutoff);
        uiSeasick.store (seasickAmt);
        uiFrozen.store (frozen);
        uiDelay.store ((delay - kCentreMs) / kMaxSwingMs);
        uiOutput.store (peak);
    }

    int steps() const { return field.steps(); }
    float currentDelayMs() const { return delay; }

    // read by the panel
    std::atomic<float> uiCents { 0.0f }, uiWobble { 0.0f }, uiGate { 0.0f }, uiCutoff { 450.0f }, uiSeasick { 0.0f },
                       uiDelay { 0.0f }, uiOutput { 0.0f };
    std::atomic<bool> uiFrozen { false };
    std::atomic<int> uiRerolls { 0 };
    std::array<std::atomic<float>, kHist> histDelay {}, histStep {}, histGate {};
    std::atomic<int> histPos { 0 };

private:
    float sr = 48000.0f;
    int look = 48;
    DelayLine ahead, bbd;
    KField field;
    Rng noise;
    Biquad bbdIn, lo1, lo2, hi1, hi2;
    SvfLP lpf;
    Smoother depthSm, volSm, mixSm;
    float lastSplit = -1.0f, delay = kCentreMs, vel = 0.0f, holdVel = 0.0f;
    float fast = 0.0f, slow = 0.0f, open = 0.0f, cutoff = 450.0f, lvlFast = 0.0f, lvlSlow = 0.0f, pluckEnv = 0.0f;
    float seasickAmt = 0.0f, fade = 1.0f, wobble2 = 0.0f;
    int histEvery = 750, histCount = 0, cutCount = 0;
    bool wasFrozen = false, wasBending = false, recovering = false;
    uint32_t rerollSeed = 0x9e3779b9u;
    std::atomic<bool> rerollPending { false };
};
} // namespace ss
