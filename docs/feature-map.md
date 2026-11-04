# SeaSick: research and feature map

Written 4 November 2026. SeaSick recreates the behaviour of a random-modulation BBD pedal with an envelope low pass gate (the Fairfield Circuitry Shallow Water K-Field Modulator) in the Flip Fella family look. All DSP is original code written from public descriptions of the pedal. The plugin UI names no brands.

Status: **draft for Kevin's review.** Nothing is built yet. Open questions are listed at the end.

## Versions

| Version | Year | What changed |
| --- | --- | --- |
| Shallow Water (KFM) | 2016 to now | The only circuit. Six knobs (Rate, Damp, Depth, LPG, Mix, Volume), one true-bypass footswitch, mono, 9 V. Inside: Boost and Pad jumpers and an LPG Adjust trimpot |
| Special KFM (SKFM) | 2023 | 500 units marking 10,000 pedals made. Each enclosure is scorched by fire and washed in water. Same circuit and specs; the change is cosmetic |

No source documents a circuit revision, a MkII, an expression or CV jack, or any firmware. Some store listings show only three or four knobs, but those listings are incomplete, not an older version. The manual and every review list six knobs.

**Recommendation: recreate the standard KFM.** It is the only circuit, so this loses nothing. The Special Edition's scorched finish could be a panel skin later, but it doesn't change the sound.

## Controls (from the manual)

| Control | What the pedal does | How SeaSick does it |
| --- | --- | --- |
| Rate | How often the random pitch changes happen, "from rare to nervous". The range is wide | A random step generator. The time between steps is itself random, scattered round the Rate period (see *How the randomness moves*), from about one step every 8 s to about 25 a second, on an exponential taper |
| Damp | The slope of the modulation. At minimum the changes are quick and sudden. Turning it up stretches and softens them. At high settings they get so slow they stop sounding like pitch changes, and you need some dry signal to hear them as chorus | Two integrators in series (a 2-pole slew) between the step generator and the delay line, about 2 ms to 2 s, exponential. Because pitch shift is the *speed* the delay changes, a hard step through low Damp is a sharp lurch, and the same step through high Damp is a slow drift of a few cents |
| Depth | How far the delay time moves. The manual says "less is more" | Delay swing from 0 to about ±5 ms around a centre of about 7 ms (no source gives the real figures, so these are estimates). Lower half of the knob is the subtle zone; the top fifth is seasick |
| LPG | How much of the input's envelope opens a low pass filter and gate on the effect. Low: darker, quiet notes choked, less sustain. High: brighter, more sustain, and more BBD hiss can come through | An envelope follower drives a 2-pole low pass filter and a gain stage together, like a vactrol gate: opens almost instantly, closes in two stages (fast, then a slow tail). The knob sets how far the envelope opens it. At minimum it sits on a fixed dark tone, as on the pedal |
| Mix | Dry against wet. Full wet is vibrato; adding dry gives chorus | Equal-power blend. Full wet is pure pitch wobble; around noon it's chorus, and the high-Damp drift becomes audible as the manual describes |
| Volume | Output level. Users report noon is a little past unity, and the input preamp breaks into overdrive higher up, which bass players like | 0 to +12 dB, unity just before noon. Above about 1 o'clock a soft clipper adds the gentle breakup, mostly on hard picking |
| Footswitch | True bypass | Footswitch A |
| Boost jumper (inside) | On by default for guitar. Off takes 6 dB off the input if it distorts | The Input pills: **Guitar** is Boost on. **Bass** is Boost off plus the bass protections below |
| Pad jumper (inside) | For line level: 6 dB more cut and a lower input impedance | Input pill **Line** (synths, drum machines, a DAW send) |
| LPG Adjust trimpot (inside) | The lowest frequency of the filter. Set with Mix full wet, LPG and Depth at minimum. Very sensitive. Users say it also changes how quickly the gate opens, and too fast gives a pop | **Floor** on the Deep page, 150 Hz to 1.5 kHz, default about 450 Hz. The pop is kept out by the follower's attack, so Floor only moves the tone |

## Hidden behaviours to keep

- **High Rate plus high Damp cancels out.** Fast steps can't get through a slow integrator, so the wobble shrinks. That falls out of the model naturally rather than being faked.
- **High Damp needs dry.** Full wet with high Damp sounds almost still. Bring Mix back and the slow drift appears as a deep chorus. The plugin does the same, and a hint on the panel says so.
- **The LPG hiss.** Opening the gate wide lets more BBD noise through. SeaSick adds a quiet, filtered hiss inside the wet path, so it opens and closes with the gate. A Clean chip turns it off.
- **BBD colour.** The wet path is band-limited to about 7 kHz, with a light companding squash, so it sounds like a short analogue delay, not a digital one.
- **The low end.** One review called the LPG the pedal's weak spot on low frequencies, while a bass player on TalkBass loved the extra low end and the dynamics. Both are handled by the bass protections below.

## How the randomness moves

This is the heart of it, and a clean LFO can't do it. SeaSick's "K-field" generator:

