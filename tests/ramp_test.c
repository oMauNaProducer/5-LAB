/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 5-LAB 1.0.3: RAMP. Real sequencer / project / UI sources: an automation EVENT holds its value until the next (HOLD, as Felucca)
 * or, in RAMP, the value moves in a straight line to the next event of the same parameter, block by block; the locks are
 * not touched; the setting is saved with the project (FUN10 for tracks 1..4, PROJ5 for the fifth) and zero (HOLD) before. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define RS 16u                                       /* steps played in the runs */
/* plays one pass of track t (LEN 16) block by block; v[s][0] is the value at the first block of step s, v[s][1] the last one,
 * nd[s] how many distinct values the step went through */
static int16_t rv0[RS], rv1[RS];
static uint32_t rnd[RS];
static int16_t rmin, rmax;
static void ramp_run(track_t *t, uint32_t id)
{
    uint32_t s, blocks = 0, last = 0xFFFFu;
    int16_t prev = 0x7FFF;
    memset(rv0, 0, sizeof rv0); memset(rv1, 0, sizeof rv1); memset(rnd, 0, sizeof rnd);
    rmin = 0x7FFF; rmax = -0x7FFF;
    seq_start();
    while (blocks++ < 400000u) {
        seq_tick(t, CTL);
        s = t->seq_idx;
        if (s >= RS || (t->seq_idx == 0 && last == RS - 1u && blocks > 10u))
            break;
        if (s != last) { rv0[s] = t->p[id]; prev = 0x7FFF; last = s; }
        rv1[s] = t->p[id];
        if (t->p[id] != prev) { rnd[s]++; prev = t->p[id]; }
        if (t->p[id] < rmin) rmin = t->p[id];
        if (t->p[id] > rmax) rmax = t->p[id];
    }
    seq_stop();
}
static track_t *ramp_track(uint32_t k)
{
    track_t *t;
    ui_power_on();
    song.sel = (uint8_t)k;
    t = &trk[k];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    motion_clear(t);
    motion_set_ramp(t, 0);
    return t;
}
static int hold_and_ramp(void)
{
    int bad = 0, ok;
    uint32_t s;
    track_t *t = ramp_track(0);
    t->p[P_REV] = 5;
    motion_set_event(t, 0, P_REV, 0);
    motion_set_event(t, 15, P_REV, 100);
    bad += check("RAMP is OFF (HOLD) by default and the pool has no ramp bit", !motion_ramping(t) && motion.rsv[0] == 0u);
    ramp_run(t, P_REV);
    for (s = 1, ok = 1; s < 15u; s++)
        ok &= rv0[s] == 0 && rv1[s] == 0;
    bad += check("HOLD: step 1 = 0 held through step 15; the 100 only on the last step", ok && rv0[0] == 0 && rv1[15] == 100);
    motion_set_ramp(t, 1);
    bad += check("RAMP ON: a bit of the track, saved in rsv[0]", motion_ramping(t) && motion.rsv[0] == 1u && !motion_ramping(&trk[1]));
    ramp_run(t, P_REV);
    ok = rv0[0] == 0 && rv0[15] == 100;
    for (s = 1; s < 15u; s++)
        ok &= rv0[s] >= rv1[s - 1u] - 1 && rv1[s] >= rv0[s];                         /* (never down) */
    bad += check("RAMP: from 0 on step 1 up to 100 on step 16, never down", ok && rmin == 0 && rmax == 100);
    bad += check("  the middle of the pass is the middle of the line (step 8 starts at 7/15 of the way: 47)",
                 rv0[7] >= 46 && rv0[7] <= 48 && rv0[3] >= 19 && rv0[3] <= 21 && rv0[11] >= 72 && rv0[11] <= 74);
    ok = 1;
    for (s = 1; s < 15u; s++)
        ok &= rnd[s] > 1u;                                                            /* (more than one value inside a step: smooth) */
    bad += check("  smooth inside a step too (more than one value per step, not a staircase)", ok);
    motion_set_event(t, 0, P_REV, 100);
    motion_set_event(t, 15, P_REV, 0);
    ramp_run(t, P_REV);
    ok = rv0[0] == 100 && rv0[15] == 0 && rmin == 0 && rmax == 100 && rv0[7] >= 52 && rv0[7] <= 54;
    for (s = 1; s < 15u; s++)
        ok &= rv1[s] <= rv0[s];
    bad += check("  and down: 100 -> 0, never up (rounded to the nearest)", ok);
    return bad;
}
static int ramp_range(void)
{
    int bad = 0, ok;
    track_t *t = ramp_track(1);
    motion_set_event(t, 2, P_PAN, -40);
    motion_set_event(t, 10, P_PAN, 40);
    motion_set_event(t, 14, P_PAN, -10);
    motion_set_ramp(t, 1);
    ramp_run(t, P_PAN);
    ok = rv0[2] == -40 && rv0[10] == 40 && rv0[14] == -10 && rv0[6] >= -1 && rv0[6] <= 1 && rv0[12] >= 14 && rv0[12] <= 16 &&
         rv0[0] != -40 && rv0[1] != -40;
    bad += check("RAMP: three events (-40, 40, -10) through zero: the line of each pair, before the first one nothing", ok);
    ok = rmin >= -64 && rmax <= 63;
    bad += check("  inside the parameter's own range", ok);
    t->p[P_REV] = 33;
    motion_set_event(t, 3, P_REV, 20);
    ramp_run(t, P_REV);
    bad += check("  one event of a parameter: nothing to ramp to, it holds (as HOLD)", rv0[3] == 20 && rv1[15] == 20 && rv0[2] == 33);
    return bad;
}
static int ramp_lock_and_off(void)
{
    int bad = 0;
    track_t *t = ramp_track(0);
    t->p[P_REV] = 5;
    motion_set_event(t, 0, P_REV, 0);
    motion_set_event(t, 15, P_REV, 100);
    motion_set_lock(t, 4, P_REV, 77);
    motion_set_ramp(t, 1);
    ramp_run(t, P_REV);
    bad += check("RAMP: a lock sounds on its step as it is (77) and the line goes on after it",
                 rv0[4] == 77 && rv1[4] == 77 && rv0[5] > 30 && rv0[5] < 40 && rv0[3] > 15 && rv0[3] < 25);
    motion_set_ramp(t, 0);
    ramp_run(t, P_REV);
    bad += check("  RAMP OFF again: the events hold (the lock too)", rv0[3] == 0 && rv0[5] == 0 && rv0[4] == 77 && rv1[14] == 0);
    motion_set_ramp(t, 1);
    motion_set_enabled(t, 0);
    t->p[P_REV] = 5;
    ramp_run(t, P_REV);
    bad += check("  PLAY OFF: the automation does not play, ramp or not (the sound's own 5)", rmin == 5 && rmax == 5);
    motion_set_enabled(t, 1);
    ramp_run(t, P_REV);
    bad += check("  stopped: the sound's own value is back (5)", t->p[P_REV] == 5);
    motion_set_ramp(t, 1);
    motion_clear(t);
    bad += check("  CLEAR keeps the setting (as QUANTIZE): the track is still in RAMP", motion_ramping(t) && motion_count(t) == 0u);
    return bad;
}
/* the knob turned while REC and PLAY run and the automation REC row is on: mode 0 a line from `a` (step 0) to `b` (step 11),
 * 1 up to b at step 5 and down to a at step 11, 2 up a to b over steps 0..3, still 4..6, up again b to c over 7..10 */
