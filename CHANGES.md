# 5-LAB: changes from Felucca 1.6.1

5-LAB is a fork of [Felucca](https://github.com/hugelton/Felucca) 1.6.1 by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only like the
original. **Version 1.1.4** (the first versions, 0.6 to 0.9.8, were called LAB-4T) is the fork's own numbering, so it is never mistaken for an official Felucca release.
Five audio tracks, every engine on all five; the lab is for trying things out.

Not affiliated with or endorsed by Hügelton Instruments or M-VAVE. Everything not listed here is Felucca 1.6.1 as it was
(the sound is the same: the 98 golden renders of `tests/golden.txt` are unchanged).

## 1.1.4: every note, 2 to 64 steps

- **Any length, 1 to 64.** A LONG note now has a length of its own from 1 to 64 steps (1.1.3: up to 14, then END). KNOB 3 on a held note (or on a step's only note, or the whole
  chord with no key) goes **one step a click all the way**: no jump to END; END (the end of the pattern) is just the last length. A chord of four notes can go from step 1 to 63 (or 64)
  on any of the 5 tracks, each note its own length; any number of notes of the pattern can be long. Down: 2 steps, then **LONG NOTE OFF** (a LONG note of one step is a plain note).
- **More at once.** Up to **8** LONG notes sound on a track together (1.1.3: 4).
- **Storage (the saved format changes).** `step_t` is one byte bigger (`ext2`: the high 2 bits of each note's code; the code is now 6 bits, stored as length - 1). On flash the 5th
  track's block PROJ5 is **1480 bytes** (+200: after the old content, under the same hash: a bitmap of the 320 steps that have high bits, then one byte for each of the first 160 of them: a project with more steps than that of lengths over 16 loses the rest), inside the same FUN11 object. A project saved
  by 1.1.0 .. 1.1.3 **opens** (block of 1280 bytes: the high bits are 0; a note with an END code 15 of 1.1.3 is 16 steps); a project saved now does **not open in 1.1.3 or before** (the object is longer).
  The editor's backup takes both sizes.
- **Memory moved (room for per-note fields later).** The delay line is **half as long**: 0.74 s (32,768 samples; 1.49 s before): a 1/4 delay is in time from 81 BPM, a 1/8 from 41 BPM,
  1/4D from 122, 1/2 from 163 (below that the delay plays as long as the line allows). The song `chain` (17.7 KB) moved from the RAM `.bss` to the pool; together they free about 65 KB of the pool
  and 17.7 KB of the RAM. No sound or behaviour change otherwise.
- **RAM.** About 2 KB more (a byte a step in about 2,000 steps kept: the 5 tracks, the project in RAM, the chain's sources, the undo) plus 5 x 200 bytes in the pool for the block
  copies (the first 1.1.4 build with 320 each left the pool 332 B short of the 8192 check). `build.py` checks the headroom (RAM 4096, pool 8192): not measured on the host.
- **KNOB 2 inside a long chord.** On an empty step (REST or TIE) that a chord passes through, turning NOTE no longer makes a lone one-step note: the chord is **cut there**
  (the first part ends before this step) and goes on from it as a chord of its own, with the notes' remaining lengths, and KNOB 2 moves that whole chord, as it does at its first step.
- **Recording into a long chord.** A note played (REC) into a step that a chord passes through (second pass over a chord recorded from its first step) is **added over it**: the chord is
  not cut or replaced. The notes of a chord made of TIEs become LONG notes of the lengths they had (so they can go on under the new note), and the new note is its own step.
- **No duplicate under a long note.** The same pitch played (REC) into a step that a LONG note (or a note's TIEs) of that pitch already lasts through is not recorded under it any more. Step entry (REC armed, stopped) does the same, and a step entered into a chord's TIEs makes that chord LONG notes instead of cutting it.
- **AUTOMATION list: only locks and events.** The rows for a step's CHANCE, RATCH, NUDGE and VEL are gone from SEQ > AUTOMATION (and from + ADD): they were the same fields DETAIL edits.
  The list is PLAY, REC, QUANTIZE, RAMP, the LOCK / AUTO records by step, + ADD. Nothing stored changes (those fields live in the step), and EDIT / CLEAR on the page are as before.
- **Holding a recorded note over other notes.** With REC + PLAY a key held through steps that hold notes no longer overwrites them with its TIEs (the held note erased the steps after it,
  and all of them in the loop): when the hold reaches a step with notes, the held note goes on as a LONG note over them.
- **Held round the end.** A recorded note held through steps with notes, to the end of the pattern, goes on into the next loop as before: from step 1 as a note of its own (added to
  step 1's chord), up to the step where it began, as long as the key is down.
- **A chord's sound does not stop.** Adding a note into a chord made of TIEs while it plays turned the chord into LONG notes, and the notes that were sounding were released at the next step.
  They now go to the LONG notes' keeper and sound to the end of their length.
- **Fix (since 1.1.3):** the notes 3 and 4 of a chord did not redraw the screen while their LEN was turned (only when the key was let go): the step hash that decides a redraw did not include `ext`.
- Tests: `tests/notelen_test.c` (`len64`: a chord of 4 to step 63 on the 5 tracks, saved, loaded, and the 1280-byte block).

## 1.1.3: each note of a chord has its own length, and a note can last past the notes after it

- **Length per note, to the end.** On SEQ > STEP, **hold a note's key and turn KNOB 3 (LEN)**: only that note changes. Its TIEs grow first, up to the next note; one more turn
  makes it a **LONG note** that goes on over the notes after it (1 .. 14 steps, then **END**: up to the end of the pattern, which follows LEN; back from END it goes to 14).
  Below 1 the LONG note is off. **Every note of a chord can be long, each its own length** (four at most, the chord's size). A step with one note needs no key: KNOB 3 is that note.
- **The whole chord.** With a chord and **no key held**, KNOB 3 moves **every note** by the turn (a 3-note chord gets 3 notes longer, each keeping its difference), passing the
  notes after it like a single note does. A drum step (hits) keeps the TIE length it had.
- **Stretching takes over the same note.** When a note is stretched over steps that hold the **same pitch**, those are taken over: the pitch goes out of them (a step left
  empty becomes a REST with its TIEs) instead of sounding again under the long one.
- **Sound.** A LONG note sounds from its step until the gate of its last step, outside the step-by-step release: the notes of the steps in between do not cut it,
  and the same pitch played again by a later step ends it first. STOP ends it. A RATCH step plays its notes as usual (no LONG notes). Up to four LONG notes sound on a track.
- **Piano roll.** One bar per note from its step to its last, over the notes of the steps between; shown from earlier pages. The notes of a chord that end earlier leave no bar over the TIEs.
- **Storage.** No new flash format, no more RAM: a step's notes 0 and 1 keep their LONG code in the `acc` byte (low, high nibble) and notes 2 and 3 in a new `ext` byte, taking the
  place that `n` and `time` shared (they are now bit fields of one byte: same size). Both only while the step has no drum hit. On flash the hit byte carries `ext` (bit 7 of the
  step's flags byte says so). Codes: 0 none, 1..14 steps, 15 END. A project from an older version loads as it was; a lower length than 15 and below END is not possible for a
  pattern longer than 16 (a note that long goes to END).
- **TIE lengths (1.1.0).** A TIE step's `vel` is `0x10 | mask`: the notes of the NOTE step before it that ended before that TIE (any other `vel`: all go on).
- **Live recording.** A chord grows key by key: a note recorded into another step no longer cuts the notes still held from the step before: they go on as LONG notes as long as
  their keys are held. A chord let go key by key keeps each note's length (the TIE masks, written as the keys come up).
- `track_t` grew for the LONG notes and the recording (its pad absorbed it: still a multiple of 16). `step_t` is the same size. Tests: `tests/notelen_test.c`.

## 1.1.0: each note of a chord has its own length

- **Length per note.** On SEQ > STEP, with a chord on the cursor step, **hold a note's key and turn KNOB 3 (LEN)**: that note alone gets longer or shorter,
  while the others keep theirs (the column shows `NOTE` under the number). With **no key held** KNOB 3 is the whole chord as before (the notes that go on to the
  end of the chain get the longer or shorter end; the ones that ended earlier keep theirs).
- **How it is stored.** No new format: a TIE step's `vel` (unused on a TIE until now) holds `0x10 | mask`: bit k of the mask = the NOTE step's note k has
  **ended before that TIE**. Any other `vel` (0, 96 ...) means all go on, which is every older project and recording: nothing changes for them, and an older
  firmware plays a 1.1.0 project with all notes to the end of the chain. A note taken out of a chord, or a chord played over again, fixes its TIEs' bits.
- **Sound.** The sequencer releases each note at its own end (the gate of the last step it covers); a pitch another note of the chord still holds goes on.
- **Piano roll.** Each note is drawn with its own length; the notes that ended leave no bar over the TIEs after them.
- Chains shorten to the longest note: when the longest note is shortened, the TIEs nobody holds any more become RESTs.
- `track_t` grew by 6 bytes (the pad `rsv_sz` took them: same size, same 16-byte multiple). No change to the flash format or the other engines' sound.
  Tests: `tests/notelen_test.c`.

## 1.0.8: the piano roll: a nudged note joins its ties, the slide runs across an overlap

- **A note and its ties.** A note nudged early (QUANTIZE OFF) left a gap between its own bar and the bars of its TIE steps after it, which were drawn from their
  own columns. The bar now goes on from where it is drawn to where the ties begin, so the note's length is one bar, whatever the NUDGE.
- **Slide into a note that starts before this one ends.** A note whose length goes past the start of the next note (the next one nudged early, or this one
  longer by its ties) used to draw the slide from the end of its bar backwards. It now runs from where the next note starts (on this note's row) to where this
  one ends (on the next note's row). A slide where there is no overlap is as before.
- The space before the next bar of the same note (1.0.7) also applies to the last bar of a note with ties.
- No change to the sound, the project, the flash or the RAM. Tests: the `roll_nudge_near` render shows both cases.

## 1.0.7: the piano roll keeps a space between nudged notes and the slide joins their ends

- **Space.** With QUANTIZE OFF, a bar whose next step has the **same note** ends 2 px before the next bar starts (at least 1 px), so a late note and an
  early one after it are two bars, not one. A different note is not shortened (it does not touch). With QUANTIZE ON nothing changes (the bars
  already have 3 px between them). Two notes played less than 3 px (about a sixteenth of a step) apart leave a 1 px bar: there is no room for the space.
- **Slide.** The line of a slide goes from the end of its bar to the start of the next step's bar as they are drawn now (nudged), not from where
  they would be with QUANTIZE ON.
- Not changed: RATCH steps, a TIE's carried bars, the DRUM grid; no change to the sound, the project, the flash or the RAM.
- Tests: `tests/nudge_roll_test.c` (the width for the same note near / far, another note, a chord, the 1 px floor); a new render `roll_nudge_near`.

## 1.0.6: the piano roll shows a NUDGE where it plays

- **What.** SEQ > STEP's piano roll draws the bar of a step that plays off its start (QUANTIZE OFF and a NUDGE, -8..+7 sixteenths of the step) moved
  by it: 6 px early to 5 px late of the 12 px column, kept inside the grid. With QUANTIZE ON the bars sit on their columns, as they play, so turning
  QUANTIZE on and off snaps the notes into the grid and releases them. The grid, the cursor, the playhead and the lock marks stay on the steps.
- **Not moved.** A RATCH step (it plays on its start), a TIE's carried bars, a slide, a REST; the DRUM grid (a later version, if wanted).
- No change to the sound, the project format, the flash or the RAM; one calculation per bar drawn.
- Tests: new `tests/nudge_roll_test.c` (the shift for every NUDGE, the edges, QUANTIZE ON, RATCH, REST); two new renders, `roll_nudge` and `roll_nudge_quantized`.

## 1.0.5: three fixes in SEQ > AUTOMATION

- **Track 5's automation landed on track 1.** A record's address (track * 64 + step, up to 319 with five tracks) was stored in a byte in three places
  (adding a record, a value edit, and the SEQ tools that shift or move steps), so every record of track 5 became a record of track 1 at the same step. Fixed:
  the address is kept whole. **Records of track 5 made before this version are on track 1** (and in the saved projects as such): clear them
  (AUTOMATION > PLAY > OCT+ CLEAR on track 1) and record again.
- **REC (the automation arm) is a track's own.** Turning it on for one track no longer turns it on for all of them: each track has its REC
  (off at power-on and at every project load).
- **REC and RAMP show their new value at once.** The page did not redraw when they changed (only after moving to another row or page): the page's
  signature now includes them.
- Tests: `tests/ramp_test.c` (track 5's records and a recorded turn on it, REC of track 1 / 2 / 5, the page signature on REC and RAMP).

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
