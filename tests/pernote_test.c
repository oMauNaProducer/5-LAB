/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 5-LAB 1.1.5: a note's own VEL and NUDGE (core.h step_t nv / nd): the accessors and the note that leaves, the sequencer (the
 * note's VEL; the note starts after its own NUDGE, QUANTIZE OFF only), live recording (the notes after a step's first), the saved
 * project (PROJ5 3400 bytes; older blocks load with none) and the DETAIL knobs with a note's key held. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define CHK(name, cond) bad += check(name, (cond))

static track_t *loop2(void)
{
    track_t *t;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    motion_clear(t);
    t->p[P_SLEN] = 2; t->p[P_SDIV] = 2; t->p[P_SGATE] = 64; t->p[P_SSWING] = 0; t->p[P_VOICE] = V_POLY; t->p[P_SQNT] = 0;
    song.g[G_SWING] = 0; song.g[G_BPM] = 120;
    t->step[0] = (step_t){{60, 64}, 2, ST_NOTE, 0, 100};
    t->step[1] = (step_t){{0}, 0, ST_REST, 0, 0};
    return t;
}
static uint32_t period(const track_t *t) { return div_samples((uint32_t)t->p[P_SDIV]); }
/* note-ons over `blocks` blocks: the block of each (at[]), the pitch (nt[]) */
static uint32_t run(track_t *t, uint32_t blocks, uint32_t *at, uint32_t *nt, uint32_t nat)
{
    uint32_t b, n = 0, v0, h;
    for (b = 0; b < blocks; b++) {
        v0 = vage;
        seq_tick(t, CTL);
        for (; v0 < vage; v0++, n++)
            if (n < nat) {
                at[n] = b * CTL;
                nt[n] = 0;
                for (h = 0; h < NVOICE; h++)
                    if (t->v[h].age == v0 + 1u || t->v[h].age == v0)
                        nt[n] = t->v[h].note;
            }
    }
    return n;
}

static int basics(void)
{
    int bad = 0, ok = 1;
    int32_t v;
    step_t s;
    uint32_t k;
    memset(&s, 0, sizeof s);
    for (k = 0; k < 4u; k++)
        for (v = -8; v <= 7; v++) {
            step_set_note_nd(&s, k, v);
            ok &= step_note_nd(&s, k) == v;
            step_set_note_nd(&s, k, 0);
        }
    CHK("pernote: each note's NUDGE offset -8..+7 round trips in its own nibble", ok);
    step_set_note_nd(&s, 0, 3); step_set_note_nd(&s, 1, -2); step_set_note_nd(&s, 2, 5); step_set_note_nd(&s, 3, -8);
    ok = step_note_nd(&s, 0) == 3 && step_note_nd(&s, 1) == -2 && step_note_nd(&s, 2) == 5 && step_note_nd(&s, 3) == -8;
    CHK("  four notes do not touch each other", ok);
    s.n = 4; s.note[0] = 60; s.note[1] = 62; s.note[2] = 64; s.note[3] = 65;
    s.nv[0] = 10; s.nv[1] = 20; s.nv[2] = 30; s.nv[3] = 40;
    step_note_del(&s, 1);
    ok = s.note[1] == 64 && s.note[2] == 65 && s.note[3] == 0 && s.nv[0] == 10 && s.nv[1] == 30 && s.nv[2] == 40 && s.nv[3] == 0 &&
         step_note_nd(&s, 0) == 3 && step_note_nd(&s, 1) == 5 && step_note_nd(&s, 2) == -8 && step_note_nd(&s, 3) == 0;
    CHK("  a note out: the ones after it move up with their VEL and NUDGE, the last place is clear", ok);
    step_pn_clear(&s);
    CHK("  step_pn_clear: none left", !step_pn_any(&s));
    memset(&s, 0, sizeof s);
    s.flags = 0; step_set_nudge(&s, 6); step_set_note_nd(&s, 1, 7);
    CHK("  the note's NUDGE as it plays is the step's plus its own, kept in -8..+7", step_nd_eff(&s, 1) == 7 && step_nd_eff(&s, 0) == 6);
    step_set_nudge(&s, -7); step_set_note_nd(&s, 1, -8);
    CHK("  .. at the other end too", step_nd_eff(&s, 1) == -8);
    return bad;
}

