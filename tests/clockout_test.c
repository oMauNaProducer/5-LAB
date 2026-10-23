/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* CLOCK OUT (1.6.2; src/seq.c clock_out_block): MIDI clock on USB MIDI while the song plays on its own clock, through
 * the real audio path (audio.c audio_block -> events_block), the packets read from usb.c's MIDI out ring:
 *   OFF: nothing leaves; ON: Start (0xFA) in the block step 0 plays in, with the first pulse (0xF8) right after it;
 *   24 pulses per quarter note at 30 / 120 / 300 BPM, every pulse within one block (32 samples) of its exact time and
 *   no drift over 21 s; a tempo change moves the next pulse; Stop (0xFC) once, on a stop and when CLOCK OUT is
 *   turned off; a restart from the top (GLO + PLAY) sends Start again and no Stop; an external clock (G_CLOCK USB /
 *   TRS) sends nothing; turned on mid-song: silent until the next start from the top; no host (usb.config 0): nothing
 *   queued; the ring never overflows. */
#include <sys/stat.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 1u
static volatile uint32_t t5_nested_ticks;
static uint32_t fm1_ticks(void) { return 0; }
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p)
{ (void)b; (void)n; (void)isr; (void)p; }
void isr_alnk0(void) {}
#define FELUCCA_UAC 1                                  /* audio.c taps USB audio: here into tap */
static int32_t tap_blk[2 * CTL];
static void uac_tap(const int32_t *out, uint32_t n) { memcpy(tap_blk, out, 8u * n); }
static void uac_render_start(void) {}
#include "../firmware/src/audio.c"

#define MAXB 30000u                                    /* blocks recorded (~21.8 s) */
static int32_t dac[MAXB * CTL], usbo[MAXB * CTL];       /* left channel, Q15 (the DAC's as before OUT_SHIFT) */
static uint8_t blk_play[MAXB], blk_cnt[MAXB];
static uint16_t blk_idx[MAXB];
static int8_t blk_beat[MAXB];                          /* seq.c click_beat_now after the block (the header's metronome) */
static uint32_t nb;                                    /* blocks rendered */

static void fresh(void)
{
    uint32_t p;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&chain, 0, sizeof chain);
    memset(&midi_clock, 0, sizeof midi_clock);
    host_tracks_init();
    for (p = 0; p < NPART; p++) host_preset(&trk[p], 0, 0);
    for (p = 0; p < NPART; p++)
        trk[p].p[P_DIST] = trk[p].p[P_CHOR] = trk[p].p[P_DLY] = trk[p].p[P_REV] = 0;
    transport_req = panic_req = 0;
    seq_stop();
    clk_pos = CLK_START; clk_step = 0;
    lim_env = LIM_T; dc_l = dc_r = dce_l = dce_r = 0;
    memset(&clk, 0, sizeof clk);
    click_req = 0; click_mode = CLICK_OFF; click_lvl = 1; cin_bars = 0;
    fx_usb_fixed = 0;
    mi_r = mi_w = 0; midi_in_overflow = 0; midi_beat_samples = 0;
    fm1_in.notes = kb_prev = 0;
    fm1_ms = 0;
    song.sel = 0;
    nb = 0;
}
static void run(uint32_t n)                            /* n blocks through audio_block */
{
    int32_t out[2 * CTL];
    uint32_t i;
    while (n-- && nb < MAXB) {
        audio_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            dac[nb * CTL + i] = out[2u * i] / (1 << OUT_SHIFT);
            usbo[nb * CTL + i] = tap_blk[2u * i];
        }
        blk_play[nb] = song.playing;
        blk_cnt[nb] = seq_counting();
        blk_idx[nb] = trk[0].seq_idx;
        blk_beat[nb] = (int8_t)click_beat_now();
        nb++;
        fm1_ms = (uint32_t)((uint64_t)nb * CTL * 1000u / FS);
    }
}

