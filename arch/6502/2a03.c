/* NES APU (2A03) implementation */

#include "2a03.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
/* MSVC doesn't support C11 atomics without /experimental:c11atomics.
 * On MSVC, volatile already implies acquire/release barriers on x86/x64,
 * so plain volatile reads/writes give the same correctness guarantees. */
#define memory_order_relaxed  0
#define memory_order_acquire  1
#define memory_order_release  2
#define atomic_load_explicit(ptr, order)          (*(volatile int *)(ptr))
#define atomic_store_explicit(ptr, val, order)    ((void)(*(volatile int *)(ptr) = (int)(val)))
#endif

/* ---------------------------------------------------------------------------
 * Lookup tables
 * --------------------------------------------------------------------------- */

static const uint8_t LENGTH_TABLE[32] = {
    10, 254, 20,  2, 40,  4, 80,  6,
   160,   8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22,
   192,  24, 72, 26, 16, 28, 32, 30
};

static const uint8_t DUTY_TABLE[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 },
};

static const uint8_t TRIANGLE_SEQ[32] = {
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

static const uint16_t NOISE_PERIOD[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const uint16_t DMC_RATE[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106,  84,  72,  54
};

/* Frame counter step cycle counts (NTSC, CPU cycles).
 * 4-step:  quarter at 3729, 7457, 11186, 14915
 *          half    at       7457,        14915
 *          IRQ at 14914, 14915, 0
 * 5-step:  quarter at 3729, 7457, 11186, (skip 14915), 18641
 *          half    at       7457,                        18641
 */

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

static void envelope_clock(struct apu_envelope *env) {
    if (env->start) {
        env->start = false;
        env->volume = 15;
        env->divider = env->period;
    } else {
        if (env->divider == 0) {
            env->divider = env->period;
            if (env->volume > 0) {
                env->volume--;
            } else if (env->loop) {
                env->volume = 15;
            }
        } else {
            env->divider--;
        }
    }
}

static uint16_t sweep_target(struct apu_pulse *p) {
    uint16_t shifted = p->timer_period >> p->sweep.shift;
    if (p->sweep.negate) {
        /* pulse1: one's complement negate; pulse2: two's complement */
        if (p->id == 1) {
            return p->timer_period - shifted - 1;
        } else {
            return p->timer_period - shifted;
        }
    }
    return p->timer_period + shifted;
}

static bool sweep_mutes(struct apu_pulse *p) {
    uint16_t target = sweep_target(p);
    return (p->timer_period < 8) || (target > 0x7FF);
}

static void sweep_clock(struct apu_pulse *p) {
    if (p->sweep.reload) {
        p->sweep.divider = p->sweep.period;
        p->sweep.reload = false;
    } else if (p->sweep.divider > 0) {
        p->sweep.divider--;
    } else {
        p->sweep.divider = p->sweep.period;
        if (p->sweep.enable && !sweep_mutes(p)) {
            p->timer_period = sweep_target(p);
        }
    }
}

static void length_clock(struct apu_length *len) {
    if (!len->halt && len->counter > 0) {
        len->counter--;
    }
}

static uint8_t pulse_output(struct apu_pulse *p) {
    if (p->len.counter == 0) return 0;
    if (DUTY_TABLE[p->duty][p->duty_pos] == 0) return 0;
    if (sweep_mutes(p)) return 0;
    if (p->timer_period < 8) return 0;
    return p->env.constant ? p->env.period : p->env.volume;
}

static uint8_t triangle_output(struct apu_triangle *t) {
    if (t->len.counter == 0) return 0;
    if (t->linear == 0) return 0;
    return TRIANGLE_SEQ[t->seq_pos];
}

static uint8_t noise_output(struct apu_noise *n) {
    if (n->lfsr & 0x0001) return 0;
    if (n->len.counter == 0) return 0;
    return n->env.constant ? n->env.period : n->env.volume;
}

static void dmc_restart(struct apu_dmc *dmc) {
    dmc->cur_addr = dmc->sample_addr;
    dmc->bytes_remaining = dmc->sample_len;
}

static void dmc_fill_buffer(struct apu2a03 *apu) {
    if (apu->dmc.sample_buf_empty && apu->dmc.bytes_remaining > 0) {
        apu->dmc.sample_buf = apu->cpu_read(apu->dmc.cur_addr);
        /* Advance address, wrapping from $FFFF to $8000 */
        if (apu->dmc.cur_addr == 0xFFFF) {
            apu->dmc.cur_addr = 0x8000;
        } else {
            apu->dmc.cur_addr++;
        }
        apu->dmc.bytes_remaining--;
        apu->dmc.sample_buf_empty = false;

        if (apu->dmc.bytes_remaining == 0) {
            if (apu->dmc.loop) {
                dmc_restart(&apu->dmc);
            } else if (apu->dmc.irq_enable) {
                apu->dmc.irq_pending = true;
            }
        }
    }
}

static void dmc_clock_output(struct apu_dmc *dmc) {
    if (!dmc->silence) {
        if (dmc->shift_reg & 0x01) {
            if (dmc->output_level <= 125) dmc->output_level += 2;
        } else {
            if (dmc->output_level >= 2) dmc->output_level -= 2;
        }
    }
    dmc->shift_reg >>= 1;
    if (dmc->bits_remaining > 0) dmc->bits_remaining--;

    if (dmc->bits_remaining == 0) {
        dmc->bits_remaining = 8;
        if (dmc->sample_buf_empty) {
            dmc->silence = true;
        } else {
            dmc->silence = false;
            dmc->shift_reg = dmc->sample_buf;
            dmc->sample_buf_empty = true;
        }
    }
}

static float mixer_output(struct apu2a03 *apu) {
    uint8_t p1 = pulse_output(&apu->pulse1);
    uint8_t p2 = pulse_output(&apu->pulse2);
    uint8_t t  = triangle_output(&apu->triangle);
    uint8_t n  = noise_output(&apu->noise);
    uint8_t d  = apu->dmc.output_level;

    float pulse_out = (p1 + p2 == 0) ? 0.0f
        : 95.88f / (8128.0f / (float)(p1 + p2) + 100.0f);

    float tnd_denom = (float)t / 8227.0f + (float)n / 12241.0f + (float)d / 22638.0f;
    float tnd_out = (tnd_denom == 0.0f) ? 0.0f
        : 159.79f / (1.0f / tnd_denom + 100.0f);

    return pulse_out + tnd_out;
}

/* ---------------------------------------------------------------------------
 * Frame counter quarter/half frame events
 * --------------------------------------------------------------------------- */

static void clock_quarter_frame(struct apu2a03 *apu) {
    envelope_clock(&apu->pulse1.env);
    envelope_clock(&apu->pulse2.env);
    envelope_clock(&apu->noise.env);

    /* Triangle linear counter */
    if (apu->triangle.linear_reload) {
        apu->triangle.linear = apu->triangle.linear_period;
    } else if (apu->triangle.linear > 0) {
        apu->triangle.linear--;
    }
    if (!apu->triangle.control) {
        apu->triangle.linear_reload = false;
    }
}

static void clock_half_frame(struct apu2a03 *apu) {
    /* Length counters */
    length_clock(&apu->pulse1.len);
    length_clock(&apu->pulse2.len);
    length_clock(&apu->triangle.len);
    length_clock(&apu->noise.len);

    /* Sweep units */
    sweep_clock(&apu->pulse1);
    sweep_clock(&apu->pulse2);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void apu_init(struct apu2a03 *apu, uint8_t (*cpu_read_fn)(uint16_t addr)) {
    memset(apu, 0, sizeof(*apu));
    apu->cpu_read = cpu_read_fn;
    apu->cycles_per_sample = (float)(APU_CPU_HZ / APU_SAMPLE_RATE);
    apu->pulse1.id = 1;
    apu->pulse2.id = 2;
    apu->noise.lfsr = 1;
    apu->dmc.sample_buf_empty = true;
    apu->dmc.bits_remaining = 8;
    apu->dmc.silence = true;
}

void apu_reset(struct apu2a03 *apu) {
    /* $4015 is cleared — disable all channels and zero their length counters */
    apu->pulse1.len.enabled    = false;
    apu->pulse1.len.counter    = 0;
    apu->pulse2.len.enabled    = false;
    apu->pulse2.len.counter    = 0;
    apu->triangle.len.enabled  = false;
    apu->triangle.len.counter  = 0;
    apu->noise.len.enabled     = false;
    apu->noise.len.counter     = 0;
    apu->dmc.enabled           = false;
    apu->dmc.bytes_remaining   = 0;
    apu->dmc.irq_pending       = false;
    /* Frame counter IRQ flag is cleared on reset */
    apu->frame.irq_pending     = false;
    apu->irq_pending           = false;
    apu->sample_cycles         = 0.0f;
    apu_ring_reset(apu);
    /* Frame counter restarts as if last_4017 was re-written 9-12 cycles before
     * the CPU begins executing from the reset vector.  At power-on last_4017=0
     * (zeroed by apu_init memset), matching the hardware's implicit $00 write.
     * On soft reset the last value written to $4017 is re-applied (Blargg
     * 4017_written test).  We pre-advance frame.cycles to 9 to simulate the
     * ~9-12 cycle gap between the $4017 write and CPU start (4017_timing). */
    apu->frame.mode         = (apu->last_4017 >> 7) & 0x01;
    apu->frame.irq_inhibit  = (apu->last_4017 >> 6) & 0x01;
    if (apu->frame.irq_inhibit)
        apu->frame.irq_pending = false;
    apu->frame.cycles       = 9;
    apu->frame.reload_delay = 0; /* counter already running; no startup delay */
    /* Noise LFSR state is preserved across reset (per hardware). */
}

void apu_clock(struct apu2a03 *apu) {
    apu->cycle++;

    /* Triangle clocks every CPU cycle */
    if (apu->triangle.timer == 0) {
        apu->triangle.timer = apu->triangle.timer_period;
        if (apu->triangle.len.counter > 0 && apu->triangle.linear > 0) {
            apu->triangle.seq_pos = (apu->triangle.seq_pos + 1) & 0x1F;
        }
    } else {
        apu->triangle.timer--;
    }

    /* Pulse and noise clock every other CPU cycle */
    apu->pulse_clock_toggle ^= 1;
    if (apu->pulse_clock_toggle) {
        /* Pulse 1 */
        if (apu->pulse1.timer == 0) {
            apu->pulse1.timer = apu->pulse1.timer_period;
            apu->pulse1.duty_pos = (apu->pulse1.duty_pos + 1) & 0x07;
        } else {
            apu->pulse1.timer--;
        }

        /* Pulse 2 */
        if (apu->pulse2.timer == 0) {
            apu->pulse2.timer = apu->pulse2.timer_period;
            apu->pulse2.duty_pos = (apu->pulse2.duty_pos + 1) & 0x07;
        } else {
            apu->pulse2.timer--;
        }

        /* Noise */
        uint16_t np = NOISE_PERIOD[apu->noise.period_index];
        if (apu->noise.timer == 0) {
            apu->noise.timer = np;
            uint16_t feedback;
            if (apu->noise.mode) {
                feedback = ((apu->noise.lfsr >> 14) ^ (apu->noise.lfsr >> 8)) & 0x01;
            } else {
                feedback = ((apu->noise.lfsr >> 14) ^ (apu->noise.lfsr >> 13)) & 0x01;
            }
            apu->noise.lfsr = (apu->noise.lfsr >> 1) | (feedback << 14);
        } else {
            apu->noise.timer--;
        }
    }

    /* DMC — decrement then check so the period matches the table value exactly */
    if (apu->dmc.enabled) {
        if (apu->dmc.timer > 0) apu->dmc.timer--;
        if (apu->dmc.timer == 0) {
            apu->dmc.timer = apu->dmc.timer_period;
            dmc_clock_output(&apu->dmc);
            dmc_fill_buffer(apu);
        }
    }

    /* Frame counter — apu_clock() is called once per CPU cycle.
     * Step values are in CPU cycles (NES NTSC: ~1.789773 MHz). */
    if (apu->frame.reload_delay > 0) {
        apu->frame.reload_delay--;
        if (apu->frame.reload_delay == 0) {
            apu->frame.cycles = 0;
            if (apu->frame.mode) {
                /* 5-step: immediately fire all units on reset */
                clock_quarter_frame(apu);
                clock_half_frame(apu);
            }
        }
        /* Counter does not advance while reload is pending */
    } else {
        apu->frame.cycles++;
    }

    if (!apu->frame.mode) {
        /* 4-step mode — CPU cycle values (apu_clock called every CPU cycle):
         *   7458 CPU  (quarter-frame 1)
         *  14913 CPU  (half-frame 1 = quarter-frame 2)
         *  22371 CPU  (quarter-frame 3)
         *  29828 CPU  (IRQ set, first)
         *  29829 CPU  (half-frame 2 = quarter-frame 4)
         *  29830 CPU  (IRQ set again + counter reset) */
        switch (apu->frame.cycles) {
        case 7457:
            clock_quarter_frame(apu);
            break;
        case 14913:
            clock_quarter_frame(apu);
            clock_half_frame(apu);
            break;
        case 22371:
            clock_quarter_frame(apu);
            break;
        case 29828:
            if (!apu->frame.irq_inhibit) {
                apu->frame.irq_pending = true;
            }
            break;
        case 29829:
            clock_quarter_frame(apu);
            clock_half_frame(apu);
            if (!apu->frame.irq_inhibit) {
                apu->frame.irq_pending = true;
            }
            break;
        case 29830:
            if (!apu->frame.irq_inhibit) {
                apu->frame.irq_pending = true;
            }
            apu->frame.cycles = 0;
            break;
        }
    } else {
        /* 5-step mode — CPU cycle values (apu_clock called every CPU cycle):
         *   7457 CPU  (quarter-frame 1)
         *  14913 CPU  (half-frame 1 = quarter-frame 2)
         *  22371 CPU  (quarter-frame 3)
         *  29829 CPU  (quarter-frame 4, no half-frame)
         *  37281 CPU  (half-frame 2 = quarter-frame 5 + reset) */
        switch (apu->frame.cycles) {
        case 7457:
            clock_quarter_frame(apu);
            break;
        case 14913:
            clock_quarter_frame(apu);
            clock_half_frame(apu);
            break;
        case 22371:
            clock_quarter_frame(apu);
            break;
        /* step 4 (at ~29829) does nothing in 5-step mode */
        case 37281:
            clock_quarter_frame(apu);
            clock_half_frame(apu);
            break;
        case 37282:
            apu->frame.cycles = 0;
            break;
        }
    }

    /* Downsampling: accumulate output and emit when enough cycles have passed */
    apu->sample_cycles += 1.0f;
    if (apu->sample_cycles >= apu->cycles_per_sample) {
        apu->sample_cycles -= apu->cycles_per_sample;
        float s = mixer_output(apu);

        /* NES hardware audio filters (applied at the output sample rate):
         *   HP1: ~90 Hz  — removes DC bias from channel enable/disable pops
         *   HP2: ~440 Hz — second high-pass stage matching hardware
         *   LP:  ~14 kHz — softens sharp pulse/noise transients
         * Coefficients for 44100 Hz output:
         *   hp_alpha = exp(-2π * fc / 44100)
         *   lp_beta  = 1 - exp(-2π * fc / 44100) */
        /* HP1 at 90 Hz: α ≈ 0.98724 */
        float hp1_out = 0.98724f * (apu->hp1_prev_out + s - apu->hp1_prev_in);
        apu->hp1_prev_in  = s;
        apu->hp1_prev_out = hp1_out;

        /* HP2 at 440 Hz: α ≈ 0.93897 */
        float hp2_out = 0.93897f * (apu->hp2_prev_out + hp1_out - apu->hp2_prev_in);
        apu->hp2_prev_in  = hp1_out;
        apu->hp2_prev_out = hp2_out;

        /* LP at 14 kHz: β ≈ 0.87497 */
        apu->lp_prev = apu->lp_prev + 0.87497f * (hp2_out - apu->lp_prev);

        int w = atomic_load_explicit(&apu->ring_write, memory_order_relaxed);
        int next_w = (w + 1) & (APU_RING_SIZE - 1);
        int r = atomic_load_explicit(&apu->ring_read, memory_order_acquire);
        if (next_w != r) {
            apu->ring_buf[w] = apu->lp_prev;
            atomic_store_explicit(&apu->ring_write, next_w, memory_order_release);
        }
        /* When full, drop the sample — backpressure is handled in the main loop */
    }
}

void apu_write(struct apu2a03 *apu, uint16_t addr, uint8_t data) {
    switch (addr) {
    /* Pulse 1 */
    case 0x4000:
        apu->pulse1.duty       = (data >> 6) & 0x03;
        apu->pulse1.env.loop   = (data >> 5) & 0x01;
        apu->pulse1.len.halt   = (data >> 5) & 0x01;
        apu->pulse1.env.constant = (data >> 4) & 0x01;
        apu->pulse1.env.period = data & 0x0F;
        break;
    case 0x4001:
        apu->pulse1.sweep.enable = (data >> 7) & 0x01;
        apu->pulse1.sweep.period = (data >> 4) & 0x07;
        apu->pulse1.sweep.negate = (data >> 3) & 0x01;
        apu->pulse1.sweep.shift  = data & 0x07;
        apu->pulse1.sweep.reload = true;
        break;
    case 0x4002:
        apu->pulse1.timer_period = (apu->pulse1.timer_period & 0xFF00) | data;
        break;
    case 0x4003:
        apu->pulse1.timer_period = (apu->pulse1.timer_period & 0x00FF) | ((uint16_t)(data & 0x07) << 8);
        if (apu->pulse1.len.enabled) {
            apu->pulse1.len.counter = LENGTH_TABLE[(data >> 3) & 0x1F];
        }
        apu->pulse1.duty_pos = 0;
        apu->pulse1.env.start = true;
        break;

    /* Pulse 2 */
    case 0x4004:
        apu->pulse2.duty       = (data >> 6) & 0x03;
        apu->pulse2.env.loop   = (data >> 5) & 0x01;
        apu->pulse2.len.halt   = (data >> 5) & 0x01;
        apu->pulse2.env.constant = (data >> 4) & 0x01;
        apu->pulse2.env.period = data & 0x0F;
        break;
    case 0x4005:
        apu->pulse2.sweep.enable = (data >> 7) & 0x01;
        apu->pulse2.sweep.period = (data >> 4) & 0x07;
        apu->pulse2.sweep.negate = (data >> 3) & 0x01;
        apu->pulse2.sweep.shift  = data & 0x07;
        apu->pulse2.sweep.reload = true;
        break;
    case 0x4006:
        apu->pulse2.timer_period = (apu->pulse2.timer_period & 0xFF00) | data;
        break;
    case 0x4007:
        apu->pulse2.timer_period = (apu->pulse2.timer_period & 0x00FF) | ((uint16_t)(data & 0x07) << 8);
        if (apu->pulse2.len.enabled) {
            apu->pulse2.len.counter = LENGTH_TABLE[(data >> 3) & 0x1F];
        }
        apu->pulse2.duty_pos = 0;
        apu->pulse2.env.start = true;
        break;

    /* Triangle */
    case 0x4008:
        apu->triangle.control       = (data >> 7) & 0x01;
        apu->triangle.len.halt      = (data >> 7) & 0x01;
        apu->triangle.linear_period = data & 0x7F;
        break;
    case 0x4009:
        break; /* unused */
    case 0x400A:
        apu->triangle.timer_period = (apu->triangle.timer_period & 0xFF00) | data;
        break;
    case 0x400B:
        apu->triangle.timer_period = (apu->triangle.timer_period & 0x00FF) | ((uint16_t)(data & 0x07) << 8);
        if (apu->triangle.len.enabled) {
            apu->triangle.len.counter = LENGTH_TABLE[(data >> 3) & 0x1F];
        }
        apu->triangle.linear_reload = true;
        break;

    /* Noise */
    case 0x400C:
        apu->noise.env.loop     = (data >> 5) & 0x01;
        apu->noise.len.halt     = (data >> 5) & 0x01;
        apu->noise.env.constant = (data >> 4) & 0x01;
        apu->noise.env.period   = data & 0x0F;
        break;
    case 0x400D:
        break; /* unused */
    case 0x400E:
        apu->noise.mode         = (data >> 7) & 0x01;
        apu->noise.period_index = data & 0x0F;
        break;
    case 0x400F:
        if (apu->noise.len.enabled) {
            apu->noise.len.counter = LENGTH_TABLE[(data >> 3) & 0x1F];
        }
        apu->noise.env.start = true;
        break;

    /* DMC */
    case 0x4010:
        apu->dmc.irq_enable = (data >> 7) & 0x01;
        apu->dmc.loop       = (data >> 6) & 0x01;
        apu->dmc.rate_index = data & 0x0F;
        apu->dmc.timer_period = DMC_RATE[apu->dmc.rate_index];
        if (!apu->dmc.irq_enable) {
            apu->dmc.irq_pending = false;
        }
        break;
    case 0x4011:
        apu->dmc.output_level = data & 0x7F;
        break;
    case 0x4012:
        apu->dmc.sample_addr = 0xC000 + (uint16_t)data * 64;
        break;
    case 0x4013:
        apu->dmc.sample_len = (uint16_t)data * 16 + 1;
        break;

    /* Channel enable */
    case 0x4015: {
        apu->pulse1.len.enabled  = (data >> 0) & 0x01;
        apu->pulse2.len.enabled  = (data >> 1) & 0x01;
        apu->triangle.len.enabled = (data >> 2) & 0x01;
        apu->noise.len.enabled   = (data >> 3) & 0x01;
        bool dmc_enable = (data >> 4) & 0x01;

        if (!apu->pulse1.len.enabled)   apu->pulse1.len.counter  = 0;
        if (!apu->pulse2.len.enabled)   apu->pulse2.len.counter  = 0;
        if (!apu->triangle.len.enabled) apu->triangle.len.counter = 0;
        if (!apu->noise.len.enabled)    apu->noise.len.counter   = 0;

        /* DMC IRQ cleared on $4015 write */
        apu->dmc.irq_pending = false;

        if (!dmc_enable) {
            apu->dmc.enabled = false;
            apu->dmc.bytes_remaining = 0;
        } else {
            apu->dmc.enabled = true;
            if (apu->dmc.bytes_remaining == 0) {
                dmc_restart(&apu->dmc);
                /* Set timer=0 so the first output fires in the same APU tick
                 * as this write.  The timer is NOT reset to timer_period here
                 * because on real hardware the DMC timer runs continuously;
                 * sync_dmc_fast relies on this to phase-align measurements.
                 * The lazy dmc_fill_buffer() in apu_clock() handles the first
                 * sample byte fetch on the next timer fire (if sample_buf is
                 * empty) or defers it to the first shift-register reload. */
                apu->dmc.timer = 0;
            }
        }
        break;
    }

    /* Frame counter */
    case 0x4017:
        apu->last_4017         = data; /* remember for soft reset re-application */
        apu->frame.mode        = (data >> 7) & 0x01;
        apu->frame.irq_inhibit = (data >> 6) & 0x01;
        if (apu->frame.irq_inhibit) {
            apu->frame.irq_pending = false;
        }
        /* If written on an odd CPU cycle the reset is delayed one extra cycle
         * (3 cycles on even, 4 on odd) — this is the hardware jitter. */
        apu->frame.reload_delay = (apu->cycle & 1) ? 4 : 3;
        break;

    default:
        break;
    }
}

uint8_t apu_read(struct apu2a03 *apu, uint16_t addr) {
    if (addr == 0x4015) {
        uint8_t val = 0;
        if (apu->pulse1.len.counter > 0)   val |= 0x01;
        if (apu->pulse2.len.counter > 0)   val |= 0x02;
        if (apu->triangle.len.counter > 0) val |= 0x04;
        if (apu->noise.len.counter > 0)    val |= 0x08;
        if (apu->dmc.bytes_remaining > 0)  val |= 0x10;
        if (apu->frame.irq_pending)        val |= 0x40;
        if (apu->dmc.irq_pending)          val |= 0x80;
        /* Reading $4015 acknowledges frame IRQ */
        apu->frame.irq_pending = false;
        return val;
    }
    /* All other APU addresses return open bus (0) */
    return 0;
}

bool apu_irq_pending(struct apu2a03 *apu) {
    apu->irq_pending = apu->frame.irq_pending || apu->dmc.irq_pending;
    bool pending = apu->irq_pending;
    /* The caller (CPU IRQ handler) is responsible for acknowledging individual
     * IRQ sources; we just report combined state here */
    return pending;
}

void apu_audio_callback(void *userdata, uint8_t *stream, int len) {
    struct apu2a03 *apu = (struct apu2a03 *)userdata;
    float *out = (float *)(void *)stream;
    int n = len / (int)sizeof(float);
    for (int i = 0; i < n; i++) {
        int r = atomic_load_explicit(&apu->ring_read, memory_order_relaxed);
        int w = atomic_load_explicit(&apu->ring_write, memory_order_acquire);
        if (r != w) {
            out[i] = apu->ring_buf[r];
            atomic_store_explicit(&apu->ring_read, (r + 1) & (APU_RING_SIZE - 1),
                                  memory_order_release);
        } else {
            out[i] = 0.0f; /* underrun: output silence */
        }
    }
}

int apu_drain_samples(struct apu2a03 *apu, float *buf, int max) {
    int count = 0;
    while (count < max) {
        int r = atomic_load_explicit(&apu->ring_read, memory_order_relaxed);
        int w = atomic_load_explicit(&apu->ring_write, memory_order_acquire);
        if (r == w) break;
        buf[count++] = apu->ring_buf[r];
        atomic_store_explicit(&apu->ring_read, (r + 1) & (APU_RING_SIZE - 1),
                              memory_order_release);
    }
    return count;
}

int apu_ring_available(struct apu2a03 *apu) {
    int w = atomic_load_explicit(&apu->ring_write, memory_order_acquire);
    int r = atomic_load_explicit(&apu->ring_read, memory_order_relaxed);
    return (w - r + APU_RING_SIZE) & (APU_RING_SIZE - 1);
}

void apu_ring_reset(struct apu2a03 *apu) {
    int w = atomic_load_explicit(&apu->ring_write, memory_order_acquire);
    atomic_store_explicit(&apu->ring_read, w, memory_order_release);
}
