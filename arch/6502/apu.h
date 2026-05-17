#ifndef __APU_H__
#define __APU_H__

#include <stdbool.h>
#include <stdint.h>

/* Maximum audio samples buffered per frame (44100/60 + margin) */
#define APU_SAMPLE_BUFFER_SIZE 1024

/* NTSC CPU clock rate */
#define APU_CPU_HZ 1789773.0
/* Target audio sample rate */
#define APU_SAMPLE_RATE 44100.0

struct apu_envelope {
    bool start;
    bool loop;      /* doubles as length counter halt */
    bool constant;  /* constant volume mode */
    uint8_t period; /* divider reload value (4-bit) */
    uint8_t divider;
    uint8_t volume; /* current decay level (0-15) */
};

struct apu_length {
    uint8_t counter;
    bool halt;
    bool enabled;
};

struct apu_sweep {
    bool enable;
    uint8_t period;
    bool negate;
    uint8_t shift;
    bool reload;
    uint8_t divider;
};

struct apu_pulse {
    uint8_t duty;     /* 0-3 */
    uint8_t duty_pos; /* 0-7 position in sequencer */
    struct apu_envelope env;
    struct apu_sweep sweep;
    struct apu_length len;
    uint16_t timer_period; /* 11-bit */
    uint16_t timer;
    int id; /* 1 or 2 (affects sweep negate) */
};

struct apu_triangle {
    uint8_t linear_period;
    uint8_t linear;
    bool linear_reload;
    bool control; /* bit7 of $4008: halt + linear counter control */
    struct apu_length len;
    uint16_t timer_period;
    uint16_t timer;
    uint8_t seq_pos; /* 0-31 */
};

struct apu_noise {
    uint16_t lfsr;        /* 15-bit, init to 1 */
    bool mode;            /* false=long(32767 steps), true=short(93 steps) */
    struct apu_envelope env;
    struct apu_length len;
    uint8_t period_index; /* 0-15 into NTSC table */
    uint16_t timer;
};

struct apu_dmc {
    bool irq_enable;
    bool loop;
    uint8_t rate_index; /* 0-15 */
    uint16_t timer;
    uint16_t timer_period;
    uint8_t output_level; /* 7-bit, 0-127 */
    uint16_t sample_addr;
    uint16_t sample_len;
    uint16_t cur_addr;
    uint16_t bytes_remaining;
    uint8_t sample_buf;
    bool sample_buf_empty;
    uint8_t shift_reg;
    uint8_t bits_remaining;
    bool silence;
    bool irq_pending;
    bool enabled;
};

struct apu_frame_counter {
    uint32_t cycles;    /* cycles elapsed in current sequence */
    bool mode;          /* false=4-step, true=5-step */
    bool irq_inhibit;
    bool irq_pending;
    bool reload;
    uint8_t reload_delay;
};

struct apu2a03 {
    struct apu_pulse    pulse1;
    struct apu_pulse    pulse2;
    struct apu_triangle triangle;
    struct apu_noise    noise;
    struct apu_dmc      dmc;
    struct apu_frame_counter frame;

    /* Downsampling state */
    float sample_accum;       /* accumulated output this period */
    float sample_cycles;      /* cycles accumulated */
    float cycles_per_sample;  /* APU_CPU_HZ / APU_SAMPLE_RATE */

    /* Per-frame sample buffer (float32, mono, 0.0 to ~1.0) */
    float sample_buf[APU_SAMPLE_BUFFER_SIZE];
    int sample_count;

    bool irq_pending; /* frame counter or DMC IRQ (ORed) */

    int pulse_clock_toggle; /* alternates each CPU cycle for pulse/noise clocking */

    /* CPU memory read callback (for DMC) */
    uint8_t (*cpu_read)(uint16_t addr);
};

void    apu_init(struct apu2a03 *apu, uint8_t (*cpu_read_fn)(uint16_t addr));
void    apu_reset(struct apu2a03 *apu);
void    apu_clock(struct apu2a03 *apu);
void    apu_write(struct apu2a03 *apu, uint16_t addr, uint8_t data);
uint8_t apu_read(struct apu2a03 *apu, uint16_t addr);
bool    apu_irq_pending(struct apu2a03 *apu);
int     apu_get_samples(struct apu2a03 *apu, float **buf_out);
void    apu_clear_samples(struct apu2a03 *apu);

#endif /* __APU_H__ */
