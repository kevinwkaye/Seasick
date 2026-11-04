// Footswitch and expression test: the real SeaSick processor, driven only by plain MIDI
// press (127) and release (0) messages on the shipped defaults, as a footswitch sends them.
// Checks tap, hold, A+B and C+D combos, the 1 second C+D Keeper hold, Re-roll waiting out
// the pair window, the Depth expression curve, the remote-control parameters, and that
// nothing else on the MIDI cable (sustain pedal, mod wheel, the wrong channel) changes anything.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

using namespace juce;

namespace
{
int failures = 0;

void expect (bool ok, const char* what)
{
    std::printf ("  %-62s %s\n", what, ok ? "ok" : "FAILED");
    if (! ok) ++failures;
}

struct Rig
{
    SeaSickProcessor proc;
    AudioBuffer<float> buffer { 2, 64 };
    double now = 100.0;

    Rig()
    {
        // the shipped defaults, whatever is saved on this computer
        auto& s = proc.pedals();
        s.resetFootswitches();
        s.expChannel = 16; s.expCC = 11;
        s.keeperChannel = 0; s.keeperCC = -1;
        s.heel = 0; s.toe = 127; s.curve = 0;
        proc.prepareToPlay (48000.0, 64);
        proc.apvts.getParameter (ParamIDs::expSmooth)->setValueNotifyingHost (0.0f);
        settle (0.05);
    }

    void cc (int number, int value, int channel = 16)
    {
        MidiBuffer midi;
        midi.addEvent (MidiMessage::controllerEvent (channel, number, value), 0);
        buffer.clear();
        proc.processBlock (buffer, midi);
        proc.pump (now);
    }

    // let time pass, pumping the message-thread work every 10 ms as the timer does
    void settle (double seconds)
    {
        for (double t = 0.0; t < seconds; t += 0.01)
        {
            now += 0.01;
            proc.pump (now);
        }
    }

    // run audio so the engine picks up what the processor asked for
    void audio()
    {
        MidiBuffer none;
        buffer.clear();
        proc.processBlock (buffer, none);
    }

    void press (int sw) { cc (20 + sw, 127); }
    void release (int sw) { cc (20 + sw, 0); }
    void tap (int sw) { press (sw); settle (0.1); release (sw); settle (0.05); }
    void hold (int sw, double seconds) { press (sw); settle (seconds); }

    bool on (const char* id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }
    float value (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); }
    void set (const char* id, float plain)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
    int rerolls() { audio(); return proc.getEngine().uiRerolls.load(); }

    std::map<String, float> snapshot()
    {
        std::map<String, float> m;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p)) m[rp->getParameterID()] = rp->getValue();
        return m;
    }
};
} // namespace

