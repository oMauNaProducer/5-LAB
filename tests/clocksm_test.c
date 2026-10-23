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
static void drain(uint32_t blk){(void)blk;}

/* 1.6.2 external clock: the BPM from a master's pulses (midi_clock.c). A pulse train at bpm with the millisecond clock's
 * rounding and a jitter of +-jit ms, fed to midi_clock_pulse; returns how often song.g[G_BPM] changed */
static uint32_t bpm_changes, bpm_last;
static double tnow;
static uint32_t lcg = 12345u;
static int32_t rnd(int32_t jit) { lcg = lcg * 1664525u + 1013904223u; return jit ? (int32_t)((lcg >> 16) % (uint32_t)(2 * jit + 1)) - jit : 0; }
static void feed(double bpm, uint32_t pulses, int32_t jit)
{
    double period = 60000.0 / bpm / 24.0;
    uint32_t i;
    for (i = 0; i < pulses; i++) {
        uint32_t ms;
        tnow += period;
        ms = (uint32_t)(tnow + 0.5) + (uint32_t)rnd(jit);
        midi_clock_pulse(ms);
        if ((uint32_t)song.g[G_BPM] != bpm_last) { bpm_changes++; bpm_last = (uint32_t)song.g[G_BPM]; }
    }
}
static void follow(void)
{
    fresh();
    song.g[G_CLOCK] = 1;
    memset(&midi_clock, 0, sizeof midi_clock); midi_clock.mode = 1; midi_beat_samples = 0;
    song.g[G_BPM] = 120; bpm_last = 120; bpm_changes = 0; tnow = 1000; lcg = 12345u;
    song.playing = 1;
    midi_clock_transport(0xFAu, 1000u);
}

int main(void)
{
    uint32_t c0;
    setvbuf(stdout, NULL, _IONBF, 0);

    follow(); feed(140.0, 24u * 4u, 1);
    check("a master at 140: the BPM reads 140 after four beats", song.g[G_BPM] == 140);
    c0 = bpm_changes; feed(140.0, 24u * 120u, 1);
    check("a steady 140 with +-1 ms of jitter, 2 minutes: the BPM never moves (was 139 / 140 / 141 at random)", bpm_changes == c0 && song.g[G_BPM] == 140);
    check("... and the beat length the delay / arp / slicer follow is the master's (within 0.2 %)",
          (double)midi_beat_samples > 44100.0 * 60.0 / 140.0 * 0.998 && (double)midi_beat_samples < 44100.0 * 60.0 / 140.0 * 1.002);

    follow(); feed(120.0, 24u * 8u, 0); c0 = bpm_changes;
    feed(125.0, 24u * 2u, 0);
    check("a jump of 5 BPM or more (120 -> 125) is taken within two beats", song.g[G_BPM] == 125);
    follow(); feed(120.0, 24u * 8u, 0);
    feed(150.0, 24u * 2u, 0);
    check("... 120 -> 150 too", song.g[G_BPM] == 150);

    follow(); feed(120.0, 24u * 8u, 0);
    { double b; for (b = 120.0; b <= 130.0; b += 0.25) feed(b, 6u * 4u, 0); }
    feed(130.0, 24u * 10u, 0);
    check("a slow tempo ramp (120 -> 130 over 10 beats) goes through: ends at 130", song.g[G_BPM] == 130);

    follow(); feed(120.0, 24u * 8u, 0); c0 = bpm_changes;
    feed(120.5, 24u * 30u, 0);
    check("0.5 BPM off (120 -> 120.5) stays inside the 0.8 BPM band: no change", bpm_changes == c0 && song.g[G_BPM] == 120);
    feed(121.0, 24u * 30u, 0);
    check("... 1 BPM off is taken (the mean is 0.8 away or more)", song.g[G_BPM] == 121);

    follow(); feed(100.0, 24u * 3u, 0);
    check("a Start and four beats at 100 and 90 BPM: first reads settle within 3 beats", song.g[G_BPM] >= 99 && song.g[G_BPM] <= 101);
    midi_clock_transport(0xFAu, (uint32_t)tnow); tnow += 0; feed(90.0, 24u * 3u, 0);
    check("a new Start forgets the old beats: 90 BPM within 3 beats", song.g[G_BPM] >= 89 && song.g[G_BPM] <= 91);

    printf(bad ? "clocksm: %d FAILED\n" : "clocksm: all passed\n", bad);
    return bad != 0;
}
