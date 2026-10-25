/* SPDX-License-Identifier: GPL-3.0-only
 * 5-LAB: the fifth track's half of a project (firmware/src/project.c PROJ5, "FT5A"): FUN10 keeps four tracks byte for byte
 * and this second object carries track 5 (its sound, 64 steps, FM6 patch, MIDI IN channel, section lane, automation
 * and whether it was selected), linked to the FUN10 it belongs to by that FUN10's hash. Tested: the round trip; a
 * project with no half, an unlinked one (the FUN10 saved again by firmware without track 5), a damaged one: track 5 at its
 * power-on sound, silent in a song, the other four tracks untouched; FUN10 of four tracks unchanged by track 5.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"
#include "old_pack.h"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static project_t mk(void)
{
    project_t q;
    uint32_t t, i, k;
    memset(&q, 0, sizeof q);
    q.magic = PROJ_MAGIC; q.size = sizeof q; q.parts = NTRK_DISK; q.phys = PROJ_PHYS;
    for (i = 0; i < G_COUNT; i++) q.g[i] = GP[i].def;
    chain_defaults(&q.chain);
    for (t = 0; t < NTRK; t++) {
        uint32_t e = t == 3u ? ENGI_DRUM : t == 4u ? ENGI_FM6 : 0u;
        for (i = 0; i < P_COUNT; i++) q.t[t].p[i] = param_desc_of(e, i)->def;
        q.t[t].engine = (uint8_t)e;
        q.t[t].preset = (uint8_t)(t + 1u);
        for (i = 0; i < NSTEP; i++) q.t[t].step[i].time = ST_REST;
        q.t[t].step[t].time = ST_NOTE; q.t[t].step[t].n = 1; q.t[t].step[t].note[0] = (uint8_t)(40u + t);
        q.t[t].step[t].vel = 100;
        q.t[t].p[P_LEVEL] = (int16_t)(60 + t);
        for (k = 0; k < FM6_PACKED; k++) q.fm6[t][k] = (uint8_t)((t * 7u + k) & 127u);
    }
    q.t[4].p[P_PAN] = -20; q.t[4].p[P_E0] = 3;
    q.t[4].step[9] = (step_t){{50, 53}, 2, ST_NOTE, SF_ACCENT, 90, 0, 0};
    step_set_ratchet(&q.t[4].step[9], 3); step_set_nudge(&q.t[4].step[9], -3); step_set_chance(&q.t[4].step[9], 40);
    q.mch[4] = 9;
    q.chain.count = 2;
    q.chain.row[0] = (chain_row_t){{0, 1, 2, CHAIN_SILENT, 3}, 2};
    q.chain.row[1] = (chain_row_t){{1, 1, 1, 1, CHAIN_SILENT}, 4};
    q.motion.count = 3; q.motion.on = 0x13;
    q.motion.event[0] = (motion_event_t){(uint16_t)(1u << 6 | 5u), P_REV, 33};
    q.motion.event[1] = (motion_event_t){(uint16_t)(4u << 6 | 7u), P_PAN, -9};
    q.motion.event[2] = (motion_event_t){(uint16_t)(4u << 6 | 63u), (uint8_t)(P_E1 | MOTION_LOCK), 77};
    q.sel = 4;
    memcpy(q.name, "FIVE", 4);
    q.sum = proj_sum(&q);
    return q;
}

int main(void)
{
    int bad = 0, ok;
    project_t a = mk(), b, c;
    project_store_t m, m2;
    proj5_store_t h, h2;
    uint32_t t;

    ok = proj_pack_pair(&m, &h, &a) && proj_import5(&b, &m, sizeof m, &h);
    bad += check("pair: packs and imports", ok);
    bad += check("  the round trip is the same project (five tracks, lane, MIDI channel, patch, automation, selection)",
                 ok && !memcmp(&a, &b, sizeof a));
    bad += check("  the FUN10 half is a four-track FUN10 (parts 4, 3840 bytes, FUNA)",
                 m.raw[63] == 4u && ((uint32_t *)m.raw)[0] == PROJ_MAGIC && sizeof m == 3840u && m.raw[62] == 0u);
    bad += check("  its automation: the records of tracks 1..4 only, the switch of those", ((motion_store_disk_t *)(m.raw + 68u +
                 NTRK_DISK * (P_COUNT + 2u + NSTEP * 9u) + sizeof(chain_disk_t)))->count == 1u &&
                 ((motion_store_disk_t *)(m.raw + 68u + NTRK_DISK * (P_COUNT + 2u + NSTEP * 9u) + sizeof(chain_disk_t)))->on == 3u);
    bad += check("  its song rows: four lanes and the repeat, as FUN10 always had",
                 m.raw[68u + NTRK_DISK * (P_COUNT + 2u + NSTEP * 9u) + 4u + 4u] == 2u);

    /* the same four tracks whatever track 5 holds: FUN10 does not change with it */
    c = a; c.t[4].p[P_LEVEL] = 5; c.t[4].step[0].note[0] = 99; c.mch[4] = 2; c.chain.row[0].slot[4] = 0;
    c.motion.event[1].value = 5; c.sum = proj_sum(&c);
    ok = proj_pack_pair(&m2, &h2, &c) && !memcmp(&m, &m2, sizeof m) && memcmp(&h, &h2, sizeof h);
    bad += check("FUN10 is the same bytes whatever track 5 holds; the half differs", ok);

    /* no half (a project of before, or of firmware without track 5): power-on sound, silent in the song */
    ok = proj_import5(&b, &m, sizeof m, 0);
    bad += check("no half: the four tracks, the song, the name come over", ok && !memcmp(b.t, a.t, NTRK_DISK * sizeof a.t[0]) &&
                 b.chain.count == 2u && b.chain.row[0].slot[2] == 2 && !memcmp(b.name, "FIVE", 4) && b.sel == 0u);
    bad += check("  track 5 at its power-on sound, no steps, silent in every section, channel 5, no automation, init patch",
                 b.t[4].engine == trk_def_engine(4) && b.t[4].preset == PROJ_DEF_SOUND && b.t[4].step[9].time == ST_REST &&
                 b.chain.row[0].slot[4] == CHAIN_SILENT && b.chain.row[1].slot[4] == CHAIN_SILENT && b.mch[4] == 0 &&
                 b.motion.count == 1u && !memcmp(b.fm6[4], FM6_INIT, FM6_PACKED) && b.motion.on == 3u);

    /* unlinked: the FUN10 saved again (a rename, a save by firmware without the half): the old half names another */
    c = a; c.name[0] = 'X'; c.sum = proj_sum(&c);
    proj_pack(&m2, &c);
    ok = proj_import5(&b, &m2, sizeof m2, &h);
    bad += check("a half of another FUN10 (unlinked): refused, track 5 at power-on, the new FUN10's name kept",
                 ok && b.name[0] == 'X' && b.t[4].preset == PROJ_DEF_SOUND && b.chain.row[0].slot[4] == CHAIN_SILENT);

    /* damaged halves */
    for (t = 16u, ok = 1; t < PROJ5_SIZE; t += 97u) {
        h2 = h; h2.raw[t] ^= 0x04u;
        ok &= proj_import5(&b, &m, sizeof m, &h2) && b.t[4].preset == PROJ_DEF_SOUND && !memcmp(b.t, a.t, NTRK_DISK * sizeof a.t[0]);
    }
    bad += check("a damaged half (a byte flipped in many places): refused, the four tracks untouched", ok);
    h2 = h;
    { uint32_t sum, k; h2.raw[40] = 200;   /* a parameter byte past the range, with a good hash */
      sum = proj_hash(h2.raw, PROJ5_SIZE - 4u); memcpy(h2.raw + PROJ5_SIZE - 4u, &sum, 4); (void)k; }
    ok = proj_import5(&b, &m, sizeof m, &h2) && b.t[4].preset == PROJ_DEF_SOUND;
    bad += check("a parameter out of range with a good hash: refused", ok);
    h2 = h; h2.raw[13] = 40; { uint32_t sum = proj_hash(h2.raw, PROJ5_SIZE - 4u); memcpy(h2.raw + PROJ5_SIZE - 4u, &sum, 4); }
    ok = proj_import5(&b, &m, sizeof m, &h2) && b.t[4].preset == PROJ_DEF_SOUND;
    bad += check("a MIDI channel past OFF with a good hash: refused", ok);

    /* automation of the fifth track merges after the four tracks' (room for 128 in all) */
    c = a; c.motion.count = 0; memset(c.motion.event, 0, sizeof c.motion.event);
    for (t = 0; t < 100u; t++) c.motion.event[c.motion.count++] = (motion_event_t){(uint16_t)((t / 64u) << 6 | t % 64u), P_REV, 1};
    for (t = 0; t < 28u; t++) c.motion.event[c.motion.count++] = (motion_event_t){(uint16_t)(4u << 6 | t), P_PAN, 2};
    c.motion.on = 31; c.sum = proj_sum(&c);
    ok = c.motion.count == 128u && proj_pack_pair(&m2, &h2, &c) && proj_import5(&b, &m2, sizeof m2, &h2) && b.motion.count == 128u &&
         b.motion.on == 31u;
    for (t = 0; ok && t < 28u; t++) ok &= b.motion.event[100u + t].place == (4u << 6 | t) && b.motion.event[100u + t].value == 2;
    bad += check("128 records, 28 of them track 5's: all come back (the four tracks' first, then track 5's)", ok);

    /* older formats give track 5 its power-on sound */
    {
        static uint8_t v9[3648];
        c = a; c.chain.row[0] = chain_row_of(2, 3); c.chain.row[1] = chain_row_of(1, 1); c.motion.count = 1;
        memset(c.motion.event + 1, 0, 2 * sizeof c.motion.event[0]); c.parts = NTRK_DISK; c.sum = proj_sum(&c);
        ok = old_pack(v9, &c, OLD_FUN9, 99u) == sizeof v9 && proj_import(&b, v9, sizeof v9);
        bad += check("FUN9 (1.1): the four tracks come over, the fifth at power-on, silent in the song",
                     ok && !memcmp(b.t, c.t, NTRK_DISK * sizeof c.t[0]) && b.t[4].preset == PROJ_DEF_SOUND &&
                     b.t[4].engine == trk_def_engine(4) && b.chain.row[0].slot[4] == CHAIN_SILENT && b.chain.row[0].slot[3] == 2 &&
                     !memcmp(b.fm6[4], FM6_INIT, FM6_PACKED) && b.mch[4] == 0);
    }
    printf(bad ? "PROJ5 TEST FAILED\n" : "proj5 test passed\n");
    return bad != 0;
}