static int play_vel(void)
{
    int bad = 0, ok = 0;
    track_t *t = loop2();
    uint32_t h, at[8], nt[8];
    t->step[0].nv[1] = 40;
    seq_start();
    run(t, 2, at, nt, 8);
    for (h = 0; h < NVOICE; h++)
        if (t->v[h].note == 64) ok = t->v[h].vel == 40u;
    CHK("pernote play: a note's own VEL sounds, the step's does for the others", ok);
    ok = 0;
    for (h = 0; h < NVOICE; h++)
        if (t->v[h].note == 60) ok = t->v[h].vel == 100u;
    CHK("  note 1 keeps the step's VEL", ok);
    return bad;
}

static int play_nudge(void)
{
    int bad = 0, ok;
    track_t *t = loop2();
    uint32_t p = period(t), at[16], nt[16], n, i, d;
    t->step[0].nd[0] = 0; step_set_note_nd(&t->step[0], 1, 8 > 7 ? 7 : 8);   /* +7 sixteenths */
    seq_start();
    n = run(t, (2u * p) / CTL - 1u, at, nt, 16);
    d = n >= 2u ? at[1] - at[0] : 0u;
    ok = n == 2u && d + CTL >= 7u * p / 16u && d <= 7u * p / 16u + CTL;
    CHK("pernote play: a note with +7/16 starts 7/16 of a step after the first", ok);
    /* the note ends its GATE after its own start, nothing sticks */
    ok = 1;
    for (i = 0; i < t->lng_p[0] + NLNG; i++) ;
    {
        uint32_t b, sound = 0;
        for (b = 0; b < (2u * p) / CTL; b++) { seq_tick(t, CTL); }
        for (i = 0; i < NLNG; i++) sound |= t->lng_p[i];
        ok = sound == 0 && t->seq_n == 0 && t->dq_n == 0;
    }
    CHK("  and both are off after their GATE (no note left in the queue or the long list)", ok);
    seq_stop();

    t = loop2();
    t->p[P_SQNT] = 1;                               /* QUANTIZE ON: the nudges are not played */
    step_set_note_nd(&t->step[0], 1, 7);
    seq_start();
    n = run(t, (2u * p) / CTL - 1u, at, nt, 16);
    CHK("  QUANTIZE ON: the chord starts together", n == 2u && at[0] == at[1]);
    seq_stop();

    t = loop2();                                    /* the step is early by -4/16, its second note on the beat (+4): the step plays early, the note after */
    step_set_nudge(&t->step[0], -4);
    step_set_note_nd(&t->step[0], 1, 4);
    seq_start();
    n = run(t, (4u * p) / CTL - 1u, at, nt, 16);
    ok = n >= 4u && at[1] - at[0] + 3u * CTL >= p / 4u && at[1] - at[0] <= p / 4u + 3u * CTL &&
         at[2] + 3u * CTL >= 2u * p - p / 4u && at[2] <= 2u * p - p / 4u + 3u * CTL && at[3] + 3u * CTL >= 2u * p && at[3] <= 2u * p + 3u * CTL;
    CHK("  step -4 and a note +4: the first note 4/16 before the beat (the pass after the first), the second on it", ok);
    seq_stop();

    t = loop2();
    t->step[0].time = ST_NOTE; t->step[0].flags = SF_SLIDE; step_set_note_nd(&t->step[0], 1, 7);
    seq_start();
    n = run(t, (2u * p) / CTL - 1u, at, nt, 16);
    CHK("  a SLIDE step plays its notes together (held on)", n == 2u && at[0] == at[1]);
    seq_stop();
    return bad;
}

