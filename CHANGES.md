# 5-LAB: changes from Felucca 1.6.1

5-LAB is a fork of [Felucca](https://github.com/hugelton/Felucca) 1.6.1 by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only like the
original. **Version 1.0.4** (the first versions, 0.6 to 0.9.8, were called LAB-4T) is the fork's own numbering, so it is never mistaken for an official Felucca release.
Five audio tracks, every engine on all five; the lab is for trying things out.

Not affiliated with or endorsed by Hügelton Instruments or M-VAVE. Everything not listed here is Felucca 1.6.1 as it was
(the sound is the same: the 98 golden renders of `tests/golden.txt` are unchanged).

## 1.0.4: RAMP records the corners of a knob turn, not every step

- **Why.** With REC, PLAY and the automation REC row on, a knob turned from step 1 to step 12 wrote a record on every step it passed, 12 of the 128
  for what RAMP can play back from two.
- **What.** On a track in RAMP, a recorded turn keeps its corners: the start, the end and where the turn changes direction or pauses. Turn the LEVEL
  straight up over 12 steps: 2 records. Up and then down: 3 (the peak). A point that lies on the line of its neighbours, within 3 % of the parameter's
  range, is not kept. In HOLD (the default) nothing changes: every step is a record, as the staircase needs.
- **Over an older recording.** A turn recorded over earlier records of the same parameter replaces those between its start and its end; the ones outside
  it and the locks stay.
- No change to the project format, flash or RAM (a few bytes of state for four gestures at a time).
- Tests: `tests/ramp_test.c` records turns (a line, up and down, up / pause / up, over an older recording, in HOLD) and counts the records.

## 1.0.3: RAMP, automation events that move to the next one (SEQ > AUTOMATION > RAMP)

- **Why.** An automation EVENT held its value until the next event, so a rise or fall (LEVEL from -10 dB at step 1 to 0 dB at step 16, as in a
  DAW's lane) needed an event on every step: 16 of the 128 records for one parameter over a bar.
- **What.** A new row, **RAMP**, after QUANTIZE (KNOB 4: ON / OFF, per track, OFF by default = HOLD, Felucca's behaviour). In RAMP, from one event to the
  next event of the **same parameter** on the track the value moves in a straight line, updated every audio block (not a staircase of steps). Two records
  (step 1 and step 64) make a ramp over the whole pattern. Before the first event and after the last the value holds, as before.
- **Locks.** A LOCK is not an event: it sounds on its step as it is and the line goes on after it. Only events (AUTO) are joined.
- **Saved.** The setting is a bit of the track in the project's automation record: a reserved byte of FUN10 (tracks 1..4; zero in every older project, so
  they stay in HOLD) and a bit of the fifth track's block. Same flash and RAM layout, one byte of RAM (none for the song: a song plays each slot's setting).
  Like QUANTIZE it is a setting, not an undo step; CLEAR keeps it.
- **Cost.** Only a track in RAMP, with automation on, does the work: one pass over its records (at most 128) per audio block. Nothing for the others.
- Not done (on purpose): a ramp from the last event to the first of the next pass; events beyond LEN still pull the line (a ramp aims at the next
  event even past the pattern's length).
- Tests: new `tests/ramp_test.c` (HOLD / RAMP values step by step and inside a step, up and down, a parameter with negative values, three events, a lock in the
  line, PLAY OFF, CLEAR, the project round trip of tracks 1, 3 and 5, flash save / load, a project without the byte, invalid bits, the row and its keys);
  the tests that count AUTOMATION's rows count one more; a new render `auto_ramp`.

## 1.0.2: automation records only when you arm it (SEQ > AUTOMATION > REC)

- **Why.** With REC on and the song playing, any knob you turned (on any page), a MIDI CC or an editor edit was written as automation on the selected
  track, so playing live meant going back to AUTOMATION to delete what you had not meant to record.
- **What.** AUTOMATION has a new row, **REC**, right under PLAY (KNOB 4: ON / OFF). It is **OFF** at power-on and at every project load, and it is never
  saved. Automation is recorded only while it is ON (and REC and PLAY are on, on the selected track, as before); with it OFF a turned knob just changes the
  sound, as with REC off. To record: open SEQ > AUTOMATION, set REC to ON, play and move the knobs; turn it OFF when done. The card icon of the
  recording (MOTION REC) shows only while it is armed.
- **Unchanged.** PLAY (play the automation back) is as before; locks made by holding a step and turning a knob, + ADD and the list's own edits are not
  affected. No change to the project format, the flash or the RAM (one byte).
- Tests: `motion_test`, `ui_test`, `ui155_test`, `step15_test`, `midi_control_test` arm REC where they record and count the new row; a new check that
  REC and PLAY with the row OFF record nothing; a new render `auto_rec`.

## 1.0.1: FUN11, a project and its fifth track in ONE flash object

- **Why.** In 1.0.0 track 5 lived in a second flash object beside the FUN10 project, so a power cut between the two writes could leave a
  project with the wrong (or no) track 5. Now they are one object, committed together: after any cut the whole previous project is there, or the whole new one.
- **How.** A project is an object of two flash sectors a copy (A/B as before): the FUN10 record (tracks 1..4, 3840 bytes), then the fifth track's block
  (1280 bytes, "FT5A"), 5120 bytes in all; the record in the first sector is written last, over a CRC of both. The four slots and the autosave are in
  the flash of the old third sample slot (0xC8000..0xDBFFF, 20 sectors); the object can grow to 7936 bytes, room for more tracks or data
  without another format. (`storage.c` `st_save2` / `st_load2`, `project.c` `proj_flash_pair*`; `tests/storage_test.c` cuts a save at every byte.)
- **No extra RAM.** The two sectors are read and written in 256-byte chunks straight from/to the project's buffers (the CRC runs over the chunks), so the
  shared flash buffer stays one sector's and the build's pool headroom check (8 KB) holds.
- **Your projects.** A slot with no FUN11 object yet (Felucca's, or 5-LAB 1.0.0's) loads its FUN10 as before, track 5 empty; it becomes FUN11 at its next
  save. 1.0.0's separate track-5 objects are not read: **track 5 of a project saved by 1.0.0 is lost at the update** (the other four tracks, the name and the
  globals load). The first FUN11 save of a slot ends the old object's say for it, and an erased slot stays empty. The autosave behaves the same.
- **Going back.** A project saved by 1.0.1 is not read by 1.0.0 or Felucca (a newer record): use MENU/editor backups before going back.
- The editor's backup archive is unchanged (ids 0..14, 32, 33); restoring ids 2..5 then 11..14 writes the project, then the project with its block.

## 1.0.0: five tracks (the project is now called 5-LAB)

**Five tracks, every engine on all five**
- Felucca has four tracks; 5-LAB has five (`NTRK` 5), and any of the 13 engines can be on any of them. The 8 voices stay shared.
- **Projects keep their format.** FUN10 still stores four tracks, so a 5-LAB project opens in Felucca 1.6.1 (without track 5) and every older project
  loads here with track 5 empty. Track 5 lives in a linked 1280-byte object ("FT5A", tied to the project by its hash) in the flash that USR3 used. The
  autosave has its own companion. (`project.c` PROJ5, `storage.c`; `tests/proj5_test.c`, `tests/persistence_test.c`.)
- **USR3 is gone.** Its flash holds track 5's companion objects, so there are two user sample slots (USR1, USR2). SAMPLE / GRAIN / SLICE lists lose USR3
  (SLICE's PIANO is source 3; an old 4 is clamped to it). A backup made before keeps its USR3 in the file, and a restore skips it and says so.
- The song chain has five slots per row, the MOTION places cover five tracks, MIDI IN TRACK has a fifth channel.

**Controls**
- **GLO layer:** black keys mute T1..T5 (D#4 for T5), the white keys at C4 and up solo them, SOLO 5 on C4. The four functions move to the last four white
  keys from D5: **D5 UNMUTE ALL, E5 MIDI LEARN, F5 FILL, G5 TAP**. In the FX layer the fifth black key mutes track 5 as the others.
- **HOME > LEVELS is removed.** The level of every track, T5 included, is edited in the MIXER, selecting each track.
- **MENU > MIDI > T5 CHANNEL** (a row moved out of the MIDI tab makes room); SCREEN WAKE moved to SYSTEM.

**Screens redrawn for five columns**
- MIXER (46 px columns; REC / ARM and the MUTE badge fit beside the name; PAN shown as -64..+63), HOME TRACKS, SONG lanes, the GLO and FX key maps.

**Web editor, backup and protocol**
- The editor shows five tracks and two sample slots; SONG lanes are 5 wide. Backups use ids 0..9, 10 (T5 half of the live music), 11..14 (halves of
  slots A..D), 32, 33 (17 objects); older archives (13 / 12 / 11 objects) still read. `EDITOR_PROTOCOL.md` has the details.

**Reading notes**
- The 98 golden renders are unchanged (the sound is the same). Sources of the listed numbers: `tests/` (ui_test, editor_test, proj5_test, storage_test,
  persistence_test, perform_test, slice_test, sample_test and the web tests).

## 0.9.8 (and the 0.6 before it)

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

**Knobs answer on every page**
- On HOME (SCOPE view) and the pages whose picture is the output scope, the knobs felt stiff next to the MIXER's: the scope was redrawn every other frame,
  which made the frame long, and the knobs are read once a frame. While a knob is turned (and for about half a second after) the scope is redrawn every
  fourth frame instead, then every other frame again. (`ui_graph.c` `graph_signature`.)

**SELECT in PAGE mode stops at the ends**
- MENU > CONTROL > SELECT PAGE no longer goes round: turned back it stops at the first page of the family, turned forward at the last one
  (HOME is MIXER, CLOCK after it). The family's button and HOME's taps still go round. (`ui.c` `page_turn`; `tests/ui155_test.c`.)

**Taken from Felucca's main (after 1.6.1)**
- `fx.c`: no divide by zero when the BPM reads 0 for a moment (#282); `tests/drumfuzz_test.c` includes `<stdint.h>` (#281).
- Licence texts as Leo updated them: the Hügelton Sample Pack and `tools/gen_waves.py` are CC0 1.0 (`LICENSING.md`, `CREDITS.md`, the ABOUT credits, the installer footer).

**Fork**
- Version `v0.9.8` (the editor reports `FELUCCA v0.9.8 5-LAB`; the "FELUCCA " prefix is what the web editor and installer look for), splash and ABOUT say 5-LAB and credit Hügelton Instruments.
- `.github/workflows/build.yml`: builds the package, the browser emulator ("Try"), the web editor and installer, runs `tests/ci_quick.sh`, and
  publishes to GitHub Pages (a public repository).
- New tests: the CLK checks in `tests/ui_test.c`; `tests/ci_quick.sh`.

## Known limits
- With CLK on USB or TRS, the sequencer stops if the clock is silent for more than 0.5 s (as in Felucca).
- The firmware was built and run on one FM-1 by the maintainer; use at your own risk, keep the official package or Felucca's `.fwsc` at hand.
