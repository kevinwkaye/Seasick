// Offline engine test: no plugin host, no JUCE. Plays guitar and bass phrases through every
// scenario at three sample rates, checks for NaNs, levels above full scale and dead output,
// then checks the behaviours that make SeaSick what it is: the modulation isn't an LFO,
// Rate plus Damp cancel out, the gate darkens and plucks, both Freeze modes, Re-roll, the
// bass protections, bypass and the hiss. Finally it measures CPU per instance at 48 kHz
// with 64 and 128 sample buffers (a house rule for every release).
//
// Pass a folder to also write a WAV of each scenario.

#include "SeaEngine.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ss;

namespace
{
constexpr double kTau = 6.283185307179586;

// a strummed chord with a pick-like attack, every 1.25 s, alternating hard and soft
float guitar (double t)
{
    static const double freqs[4] = { 146.83, 196.0, 246.94, 329.63 };
    const double local = std::fmod (t, 1.25);
    const double vel = ((int) (t / 1.25) % 2) ? 0.45 : 1.0;
    const double env = vel * std::exp (-local * 2.6) * std::min (1.0, local * 400.0);
    double s = 0.0;
    for (double f : freqs)
        s += std::sin (kTau * f * t) + 0.35 * std::sin (2.0 * kTau * f * t) + 0.15 * std::sin (3.0 * kTau * f * t);
    return (float) (0.12 * env * s);
}

// a bass line: E1 and A1 plucks with a bright attack
float bass (double t)
{
    const double local = std::fmod (t, 0.5);
    const double f = ((int) (t / 0.5) % 4) < 2 ? 41.2 : 55.0;
    const double env = std::exp (-local * 3.0) * std::min (1.0, local * 600.0);
    return (float) (0.5 * env * (std::sin (kTau * f * t) + 0.4 * std::sin (2.0 * kTau * f * t) + 0.2 * std::sin (3.0 * kTau * f * t)));
}

// the same strum with a full set of harmonics, so brightness shows
float brightGuitar (double t)
{
    static const double freqs[3] = { 110.0, 164.81, 220.0 };
    const double local = std::fmod (t, 1.25);
    const double env = std::exp (-local * 2.0) * std::min (1.0, local * 400.0);
    double s = 0.0;
    for (double f : freqs)
        for (int k = 1; k <= 24; ++k) s += std::sin (kTau * f * k * t) / k;
    return (float) (0.08 * env * s);
}

float sine55 (double t) { return (float) (0.5 * std::sin (kTau * 55.0 * t)); }

struct Trace
{
    double sr = 48000.0;
    std::vector<float> out, dry;           // output, and the input delayed by the engine's latency
    std::vector<float> cents, gate, delay; // one per block
    std::vector<int> steps;
    int block = 64;
    double rms (double from, double to, bool wetOnly = false) const
    {
        const size_t a = (size_t) (from * sr), b = std::min (out.size(), (size_t) (to * sr));
        double s = 0.0;
        for (size_t i = a; i < b; ++i) { const double v = wetOnly ? out[i] - dry[i] : out[i]; s += v * v; }
        return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
    }
    size_t blk (double t) const { return std::min (cents.size() - 1, (size_t) (t * sr / block)); }
};

struct Scene
{
    std::string name;
    EngineParams p;
    double seconds = 6.0;
    std::function<float (double)> source = guitar;
    std::function<void (double t, EngineParams&, SeaEngine&)> script;
    std::function<bool (const Trace&, std::string& why)> check;
};

void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t frames = (uint32_t) x.size(), dataBytes = frames * 2;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (1); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 2); u16 (2); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (float v : x) { const int16_t a = (int16_t) std::lround (clampf (v, -1.0f, 1.0f) * 32767.0f); std::fwrite (&a, 2, 1, f); }
    std::fclose (f);
}

Trace render (const Scene& s, double sr)
{
    auto eng = std::make_unique<SeaEngine>();
    eng->prepare (sr);
    Trace tr;
    tr.sr = sr;
    const int block = tr.block;
    const size_t total = (size_t) (s.seconds * sr);
    const int lat = eng->latencySamples();
    tr.out.resize (total);
    tr.dry.resize (total);
    std::vector<float> in (total);
    for (size_t i = 0; i < total; ++i) in[i] = s.source ((double) i / sr);
    for (size_t i = 0; i < total; ++i) tr.dry[i] = i >= (size_t) lat ? in[i - (size_t) lat] : 0.0f;
    EngineParams p = s.p;
    std::vector<float> l (block), r (block);
    for (size_t pos = 0; pos < total; pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, total - pos);
        if (s.script) s.script ((double) pos / sr, p, *eng);
        for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = in[pos + (size_t) i];
        eng->process (l.data(), r.data(), l.data(), r.data(), n, p);
        for (int i = 0; i < n; ++i) tr.out[pos + (size_t) i] = l[(size_t) i];
        tr.cents.push_back (eng->uiCents.load());
        tr.gate.push_back (eng->uiGate.load());
        tr.delay.push_back (eng->currentDelayMs());
        tr.steps.push_back (eng->steps());
    }
    return tr;
}