static uint32_t rec_pts(uint32_t id, uint32_t mode, int32_t a, int32_t b, int32_t c, uint32_t *s0, int16_t *v0, uint32_t max)
{
    track_t *t = &trk[0];
    uint32_t blocks = 0, i, n = 0, last = 0xFFFFu;
    seq_start();
    while (blocks++ < 400000u) {
        uint32_t cur, step;
        int32_t v;
        seq_tick(t, CTL);
        step = t->seq_idx;
        cur = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), step);
        if (step >= 12u) break;
        if (mode == 2u && step >= 4u && step <= 6u) continue;
        {   int32_t pos = (int32_t)step * 256 + (int32_t)((uint64_t)t->seq_pos * 256u / cur);       /* (256ths of a step) */
            if (mode == 0u) v = a + (b - a) * pos / (11 * 256);
            else if (mode == 1u) v = pos <= 5 * 256 ? a + (b - a) * pos / (5 * 256) : b + (a - b) * (pos - 5 * 256) / (6 * 256);
            else v = pos <= 3 * 256 ? a + (b - a) * pos / (3 * 256) : pos <= 7 * 256 ? b : b + (c - b) * (pos - 7 * 256) / (3 * 256);
        }
        t->p[id] = (int16_t)v;
        motion_capture(t, id, (int16_t)v);
        last = step;
    }
    seq_stop();
    for (i = 0; i < motion.count && n < max; i++)
        if ((motion.event[i].place >> 6) == 0u && MOTION_ID(&motion.event[i]) == id && !(motion.event[i].param & MOTION_LOCK)) {
            s0[n] = motion.event[i].place & 63u; v0[n] = motion.event[i].value; n++;
        }
    (void)last;