static int recording(void)
{
    int bad = 0, ok;
    track_t *t = loop2();
    uint32_t p = period(t), i;
    for (i = 0; i < 2u; i++) { memset(&t->step[i], 0, sizeof t->step[i]); t->step[i].time = ST_REST; }
    song.rec = 1; song.playing = 1;
    t->seq_idx = 0; t->seq_pos = 0;
    rec_note(t, 60, 100);
    t->seq_pos = p / 2u;
    rec_note(t, 64, 70);
    t->seq_pos = p / 4u;
    rec_note(t, 67, 100);
    ok = t->step[0].n == 3u && step_nudge(&t->step[0]) == 0 && t->step[0].vel == 100u && t->step[0].nv[0] == 0u &&
         step_note_nd(&t->step[0], 1) == 7 && t->step[0].nv[1] == 70u && step_note_nd(&t->step[0], 2) == 4 && t->step[0].nv[2] == 0u &&
         !(t->step[0].flags & SF_ACCENT);
    CHK("rec: the first note sets the step's NUDGE and VEL, later ones keep their own (equal VEL: none of its own)", ok);
    t->seq_pos = 0;
    rec_note(t, 64, 70);
    CHK("  a note already there is not changed", step_note_nd(&t->step[0], 1) == 7 && t->step[0].n == 3u);
    song.rec = 0; song.playing = 0;
    /* a step started over has none left */
    memset(&t->step[1], 0, sizeof t->step[1]); t->step[1].time = ST_REST;
    t->step[1] = (step_t){{50, 52}, 2, ST_NOTE, 0, 90};
    t->step[1].nv[0] = 11; step_set_note_nd(&t->step[1], 1, 3);
    song.rec = 1; song.playing = 1;
    t->seq_idx = 1; t->seq_pos = 0;
    t->step[1].time = ST_REST; t->step[1].n = 0;
    rec_note(t, 70, 100);
    ok = t->step[1].n == 1u && !step_pn_any(&t->step[1]);
    CHK("  a fresh step over an old one: no VEL / NUDGE of the old notes is left", ok);
    song.rec = 0; song.playing = 0;
    return bad;
}

static int disk(void)
{
    int bad = 0, ok;
    track_t *t = loop2();
    project_store_t raw;
    proj5_store_t b5, b4;
    uint32_t i, k, tk, sum;
    t->step[0].nv[1] = 33; step_set_note_nd(&t->step[0], 1, -5);
    trk[4].step[3] = (step_t){{60, 64, 67, 71}, 4, ST_NOTE, 0, 100};
    trk[4].step[3].nv[0] = 1; trk[4].step[3].nv[3] = 127; step_set_note_nd(&trk[4].step[3], 2, 7); step_set_note_nd(&trk[4].step[3], 3, -8);
    trk[2].step[63] = (step_t){{50, 55}, 2, ST_NOTE, 0, 100};
    trk[2].step[63].nv[1] = 9; step_set_note_nd(&trk[2].step[63], 0, 2);
    project_capture(&proj_scratch);
    ok = proj_pack_pair(&raw, &b5, &proj_scratch);
    memset(&proj_scratch, 0, sizeof proj_scratch);
    ok &= proj_import5(&proj_scratch, &raw, (int)sizeof raw, &b5);
    {
        const step_t *a = &proj_scratch.t[0].step[0], *b = &proj_scratch.t[4].step[3], *c = &proj_scratch.t[2].step[63];
        ok &= a->nv[1] == 33 && step_note_nd(a, 1) == -5 && !a->nv[0] && b->nv[0] == 1 && b->nv[3] == 127 && step_note_nd(b, 2) == 7 &&
              step_note_nd(b, 3) == -8 && c->nv[1] == 9 && step_note_nd(c, 0) == 2;
    }
    CHK("pernote disk: tracks 1, 3 and 5 keep every note's VEL and NUDGE through FUN10 + PROJ5", ok);
    /* data of a place with no note is dropped on load */
    b5.raw[PROJ5_V4 - 4u + (0u * 64u + 5u) * 6u + 2u] = 99;          /* track 1 step 6 (empty) nv[2] */
    sum = proj_hash(b5.raw, PROJ5_SIZE - 4u); memcpy(b5.raw + PROJ5_SIZE - 4u, &sum, 4);
    memset(&proj_scratch, 0, sizeof proj_scratch);
    ok = proj_import5(&proj_scratch, &raw, (int)sizeof raw, &b5) && !step_pn_any(&proj_scratch.t[0].step[5]);
    CHK("  a VEL stored for a place the step has no note on is dropped", ok);
    /* the block of 1.1.4 (1480 bytes, its hash at 1476): opens, with none */
    memset(&b4, 0, sizeof b4);
    memcpy(b4.raw, b5.raw, PROJ5_V4 - 4u);
    k = PROJ5_V4; memcpy(b4.raw + 4, &k, 4);
    sum = proj_hash(b4.raw, PROJ5_V4 - 4u); memcpy(b4.raw + PROJ5_V4 - 4u, &sum, 4);
    memset(&proj_scratch, 0, sizeof proj_scratch);
    ok = proj_import5(&proj_scratch, &raw, (int)sizeof raw, &b4);
    for (tk = 0; tk < 5u; tk++)
        for (i = 0; i < 64u; i++)
            ok &= !step_pn_any(&proj_scratch.t[tk].step[i]);
    ok &= PROJ5_LEN_OK(PROJ5_V4) && PROJ5_LEN_OK(PROJ5_OLD) && PROJ5_LEN_OK(PROJ5_SIZE) && !PROJ5_LEN_OK(1000u);
    CHK("  the block of 1.1.4 (1480 bytes) loads, with no VEL / NUDGE of their own", ok);
    return bad;
}