// high-frequency share: first-difference energy over total energy, on the wet part
double brightness (const Trace& t, double from, double to)
{
    const size_t a = (size_t) (from * t.sr) + 1, b = std::min (t.out.size(), (size_t) (to * t.sr));
    double d = 0.0, e = 0.0;
    for (size_t i = a; i < b; ++i) { const double v = t.out[i], u = t.out[i - 1]; d += (v - u) * (v - u); e += v * v; }
    return e > 0.0 ? d / e : 0.0;
}

// spread of the time between upward zero crossings, as a share of the mean period
double periodJitter (const Trace& t, double from, double to)
{
    std::vector<double> xs;
    const size_t a = (size_t) (from * t.sr) + 1, b = std::min (t.out.size(), (size_t) (to * t.sr));
    for (size_t i = a; i < b; ++i)
        if (t.out[i - 1] < 0.0f && t.out[i] >= 0.0f)
            xs.push_back ((double) (i - 1) + t.out[i - 1] / (double) (t.out[i - 1] - t.out[i]));
    if (xs.size() < 4) return 1.0;
    double m = 0.0, v = 0.0;
    std::vector<double> per;
    for (size_t i = 1; i < xs.size(); ++i) per.push_back (xs[i] - xs[i - 1]);
    for (double p : per) m += p;
    m /= (double) per.size();
    for (double p : per) v += (p - m) * (p - m);
    return std::sqrt (v / (double) per.size()) / m;
}

double maxAbsCents (const Trace& t, double from, double to)
{
    double m = 0.0;
    for (size_t i = t.blk (from); i < t.blk (to); ++i) m = std::max (m, (double) std::abs (t.cents[i]));
    return m;
}

double meanSquareCents (const Trace& t, double from, double to)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = t.blk (from); i < t.blk (to); ++i) { s += (double) t.cents[i] * t.cents[i]; ++n; }
    return n ? s / (double) n : 0.0;
}

double cpuPercent (int block)
{
    const double sr = 48000.0;
    SeaEngine eng;
    eng.prepare (sr);
    EngineParams p;
    p.input = Bass; p.depth = 0.9f; p.rate = 0.8f; p.damp = 0.2f; p.lpg = 0.8f;   // everything working
    const int seconds = 20;
    std::vector<float> l ((size_t) block), r ((size_t) block);
    const auto start = std::chrono::steady_clock::now();
    for (long pos = 0; pos < (long) (seconds * sr); pos += block)
    {
        for (int i = 0; i < block; ++i) l[(size_t) i] = r[(size_t) i] = guitar ((double) (pos + i) / sr);
        eng.process (l.data(), r.data(), l.data(), r.data(), block, p);
    }
    const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    return 100.0 * used / (double) seconds;
}

