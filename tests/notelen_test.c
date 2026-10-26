/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 5-LAB 1.1.0: a chord's notes with lengths of their own. A TIE step's vel low 4 bits = the notes of the NOTE step before it that
 * ended before that TIE (0: all go on, as before): the sequencer (seq.c), the length edit (ui.c note_set_len_k), the roll */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define PX(x, y) (cv_px[(uint32_t)(y) * cv_w + (uint32_t)(x)] == 0x1FF8u)

static int sounding(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        if (t->seq_notes[i] == note)
            return 1;
    return 0;
}
static track_t *chord3(void)
{
    track_t *t;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    motion_clear(t);
    t->p[P_SLEN] = 16; t->p[P_SDIV] = 2; t->p[P_SGATE] = 64; t->p[P_SSWING] = 0; t->p[P_VOICE] = V_POLY;
    song.g[G_SWING] = 0; song.g[G_BPM] = 120;
    t->step[0] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 0};
    t->step[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[2] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[3] = (step_t){{0}, 0, ST_TIE, 0, 0};
    return t;
}
/* the ticks up to sample position `at` (from PLAY) */
static uint32_t pos;
static void run_to(track_t *t, uint32_t at)
{
    while (pos < at) { seq_tick(t, CTL); pos += CTL; }
}
static int seq_lengths(void)
{
    int bad = 0, ok;
    track_t *t = chord3();
    uint32_t ss = div_samples(2), m = 3u * CTL;
    seq_start(); pos = 0;
    run_to(t, ss + m);
    bad += check("notelen: mask 0 on every TIE: all three notes go on over the chain (as always)",
                 sounding(t, 60) && sounding(t, 64) && sounding(t, 67));
    run_to(t, 3u * ss + m);
    ok = sounding(t, 60) && sounding(t, 64) && sounding(t, 67);
    bad += check("  ... to the last TIE", ok);
    run_to(t, 4u * ss + m);
    bad += check("  ... and then all off", t->seq_n == 0);
    seq_stop();

    t = chord3();                                   /* C 4 steps, E 3, G 2 */
    t->step[2].vel = tie_vel(4); t->step[3].vel = tie_vel(6);
    seq_start(); pos = 0;
    run_to(t, ss + m);
    bad += check("  G 2 / E 3 / C 4: all sound in step 2", sounding(t, 60) && sounding(t, 64) && sounding(t, 67));
    run_to(t, 2u * ss + m);
    bad += check("  step 3: G is off, E and C go on", !sounding(t, 67) && sounding(t, 64) && sounding(t, 60));
    run_to(t, 3u * ss + m);
    bad += check("  step 4: E is off too, C goes on", !sounding(t, 67) && !sounding(t, 64) && sounding(t, 60));
    run_to(t, 4u * ss + m);
    bad += check("  step 5: all off", t->seq_n == 0);
    seq_stop();

    t = chord3();                                   /* the lowest note shortest: 1 step */
    t->step[1].vel = tie_vel(1); t->step[2].vel = tie_vel(1); t->step[3].vel = tie_vel(1);
    seq_start(); pos = 0;
    run_to(t, ss + m);
    bad += check("  C 1 step: off in step 2, E and G go on", !sounding(t, 60) && sounding(t, 64) && sounding(t, 67));
    seq_stop();
    return bad;
}