static int detail_knobs(void)
{
    int bad = 0, ok;
    track_t *t = loop2();
    step_t *st;
    uint32_t k;
    ui.cursor = 0;
    st = &t->step[0];
    st->vel = 100;
    CHK("pernote ui: no key held: no note picked", sd_note(st) < 0);
    for (k = 0; k < 27u && kb_map(t, k) > 127u; k++)
        ;
    kb_chn[k] = 1; kb_trk[k] = (uint8_t)song.sel; kb_chord[k][0] = 64;
    ok = sd_note(st) == 1;
    CHK("  the key of the second note held: that note", ok);
    sd_turn_note(1, EVK_VEL, -30);
    CHK("  VEL -30: the note's own 70", st->nv[1] == 70u && st->vel == 100u && !st->nv[0]);
    sd_turn_note(1, EVK_VEL, 30);
    CHK("  back to the step's: none of its own", st->nv[1] == 0u);
    step_set_nudge(st, 2);
    sd_turn_note(1, EVK_NUDGE, 3);
    CHK("  NUDGE +3 from the step's 2: the note plays at 5, an offset of 3", step_nd_eff(st, 1) == 5 && step_note_nd(st, 1) == 3 && step_nudge(st) == 2);
    sd_turn_note(1, EVK_NUDGE, 20);
    CHK("  and stops at +7", step_nd_eff(st, 1) == 7);
    kb_chn[k] = 0;
    st->n = 1;
    kb_chn[k] = 1;
    CHK("  a step of one note: the step's own fields", sd_note(st) < 0);
    kb_chn[k] = 0;
    return bad;
}

static int roll(void)
{
    int bad = 0;
    track_t *t = loop2();
    step_t *st = &t->step[0];
    pr_tr = t; pr_cols = 16;
    CHK("pernote roll: no per-note NUDGE: the bars are not moved", pr_note_dx(st, 0) == 0 && pr_note_dx(st, 1) == 0);
    step_set_note_nd(st, 1, 8 > 7 ? 7 : 8);
    CHK("  a note +7/16: its bar 5 px right of the first one (the step's, where its earliest note plays)", pr_note_dx(st, 0) == 0 && pr_note_dx(st, 1) == 5);
    step_set_nudge(st, -8);
    step_set_note_nd(st, 1, 7);
    CHK("  the step -8, a note +7 (it plays at -1): 5 px right of the first", pr_note_dx(st, 0) == 0 && pr_note_dx(st, 1) == 5);
    CHK("  and the step is drawn at its earliest note (seq_nudge -8)", seq_nudge(t, st) == -8);
    t->p[P_SQNT] = 1;
    CHK("  QUANTIZE ON: none moved", pr_note_dx(st, 1) == 0);
    t->p[P_SQNT] = 0;
    st->flags |= SF_SLIDE;
    CHK("  a SLIDE step: none moved (its notes play together)", pr_note_dx(st, 1) == 0 && seq_nudge(t, st) == -8);
    st->flags &= ~SF_SLIDE;
    ui.cursor = 0; ui.bank = 0;
    graph_roll(t, T_TEXT);                           /* (drawn without trouble) */
    CHK("  the roll draws", pr_tr == 0);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += basics();
    bad += play_vel();
    bad += play_nudge();
    bad += recording();
    bad += disk();
    bad += detail_knobs();
    bad += roll();
    puts(bad ? "pernote tests FAILED" : "pernote tests passed");
    return bad != 0;
}
