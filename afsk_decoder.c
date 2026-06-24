/*
 * SMC Audio-Jack AFSK Decoder - ATmega328P bare-metal firmware
 * No Arduino core, no libraries beyond avr-libc. Direct register access.
 *
 * PURPOSE: Decode Bell-202-style AFSK (1200Hz=bit0, 2200Hz=bit1) arriving
 * from the analog front-end (LM393 Schmitt comparator) on ICP1 (PB0/D8
 * on Arduino Uno pinout, but this is NOT written against Arduino libs).
 *
 * SAFETY MODEL: this firmware NEVER outputs a motor command directly.
 * It only ever produces a validated INTENT byte that Layer 1 (the
 * separate motor MCU) independently re-validates and clamps. A bad
 * decode here degrades to "no intent sent" - it cannot fail INTO a
 * dangerous output. See protocol spec: Layer 1 trusts nothing blindly.
 *
 * Toolchain: avr-gcc, avr-libc, avrdude. No IDE required.
 * Target: ATmega328P @ 16MHz (also runs fine at 8MHz internal RC if
 * a crystal isn't available - AFSK timing tolerates several % drift).
 */

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- Bell 202 AFSK timing constants ---- */
/* At 16MHz with Timer1 prescaler /8, each timer tick = 0.5us */
#define TIMER_TICKS_PER_US   2UL

/* Expected half-period (one edge-to-edge interval) in timer ticks.
 * 1200Hz tone: period = 833.3us, half-period = 416.7us = 833 ticks
 * 2200Hz tone: period = 454.5us, half-period = 227.3us = 455 ticks
 * Midpoint threshold to classify an edge interval as "low tone" vs
 * "high tone" - splits the difference on a log-ish scale since these
 * aren't symmetric around a linear midpoint. */
#define TICKS_1200HZ_HALFPERIOD   833
#define TICKS_2200HZ_HALFPERIOD   455
#define TICKS_CLASSIFY_THRESHOLD  644   /* roughly geometric mean */

/* Reject anything outside plausible tone range entirely (not 1200 or
 * 2200, e.g. line noise, a dropped cable, someone's music playing) */
#define TICKS_MIN_VALID    300   /* faster than ~2200Hz upper tolerance */
#define TICKS_MAX_VALID    1100  /* slower than ~1200Hz lower tolerance */

/* Bell 202 runs ~1200 baud -> one bit per ~833us. We sample multiple
 * half-periods per bit and vote, rather than trusting a single edge -
 * this is the core anti-noise measure. */
#define HALFPERIODS_PER_BIT_VOTE  3

/* ---- State ---- */
typedef enum {
    TONE_INVALID = 0,
    TONE_LOW,   /* 1200Hz -> bit 0 */
    TONE_HIGH   /* 2200Hz -> bit 1 */
} tone_t;

static volatile uint16_t last_capture = 0;
static volatile uint16_t last_interval = 0;
static volatile bool     new_edge = false;

/* Sliding vote buffer for noise rejection */
static tone_t vote_buf[HALFPERIODS_PER_BIT_VOTE];
static uint8_t vote_idx = 0;

/* Bit-frame assembly: we're building a byte from decoded bits,
 * matching the INTENT_MOVE payload structure from the CAN protocol */
static uint8_t  rx_byte = 0;
static uint8_t  rx_bit_count = 0;
static bool     frame_active = false;

/* Output: validated bytes ready to hand to the CAN driver. Kept as a
 * tiny ring buffer so the ISR never blocks on a slow consumer. */
#define RXBUF_SIZE 8
static volatile uint8_t rxbuf[RXBUF_SIZE];
static volatile uint8_t rxbuf_head = 0;
static volatile uint8_t rxbuf_tail = 0;

static void rxbuf_push(uint8_t b) {
    uint8_t next = (rxbuf_head + 1) % RXBUF_SIZE;
    if (next == rxbuf_tail) {
        /* Buffer full - drop the byte. Dropping is safer than
         * overwriting silently and corrupting frame sync. The CAN
         * layer above is expected to keep up; if it can't, losing
         * one intent update is harmless (next one arrives in ~833ms
         * at worst, well within human-perceptible control latency
         * budgets for a wheelchair joystick update). */
        return;
    }
    rxbuf[rxbuf_head] = b;
    rxbuf_head = next;
}

bool decoder_rxbuf_pop(uint8_t *out) {
    if (rxbuf_tail == rxbuf_head) return false;
    *out = rxbuf[rxbuf_tail];
    rxbuf_tail = (rxbuf_tail + 1) % RXBUF_SIZE;
    return true;
}

/* ---- Timer1 Input Capture ISR ----
 * Fires on every rising edge of the comparator output on ICP1 (PB0).
 * This is the only place we touch raw timing - everything else
 * downstream works in already-classified tones/bits. */