#ifdef RAMP_DBG
    printf("pts(%u):", n); for (i = 0; i < n; i++) printf(" %u:%d", s0[i], v0[i]); printf("\n");
#endif
    return n;
}
static int ramp_record(void)
{
    int bad = 0;
    uint32_t s[16], n, i, ok;
    int16_t v[16];
    track_t *t = ramp_track(0);
    song.rec = 1; motion_arm = 1;
    motion_set_ramp(t, 1);
    n = rec_pts(P_REV, 0u, 20, 70, 0, s, v, 16);
    bad += check("RAMP recording: a knob turned in a line over 12 steps leaves two records, the start and the end", n == 2u &&
                 s[0] <= 1u && s[1] >= 10u && v[0] >= 18 && v[0] <= 26 && v[1] >= 64 && v[1] <= 76);
    ramp_run(t, P_REV);
    bad += check("  and it plays back as the line", rv0[6] >= 40 && rv0[6] <= 48 && rv0[2] > 20 && rv0[2] < rv0[6]);
    motion_clear(t); motion_set_ramp(t, 1); song.rec = 1;
    n = rec_pts(P_REV, 1u, 20, 90, 0, s, v, 16);
    ok = n >= 3u && n <= 4u && v[n - 1u] <= 30;
    for (i = 1, rmax = 0; i + 1u < n; i++)
        rmax = v[i] > rmax ? v[i] : rmax;
    bad += check("  up and down: the peak is a corner (3 records; 4 when it falls between two steps, one on each side), not one a step",
                 ok && rmax >= 80);
    motion_clear(t); motion_set_ramp(t, 1); song.rec = 1;
    n = rec_pts(P_REV, 2u, 20, 50, 80, s, v, 16);
    ok = n >= 4u && n <= 5u;
    bad += check("  up, a pause, up again: the corners only (4 or 5 records, not one a step)", ok);
    motion_clear(t); motion_set_ramp(t, 0); song.rec = 1;
    n = rec_pts(P_REV, 0u, 20, 70, 0, s, v, 16);
    bad += check("  in HOLD every step the knob passed is a record, as before", n >= 10u);
    motion_clear(t); motion_set_ramp(t, 1); song.rec = 1;
    motion_set_event(t, 3, P_REV, 99); motion_set_event(t, 6, P_REV, 3); motion_set_event(t, 8, P_REV, 77);
    motion_set_event(t, 14, P_REV, 55); motion_set_lock(t, 5, P_DLY, 12);
    n = rec_pts(P_REV, 0u, 20, 70, 0, s, v, 16);
    ok = 1;
    for (i = 0; i < n; i++)
        ok &= !(s[i] >= 2u && s[i] <= 9u && v[i] != 20 && (v[i] == 99 || v[i] == 3 || v[i] == 77));
    bad += check("  over an older recording: its records inside the new line are dropped, the one after (step 15) and a lock stay",
                 ok && n >= 2u && n <= 4u && motion_lock_count(t) == 1u && motion_find_ev(0, 14, P_REV) >= 0);
    motion_set_ramp(t, 0); motion_clear(t); song.rec = 0; motion_arm = 0;
    return bad;
}
static int ramp_project(void)
{
    int bad = 0;
    project_t before, after;
    project_store_t a;
    proj5_store_t h;
    uint32_t ok;
    track_t *t = ramp_track(0);
    motion_set_event(t, 0, P_REV, 0);
    motion_set_event(t, 15, P_REV, 100);
    motion_set_ramp(&trk[0], 1);
    motion_set_ramp(&trk[2], 1);
    motion_set_ramp(&trk[4], 1);
    motion_set_event(&trk[4], 1, P_REV, 9);
    project_capture(&before);
    ok = proj_pack_pair(&a, &h, &before) && proj_import5(&after, &a, sizeof a, &h) && !memcmp(&before, &after, sizeof before);
    bad += check("FUN10 + PROJ5 round trip keeps RAMP of tracks 1, 3 and 5 (bits 0, 2, 4 of rsv[0]) and no other",
                 ok && after.motion.rsv[0] == 0x15u);
    project_save(2);
    motion_set_ramp(&trk[0], 0); motion_set_ramp(&trk[2], 0); motion_set_ramp(&trk[4], 0);
    project_load(2);
    bad += check("  saved to flash and loaded: the three tracks are in RAMP again", motion_ramping(&trk[0]) && !motion_ramping(&trk[1]) &&
                 motion_ramping(&trk[2]) && !motion_ramping(&trk[3]) && motion_ramping(&trk[4]));
    {   /* a project of 1.0.2 or before: the byte was reserved, zero: HOLD; the fifth track's flag byte 0 or 1 */
        project_t old;
        memcpy(&old, &before, sizeof old);
        old.motion.rsv[0] = 0;
        ok = proj_pack_pair(&a, &h, &old) && proj_import5(&after, &a, sizeof a, &h) && after.motion.rsv[0] == 0u;
        bad += check("  a project saved without it (zero byte): every track in HOLD", ok);
        old.motion.rsv[0] = 0x20u;
        bad += check("  a ramp bit of a sixth track is not a valid store", !motion_valid(&old.motion));
        old.motion.rsv[0] = 0; old.motion.rsv[1] = 1;
        bad += check("  nor a byte after it", !motion_valid(&old.motion));
    }
    return bad;
}
static int ramp_ui(void)
{
    int bad = 0;
    uint16_t rw[EV_ROWS];
    track_t *t = ramp_track(0);
    go_page(GR_EVENTS); frame();
    ui.ev_row = 0;
    (void)ev_list(rw);
    bad += check("AUTOMATION: the row after QUANTIZE is RAMP", rw[3] == EVC(EVK_RMP, 0) && auto_pick(EVC(EVK_RMP, 0)));
    turn(EN_K4, 1);
    bad += check("  KNOB 4 right: ON (the track's bit), left: OFF", motion_ramping(t));
    turn(EN_K4, -1);
    bad += check("  and back", !motion_ramping(t));
    press(B_OCTUP); press(B_EDIT);
    bad += check("  OCT+ and EDIT do nothing there", !motion_ramping(t) && !act_ready() && str_eq(act_name(4), "--"));
    turn(EN_K4, 1); song.sel = 1;
    bad += check("  another track has its own RAMP (OFF)", !motion_ramping(&trk[1]) && motion_ramping(t));
    return bad;
}
int main(void)
{
    int bad = hold_and_ramp() + ramp_range() + ramp_lock_and_off() + ramp_record() + ramp_project() + ramp_ui();
    printf("%s\n", bad ? "RAMP TEST FAILED" : "ramp tests passed"); return bad != 0;
}
