/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from MIDI clock contributions by ChanceTheMaker and keremimo (2026).
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Audio ISR state. Input timestamps keep USB/TRS jitter outside the render loop. */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, rem, interval_ms;
    uint32_t pulse_samples, interp_q8;
    uint32_t base_ms, qstart_ms, jump_ms;         /* (1.6.2, the tempo) the first reading's start, this quarter's start, a possible new tempo's */
    int32_t period_q16, pred_q16, pred_q16_keep;     /* (pred_q16_keep: the estimate a second look must differ from) */
                  /* the pulse period and the next interval expected, ms in Q16 */
    uint16_t k;                                   /* pulses since the first reading's start */
    uint8_t mode, have_pulse, tempo_valid;
    uint8_t reread;                               /* the first reading in progress is a second look at a possible new tempo */
    uint8_t skip, state, qn, off_q;               /* pulses still ignored; 0 none, 1 first reading, 2 tracking; pulses in the quarter; quarters off */
} midi_clock;

static __attribute__((noinline)) void midi_clock_transport(uint32_t status, uint32_t ms)
{
    if (status == 0xFAu) {                         /* Start: step zero */
        midi_clock.pos = midi_clock.rendered = midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        seq_start();
    } else if (status == 0xFBu) {                  /* Continue: preserve the step */
        midi_clock.pos = midi_clock.rendered;
        midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        if (!song.playing)
            motion_begin();
        song.playing = 1;
    } else if (status == 0xFCu) {
        seq_stop();
    }
}

/* 1.6.2: the master's tempo from its pulses. A pulse timed to the millisecond is 1 to 2 % off in a 140's 17.9 ms (the computer, the
 * USB frame, our own blocks), so a tempo read from one beat, or from a quarter, wanders by a BPM or two. This follows the pulses
 * with a small loop: each pulse is compared with where the last estimate expected it and the period (and the expected time) is
 * corrected by a part of the difference (1/256 and 7/8: the period's noise is about 1/40 of the pulse's, a ramp is followed
 * within 0.3 BPM, a small change of tempo is in within 100 pulses). Before it starts, the clock is read plainly: the first two quarters after a
 * Start are ignored (a DAW's first pulses come early or late), then 24 pulses (a beat) between the first and the last give the
 * period, jitter at the two ends only. A quarter 4 % off the estimate twice running is a new tempo: read plainly again from
 * the start of the first of the two. A gap of 200 ms or more begins the whole thing again. The BPM shown changes only when the
 * estimate is 0.9 BPM from it (a 140.5 stays where it is: the integer does not flicker); the beat the delay, the arp and the slicer
 * (and the step grid) use changes when the estimate is 0.2 % from it, the steps' phase carried over as before. The
 * sequencer itself follows the pulses and is not affected by any of this */
#define MCT_SKIP 12u                              /* pulses until the first reading starts */
#define MCT_READ 24u                              /* pulses of the first reading */
static int32_t sdiv(int32_t v, int32_t d) { return (v + (v < 0 ? -d / 2 : d / 2)) / d; }   /* (rounded, symmetric) */
static void midi_clock_publish(uint32_t first)
{
    int32_t cur = song.g[G_BPM], bpm10 = (int32_t)((1638400000u + (uint32_t)midi_clock.period_q16 / 2u) / (uint32_t)midi_clock.period_q16);
    uint32_t old_beat = beat_samples(), new_beat = (uint32_t)(((uint64_t)(uint32_t)midi_clock.period_q16 * 69363302u) >> 32);   /* (FS * 24 / 1000 / 65536, in 2^-32) */
    if (first || bpm10 - cur * 10 >= 9 || cur * 10 - bpm10 >= 9)
        song.g[G_BPM] = (int16_t)clamp((bpm10 + 5) / 10, 40, 240);
    if (first || (new_beat > old_beat ? new_beat - old_beat : old_beat - new_beat) * 512u >= old_beat) {
        if (song.playing && new_beat != old_beat) {
            uint32_t i, ratio = (new_beat << 12) / old_beat;
            /* Preserve each track's fractional step phase when the master
             * changes tempo. The next pulse then lands on its new boundary. */
            for (i = 0; i < NTRK; i++) {
                if (trk[i].seq_pos < (uint32_t)FS * 2u)
                    trk[i].seq_pos = (uint32_t)(((uint64_t)trk[i].seq_pos * ratio + 2048u) >> 12);
                if (trk[i].seq_off)
                    trk[i].seq_off = (uint32_t)(((uint64_t)trk[i].seq_off * ratio + 2048u) >> 12);
                if (trk[i].nd_late)            /* (NUDGE: the latched plays, seq_pos + 1, with the step) */
                    trk[i].nd_late = (uint32_t)(((uint64_t)(trk[i].nd_late - 1u) * ratio + 2048u) >> 12) + 1u;
                if (trk[i].nd_early)
                    trk[i].nd_early = (uint32_t)(((uint64_t)(trk[i].nd_early - 1u) * ratio + 2048u) >> 12) + 1u;
            }
            if (clk_pos < (uint32_t)FS * 2u)   /* (the metronome's beat as the steps) */
                clk_pos = (uint32_t)(((uint64_t)clk_pos * ratio + 2048u) >> 12);
        }
        midi_beat_samples = new_beat;
    }
}

