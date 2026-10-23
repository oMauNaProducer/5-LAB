# LAB-4T: changes from Felucca 1.6.1

LAB-4T is a fork of [Felucca](https://github.com/hugelton/Felucca) 1.6.1 by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only like the
original. **Version 0.6** is the fork's own numbering: it starts again from 0.x so it is never mistaken for an official Felucca release.
Four audio tracks (the "4T"), as in Felucca; the lab is for trying things out.

Not affiliated with or endorsed by Hügelton Instruments or M-VAVE. Everything not listed here is Felucca 1.6.1 as it was
(the sound is the same: the 98 golden renders of `tests/golden.txt` are unchanged).

## 0.6

**MIDI clock out (MENU > SYSTEM > CLOCK OUT, OFF / ON)**
- With CLK on INT, the FM-1 sends on USB MIDI: Start (0xFA) when the song starts from the top, 24 clock pulses (0xF8) per quarter note at the
  song tempo, Stop (0xFC) when it stops. A second FM-1, a DAW or a drum machine set to follow the USB clock plays along.
- The pulses go on while the FM-1 is stopped (no Start), so the other box knows the tempo before PLAY.
- Not sent with CLK on USB or TRS (no loop through a computer). Nothing is queued without a USB host.
- Turned on while the song plays, it waits for the next start from the top (a Start is always step 0: there is no song position).
- The editor knows it (MENU setting id 33, `web/EDITOR_PROTOCOL.md`).

**External clock (CLK USB / TRS)**
- **CLK is kept** in the settings (it came back to INT at every power-on): a project load leaves it, so does the CLOCK OUT switch.
- **A steady BPM:** the tempo is no longer read from each quarter beat (a pulse timed to the millisecond is 1 to 2 % off, so a 140 wandered
  between 139 and 141). A small loop follows the pulses: each is compared with where the estimate expected it, and the period is corrected
  by a part of the difference. The BPM shown (and the beat the delay, the arp and the slicer follow) moves only when the estimate is 0.9 BPM
  (0.2 % for the beat) away: a 140 or a 140.5 with a millisecond or two of jitter keeps one value. A quarter beat 4 % off the estimate twice
  running is a new tempo, read again at once; a ramp is followed within 0.3 BPM; a pulse lost on the way, or a gap of 200 ms, is handled.
- **No jump at PLAY:** the first two quarters after a Start are not read (a DAW's first pulses are early or late and read as 143 on a 140);
  the BPM appears after the next beat. The sequencer follows the pulses as before.

**Fork**
- Version `v0.6` (the editor reports `FELUCCA v0.6 LAB-4T`; the "FELUCCA " prefix is what the web editor and installer look for), splash and ABOUT say LAB-4T and credit Hügelton Instruments.
- `.github/workflows/build.yml`: builds the package, the browser emulator ("Try"), the web editor and installer, runs `tests/ci_quick.sh`, and
  publishes to GitHub Pages (a public repository).
- New tests: `tests/clockout_test.c`, `tests/clocksm_test.c`, the CLK checks in `tests/ui_test.c`; `tests/ci_quick.sh`.

## Known limits
- No Song Position Pointer and no Continue on the way out; the Start always plays from step 0.
- With CLK on USB or TRS, the sequencer stops if the clock is silent for more than 0.5 s (as in Felucca).
- The FM-1's USB MIDI clock reaches a DAW with about a millisecond of jitter per pulse (the audio block, the USB poll and the USB frame); the tempo
  is exact on average, a DAW may show 139.9 or 140.1.
- Two FM-1 cannot clock each other over a USB cable: both are USB devices. A host (a computer, a USB MIDI host box) is needed.
- The firmware was built and run on one FM-1 by the maintainer; use at your own risk, keep the official package or Felucca's `.fwsc` at hand.
