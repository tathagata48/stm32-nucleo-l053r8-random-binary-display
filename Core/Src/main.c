/* =====================================================================
 * LAB 1 - RANDOM BINARY DISPLAY ON LEDs
 * Board: NUCLEO-L053R8 (Cortex-M0+, bare-metal, no HAL)
 *
 * WHAT THIS PROGRAM DOES:
 *   - Blue button (PC13) -> shows a random 4-bit number in binary on 4 LEDs
 *   - Red button  (PB5)  -> turns all LEDs off
 *   - Randomness comes from a free-running counter sampled at the
 *     unpredictable instant the human presses the button.
 * ===================================================================== */

#include <stdint.h>          /* fixed-width integer types: uint32_t, uint8_t */
#include "stm32l0xx.h"       /* CMSIS: register definitions (GPIOB, RCC, etc.) */

/* ---------------------------------------------------------------------
 * BIT-BAND STYLE SINGLE-BIT READ
 * WHY A MACRO, NOT REAL BIT-BANDING?
 *   The assignment says "read via bit-banding." True ARM bit-banding is
 *   ONLY on Cortex-M3/M4/M7. The L053 is Cortex-M0+, which does NOT have
 *   it. Also the L0's GPIO lives at 0x5000_xxxx (AHB2), outside the
 *   bit-band alias region (0x4000_0000-0x400F_FFFF). So I reproduce the
 *   *behaviour* of a bit-band read (isolate one bit, return 0 or 1) using
 *   a shift and a mask - which is what bit-banding does under the hood.
 *
 *   (REG >> BITNUM)  : slide the bit I want down to position 0
 *   & 0x1UL          : throw away every other bit, keep only bit 0
 * If asked "why 0x1?": because I only want ONE bit's value (0 or 1).
 * ------------------------------------------------------------------- */
#define READ_BIT_BB(REG, BITNUM)   (((REG) >> (BITNUM)) & 0x1UL)

/* ---------------------------------------------------------------------
 * LED_MASK - identifies exactly the four LED bits.
 * LEDs are wired to PB0, PB1, PB2, PB10 (physical board wiring).
 * (1U<<n) sets bit n; OR-ing them builds one number with those 4 bits set.
 *   (1U<<0)=0x001, (1U<<1)=0x002, (1U<<2)=0x004, (1U<<10)=0x400
 *   OR together = 0x407.
 * WHY PB10 AND NOT PB3? PB3 = SWO and PB4 = NJTRST are DEBUG pins. When a
 * debugger is attached they stop responding to ODR writes, so I skipped
 * them and jumped to PB10 to keep the LEDs controllable at all times.
 * WHY A MASK AT ALL? So I can change only the LED bits and never disturb
 * the debug pins, the button pin, or anything else on port B.
 * ------------------------------------------------------------------- */
#define LED_MASK   ((1U<<0) | (1U<<1) | (1U<<2) | (1U<<10))

/* ---------------------------------------------------------------------
 * delay() - crude busy-wait used for button debouncing.
 * 'volatile' is ESSENTIAL: without it the compiler sees a loop that does
 * nothing and DELETES it, so the delay vanishes. volatile forces the
 * compiler to run every iteration as written.
 * count-- : test the value, THEN decrement (post-decrement). Loop runs
 * exactly 'count' times, then count hits 0 and the while exits.
 * __NOP() : a "do nothing" instruction - burns one CPU cycle so the loop
 * has a body and takes real time (~3-4 cycles/iteration at 16 MHz).
 * ------------------------------------------------------------------- */
static void delay(volatile uint32_t count) {
    while (count--) { __NOP(); }
}

/* ---------------------------------------------------------------------
 * free_counter - increments once per main-loop pass, forever.
 * 'volatile' because it changes in the background relative to any point
 * where I read it; the compiler must not cache it in a register.
 * WHY IS THIS "RANDOM"? The COUNTER itself is fully deterministic. The
 * randomness is the HUMAN: I cannot press the button at a predictable
 * counter value, so the sampled value is unpredictable in practice.
 * ------------------------------------------------------------------- */
static volatile uint32_t free_counter = 0;

/* ---------------------------------------------------------------------
 * xorshift32 - a pseudo-random number generator.
 * WHY NOT HARDWARE RNG? The L053R8 has no TRNG peripheral, so I generate
 * the number in software.
 * seed ? seed : 1U : if the seed is 0, use 1 instead. xorshift is broken
 * for a zero input (0 stays 0 forever), so this guards against it.
 * The three lines mix the bits: each x ^= x<<13 / >>17 / <<5 folds far-
 * apart bits into each other so the output looks scrambled. 13,17,5 are
 * the well-known Marsaglia constants proven to give a full-period,
 * good-quality sequence for 32-bit xorshift.
 * IS IT CRYPTOGRAPHIC? No - it's deterministic and reversible. Fine here
 * because there is no attacker; I only need it to LOOK random to a human.
 * ------------------------------------------------------------------- */
