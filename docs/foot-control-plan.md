# SeaSick foot control plan

Written 4 November 2026. Proposed, waiting for Kevin's approval. Follows the house rules and OilSlick's `PedalControl`.

## Footswitch defaults

Momentary CC 20, 21, 22, 23 on channel 16, or any channel when read straight from the footswitch's USB cable. Learn remaps any of them. Ready out of the box for the M-Vave Chocolate Plus in Custom mode with the EX-P on its jack.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass on or off, acting on the press | Momentary: flips while held |
| B | Seasick on or off | Swell into Seasick over about 1 s, back to the knobs on release |
| C | Freeze on or off | Freeze while held |
| D | Re-roll: a new seed and a fresh lurch now | Pluck: gate snaps short while held |
| A+B | Calm: clear Seasick, Freeze and Pluck | |
| C+D | Hand the footswitches to the next SeaSick | Held 1 second: Keeper |

Why:

- **A** is the pedal's only switch, so muscle memory carries over.
- **B** is your "hold for a deeper wobble". Tap latches it for a whole section; hold is a swell for one phrase. Seasick moves three things together (Depth up, Damp eased, Rate up a notch), because Depth alone at high Damp barely changes anything, so the hold would feel dead.
- **C** freezes the generator and lets the delay settle where it sits. The pitch comes back to centre, because pitch only bends while the delay is moving, but the chorus colour of that moment stays. It's the "tape stops warping" moment. Tap or hold, the same as Freeze on OilSlick's D.
- **D** re-rolls the randomness. At low Rate the water can sit still for seconds, so a tap gives you a lurch exactly where you want it. Holding D gives the low pass gate a job of its own: a staccato pluck for funky bass lines.
- **A+B Calm** is the safety net, as Tame is on OilSlick.
- **Keeper** stays on hold C+D. It doesn't clash: the pedal has one switch, and C and D's single jobs wait out the 0.12 s combo window.

Timing: A acts on the press and is undone if the press turns out to be half of A+B. B, C and D start after the 0.12 s combo window, too short to feel. A tap is under 0.35 s; anything longer is a hold.

## Expression default

CC 11 on channel 16. Moves Depth, shaped so the first 60% of travel is the subtle zone and the last fifth is seasick. On-screen chips switch it to Mix or LPG, and the menu reaches every other control. Calibration, curve and the shared settings come from `PedalControl`. Min, max and smoothing are per preset.

## Original pedal MIDI map

None: the pedal has no MIDI, so nothing can clash with the footswitch and expression defaults.

## Tests (planned)

`Tests/FootTest.cpp`: tap, hold, A+B and C+D in both orders, the 1 second Keeper hold, the Depth expression curve, the remote parameters, and a clash test (sustain CC 64, mod wheel CC 1, and CC 20 to 23 and 11 on channels 1, 2 and 15 change nothing).