// The generator on its own: are the steps irregular, as on the pedal, rather than a clock?
bool kfieldIsNotAnLfo (std::string& why)
{
    KField k;
    const double sr = 48000.0;
    k.prepare (sr, 99u);
    std::vector<double> at;
    int last = k.steps();
    for (long i = 0; i < (long) (60 * sr); ++i)
    {
        k.next (0.5f, 0.3f, false);
        if (k.steps() != last) { last = k.steps(); at.push_back ((double) i / sr); }
    }
    if (at.size() < 20) { why = "too few steps: " + std::to_string (at.size()); return false; }
    double m = 0.0, v = 0.0;
    std::vector<double> gaps;
    for (size_t i = 1; i < at.size(); ++i) gaps.push_back (at[i] - at[i - 1]);
    for (double g : gaps) m += g;
    m /= (double) gaps.size();
    for (double g : gaps) v += (g - m) * (g - m);
    const double cv = std::sqrt (v / (double) gaps.size()) / m;
    const double expected = stepPeriodSeconds (0.5f);
    if (cv < 0.3) { why = "steps too regular, spread " + std::to_string (cv); return false; }
    if (m < expected * 0.5 || m > expected * 2.0) { why = "average gap off: " + std::to_string (m); return false; }
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string wavDir = argc > 1 ? argv[1] : "";
    std::vector<Scene> scenes;
    auto hasWet = [] (double minRms)
    {
        return [minRms] (const Trace& t, std::string& why)
        {
            const double r = t.rms (0.5, t.out.size() / t.sr - 0.2, true);
            if (r < minRms) { why = "too close to dry: " + std::to_string (r); return false; }
            return true;
        };
    };

    { Scene s; s.name = "01_default_chorus"; s.check = hasWet (0.004); scenes.push_back (s); }
    { Scene s; s.name = "02_rare_and_slow"; s.p.rate = 0.1f; s.p.damp = 0.9f; s.p.mix = 0.5f; scenes.push_back (s); }
    { Scene s; s.name = "03_nervous_and_sharp"; s.p.rate = 0.95f; s.p.damp = 0.05f; s.p.depth = 0.5f; s.check = hasWet (0.004); scenes.push_back (s); }
    { Scene s; s.name = "04_warped_tape_vibrato"; s.p.rate = 0.45f; s.p.damp = 0.35f; s.p.depth = 0.6f; s.p.mix = 1.0f; s.check = hasWet (0.004); scenes.push_back (s); }
    {
        Scene s; s.name = "05_seasick_latch"; s.p.seasick = true;
        s.check = [] (const Trace& t, std::string& why)
        { const double w = meanSquareCents (t, 1.0, 5.5); if (w < 100.0) { why = "Seasick isn't deep: rms " + std::to_string (std::sqrt (w)) + " cents"; return false; } return true; };
        scenes.push_back (s);
    }
    {
        // hold B: deeper while held, back to the knobs after
        Scene s; s.name = "06_swell_hold"; s.seconds = 9.0; s.p.depth = 0.15f; s.p.damp = 0.7f;
        s.script = [] (double t, EngineParams& p, SeaEngine&) { p.swell = t > 3.0 && t < 6.0; };
        s.check = [] (const Trace& t, std::string& why)
        {
            const double before = meanSquareCents (t, 0.5, 3.0), during = meanSquareCents (t, 4.0, 6.0), after = meanSquareCents (t, 7.5, 9.0);
            if (during < before * 4.0 + 25.0) { why = "the swell didn't deepen the wobble"; return false; }
            if (after > during * 0.5) { why = "the swell didn't fall back on release"; return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // LPG low is dark and chokes, LPG high is bright
        Scene s; s.name = "07_lpg_dark"; s.source = brightGuitar; s.p.lpg = 0.0f; s.p.mix = 1.0f; s.p.depth = 0.0f;
        s.check = [] (const Trace& dark, std::string& why)
        {
            Scene b; b.source = brightGuitar; b.p.lpg = 1.0f; b.p.mix = 1.0f; b.p.depth = 0.0f;
            const Trace bright = render (b, dark.sr);
            const double d = brightness (dark, 0.5, 5.5), br = brightness (bright, 0.5, 5.5);
            if (d > br * 0.5) { why = "low LPG isn't darker: " + std::to_string (d) + " vs " + std::to_string (br); return false; }
            if (dark.rms (0.5, 5.5) > bright.rms (0.5, 5.5)) { why = "low LPG isn't quieter"; return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // hold D: the gate snaps short
        Scene s; s.name = "08_pluck"; s.p.lpg = 0.8f; s.p.pluck = true;
        s.check = [] (const Trace& pl, std::string& why)
        {
            Scene b; b.p.lpg = 0.8f;
            const Trace open = render (b, pl.sr);
            double a = 0.0, o = 0.0;
            for (size_t i = pl.blk (0.5); i < pl.blk (5.5); ++i) { a += pl.gate[i]; o += open.gate[i]; }
            if (a > o * 0.6) { why = "Pluck didn't shorten the gate: " + std::to_string (a / o); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "09_bypass_is_clean";
        s.script = [] (double t, EngineParams& p, SeaEngine&) { p.active = t < 3.0; };
        s.check = [] (const Trace& t, std::string& why)
        {
            double m = 0.0;
            for (size_t i = (size_t) (3.2 * t.sr); i < t.out.size(); ++i) m = std::max (m, (double) std::abs (t.out[i] - t.dry[i]));
            if (m > 1.0e-4) { why = "bypassed output isn't the dry signal: " + std::to_string (m); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // Freeze, Settle: the water stops, so the pitch comes home and stays
        Scene s; s.name = "10_freeze_settle"; s.p.depth = 0.7f; s.p.rate = 0.7f; s.p.damp = 0.4f;
        s.script = [] (double t, EngineParams& p, SeaEngine&) { p.freeze = t > 2.0 && t < 4.5; };
        s.check = [] (const Trace& t, std::string& why)
        {
            if (maxAbsCents (t, 2.6, 4.4) > 0.5) { why = "pitch still moving while frozen: " + std::to_string (maxAbsCents (t, 2.6, 4.4)); return false; }
            if (t.steps[t.blk (4.4)] != t.steps[t.blk (2.05)]) { why = "the generator stepped while frozen"; return false; }
            if (maxAbsCents (t, 4.6, 6.0) < 2.0) { why = "didn't start moving again after Freeze"; return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // Freeze, Hold bend: freeze mid-bend and the bend carries on, then glides home
        Scene s; s.name = "11_freeze_hold_bend"; s.seconds = 8.0; s.p.depth = 0.8f; s.p.rate = 0.5f; s.p.damp = 0.45f; s.p.freezeMode = HoldBend;
        auto frozeAt = std::make_shared<double> (-1.0);
        s.script = [frozeAt] (double t, EngineParams& p, SeaEngine& e)
        {
            if (*frozeAt < 0.0 && t > 1.0 && std::abs (e.uiCents.load()) > 8.0f) *frozeAt = t;
            p.freeze = *frozeAt >= 0.0 && t < *frozeAt + 1.5;
        };
        s.check = [frozeAt] (const Trace& t, std::string& why)
        {
            if (*frozeAt < 0.0) { why = "never bent far enough to test"; return false; }
            const double f = *frozeAt;
            double minHeld = 1e9;
            for (size_t i = t.blk (f + 0.05); i < t.blk (f + 0.3); ++i) minHeld = std::min (minHeld, (double) std::abs (t.cents[i]));
            if (minHeld < 2.0) { why = "the bend didn't hold: " + std::to_string (minHeld); return false; }
            if (maxAbsCents (t, f + 1.5, t.out.size() / t.sr) > 70.0) { why = "chirped on the way home"; return false; }
            *frozeAt = -1.0;   // the next sample rate finds its own moment
            return true;
        };
        scenes.push_back (s);
    }
    {
        // Re-roll: a step lands at once, even with the water nearly still
        Scene s; s.name = "12_reroll"; s.p.rate = 0.0f; s.p.depth = 0.6f;
        auto before = std::make_shared<int> (-1);
        s.script = [before] (double t, EngineParams&, SeaEngine& e)
        {
            if (t >= 2.0 && *before < 0) { *before = e.steps(); e.reroll(); }
        };
        s.check = [before] (const Trace& t, std::string& why)
        {
            const int after = t.steps[t.blk (2.01)];
            const bool ok = after == *before + 1;
            *before = -1;
            if (! ok) { why = "Re-roll didn't step once at once"; return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // the manual: fast steps can't get through high Damp, so the wobble shrinks
        Scene s; s.name = "13_rate_damp_cancel"; s.seconds = 8.0; s.p.rate = 0.95f; s.p.damp = 0.95f; s.p.depth = 0.6f;
        s.check = [] (const Trace& slow, std::string& why)
        {
            Scene b; b.seconds = 8.0; b.p.rate = 0.95f; b.p.damp = 0.3f; b.p.depth = 0.6f;
            const Trace sharp = render (b, slow.sr);
            const double a = meanSquareCents (slow, 1.0, 8.0), c = meanSquareCents (sharp, 1.0, 8.0);
            if (a > c * 0.12) { why = "high Damp didn't calm the fast steps: " + std::to_string (std::sqrt (a / c)); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // Bass mode with everything up: bends never pass the ceiling
        Scene s; s.name = "14_bass_ceiling"; s.source = bass; s.p.input = Bass; s.p.seasick = true; s.p.depth = 1.0f; s.p.damp = 0.0f; s.p.rate = 0.9f;
        s.check = [] (const Trace& t, std::string& why)
        {
            const double m = maxAbsCents (t, 0.2, 6.0);
            if (m > 31.0) { why = "bend passed the 30 cent ceiling: " + std::to_string (m); return false; }
            if (m < 10.0) { why = "barely moving: " + std::to_string (m); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // Bass mode keeps a low E in tune; Guitar mode at the same settings warps it
        Scene s; s.name = "15_bass_fundamental_steady"; s.source = sine55; s.p.input = Bass; s.p.seasick = true; s.p.mix = 1.0f; s.p.lpg = 1.0f;
        s.check = [] (const Trace& b, std::string& why)
        {
            Scene g; g.source = sine55; g.p.seasick = true; g.p.mix = 1.0f; g.p.lpg = 1.0f;
            const Trace gt = render (g, b.sr);
            const double jb = periodJitter (b, 1.0, 6.0), jg = periodJitter (gt, 1.0, 6.0);
            if (jb > jg * 0.25) { why = "bass fundamental not steady enough: " + std::to_string (jb) + " vs " + std::to_string (jg); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    { Scene s; s.name = "16_bass_line"; s.source = bass; s.p.input = Bass; s.check = hasWet (0.003); scenes.push_back (s); }
    { Scene s; s.name = "17_line_input"; s.p.input = Line; s.check = hasWet (0.003); scenes.push_back (s); }
    {
        // Clean removes the hiss entirely; with it on, the hiss is there but very quiet
        Scene s; s.name = "18_hiss_and_clean"; s.source = [] (double) { return 0.0f; }; s.p.lpg = 1.0f; s.p.mix = 1.0f;
        s.check = [] (const Trace& hiss, std::string& why)
        {
            Scene c; c.source = [] (double) { return 0.0f; }; c.p.lpg = 1.0f; c.p.mix = 1.0f; c.p.clean = true;
            const Trace clean = render (c, hiss.sr);
            const double h = hiss.rms (1.0, 6.0), q = clean.rms (1.0, 6.0);
            if (q > 1.0e-7) { why = "Clean still hisses: " + std::to_string (q); return false; }
            if (h <= 0.0 || h > 0.001) { why = "hiss level wrong: " + std::to_string (h); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        // Volume past noon breaks up, but never past full scale (the global check)
        Scene s; s.name = "19_volume_breakup"; s.p.volume = 1.0f;
        s.check = [] (const Trace& hot, std::string& why)
        {
            Scene u; const Trace unity = render (u, hot.sr);
            if (hot.rms (0.5, 5.5) < unity.rms (0.5, 5.5) * 2.0) { why = "Volume up isn't louder"; return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "20_knob_sweeps"; s.seconds = 8.0;
        s.script = [] (double t, EngineParams& p, SeaEngine&)
        {
            p.rate = (float) (0.5 + 0.5 * std::sin (t * 0.9));
            p.damp = (float) (0.5 + 0.5 * std::sin (t * 1.3 + 1.0));
            p.depth = (float) (0.5 + 0.5 * std::sin (t * 0.7 + 2.0));
            p.lpg = (float) (0.5 + 0.5 * std::sin (t * 1.1 + 3.0));
            p.floor = (float) (0.5 + 0.5 * std::sin (t * 0.5));
            p.mix = (float) (0.5 + 0.5 * std::sin (t * 0.8 + 4.0));
            p.input = t < 4.0 ? Guitar : Bass;
            p.splitHz = (float) (60.0 + 140.0 * (0.5 + 0.5 * std::sin (t * 2.0)));
        };
        scenes.push_back (s);
    }

    int failed = 0;
    {
        std::string why;
        const bool ok = kfieldIsNotAnLfo (why);
        if (! ok) ++failed;
        std::printf ("%-28s %8s  %s %s\n", "00_kfield_not_an_lfo", "", ok ? "ok" : "FAILED", why.c_str());
    }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (const auto& s : scenes)
        {
            const Trace t = render (s, sr);
            std::string why;
            bool ok = true;
            for (size_t i = 0; i < t.out.size() && ok; ++i)
            {
                if (! std::isfinite (t.out[i])) { why = "NaN or infinity at sample " + std::to_string (i); ok = false; }
                else if (std::abs (t.out[i]) > 1.0001f) { why = "level above full scale"; ok = false; }
            }
            if (ok && s.check) ok = s.check (t, why);
            if (! wavDir.empty() && sr == 48000.0) writeWav (wavDir + "/" + s.name + ".wav", t.out, (int) sr);
            if (! ok) ++failed;
            if (! ok || sr == 48000.0)
                std::printf ("%-28s %6.0f Hz  %s %s\n", s.name.c_str(), sr, ok ? "ok" : "FAILED", why.c_str());
        }

    std::printf ("\nCPU per instance at 48 kHz (one core, Bass mode, everything moving):\n");
    for (int block : { 64, 128 })
        std::printf ("  %3d samples: %.2f%%\n", block, cpuPercent (block));

    std::printf ("\n%s: %d failed\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
