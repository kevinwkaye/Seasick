# SeaSick

A VST3 for Ableton Live, recreating the Fairfield Circuitry Shallow Water K-Field Modulator in the Flip Fella look. All DSP here is original code written from public descriptions of the pedal's behaviour.

A short analogue-style delay whose time is pushed around by random steps instead of an LFO, through a low pass gate that follows your playing. Subtle, it's a drifting chorus or a broken-cassette warble. Pushed, it's seasick. The gate makes notes bloom or pluck.

## Install

1. Open the **builds** branch of this repo and download `SeaSick-Windows.zip`.
2. Unzip it and copy the `SeaSick.vst3` folder to `C:\Program Files\Common Files\VST3`.
3. In Ableton, rescan plugins (Preferences, Plug-Ins, Rescan). SeaSick appears under Audio Effects.

The same branch has `SeaSick-Linux-ARM64.zip`, built for the future Raspberry Pi rig.

## The panel

| Control | What it does |
| --- | --- |
| **Rate** | How often the random changes happen, from one every 8 seconds to 25 a second. The gaps are random too, so the water sits still, then lurches twice |
| **Damp** | How smoothly each change happens. Low: sudden kinks, the broken-cassette sound. High: slow, seasick swells. High Rate with high Damp cancels out, as on the pedal |
| **Depth** | How far the delay moves (red hand). Less is more |
| **LPG** | How much your playing opens the low pass gate. Low: dark, soft notes choke, short. High: bright, long and blooming, with a little hiss |
| **Floor** | The gate's darkest point, the pedal's inside trimpot |
| **Mix** | Full wet is vibrato. Bring in some dry for chorus, and to hear high Damp's slow drift |
| **Volume** | Output. Unity just before noon. Higher up it breaks up on hard picking |
| **Input** | Guitar, Bass or Line, the pedal's inside jumpers. **Bass** also keeps everything below the split dry and in tune, and caps bends at the wobble ceiling |
| **Punch** | The gate reads 1 ms ahead, so the attack is never filtered. Costs 1 ms of latency, which is reported to Ableton |
| **Hiss / Clean** | The pedal's quiet bucket-brigade hiss, which opens with the gate. Clean turns it off |
| **Freeze: Settle / Hold bend** | What Freeze does. Settle stops the water so the pitch comes home and the chorus colour of that moment stays. Hold bend keeps bending the way it was going, then drifts home |
| **Re-roll** | A fresh lurch now, from a new seed |

**Pebble** floats on a water line drawn from the real modulation. It rocks and tilts with the wobble, turns faintly green when it gets seasick, and bobs up as each note opens the gate: slow rises for blooms, quick hops for plucks. It shuts its eyes on Freeze, lurches on Re-roll, dozes when bypassed, taps its foot on a footswitch press, leans with the expression pedal and nods when you keep a moment.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. The four squares along the bottom act exactly like footswitches, taps, holds and pairs included. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or click the expression bar and choose Learn.
3. Click the expression bar, choose **Calibrate heel and toe**, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass on or off (acts on the press) | Momentary: flips while held |
| B | **Seasick** on or off: Depth deep, Damp eased, Rate up a notch | **Swell**: climbs into Seasick over about a second, falls back on release |
| C | **Freeze** on or off (Settle or Hold bend, chosen on the panel) | Freeze while held |
| D | **Re-roll**: a fresh lurch now | **Pluck**: the gate snaps short on every note |
| A+B | **Calm**: clear Seasick, Freeze and Pluck | |
| C+D | Hand the footswitches to the next SeaSick | Held 1 second: **Keeper** |

A hold is anything longer than about half a second. D waits 0.12 seconds before it re-rolls, so a C+D pair never leaves a stray lurch.

**Expression** moves Depth by default. Heel is still water, toe is seasick, and most of the travel is the subtle zone: the first 60% covers 0 to 40% Depth, and the last fifth runs into seasick. The DEPTH, MIX and LPG chips on the expression bar switch between those three (LPG as a playable bloom), and the menu reaches Rate, Damp and Volume. The Pedal page sets what the heel and toe reach, smoothing, and the curve.

### Getting footswitch MIDI to the plugin in Ableton

SeaSick reads your footswitch straight from its USB cable, so every copy you load works with no MIDI track. The dot next to **FOOTSWITCHES** turns green when it's connected, and Pebble says FOOTSWITCHES READY.

1. In Ableton's Preferences, Link Tempo MIDI, turn **Track**, **Sync** and **Remote** off for your footswitch's input. Windows lets only one program use a MIDI input at a time, so Ableton has to let go of it.
2. Load SeaSick. That's it.