static __attribute__((noinline)) void midi_clock_tempo(uint32_t ms)
{
    uint32_t dt_ms = ms - midi_clock.last_ms;
    int32_t err;
    if (midi_clock.tempo_valid && dt_ms >= 200u)
        midi_clock.tempo_valid = 0;               /* a gap: begin again */
    if (!midi_clock.tempo_valid) {
        midi_clock.tempo_valid = 1;
        midi_clock.skip = MCT_SKIP;
        midi_clock.state = 0;
        midi_clock.reread = 0;
        midi_clock.period_q16 = 0;
    }
    if (midi_clock.skip && --midi_clock.skip)
        return;                                   /* (the last of the ignored pulses is the first reading's start) */
    if (midi_clock.skip == 0 && midi_clock.state == 0) {
        midi_clock.state = 1;
        midi_clock.base_ms = midi_clock.qstart_ms = ms;
        midi_clock.k = 0;
        midi_clock.qn = midi_clock.off_q = 0;
        return;
    }
    if (midi_clock.state == 1) {
        if (++midi_clock.k < MCT_READ)
            return;
        midi_clock.period_q16 = (int32_t)(((ms - midi_clock.base_ms) << 16) / MCT_READ);
        if (midi_clock.period_q16 < 655360 || midi_clock.period_q16 > 4194304) {   /* not 40..240 BPM: look again */
            midi_clock.tempo_valid = 0;
            return;
        }
        if (midi_clock.reread) {                  /* a false alarm (jitter in two quarters running): the estimate stays */
            int32_t d = midi_clock.period_q16 - midi_clock.pred_q16_keep;
            midi_clock.reread = 0;
            if (d < midi_clock.pred_q16_keep / 64 && d > -midi_clock.pred_q16_keep / 64) {
                midi_clock.period_q16 = midi_clock.pred_q16_keep;
                midi_clock.state = 2;
                midi_clock.pred_q16 = midi_clock.period_q16;
                midi_clock.qstart_ms = ms;
                midi_clock.qn = midi_clock.off_q = 0;
                return;
            }
        }
        midi_clock.state = 2;
        midi_clock.pred_q16 = midi_clock.period_q16;
        midi_clock.qstart_ms = ms;
        midi_clock.qn = midi_clock.off_q = 0;
        midi_clock_publish(1u);
        return;
    }
    {   /* one pulse, or more when some were lost on the way (2 or 3 periods in one interval) */
        uint32_t n = ((dt_ms << 16) + (uint32_t)midi_clock.period_q16 / 2u) / (uint32_t)midi_clock.period_q16;
        n = n < 1u ? 1u : n > 3u ? 3u : n;
        err = (int32_t)(dt_ms << 16) - (int32_t)(n - 1u) * midi_clock.period_q16 - midi_clock.pred_q16;
        if (err > midi_clock.period_q16 / 2 || err < -midi_clock.period_q16 / 2) {   /* a pulse far off: not a tempo (it is not used) */
            midi_clock.pred_q16 = midi_clock.period_q16;
        } else {
            midi_clock.period_q16 += sdiv(err, 256);
            midi_clock.pred_q16 = midi_clock.period_q16 - (err - sdiv(err, 8));
        }
        midi_clock.qn = (uint8_t)(midi_clock.qn + n);
    }
    if (midi_clock.qn < 6u)
        return;
    {   /* a quarter ends: is it still the same tempo? */
        int32_t pq = (int32_t)(((ms - midi_clock.qstart_ms) << 16) / midi_clock.qn), d = pq - midi_clock.period_q16;
        midi_clock.qn = 0;
        if (d > midi_clock.period_q16 / 25 || d < -midi_clock.period_q16 / 25) {
            if (!midi_clock.off_q++)
                midi_clock.jump_ms = midi_clock.qstart_ms;
        } else {
            midi_clock.off_q = 0;
        }
        midi_clock.qstart_ms = ms;
        if (midi_clock.off_q >= 2u) {             /* a new tempo: read plainly from the first quarter that showed it */
            midi_clock.state = 1;
            midi_clock.reread = 1;
            midi_clock.pred_q16_keep = midi_clock.period_q16;
            midi_clock.base_ms = midi_clock.jump_ms;
            midi_clock.k = 12u;
            midi_clock.off_q = 0;
            return;
        }
    }
    midi_clock_publish(0u);
}

static __attribute__((noinline)) void midi_clock_pulse(uint32_t ms)
{
    uint32_t q;
    midi_clock_tempo(ms);
    if (song.playing) {
        if (midi_clock.have_pulse) {
            uint32_t interval = ms - midi_clock.last_ms;
            if (interval >= 8u && interval <= 80u)
                midi_clock.interval_ms = interval;
            q = beat_samples() + midi_clock.rem;
            midi_clock.pos += q / 24u;
            midi_clock.rem = q % 24u;
        }
        midi_clock.have_pulse = 1;
    }
    midi_clock.pulse_samples = beat_samples() / 24u;
    if (!midi_clock.interval_ms)
        midi_clock.interval_ms = beat_samples() * 1000u / ((uint32_t)FS * 24u);
    midi_clock.interp_q8 = midi_clock.pulse_samples * 256u / midi_clock.interval_ms;
    midi_clock.last_ms = ms;
}

static uint32_t midi_clock_advance(uint32_t now)
{
    uint32_t target, elapsed, offset, n;
    if (!midi_clock.have_pulse)
        return 0;
    elapsed = now - midi_clock.last_ms;
    /* Interpolate to the next pulse, never across it before it arrives. */
    if (elapsed > midi_clock.interval_ms)
        elapsed = midi_clock.interval_ms;
    offset = elapsed * midi_clock.interp_q8 >> 8;
    if (offset >= midi_clock.pulse_samples)
        offset = midi_clock.pulse_samples - 1u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    if (n > (uint32_t)FS / 8u)
        n = (uint32_t)FS / 8u;
    midi_clock.rendered += n;
    return n;
}

