/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 5-LAB 1.0.6: SEQ > STEP's piano roll draws a bar where it plays: moved by its NUDGE while QUANTIZE is OFF (ui_graph.c pr_nudge_x) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int roll_nudge(void)
{
    int bad = 0, ok;
    track_t *t;
    step_t s;
    int32_t n, x0 = PR_X0 + 3 * PR_CW + 2;                  /* the bar of the fourth column */
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    memset(&s, 0, sizeof s);
    s.note[0] = 60; s.n = 1; s.time = ST_NOTE; s.vel = 99;
    t->step[3] = s;
    t->p[P_SLEN] = 16;
    t->p[P_SQNT] = 1;
    bad += check("roll: QUANTIZE ON: a nudged step is drawn on its column", (step_set_nudge(&t->step[3], 5), pr_nudge_x(t, &t->step[3], x0, 9, 16) == x0));
    t->p[P_SQNT] = 0;
    step_set_nudge(&t->step[3], 0);
    bad += check("  QUANTIZE OFF, no NUDGE: on its column", pr_nudge_x(t, &t->step[3], x0, 9, 16) == x0);
    step_set_nudge(&t->step[3], -8);
    n = pr_nudge_x(t, &t->step[3], x0, 9, 16);
    bad += check("  NUDGE -8 (half a step early): 6 px left", n == x0 - 6);
    step_set_nudge(&t->step[3], 7);
    n = pr_nudge_x(t, &t->step[3], x0, 9, 16);
    bad += check("  NUDGE +7: 5 px right", n == x0 + 5);
    step_set_nudge(&t->step[3], 4);
    n = pr_nudge_x(t, &t->step[3], x0, 9, 16);
    bad += check("  NUDGE +4 (a quarter): 3 px right", n == x0 + 3);
    step_set_nudge(&t->step[3], -4);
    n = pr_nudge_x(t, &t->step[3], x0, 9, 16);
    bad += check("  NUDGE -4: 3 px left", n == x0 - 3);
    {   int32_t a = pr_nudge_x(t, &t->step[3], x0, 9, 16);
        int32_t b = (step_set_nudge(&t->step[3], -3), pr_nudge_x(t, &t->step[3], x0, 9, 16));
        bad += check("  it grows with the NUDGE (-4 .. -3: one px at most per unit)", b >= a && b - a <= 1); }
    ok = 1;
    {   int32_t v;
        for (v = -8; v <= 7; v++) {
            int32_t d;
            step_set_nudge(&t->step[3], v);
            d = pr_nudge_x(t, &t->step[3], x0, 9, 16) - x0;
            ok &= d >= -6 && d <= 5 && (v == 0 ? d == 0 : (v < 0 ? d < 0 : d > 0));
        }
    }
    bad += check("  every NUDGE -8..+7: -6..+5 px, the direction of the sign, none at 0", ok);
    step_set_nudge(&t->step[3], -8);
    bad += check("  the first column: its bar stays right of the keyboard strip", pr_nudge_x(t, &t->step[3], PR_X0 + 2, 9, 16) >= PR_X0 + 1);
    step_set_nudge(&t->step[3], 7);
    bad += check("  the last column: its bar ends inside the grid", pr_nudge_x(t, &t->step[3], PR_X0 + 15 * PR_CW + 2, 9, 16) + 9 <= PR_X0 + 16 * PR_CW);
    step_set_nudge(&t->step[3], 7);
    t->step[3].flags = 0; step_set_ratchet(&t->step[3], 2);
    bad += check("  a RATCH step is not moved (it plays on its start)", pr_nudge_x(t, &t->step[3], x0, 9, 16) == x0);
    step_set_ratchet(&t->step[3], 1);
    t->step[3].time = ST_REST;
    bad += check("  a REST has no bar to move", pr_nudge_x(t, &t->step[3], x0, 9, 16) == x0);
    return bad;
}
static int roll_space(void)
{
    int bad = 0;
    step_t a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.note[0] = 60; a.n = 1; a.time = ST_NOTE;
    b.note[0] = 60; b.n = 1; b.time = ST_NOTE;
    bad += check("roll: no next bar: the width as it was", pr_bar_w(0, 0, 60, 100, 9) == 9);
    bad += check("  the next bar of the same note far away: 9", pr_bar_w(&b, 114, 60, 100, 9) == 9);
    bad += check("  the same note close: the bar ends PR_GAP before the next one", pr_bar_w(&b, 108, 60, 100, 9) == 108 - PR_GAP - 100 &&
                 100 + pr_bar_w(&b, 108, 60, 100, 9) + PR_GAP == 108);
    bad += check("  another note: not shortened (it does not touch)", pr_bar_w(&b, 108, 72, 100, 9) == 9);
    bad += check("  a chord: only the note the next step also has", (b.note[1] = 64, b.n = 2, pr_bar_w(&b, 108, 64, 100, 9) == 6 && pr_bar_w(&b, 108, 67, 100, 9) == 9));
    bad += check("  never under 1 px, the next bar at or before it", pr_bar_w(&b, 101, 60, 100, 9) == 1 && pr_bar_w(&b, 95, 60, 100, 9) == 1);
    return bad;
}
int main(void)
{
    int bad = roll_nudge() + roll_space();
    printf("%s\n", bad ? "ROLL NUDGE TEST FAILED" : "roll nudge tests passed"); return bad != 0;
}
