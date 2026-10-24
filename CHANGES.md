# LAB-4T: changes from Felucca 1.6.1

LAB-4T is a fork of [Felucca](https://github.com/hugelton/Felucca) 1.6.1 by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only like the
original. **Version 0.6** is the fork's own numbering: it starts again from 0.x so it is never mistaken for an official Felucca release.
Four audio tracks (the "4T"), as in Felucca; the lab is for trying things out.

Not affiliated with or endorsed by Hügelton Instruments or M-VAVE. Everything not listed here is Felucca 1.6.1 as it was
(the sound is the same: the 98 golden renders of `tests/golden.txt` are unchanged).

## 0.6

**External clock (CLK USB / TRS)**
- **CLK is kept** in the settings (it came back to INT at every power-on): a project load leaves it.
- Tried and left out: MIDI clock out on USB, and a smoother BPM estimate. On the FM-1 they did not hold up (the BPM still jumped when following a
  DAW, and a DAW following the FM-1 wandered between 139 and 141), so the clock follows Felucca 1.6.1's code as it was.

**FX HOLD releases a track muted in GLO**
- A track muted in GLO stays muted until it is released. In the FX layer, its black key releases it for good when it is let go (the same as
  unmuting it in GLO; held, the track stays muted). Once the track plays, the key mutes it only while it is held, as in Felucca. The map shows a GLO-muted
  track as GLO does (lit as muted), and redraws when the key releases it. (`perform.c` `perf_press`, `ui_layer.c`; `tests/perform_test.c`.)

**KNOB ACCEL tuned (MENU > KNOB ACCEL ON)**
- The acceleration takes the tuning of JIANT's: a calmer turn is helped sooner and a fast one goes further. 40 ms a detent x2, 27 ms x3, 20 ms x4,
  10 ms x8, 5 ms x16; the cap follows the range: x2 up to 32 values, x4 up to 64, x8 up to 100, x16 above (Felucca: 25 ms x2, x4 / x8 at most). It works on
  ranges above 16 (Felucca: above 32). A slow turn (60 ms a detent or more) is still one value a detent, and a reversal never jumps. A 12-detent
  flick on a -100..100 value moves 75 (Felucca 47). The setting is still OFF until it is turned on once in the MENU. (`ui_input.c` `accel`.)

**Taken from Felucca's main (after 1.6.1)**
- `fx.c`: no divide by zero when the BPM reads 0 for a moment (#282); `tests/drumfuzz_test.c` includes `<stdint.h>` (#281).
- Licence texts as Leo updated them: the Hügelton Sample Pack and `tools/gen_waves.py` are CC0 1.0 (`LICENSING.md`, `CREDITS.md`, the ABOUT credits, the installer footer).

**Fork**
- Version `v0.6` (the editor reports `FELUCCA v0.6 LAB-4T`; the "FELUCCA " prefix is what the web editor and installer look for), splash and ABOUT say LAB-4T and credit Hügelton Instruments.
- `.github/workflows/build.yml`: builds the package, the browser emulator ("Try"), the web editor and installer, runs `tests/ci_quick.sh`, and
  publishes to GitHub Pages (a public repository).
- New tests: the CLK checks in `tests/ui_test.c`; `tests/ci_quick.sh`.

## Known limits
- With CLK on USB or TRS, the sequencer stops if the clock is silent for more than 0.5 s (as in Felucca).
- The firmware was built and run on one FM-1 by the maintainer; use at your own risk, keep the official package or Felucca's `.fwsc` at hand.