1. **Irregular clock.** Each step waits a random time round the Rate period: never shorter than a third of it, sometimes three times longer. So the water sometimes sits still, then lurches twice in quick succession.
2. **Wandering targets.** Each new delay target is drawn near the last one (a bounded random walk), with an occasional big jump. So it drifts like warped tape, then kinks.
3. **Damp integrators.** Two slew stages turn the steps into curves. Low Damp: sudden kinks, the broken-cassette sound. High Damp: slow, seasick swells.
4. **Past and future.** The maker's own description is of past and future forces meeting at irregular intervals. In the model, each step blends a little of the step before it back in, so movements sometimes echo themselves.

The panel shows the actual modulation signal scrolling past, so you can see it.

## Bass

Delay modulation bends every note by the same number of cents, but on low notes the same wobble sounds much more out of tune and beats against the fundamental. SeaSick's **Bass** input:

- **Low split.** Below about 110 Hz (adjustable 60 to 200 Hz on the Deep page) the signal skips the delay, so the fundamental stays in tune and in time. It still goes through the gate's level envelope, so plucks still pluck. Above the split, everything wobbles.
- **Wobble ceiling.** Caps how far the pitch can bend in cents: about 30 cents in Bass mode, the full range on Guitar. Seasick extremes stay musical rather than atonal.
- **Punch.** The gate reads the signal 1 ms ahead, so it's already open when the attack arrives and the pick transient is never filtered. That costs 1 ms of latency, which is reported to the host. It's on by default for Bass and optional for Guitar.
- **No choke below the floor.** In Bass mode the gate never closes fully on the fundamental, so soft notes sound mellow rather than vanishing.

## Footswitches

The pedal has one switch (bypass) and no MIDI, so there's no original MIDI map to clash with. See `foot-control-plan.md` for the full plan.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass on or off (acts on the press) | Momentary: flips while held |
| B | **Seasick** on or off: Depth goes deep, Damp eases and Rate rises, so it's the full warped-tape lurch | **Swell**: climbs into Seasick over about a second, falls back on release |
| C | **Freeze** on or off: the water stops where it sits | Freeze while held |
| D | **Re-roll**: a fresh lurch right now, from a new seed | **Pluck**: the gate shuts down hard, every note plucks short, until release |
| A+B | **Calm**: clear Seasick, Freeze and Pluck, back to the knobs | |
| C+D | Hand the footswitches to the next SeaSick | Held 1 second: **Keeper** |

## Expression (CC 11 on channel 16)

Default target: **Depth**. It's the knob you'd reach for live, and it's the most expressive: heel is still water, toe is seasick. The sweep is shaped:

- The first 60% of travel covers the subtle zone (0 to 40% Depth), where the manual says the good sounds live.
- The last fifth runs into seasick.
- In Bass mode the wobble ceiling still applies, so a full toe-down stays musical.

On-screen chips switch the pedal to **Mix** or **LPG** (LPG as a playable wah-like bloom), and the menu reaches Rate, Damp and Volume. Heel and toe calibration, curve and smoothing come from `PedalControl`.

## Pebble

Pebble sits in the right-hand section. It rocks and tilts with the actual modulation, gently seasick, and leans further and turns a little green when Seasick is on. It bobs up with each note as the low pass gate opens and settles as the gate closes, so blooms are slow, soft rises and plucks are quick hops. It holds still with its eyes shut when Freeze is on, lurches on a Re-roll, dozes when bypassed, taps its foot on a footswitch press, leans with the expression pedal and nods when you keep a moment.

## Family conventions (following OilSlick)

- `Source/PedalControl.*` and `Source/Keeper.*` copied from OilSlick and Prism Patch: Learn, calibration, curve, reading the footswitch straight from USB, combo holds, shared `Flip Fella Pedals.xml`, and Keeper (hold C+D for a second keeps the last 8 seconds).
- Pages: Main and Pedal, plus a Deep drawer for the inside-the-pedal settings (Floor, Split, Ceiling, Punch, Clean). Every footswitch job has an on-screen button.
- The engine is plain C++17 with no JUCE (portable to the Pi rig), with an offline render test and a footswitch test.
- Builds: the Windows VST3 and Linux ARM64 through GitHub Actions, published to the `builds` branch, with CPU figures at 48 kHz for 64 and 128 sample buffers.

## Sources

- Shallow Water manual (control descriptions, Boost and Pad, LPG Adjust), as mirrored on ManualsLib, manuals.plus and Drop-D Sounds
- Fairfield Circuitry FAQ (jumper positions, 9 V only)
- Retailer listings for the KFM and the 2023 Special KFM (Perfect Circuit, Gbase, Telluride Music, Austin Guitar House, Turramusic)
- Reviews: FutureMusic (three reviewers, critical of the LPG on low end), Loopy Demos, GuitarFX Direct ("a seriously broken 1970s cassette tape"), Pedal of the Day, Guitar Chalk settings guide
- Forums: TalkBass "Drowning in Shallow Water" (bass use, Volume breakup, LPG dynamics), Elektronauts Shallow Water threads (line level, synth use)

The open web wasn't reachable from the build session, so these were read through search excerpts rather than full pages.