ISR(TIMER1_CAPT_vect) {
    uint16_t capture = ICR1;
    uint16_t interval = capture - last_capture;
    last_capture = capture;
    last_interval = interval;
    new_edge = true;
}

static tone_t classify_interval(uint16_t ticks) {
    if (ticks < TICKS_MIN_VALID || ticks > TICKS_MAX_VALID) {
        return TONE_INVALID;  /* not a real AFSK edge - reject outright */
    }
    return (ticks > TICKS_CLASSIFY_THRESHOLD) ? TONE_LOW : TONE_HIGH;
}

/* Majority vote across HALFPERIODS_PER_BIT_VOTE samples. Returns
 * TONE_INVALID if the votes don't agree strongly enough - we'd
 * rather report "no decode" than guess wrong on a borderline case. */
static tone_t vote_result(void) {
    uint8_t low_count = 0, high_count = 0, invalid_count = 0;
    for (uint8_t i = 0; i < HALFPERIODS_PER_BIT_VOTE; i++) {
        if (vote_buf[i] == TONE_LOW) low_count++;
        else if (vote_buf[i] == TONE_HIGH) high_count++;
        else invalid_count++;
    }
    if (invalid_count > 0) return TONE_INVALID;  /* any bad sample voids the vote */
    if (low_count == HALFPERIODS_PER_BIT_VOTE) return TONE_LOW;
    if (high_count == HALFPERIODS_PER_BIT_VOTE) return TONE_HIGH;
    return TONE_INVALID;  /* split vote - ambiguous, reject */
}

/* Called from main loop, not ISR context - processes any new edge
 * that the ISR flagged, runs the vote/decode state machine. */
void decoder_poll(void) {
    if (!new_edge) return;

    uint16_t interval;
    /* Atomic read of ISR-shared variable */
    cli();
    interval = last_interval;
    new_edge = false;
    sei();

    tone_t t = classify_interval(interval);
    vote_buf[vote_idx] = t;
    vote_idx = (vote_idx + 1) % HALFPERIODS_PER_BIT_VOTE;

    if (vote_idx != 0) return;  /* wait until vote buffer is full */

    tone_t bit_tone = vote_result();
    if (bit_tone == TONE_INVALID) {
        /* Noise or sync loss - drop any in-progress frame rather than
         * guess. This is the critical safety behavior: ambiguous
         * input produces NO output, never a best-guess output. */
        frame_active = false;
        rx_bit_count = 0;
        rx_byte = 0;
        return;
    }

    uint8_t bit_val = (bit_tone == TONE_HIGH) ? 1 : 0;

    if (!frame_active) {
        /* Looking for start bit (defined as a 0/LOW tone, matching
         * standard async serial framing convention) */
        if (bit_val == 0) {
            frame_active = true;
            rx_bit_count = 0;
            rx_byte = 0;
        }
        return;
    }

    if (rx_bit_count < 8) {
        rx_byte |= (bit_val << rx_bit_count);
        rx_bit_count++;
        return;
    }

    /* 9th bit = parity (even parity over the 8 data bits) */
    uint8_t computed_parity = 0;
    for (uint8_t i = 0; i < 8; i++) computed_parity ^= (rx_byte >> i) & 1;

    if (computed_parity == bit_val) {
        /* Parity checks out - this byte is trustworthy enough to
         * hand upstream. Still just a byte, not a motor command;
         * Layer 1 does its own independent range validation. */
        rxbuf_push(rx_byte);
    }
    /* else: parity mismatch, byte silently dropped - no partial or
     * corrupted data ever reaches the output buffer. */

    frame_active = false;
    rx_bit_count = 0;
    rx_byte = 0;
}

/* ---- Hardware init ---- */
void decoder_init(void) {
    /* PB0 (ICP1) as input, no pull-up needed - external 4.7k pull-up
     * from the LM393 stage already defines the idle-high state */
    DDRB &= ~(1 << PB0);

    /* Timer1: normal mode, prescaler /8 -> 0.5us/tick at 16MHz.
     * Input capture on rising edge, noise canceler enabled (extra
     * real-world robustness on top of our own vote-based rejection -
     * belt and suspenders, both layers independently justified). */
    TCCR1A = 0;
    TCCR1B = (1 << ICNC1) | (1 << ICES1) | (1 << CS11);
    TIMSK1 = (1 << ICIE1);

    vote_idx = 0;
    frame_active = false;
    rx_bit_count = 0;

    sei();
}

#ifdef DECODER_STANDALONE_MAIN
int main(void) {
    decoder_init();
    uint8_t b;
    for (;;) {
        decoder_poll();
        if (decoder_rxbuf_pop(&b)) {
            /* In the real build: hand this byte to the CAN driver
             * (INTENT_MOVE / INTENT_STOP framing per protocol spec).
             * Left as a stub here since this file's job is decode
             * correctness, not the CAN transport layer. */
        }
    }
}
#endif