// --snapshot <folder>: render the panel in a few states as PNGs (needs a display, e.g. xvfb-run)
static int snapshots (const File& dir)
{
    dir.createDirectory();
    Rig r;
    std::unique_ptr<AudioProcessorEditor> ed (r.proc.createEditor());
    auto* editor = dynamic_cast<SeaSickEditor*> (ed.get());
    ed->setVisible (true);
    double phase = 0.0;
    auto play = [&] (double seconds)
    {
        for (double t = 0.0; t < seconds; t += 64.0 / 48000.0)
        {
            for (int i = 0; i < 64; ++i)
            {
                const float v = (float) (0.3 * std::sin (phase) * std::exp (-std::fmod (phase / 1382.0, 1.0) * 3.0));
                phase += 2.0 * MathConstants<double>::pi * 220.0 / 48000.0;
                r.buffer.setSample (0, i, v);
                r.buffer.setSample (1, i, v);
            }
            MidiBuffer none;
            r.proc.processBlock (r.buffer, none);
        }
        r.settle (0.01);
        MessageManager::getInstance()->runDispatchLoopUntil (400);
    };
    auto shot = [&] (const char* name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f = dir.getChildFile (String (name) + ".png");
        f.deleteFile();
        FileOutputStream out (f);
        PNGImageFormat().writeImageToStream (img, out);
        std::printf ("  wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    play (6.5); shot ("1_calm_water");
    r.set (ParamIDs::seasick, 1.0f); play (6.5); shot ("2_seasick");
    r.set (ParamIDs::seasick, 0.0f); r.set (ParamIDs::freeze, 1.0f); play (1.0); shot ("3_frozen");
    r.set (ParamIDs::freeze, 0.0f); r.set (ParamIDs::input, 1.0f); r.set (ParamIDs::pluck, 1.0f); play (3.0); shot ("4_bass_pluck");
    r.set (ParamIDs::pluck, 0.0f); r.set (ParamIDs::bypass, 1.0f); play (1.0); shot ("5_bypassed");
    r.set (ParamIDs::bypass, 0.0f); editor->setPage (1); play (0.5); shot ("6_pedal_page");
    return 0;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juce;
    if (argc > 2 && String (argv[1]) == "--snapshot") return snapshots (File (argv[2]));

    std::printf ("Footswitch A: bypass\n");
    {
        Rig r;
        r.tap (0);
        expect (r.on (ParamIDs::bypass), "tap A bypasses");
        r.tap (0);
        expect (! r.on (ParamIDs::bypass), "tap A again switches back on");
        r.hold (0, 1.0);
        expect (r.on (ParamIDs::bypass), "hold A: bypassed while held");
        r.release (0); r.settle (0.05);
        expect (! r.on (ParamIDs::bypass), "release A: back on (momentary)");
    }

    std::printf ("Footswitch B: Seasick and the swell\n");
    {
        Rig r;
        r.tap (1);
        expect (r.on (ParamIDs::seasick), "tap B latches Seasick on");
        r.tap (1);
        expect (! r.on (ParamIDs::seasick), "tap B again latches it off");
        r.hold (1, 0.3);
        expect (! r.on (ParamIDs::swell), "no swell before the hold time");
        r.settle (0.4);
        expect (r.on (ParamIDs::swell), "hold B swells");
        expect (! r.on (ParamIDs::seasick), "the swell leaves the Seasick latch alone");
        r.release (1); r.settle (0.05);
        expect (! r.on (ParamIDs::swell), "release B falls back");
    }

    std::printf ("Footswitch C: Freeze\n");
    {
        Rig r;
        r.tap (2);
        expect (r.on (ParamIDs::freeze), "tap C freezes");
        r.tap (2);
        expect (! r.on (ParamIDs::freeze), "tap C again lets it flow");
        r.hold (2, 0.8);
        expect (r.on (ParamIDs::freeze), "hold C: frozen while held");
        r.release (2); r.settle (0.05);
        expect (! r.on (ParamIDs::freeze), "release C flows again (momentary)");
    }

    std::printf ("Footswitch D: Re-roll and Pluck\n");
    {
        Rig r;
        const int before = r.rerolls();
        r.press (3); r.settle (0.05);
        expect (r.rerolls() == before, "Re-roll waits out the pair window");
        r.settle (0.1); r.release (3); r.settle (0.05);
        expect (r.rerolls() == before + 1, "tap D re-rolls once");
        r.press (3); r.release (3); r.settle (0.2);
        expect (r.rerolls() == before + 2, "a very quick tap still re-rolls");
        r.hold (3, 0.8);
        expect (r.on (ParamIDs::pluck), "hold D plucks");
        r.release (3); r.settle (0.05);
        expect (! r.on (ParamIDs::pluck), "release D opens the gate again");
    }

    std::printf ("Pairs\n");
    {
        Rig r;
        r.set (ParamIDs::seasick, 1.0f);
        r.set (ParamIDs::freeze, 1.0f);
        const int calmsBefore = r.proc.uiCalmPulse.load();
        r.press (0); r.settle (0.03); r.press (1); r.settle (0.1); r.release (0); r.release (1); r.settle (0.1);
        expect (r.proc.uiCalmPulse.load() == calmsBefore + 1, "A+B calms");
        expect (! r.on (ParamIDs::seasick) && ! r.on (ParamIDs::freeze), "Calm clears Seasick and Freeze");
        expect (! r.on (ParamIDs::bypass), "A+B leaves bypass as it was");

        const int keepsBefore = r.proc.uiKeepPulse.load(), rerollsBefore = r.rerolls();
        r.press (3); r.settle (0.03); r.press (2); r.settle (0.1); r.release (2); r.release (3); r.settle (0.2);
        expect (! r.on (ParamIDs::freeze), "C+D tap (D first) leaves freeze as it was");
        expect (r.rerolls() == rerollsBefore, "C+D tap (D first) doesn't re-roll");
        r.press (2); r.settle (0.03); r.press (3); r.settle (0.1); r.release (3); r.release (2); r.settle (0.2);
        expect (! r.on (ParamIDs::freeze), "C+D tap (C first) leaves freeze as it was");
        expect (r.rerolls() == rerollsBefore, "C+D tap (C first) doesn't re-roll");
        expect (r.proc.uiKeepPulse.load() == keepsBefore, "C+D tap doesn't keep");

        r.press (2); r.settle (0.03); r.press (3); r.settle (1.2);
        expect (r.proc.uiKeepPulse.load() == keepsBefore + 1, "C+D held 1 second keeps");
        r.release (2); r.release (3); r.settle (0.1);
        expect (! r.on (ParamIDs::pluck) && ! r.on (ParamIDs::freeze), "C+D hold leaves pluck and freeze alone");
        expect (r.proc.uiKeepPulse.load() == keepsBefore + 1, "only one Keeper per hold");
    }

    std::printf ("Expression on Depth (most of the travel is the subtle zone)\n");
    {
        Rig r;
        r.cc (11, 0); r.settle (0.05);
        expect (r.value (ParamIDs::depth) < 0.01f, "heel is still water");
        r.cc (11, 127); r.settle (0.05);
        expect (r.value (ParamIDs::depth) > 0.99f, "toe is full depth");
        r.cc (11, 64); r.settle (0.05);
        expect (r.value (ParamIDs::depth) > 0.3f && r.value (ParamIDs::depth) < 0.4f, "half travel is still subtle");
        r.cc (11, 76); r.settle (0.05);
        expect (r.value (ParamIDs::depth) < 0.41f, "60% of the travel stays under 40% Depth");
        expect (std::abs (expressionToDepth (depthToExpression (0.63f)) - 0.63f) < 0.001f, "the curve and its inverse agree");
    }

    std::printf ("Expression on Mix and LPG\n");
    {
        Rig r;
        r.set (ParamIDs::expTarget, (float) names::ExpLpg);
        const float depthBefore = r.value (ParamIDs::depth);
        r.cc (11, 127); r.settle (0.05);
        expect (r.value (ParamIDs::lpg) > 0.99f, "toe opens the gate fully");
        r.cc (11, 0); r.settle (0.05);
        expect (r.value (ParamIDs::lpg) < 0.01f, "heel closes it");
        expect (std::abs (r.value (ParamIDs::depth) - depthBefore) < 0.001f, "Depth is left alone");
        r.set (ParamIDs::expTarget, (float) names::ExpMix);
        r.cc (11, 127); r.settle (0.05);
        expect (r.value (ParamIDs::mix) > 0.99f, "Mix follows the pedal");
    }

    std::printf ("Remote control through the parameter list\n");
    {
        Rig r;
        r.set (ParamIDs::foot[0], 1.0f); r.settle (0.05); r.set (ParamIDs::foot[0], 0.0f); r.settle (0.05);
        expect (r.on (ParamIDs::bypass), "footA parameter taps A");
        r.set (ParamIDs::foot[1], 1.0f); r.settle (0.8);
        expect (r.on (ParamIDs::swell), "footB parameter held swells");
        r.set (ParamIDs::foot[1], 0.0f); r.settle (0.05);
        const int keeps = r.proc.uiKeepPulse.load();
        r.set (ParamIDs::keeper, 1.0f); r.settle (0.05);
        expect (r.proc.uiKeepPulse.load() == keeps + 1 && ! r.on (ParamIDs::keeper), "keeper parameter keeps once and resets");
        const int calms = r.proc.uiCalmPulse.load();
        r.set (ParamIDs::calm, 1.0f); r.settle (0.05);
        expect (r.proc.uiCalmPulse.load() == calms + 1 && ! r.on (ParamIDs::calm), "calm parameter calms once and resets");
        const int rolls = r.rerolls();
        r.set (ParamIDs::reroll, 1.0f); r.settle (0.05);
        expect (r.rerolls() == rolls + 1 && ! r.on (ParamIDs::reroll), "reroll parameter re-rolls once and resets");
    }

    std::printf ("Latency\n");
    {
        Rig r;
        expect (r.proc.getLatencySamples() == 48, "1 ms Punch lookahead reported to the host at 48 kHz");
    }

    std::printf ("No clashes\n");
    {
        Rig r;
        const auto before = r.snapshot();
        r.cc (64, 127); r.settle (0.6); r.cc (64, 0);
        r.cc (1, 100);
        for (int ch : { 1, 2, 15 })
        {
            for (int n : { 20, 21, 22, 23 }) { r.cc (n, 127, ch); r.settle (0.6); r.cc (n, 0, ch); }
            r.cc (11, 0, ch);
        }
        r.settle (0.2);
        expect (r.snapshot() == before, "sustain, mod wheel and other channels change nothing");
    }

    std::printf ("\n%s: %d failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