static int lng_has(const track_t *t, uint32_t p)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (t->lng_p[i] == p)
            return 1;
    return 0;
}
/* 1.1.3: a LONG note passes the notes of the steps after it */
static void clear16(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) { memset(&t->step[i], 0, sizeof t->step[i]); t->step[i].time = ST_REST; }
}
static int long_notes(void)
{
    int bad = 0;
    track_t *t = chord3();
    uint32_t ss = div_samples(2), m = 3u * CTL;
    clear16(t);
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    t->step[4] = (step_t){{67}, 1, ST_NOTE, 0, 0};
    step_ext_set(&t->step[0], 0, 8);                 /* F (60): 8 steps, over G on step 5 */
    bad += check("long: the code in the acc byte of a step with no hit", step_ext_code(&t->step[0], 0) == 8 && step_ext_code(&t->step[0], 1) == 0);
    seq_start(); pos = 0;
    run_to(t, ss / 4u);
    bad += check("  step 1: the long note sounds, outside seq_notes", lng_has(t, 60) && !sounding(t, 60));
    run_to(t, 4u * ss + m);
    bad += check("  step 5: G sounds, F goes on", lng_has(t, 60) && sounding(t, 67));
    run_to(t, 6u * ss + m);
    bad += check("  step 7: G is off (its own step), F still on", lng_has(t, 60) && !sounding(t, 67));
    run_to(t, 7u * ss + ss / 4u);
    bad += check("  step 8, first quarter: F still on", lng_has(t, 60));
    run_to(t, 8u * ss + m);
    bad += check("  step 9: F is off", !lng_has(t, 60));
    seq_stop();
    bad += check("  STOP leaves nothing", !lng_has(t, 60) && t->seq_n == 0);

    t = chord3();
    clear16(t);
    t->step[2] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    t->step[9] = (step_t){{67}, 1, ST_NOTE, 0, 0};
    step_ext_set(&t->step[2], 0, EXT_END);
    seq_start(); pos = 0;
    run_to(t, 9u * ss + m);
    bad += check("  END: F from step 3 passes G on step 10", lng_has(t, 60) && sounding(t, 67));
    run_to(t, 15u * ss + ss / 4u);
    bad += check("  END: still on in the last step", lng_has(t, 60));
    run_to(t, 15u * ss + ss * 3u / 4u);
    bad += check("  END: off at the gate of the last step of the pattern", !lng_has(t, 60));
    seq_stop();

    t = chord3();                                    /* a chord: every note its own length, over the note on step 10 */
    clear16(t);
    t->step[0] = (step_t){{60, 64, 67, 71}, 4, ST_NOTE, 0, 0};
    t->step[9] = (step_t){{72}, 1, ST_NOTE, 0, 0};
    step_ext_set(&t->step[0], 0, 4); step_ext_set(&t->step[0], 1, 8); step_ext_set(&t->step[0], 2, EXT_END); step_ext_set(&t->step[0], 3, 12);
    bad += check("  four notes: four codes (acc and ext)", step_ext_code(&t->step[0], 0) == 4 && step_ext_code(&t->step[0], 1) == 8 &&
                 step_ext_code(&t->step[0], 2) == EXT_END && step_ext_code(&t->step[0], 3) == 12);
    seq_start(); pos = 0;
    run_to(t, 3u * ss + m);
    bad += check("  step 4: all four on", lng_has(t, 60) && lng_has(t, 64) && lng_has(t, 67) && lng_has(t, 71));
    run_to(t, 5u * ss + m);
    bad += check("  step 6: the 4-step note is off, the others on", !lng_has(t, 60) && lng_has(t, 64) && lng_has(t, 67) && lng_has(t, 71));
    run_to(t, 9u * ss + m);
    bad += check("  step 10: the 8-step is off; the note there plays over the 12 and the END", !lng_has(t, 64) && lng_has(t, 67) && lng_has(t, 71) && sounding(t, 72));
    run_to(t, 12u * ss + m);
    bad += check("  step 13: the 12-step is off, the END note goes on", !lng_has(t, 71) && lng_has(t, 67));
    seq_stop();

    t = chord3();                                    /* a chord with the long note second: the others are normal */
    clear16(t);
    t->step[0] = (step_t){{60, 64}, 2, ST_NOTE, 0, 0};
    step_ext_set(&t->step[0], 1, 4);
    seq_start(); pos = 0;
    run_to(t, ss * 2u + m);
    bad += check("  a chord: the long E goes on, C ended with its step", lng_has(t, 64) && !sounding(t, 60) && !sounding(t, 64));
    run_to(t, 4u * ss + m);
    bad += check("  .. until its 4 steps are over", !lng_has(t, 64));
    seq_stop();
    t->step[0].hit = 1;
    bad += check("  a step with a hit has no long note (the acc byte is the drum accents)", step_ext_code(&t->step[0], 1) == 0);
    return bad;
}
/* 1.1.3: live recording: each key lasts as long as it was held, a chord grows key by key and lets go together (or apart) */
static void rec_at(track_t *t, uint32_t step, uint32_t half)
{
    uint32_t sl = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), step);
    t->seq_idx = (uint16_t)step;
    t->seq_pos = half ? sl * 3u / 4u : 0u;
}
static int rec_long(void)
{
    int bad = 0, ok, s;
    track_t *t = chord3();
    clear16(t);
    song.rec = 1; song.playing = 1;
    rec_at(t, 0, 0); rec_note(t, 60, 100);
    for (s = 1; s < 8; s++) {
        rec_at(t, (uint32_t)s, 0);
        rec_hold(t, (uint32_t)s, 16);
        if (s == 2) rec_note(t, 64, 100);
        if (s == 4) rec_note(t, 67, 100);
    }
    rec_at(t, 8, 0); rec_hold(t, 8, 16);
    rec_release(t, 60); rec_release(t, 64); rec_release(t, 67);
    song.rec = 0; song.playing = 0;
    ok = t->step[0].note[0] == 60 && step_ext_code(&t->step[0], 0) == 8;
    bad += check("rec: C pressed on step 1 and held to the start of step 9: its LONG note is 8 steps", ok);
    ok = t->step[2].time == ST_NOTE && t->step[2].note[0] == 64 && step_ext_code(&t->step[2], 0) == 6;
    bad += check("  E pressed on step 3 (over C): 6 steps", ok);
    ok = t->step[4].note[0] == 67 && !step_ext_any(&t->step[4]) && t->step[5].time == ST_TIE && t->step[7].time == ST_TIE && note_len(t, 4) == 4;
    bad += check("  G pressed on step 5 and let go with the others: TIEs to step 8, no LONG note", ok);
    ok = t->step[1].time == ST_TIE && note_len_eff(t, 0, 0) == 8 && note_len_eff(t, 2, 0) == 6;
    bad += check("  the length of each note as the UI reads it", ok);

    clear16(t);
    song.rec = 1; song.playing = 1;                  /* a chord of three held; another note on step 3: all three go on, each its own length */
    rec_at(t, 0, 0); rec_note(t, 60, 100); rec_note(t, 64, 100); rec_note(t, 67, 100);
    rec_at(t, 1, 0); rec_hold(t, 1, 16);
    rec_at(t, 2, 0); rec_hold(t, 2, 16); rec_note(t, 62, 100);
    for (s = 3; s < 6; s++) { rec_at(t, (uint32_t)s, 0); rec_hold(t, (uint32_t)s, 16); }
    rec_release(t, 67);                               /* at the start of step 6: G 5 steps */
    for (s = 6; s < 8; s++) { rec_at(t, (uint32_t)s, 0); rec_hold(t, (uint32_t)s, 16); }
    rec_release(t, 64);                               /* E 7 */
    rec_at(t, 8, 0); rec_hold(t, 8, 16);
    rec_release(t, 60); rec_release(t, 62);           /* C 8; D (step 3) 6 */
    song.rec = 0; song.playing = 0;
    ok = step_ext_code(&t->step[0], 0) == 8 && step_ext_code(&t->step[0], 1) == 7 && step_ext_code(&t->step[0], 2) == 5 && t->step[2].note[0] == 62;
    bad += check("rec: a chord held while another key comes in: each of its notes its own LONG length", ok);

    clear16(t);
    song.rec = 1; song.playing = 1;                  /* a chord of three, let go one by one */
    rec_at(t, 0, 0); rec_note(t, 60, 100); rec_note(t, 64, 100); rec_note(t, 67, 100);
    rec_at(t, 1, 0); rec_hold(t, 1, 16);
    rec_at(t, 2, 0); rec_hold(t, 2, 16); rec_release(t, 67);          /* G let go at the start of step 3: 2 steps */
    rec_at(t, 3, 0); rec_hold(t, 3, 16);
    rec_at(t, 4, 0); rec_hold(t, 4, 16); rec_release(t, 64);          /* E at the start of step 5: 4 steps */
    rec_at(t, 5, 0); rec_hold(t, 5, 16);
    rec_at(t, 6, 0); rec_hold(t, 6, 16); rec_release(t, 60);          /* C at the start of step 7: 6 steps */
    song.rec = 0; song.playing = 0;
    ok = note_len_k(t, 0, 2) == 2 && note_len_k(t, 0, 1) == 4 && note_len_k(t, 0, 0) == 6 && note_len(t, 0) == 6;
    bad += check("rec: a chord let go key by key: each note as long as it was held (the TIEs' masks)", ok);
    return bad;
}
static int long_edit(void)
{
    int bad = 0, ok;
    track_t *t = chord3();
    const char *m;
    uint32_t i, n;
    clear16(t);
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    t->step[7] = (step_t){{67}, 1, ST_NOTE, 0, 0};
    for (i = 0; i < 6u; i++) n = note_edit_k(t, 0, 0, 1, &m);
    ok = n == 7 && !step_ext_any(&t->step[0]) && t->step[6].time == ST_TIE && !m;
    bad += check("long edit: KNOB 3 on a note grows its TIEs up to the next note (7 steps)", ok);
    n = note_edit_k(t, 0, 0, 1, &m);
    ok = n == 8 && step_ext_code(&t->step[0], 0) == 8 && t->step[7].note[0] == 67 && !m;
    bad += check("  one more: a LONG note, over G (8 steps)", ok);
    for (i = 0; i < 6u; i++) n = note_edit_k(t, 0, 0, 1, &m);
    ok = n == 14 && step_ext_code(&t->step[0], 0) == 14;
    bad += check("  on to 14", ok);
    n = note_edit_k(t, 0, 0, 1, &m);
    ok = n == 16 && step_ext_code(&t->step[0], 0) == EXT_END;
    bad += check("  past 14: END, the end of the pattern", ok);
    n = note_edit_k(t, 0, 0, 1, &m);
    ok = n == 16 && m && !strcmp(m, "END OF PATTERN");
    bad += check("  and 'END OF PATTERN' past it", ok);
    n = note_edit_k(t, 0, 0, -1, &m);
    ok = n == 14 && step_ext_code(&t->step[0], 0) == 14;
    bad += check("  one down from END: 14", ok);
    for (i = 0; i < 13u; i++) n = note_edit_k(t, 0, 0, -1, &m);
    ok = n == 1 && step_ext_code(&t->step[0], 0) == 1;
    bad += check("  down to 1", ok);
    n = note_edit_k(t, 0, 0, -1, &m);
    ok = !step_ext_any(&t->step[0]) && m && !strcmp(m, "LONG NOTE OFF");
    bad += check("  below 1: the LONG note is off (back to its TIEs)", ok);

    /* a chord of three and a note on step 10: each note goes to the end on its own, held or not */
    clear16(t);
    t->step[0] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 0};
    t->step[9] = (step_t){{72}, 1, ST_NOTE, 0, 0};
    for (i = 0; i < 15u; i++) n = note_edit_k(t, 0, 1, 1, &m);
    ok = n == 16 && step_ext_code(&t->step[0], 1) == EXT_END && !step_ext_code(&t->step[0], 0);
    bad += check("long edit: the second note of a chord goes on to the end", ok);
    for (i = 0; i < 15u; i++) n = note_edit_k(t, 0, 0, 1, &m);
    ok = n == 16 && step_ext_code(&t->step[0], 0) == EXT_END && step_ext_code(&t->step[0], 1) == EXT_END;
    bad += check("  .. and the first too (not one note a step)", ok);
    for (i = 0; i < 15u; i++) n = note_edit_k(t, 0, 2, 1, &m);
    ok = n == 16 && step_ext_code(&t->step[0], 2) == EXT_END && t->step[9].note[0] == 72;
    bad += check("  .. and the third", ok);

    /* no key held: the whole chord, by the turn */
    clear16(t);
    t->step[0] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 0};
    t->step[9] = (step_t){{72}, 1, ST_NOTE, 0, 0};
    for (i = 0; i < 11u; i++) n = note_edit_all(t, 0, 1, &m);
    ok = n == 12 && note_len_eff(t, 0, 0) == 12 && note_len_eff(t, 0, 1) == 12 && note_len_eff(t, 0, 2) == 12 &&
         step_ext_code(&t->step[0], 0) == 12 && step_ext_code(&t->step[0], 2) == 12;
    bad += check("  the whole chord (no key): every note, past the note on step 10", ok);
    note_edit_k(t, 0, 1, -5, &m);                    /* E shorter: the chord keeps the difference */
    n = note_edit_all(t, 0, 1, &m);
    ok = note_len_eff(t, 0, 0) == 13 && note_len_eff(t, 0, 1) == 8 && note_len_eff(t, 0, 2) == 13 && n == 13;
    bad += check("  .. each keeping its difference", ok);
    n = note_edit_all(t, 0, -20, &m);
    ok = note_len_eff(t, 0, 0) == 1 && note_len_eff(t, 0, 1) == 1 && note_len_eff(t, 0, 2) == 1;
    bad += check("  .. down to 1 each", ok);

    /* taking a note out of the chord keeps the LONG notes with their notes */
    t->step[0] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 0};
    step_ext_set(&t->step[0], 1, 5); step_ext_set(&t->step[0], 2, 9);
    tie_masks_fix(t, 0, 0);
    ok = step_ext_code(&t->step[0], 0) == 5 && step_ext_code(&t->step[0], 1) == 9 && step_ext_code(&t->step[0], 2) == 0;
    bad += check("  a note out of the chord before them: the LONG notes' indices follow", ok);
    tie_masks_fix(t, 0, 4);
    ok = !step_ext_any(&t->step[0]);
    bad += check("  a fresh chord: none", ok);
    return bad;
}
static int long_absorb(void)
{
    int bad = 0, ok;
    track_t *t = chord3();
    const char *m;
    uint32_t i;
    clear16(t);
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    t->step[4] = (step_t){{60}, 1, ST_NOTE, 0, 0};          /* the same note again */
    t->step[6] = (step_t){{60, 64}, 2, ST_NOTE, 0, 0};      /* .. in a chord */
    t->step[7] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[9] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    for (i = 0; i < 5u; i++) note_edit_k(t, 0, 0, 1, &m);   /* 6 steps: reaches step 5 (index 4) */
    ok = t->step[4].time == ST_REST && !t->step[4].n && t->step[6].n == 2;
    bad += check("absorb: stretched to the same note on step 5: that step is a REST, not a second C under it", ok);
    for (i = 0; i < 2u; i++) note_edit_k(t, 0, 0, 1, &m);   /* 8 steps: reaches the chord on step 7 */
    ok = t->step[6].n == 1 && t->step[6].note[0] == 64 && t->step[6].time == ST_NOTE && step_ext_code(&t->step[0], 0) == 8;
    bad += check("  .. in a chord: only the C goes, the E stays", ok);
    for (i = 0; i < 4u; i++) note_edit_k(t, 0, 0, 1, &m);
    ok = t->step[9].time == ST_REST;
    bad += check("  .. and further steps", ok);
    t->step[2] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    t->step[3] = (step_t){{0}, 0, ST_TIE, 0, 0};
    note_absorb(t, 0, 0, 5);
    ok = t->step[2].time == ST_REST && t->step[3].time == ST_REST;
    bad += check("  a step left empty takes its TIEs with it", ok);
    return bad;
}
static int long_roll(void)
{
    int bad = 0;
    track_t *t = chord3();
    clear16(t);
    t->step[0] = (step_t){{60, 64}, 2, ST_NOTE, 0, 0};
    t->step[7] = (step_t){{67}, 1, ST_NOTE, 0, 0};
    step_ext_set(&t->step[0], 0, 11);
    step_ext_set(&t->step[0], 1, 3);
    song.sel = 0; ui.cursor = 15; ui.bank = 0;
    proll.lo = 58;
    cv_begin(240, 124, T_BG);
    graph_roll(t, 0xF81Fu);
    {
        int32_t yc = pr_row_y(60) + 2, ye = pr_row_y(64) + 2, yg = pr_row_y(67) + 2;
        #define C2(i) (PR_X0 + (i) * PR_CW + 6)
        bad += check("long roll: the bar of C from step 1 to step 11, unbroken over G's step", PX(C2(0), yc) && PX(C2(6), yc) && PX(C2(7), yc) && PX(C2(10), yc) &&
                     PX(C2(0) + 5, yc) && PX(PR_X0 + 7 * PR_CW + 1, yc) && !PX(C2(11), yc));
        bad += check("  E: its own, 3 steps", PX(C2(0), ye) && PX(C2(2), ye) && !PX(C2(3), ye));
        bad += check("  G is drawn on its own row in step 8", PX(C2(7), yg) && !PX(C2(8), yg));
    }
    step_ext_set(&t->step[0], 0, 0);
    cv_begin(240, 124, T_BG);
    graph_roll(t, 0xF81Fu);
    bad += check("  without the LONG note C is one step", PX(C2(0), pr_row_y(60) + 2) && !PX(C2(1), pr_row_y(60) + 2));
    return bad;
}
static int long_project(void)
{
    int bad = 0;
    track_t *t = chord3();
    clear16(t);
    t->step[3] = (step_t){{60, 64, 67, 71}, 4, ST_NOTE, 0, 0};
    step_ext_set(&t->step[3], 1, 9); step_ext_set(&t->step[3], 2, EXT_END); step_ext_set(&t->step[3], 3, 6);
    t->step[5] = (step_t){{0}, 0, ST_NOTE, 0, 0};
    t->step[5].hit = 1; t->step[5].acc = 1;
    project_save(0);
    t->step[3] = (step_t){{0}, 0, ST_REST, 0, 0};
    t->step[5] = t->step[3];
    project_load(0);
    t = &trk[0];
    bad += check("long project: LONG notes are saved and loaded with their step", step_ext_code(&t->step[3], 1) == 9 && step_ext_code(&t->step[3], 2) == EXT_END &&
                 step_ext_code(&t->step[3], 3) == 6 && !step_ext_code(&t->step[3], 0) && t->step[3].note[3] == 71);
    bad += check("  a drum hit's accent too", t->step[5].hit == 1 && t->step[5].acc == 1);
    return bad;
}
static int len_edit(void)
{
    int bad = 0, ok;
    track_t *t = chord3();
    ok = note_len(t, 0) == 4 && note_len_k(t, 0, 0) == 4 && note_len_k(t, 0, 2) == 4;
    bad += check("notelen: the whole chord 4 steps; every note 4 (mask 0)", ok);
    ok = note_set_len_k(t, 0, 2, 2) == 2 && tie_mask(&t->step[1]) == 0 && tie_mask(&t->step[2]) == 4 && tie_mask(&t->step[3]) == 4 &&
         note_len(t, 0) == 4 && note_len_k(t, 0, 0) == 4 && note_len_k(t, 0, 1) == 4;
    bad += check("  note 3 shortened to 2: the TIEs from the third carry its bit, the others 4", ok);
    ok = note_set_len_k(t, 0, 1, 3) == 3 && tie_mask(&t->step[3]) == 6 && tie_mask(&t->step[2]) == 4 && note_len(t, 0) == 4;
    bad += check("  note 2 to 3", ok);
    ok = note_set_len_k(t, 0, 0, 1) == 1 && note_len(t, 0) == 3 && t->step[3].time == ST_REST;
    bad += check("  note 1 to 1: the chain shrinks to the longest (3): the last TIE a REST", ok);
    ok = note_set_len_k(t, 0, 1, 1) == 1 && note_len(t, 0) == 2 && note_len_k(t, 0, 2) == 2;
    bad += check("  note 2 to 1: the chain is the third's 2", ok);
    ok = note_set_len_k(t, 0, 0, 5) == 5 && note_len(t, 0) == 5 && note_len_k(t, 0, 1) == 1 && note_len_k(t, 0, 2) == 2 &&
         t->step[4].time == ST_TIE && (tie_mask(&t->step[4]) & 7u) == 6u;
    bad += check("  note 1 to 5: new TIEs, the others ended on them", ok);
    ok = note_set_len(t, 0, 7) == 7 && (tie_mask(&t->step[6]) & 7u) == 6u && note_len_k(t, 0, 0) == 7 && note_len_k(t, 0, 2) == 2;
    bad += check("  the whole chord to 7: the new TIEs end the notes the last one ended", ok);
    ok = note_set_len(t, 0, 1) == 1 && t->step[1].time == ST_REST && t->step[1].vel == 0;
    bad += check("  the whole chord to 1: clean RESTs", ok);
    return bad;
}
static int masks_fix(void)
{
    int bad = 0;
    track_t *t = chord3();
    t->step[1].vel = tie_vel(4); t->step[2].vel = tie_vel(6); t->step[3].vel = tie_vel(7);
    tie_masks_fix(t, 0, 1);                         /* note 2 taken out: bits above it shift down */
    bad += check("notelen: a note out of the chord: the masks lose its bit", tie_mask(&t->step[1]) == 2 && tie_mask(&t->step[2]) == 2 && tie_mask(&t->step[3]) == 3);
    tie_masks_fix(t, 0, 4);
    t->step[1].vel = 96; t->step[2].vel = 71;
    bad += check("  a TIE with any other vel (96, 71: a project of before) holds every note", !tie_mask(&t->step[1]) && !tie_mask(&t->step[2]));
    bad += check("  a fresh chord: masks cleared", !tie_mask(&t->step[1]) && !tie_mask(&t->step[2]) && !tie_mask(&t->step[3]));
    return bad;
}
static int roll_bars(void)
{
    int bad = 0;
    track_t *t = chord3();
    cv_begin(240, 124, T_BG);
    t->step[2].vel = tie_vel(4); t->step[3].vel = tie_vel(6);
    song.sel = 0; ui.cursor = 15;
    proll.lo = 58;
    graph_roll(t, 0xF81Fu);
    {   /* the row of 67 (G): bars in the columns of steps 1 and 2, none in 3 and 4; E (64): up to step 3 */
        int32_t yg = pr_row_y(67) + 2, ye = pr_row_y(64) + 2, yc = pr_row_y(60) + 2;
        int32_t x;
        #define COL(i) (PR_X0 + (i) * PR_CW + 6)
        bad += check("notelen: roll: G drawn in steps 1-2 only", PX(COL(0), yg) && PX(COL(1), yg) &&
                     !PX(COL(2), yg) && !PX(COL(3), yg));
        bad += check("  E in steps 1-3", PX(COL(2), ye) && !PX(COL(3), ye));
        bad += check("  C in steps 1-4", PX(COL(3), yc) && !PX(COL(4), yc));
        x = PR_X0 + 2 * PR_CW + 2;                  /* G's last bar ends with its column: nothing in step 3's gap */
        bad += check("  G: no bar over step 3's start", !PX(x, yg));
    }
    return bad;
}
int main(void)
{
    int bad = 0;
    bad += seq_lengths();
    bad += len_edit();
    bad += long_notes();
    bad += rec_long();
    bad += long_edit();
    bad += long_absorb();
    bad += long_roll();
    bad += long_project();
    bad += masks_fix();
    bad += roll_bars();
    puts(bad ? "notelen tests FAILED" : "notelen tests passed");
    return bad != 0;
}