static int bad;
static void check(const char *what, int ok)
{
    printf("clockout: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define PK_START (0x0Fu | 0xFAu << 8)
#define PK_CLOCK (0x0Fu | 0xF8u << 8)
#define PK_STOP  (0x0Fu | 0xFCu << 8)
#define MAXP 40000u
static uint32_t pk_blk[MAXP], pk_val[MAXP], np;
static void drain(uint32_t blk)
{
    while (mo_r != mo_w) {
        if (np < MAXP) { pk_blk[np] = blk; pk_val[np] = midi_out_q[mo_r % MQ]; np++; }
        mo_r++;
    }
}
static void go(uint32_t blocks)                        /* blocks through audio_block, the ring read after each */
{
    while (blocks--) { run(1); drain(nb - 1u); }
}
static void setup(uint32_t bpm)
{
    uint32_t i;
    fresh();
    usb.config = 1; mo_r = mo_w = 0; np = 0;
    clock_out_on = 0; clk_out_run = 0; clk_out_idle = 0; clk_out_acc = 0; clk_out_gen = clk_out_seen = 0;
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    for (i = 0; i < 16u; i++)
        trk[0].step[i] = (step_t){{(uint8_t)(48 + (i * 7u) % 12u)}, 1, i % 3u ? ST_NOTE : ST_REST, 0, 100};
    song.g[G_BPM] = (int16_t)bpm;
}
static uint32_t count_of(uint32_t v, uint32_t from)
{
    uint32_t i, n = 0;
    for (i = from; i < np; i++) n += pk_val[i] == v;
    return n;
}
/* the pulses from index a on: their block, against the exact time k * period (samples, Q0 from the Start block) */
static void pulse_stats(uint32_t a, uint32_t bpm, uint32_t *n, double *worst, double *drift)
{
    uint32_t i, k = 0, first_blk = 0;
    double period = 44100.0 * 60.0 / bpm / 24.0, w = 0, last_err = 0;
    for (i = a; i < np; i++) {
        double err;
        if (pk_val[i] != PK_CLOCK) continue;
        if (!k) first_blk = pk_blk[i];
        err = (double)(pk_blk[i] - first_blk) * CTL - (double)k * period;   /* (a pulse leaves in the block it is due in) */
        if (err < 0) err = -err;
        if (err > w) w = err;
        last_err = err;
        k++;
    }
    *n = k; *worst = w; *drift = last_err;
}

static void basics(void)
{
    uint32_t n; double worst, drift;
    setup(120); go(2000);
    check("OFF: nothing leaves, playing or not", np == 0u);
    setup(120); transport_req = 1; go(2000);
    check("OFF: still nothing while the song plays", np == 0u && song.playing);

    setup(120); clock_out_on = 1; go(10);
    check("ON, stopped: pulses already, no Start and no Stop", np >= 1u && count_of(PK_CLOCK, 0) == np);
    np = 0; transport_req = 1; go(1);
    check("Start in the block the song starts in, the first pulse right after it",
          np == 2u && pk_val[0] == PK_START && pk_val[1] == PK_CLOCK && pk_blk[0] == pk_blk[1]);
    go(44100u * 10u / CTL);
    pulse_stats(0, 120, &n, &worst, &drift);
    check("120 BPM, 10 s: 480 pulses (+1: the one at the start)", n >= 480u && n <= 481u);
    check("120 BPM: every pulse within one block of its exact time, and no drift", worst < (double)CTL && drift < (double)CTL);
    check("one Start only, no Stop while it plays", count_of(PK_START, 0) == 1u && count_of(PK_STOP, 0) == 0u);

    setup(30); clock_out_on = 1; transport_req = 1; go(44100u * 20u / CTL);
    pulse_stats(0, 30, &n, &worst, &drift);
    printf("clockout:   (30 BPM: %u pulses, worst %.1f, drift %.1f samples, %u packets, %u blocks, CTL %d)\n", n, worst, drift, np, nb, (int)CTL);
    check("30 BPM, 20 s: 240 pulses, within a block of the time", n >= 240u && n <= 241u && worst < (double)CTL);
    setup(300); clock_out_on = 1; transport_req = 1; go(44100u * 10u / CTL);
    pulse_stats(0, 300, &n, &worst, &drift);
    check("300 BPM, 10 s: 1200 pulses, within a block of the time, none lost", n >= 1200u && n <= 1201u && worst < (double)CTL);

    setup(120); clock_out_on = 1; transport_req = 1; go(44100u * 21u / CTL);   /* (run() records 21.8 s at most) */
    pulse_stats(0, 120, &n, &worst, &drift);
    check("120 BPM, 21 s: 1008 pulses, the last still within a block of the exact time (no drift)",
          n >= 1008u && n <= 1009u && drift < (double)CTL && worst < (double)CTL);
}

static void transport(void)
{
    uint32_t after;
    setup(120); clock_out_on = 1; transport_req = 1; go(3000);
    transport_req = 2; go(1);
    check("STOP: one Stop, and nothing but pulses after it", count_of(PK_STOP, 0) == 1u && count_of(PK_START, 0) == 1u);
    after = np; go(3000);
    check("after the Stop: no Start, no second Stop, the pulses go on (2.18 s at 120 BPM: ~104)",
          count_of(PK_STOP, after) == 0u && count_of(PK_START, after) == 0u &&
          count_of(PK_CLOCK, after) >= 102u && count_of(PK_CLOCK, after) <= 106u);

    setup(120); clock_out_on = 1; transport_req = 1; go(3000); np = 0;
    transport_req = 3; go(1);                          /* GLO + PLAY: from the top without stopping */
    check("restart from the top: Start again with a pulse, no Stop",
          count_of(PK_START, 0) == 1u && count_of(PK_STOP, 0) == 0u && pk_val[0] == PK_START && pk_val[1] == PK_CLOCK);

    setup(120); clock_out_on = 1; transport_req = 1; go(3000);
    clock_out_on = 0; go(1);
    check("CLOCK OUT turned off while it plays: one Stop", count_of(PK_STOP, 0) == 1u);
    after = np; go(3000);
    check("... and no more pulses", np == after);
    clock_out_on = 1; go(3000);
    check("... turned on again while it plays: nothing (a Start would run the other box from step 0, off the song)",
          np == after);
    transport_req = 3; go(1);
    check("... and with the next start from the top: Start and the first pulse",
          count_of(PK_START, after) == 1u && pk_val[after] == PK_START && pk_val[after + 1u] == PK_CLOCK);
    go(3000);
    check("... then the pulses run", count_of(PK_CLOCK, after) > 100u);
}

static void tempo(void)
{
    uint32_t i, last = 0, d1 = 0, d2 = 0, n1 = 0, n2 = 0;
    setup(120); clock_out_on = 1; transport_req = 1; go(44100u * 2u / CTL);
    song.g[G_BPM] = 180; go(44100u * 2u / CTL);
    for (i = 0; i < np; i++) {
        if (pk_val[i] != PK_CLOCK) continue;
        if (last && pk_blk[i] * CTL < 44100u * 2u) { d1 += pk_blk[i] - last; n1++; }
        else if (last && pk_blk[i] * CTL > 44100u * 2u + 4000u) { d2 += pk_blk[i] - last; n2++; }
        last = pk_blk[i];
    }
    /* 120 BPM: 918.75 samples = 28.7 blocks; 180 BPM: 612.5 samples = 19.1 blocks */
    check("a tempo change (120 -> 180) moves the pulses: 28.7 blocks apart, then 19.1",
          n1 > 40u && n2 > 40u && d1 * 10u / n1 >= 280u && d1 * 10u / n1 <= 294u && d2 * 10u / n2 >= 187u && d2 * 10u / n2 <= 195u);
}

static void idle(void)
{
    uint32_t n, after; double worst, drift;
    setup(120); clock_out_on = 1; go(21000);
    pulse_stats(0, 120, &n, &worst, &drift);
    check("stopped, 120 BPM, 15.2 s: pulses at the exact time, no drift, no Start / Stop",
          count_of(PK_START, 0) == 0u && count_of(PK_STOP, 0) == 0u && n >= 730u && n <= 734u &&
          worst < (double)CTL && drift < (double)CTL);
    after = np; song.g[G_BPM] = 180; go(3000);
    pulse_stats(after, 180, &n, &worst, &drift);
    check("a tempo change while stopped: the pulses follow (180 BPM)", n >= 155u && n <= 160u);
    after = np; transport_req = 1; go(1);
    check("PLAY from stopped: Start, then the pulse of the downbeat, as before",
          pk_val[after] == PK_START && pk_val[after + 1u] == PK_CLOCK && count_of(PK_STOP, after) == 0u);
    setup(120); clock_out_on = 1; go(2000); after = np; clock_out_on = 0; go(3000);
    check("CLOCK OUT turned off while stopped: no Stop (nothing was started), and silence", count_of(PK_STOP, 0) == 0u && np == after);
    setup(120); clock_out_on = 1; song.g[G_CLOCK] = 1; go(3000);
    check("stopped with an external clock (USB): nothing leaves", np == 0u);
    setup(120); clock_out_on = 1; usb.config = 0; go(3000);
    check("stopped, no USB host: nothing queued", mo_r == mo_w && np == 0u);
}

static void guards(void)
{
    uint32_t i, flood = 0;
    setup(120); clock_out_on = 1; song.g[G_CLOCK] = 1; transport_req = 1; go(3000);
    check("an external clock (USB): nothing leaves", np == 0u);
    setup(120); clock_out_on = 1; song.g[G_CLOCK] = 2; transport_req = 1; go(3000);
    check("an external clock (TRS): nothing leaves", np == 0u);
    setup(120); clock_out_on = 1; usb.config = 0; transport_req = 1; go(3000);
    check("no USB host: nothing queued (no pulses waiting for one that connects later)", mo_r == mo_w && np == 0u);
    usb.config = 1; go(3000);
    check("... a host that connects while it plays: pulses from then on", count_of(PK_CLOCK, 0) > 100u);
    setup(300); clock_out_on = 1; transport_req = 1;
    for (i = 0; i < 4000u; i++) { run(1); flood += (mo_w - mo_r) > MQ - 4u; drain(nb - 1u); }
    check("300 BPM: the ring never fills (drained each block: at most a few packets pending)", flood == 0u);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    basics();
    transport();
    tempo();
    idle();
    guards();
    printf(bad ? "clockout: %d FAILED\n" : "clockout: all passed\n", bad);
    return bad != 0;
}