static uint32_t xorshift32(uint32_t seed) {
    uint32_t x = seed ? seed : 1U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

int main(void)
{
    /* ---- ENABLE PERIPHERAL CLOCKS ----
     * On STM32 every peripheral is clock-gated OFF at reset to save power.
     * If I skip this, EVERY write to GPIOB/GPIOC is silently ignored - the
     * program runs but nothing happens on the pins.
     * IOPENR = I/O Port Enable Register. |= sets the enable bit without
     * disturbing other ports' enable bits.
     * GPIOB: LEDs + red button. GPIOC: blue button B1. */
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;

    /* ---- CONFIGURE LED PINS AS OUTPUTS ----
     * MODER holds 2 bits PER PIN: 00=input, 01=output, 10=alt func, 11=analog.
     * CLEAR-THEN-SET pattern (used everywhere):
     *   Line 1 clears the 2-bit field of each LED pin to 00 (known state).
     *   Line 2 sets the LOW bit of each field to make it 01 (output).
     * WHY CLEAR FIRST? OR can only turn bits ON, never off. If a field were
     * already 10 or 11, OR-ing 01 would give 11 or 11 - wrong. Clearing to
     * 00 first guarantees the field ends up exactly 01.
     * MODEx_0 is the "01" constant for pin x (the low bit of its field). */
    GPIOB->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1 |
                      GPIO_MODER_MODE2 | GPIO_MODER_MODE10);
    GPIOB->MODER |=  (GPIO_MODER_MODE0_0 | GPIO_MODER_MODE1_0 |
                      GPIO_MODER_MODE2_0 | GPIO_MODER_MODE10_0);

    /* ---- ALL LEDs OFF AT STARTUP ----
     * ODR = Output Data Register; each bit = the driven level of one pin.
     * &= ~LED_MASK clears ONLY the 4 LED bits to 0 (0 V), leaving the rest
     * of port B untouched. Defensive: ODR is already 0 after reset, but I
     * state the intent explicitly so no stale pattern can ever show. */
    GPIOB->ODR &= ~LED_MASK;

    /* ---- RED BUTTON PB5: INPUT WITH INTERNAL PULL-UP, ACTIVE-LOW ----
     * MODER cleared -> 00 = input (I want to READ this pin, not drive it).
     * PUPDR holds 2 bits per pin: 00=no pull, 01=pull-up, 10=pull-down.
     *   Clear PUPD5, then set PUPD5_0 (=01) to enable the internal pull-up.
     * WHY PULL-UP? The button connects the pin to GND. With a pull-up the
     * pin idles HIGH (1) and reads LOW (0) only while pressed -> active-low.
     * Without any pull resistor the pin would float and read random noise. */
    GPIOB->MODER &= ~GPIO_MODER_MODE5;
    GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD5;
    GPIOB->PUPDR |=  GPIO_PUPDR_PUPD5_0;

    /* ---- BLUE BUTTON B1 on PC13: INPUT ----
     * Only clear MODER -> input. NO internal pull configured on purpose:
     * the Nucleo board already has an EXTERNAL resistor on B1/PC13, so
     * adding an internal one would fight it. (How did I know? The board
     * user manual / schematic states it - always check the schematic.) */
    GPIOC->MODER &= ~GPIO_MODER_MODE13;

    while (1)
    {
        /* Advance the seed every pass. Sampled only when a button fires. */
        free_counter++;

        /* ============================================================
         * BLUE BUTTON -> display a random number in binary on the LEDs
         * ========================================================== */

        /* First read of PC13 via the bit-band macro. ==0 means LOW = pressed. */
        if (READ_BIT_BB(GPIOC->IDR, 13) == 0U)          /* pressed */
        {
            /* DEBOUNCE: mechanical contacts bounce for a few ms, giving
             * many fake HIGH/LOW edges. Wait for them to settle. */
            delay(20000);

            /* Second read: if STILL low after the wait, it's a real press,
             * not a bounce spike. This double-read IS the debounce. */
            if (READ_BIT_BB(GPIOC->IDR, 13) == 0U)
            {
                /* Make a random 4-bit value (0-15).
                 * xorshift scrambles the counter; & 0x0F keeps the low 4
                 * bits because I have exactly 4 LEDs. */
                uint32_t rnd = xorshift32(free_counter) & 0x0FU;   /* 0-15 */

                /* REMAP the 4 logical bits onto the real, non-contiguous
                 * LED pins. Bits 0-2 already line up with PB0-PB2, so keep
                 * them (& 0x7). Bit 3 must land on PB10, so pull it out
                 * ((rnd>>3)&1) and shift it up to position 10 (<<10).
                 * WHY NOT WRITE rnd DIRECTLY? Bit 3 would hit PB3 (a debug
                 * pin) and PB10 would stay dark - wrong LED, wrong result. */
                uint32_t pattern = (rnd & 0x7U) | (((rnd >> 3) & 1U) << 10);

                /* WRITE via ODR. Read-modify-write:
                 *   (ODR & ~LED_MASK) clears only the 4 LED bits,
                 *   | pattern         sets them to the new value.
                 * I read from ODR (not IDR) because I want the level I last
                 * DROVE, so I can preserve every non-LED bit exactly. */
                GPIOB->ODR = (GPIOB->ODR & ~LED_MASK) | pattern;

                /* WAIT FOR RELEASE so one physical press = one number.
                 * Without this the loop would re-fire thousands of times
                 * while my finger is still down. */
                while (READ_BIT_BB(GPIOC->IDR, 13) == 0U) { __NOP(); }
                delay(20000);          /* debounce the release edge too */
            }
        }

        /* ============================================================
         * RED BUTTON -> clear all LEDs
         * ========================================================== */
        if (READ_BIT_BB(GPIOB->IDR, 5) == 0U)           /* pressed */
        {
            delay(20000);                                /* debounce press */

            if (READ_BIT_BB(GPIOB->IDR, 5) == 0U)
            {
                /* Clear only the LED bits -> all four LEDs off. */
                GPIOB->ODR &= ~LED_MASK;

                while (READ_BIT_BB(GPIOB->IDR, 5) == 0U) { __NOP(); }
                delay(20000);                            /* debounce release */
            }
        }
    }
}
