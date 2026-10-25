/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 5-LAB 1.1.0: a chord's notes with lengths of their own. A TIE step's vel low 4 bits = the notes of the NOTE step before it that
 * ended before that TIE (0: all go on, as before): the sequencer (seq.c), the length edit (ui.c note_set_len_k), the roll */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

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
#define PX(x, y) (cv_px[(uint32_t)(y) * cv_w + (uint32_t)(x)] == 0x1FF8u)
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
    bad += masks_fix();
    bad += roll_bars();
    puts(bad ? "notelen tests FAILED" : "notelen tests passed");
    return bad != 0;
}