If the dot stays grey, click **FOOTSWITCHES**. The menu says what's wrong, and you can pick your footswitch from the list if it wasn't found automatically.

With several copies loaded, the footswitches drive the one you opened or clicked last. Tap C+D to hand them to the next copy.

Prefer routing through Ableton? Choose **Only from a MIDI track in Ableton** in that menu, then turn **Track** back on for the footswitch, make a MIDI track with **MIDI From** set to the footswitch and **MIDI To** set to SeaSick, and arm it.

The pedal itself has no MIDI, so there is no original pedal MIDI map to clash with the footswitches. Anything else on the cable, the sustain pedal on CC 64 included, is ignored.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: Custom mode, footswitches A to D as momentary CC 20 to 23, the pedal jack as Expression Pedal on CC 11. It always sends on channel 1 whatever CubeSuite says; read directly from its cable, SeaSick accepts any channel. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes. If you've already set this up for OilSlick or another Flip Fella plugin, there's nothing to do.

## Bass

Choose **Bass** on the Input pills:

- Everything below the **low split** (110 Hz, adjustable 60 to 200 Hz) skips the delay, so your fundamental stays in tune and in time. It still goes through the gate's level, so plucks still pluck and soft notes go mellow rather than vanishing.
- The **wobble ceiling** caps how far the pitch can bend, 30 cents by default. Seasick stays musical.
- **Punch** is on, so the gate is already open when your finger or pick hits.

## Keeper

Press **KEEP** (or hold C+D for a second) to log the moment: every setting, the expression pedal, the tempo, the sea state and wobble in cents, the gate, and the last 8 seconds of input and output as WAV files, into `Documents\Flip Fella Keepers`. The small arrow next to KEEP adds a note, exports a zip for Claude, opens the folder, sets Live or Studio, and learns a dedicated Keeper switch.

## Remote control

Every control, every footswitch job and Keeper are in one parameter list that a host or a remote controller can read and set. `footA` to `footD` act as the footswitches (1 to press, 0 to release, so taps, holds and pairs all work). `keeper`, `calm` and `reroll` fire once and reset. `seasick`, `swell`, `freeze` and `pluck` can be set directly.

## CPU

Per instance, one core, Bass mode with everything moving (the heaviest case):

| Machine | 48 kHz, 64 samples | 48 kHz, 128 samples |
| --- | --- | --- |
| Windows x64, GitHub runner | 1.00% | 1.00% |
| Linux ARM64, GitHub runner | 1.12% | 1.11% |

Measured for v0.1 on 4 November 2026. A Raspberry Pi 5 core is slower than these runners, so expect a few times these figures there, still small.

Every build writes fresh figures for Windows x64 and Linux ARM64 to `cpu-windows-x64.txt` and `cpu-linux-arm64.txt` on the builds branch.

## Building

GitHub Actions builds the Windows VST3 and the Linux ARM64 build on every push and publishes them to the `builds` branch. To build locally: `cmake -S . -B build && cmake --build build --config Release`. JUCE downloads automatically on the first configure.

## Tests

- `seasick_render_test` runs the engine with no host: 20 scenarios at three sample rates, plus a check that the random steps are irregular rather than a clock. It covers every control, Rate and Damp cancelling, the gate darkening and plucking, Seasick and the swell, both Freeze modes, Re-roll, the bass ceiling and steady fundamental, bypass, hiss and Clean, and Volume breakup. It checks for NaNs and levels above full scale, then measures CPU. Pass a folder to write a WAV of each scenario.
- `seasick_foot_test` drives the real plugin with plain MIDI press and release messages: tap, hold, A+B, C+D in both orders, Re-roll waiting out the pair window, the 1 second Keeper hold, the expression curves, the remote parameters, the reported latency, and a clash test (sustain pedal, mod wheel and the footswitch numbers on other channels change nothing). Run it with `--snapshot <folder>` under a display to render the panel in six states.

## Project layout

| Path | What it holds |
| --- | --- |
| `Source/engine/` | The DSP engine, plain C++17 with no JUCE: the K-field generator, the BBD line, the gate |
| `Source/Params.h` | Every parameter, its range and default, and the expression curve |
| `Source/PluginProcessor.*` | Host wrapper, footswitch jobs, expression, Keeper, state |
| `Source/PluginEditor.*` | The Main and Pedal pages, the water scope, the gate meter, and Pebble |
| `Source/PedalControl.*`, `Source/Keeper.*` | Shared with every Flip Fella plugin, copied from OilSlick |
| `docs/` | The research, feature map and footswitch plan |
