/*#include <stdint.h>
#include "stm32l0xx.h"
int main(void) {
    // 1. Enable Clock for GPIOA
    // Make sure 0x40021000 + 0x20 is the exact RCC enable offset for your MCU!

	RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    //uint32_t *RCCGPIOA = (uint32_t*) (0x40021000 + 0x2C);

    //*RCCGPIOA |= (1u << 0);

    // 2. Configure PA5 as Output Mode
	GPIOA->MODER &= ~GPIO_MODER_MODE5;;
    //uint32_t *GPIOMODE = (uint32_t*) 0x50000000;
    //*GPIOMODE &= ~(3u << 10); // Clear bits 10 and 11
	GPIOA->MODER |= GPIO_MODER_MODE5_0;
    //*GPIOMODE |= (1u << 10);  // Set bit 10 to 1 (Output mode)
	GPIOA->ODR |= GPIO_ODR_OD5;
    // 3. Drive PA5 HIGH using ODR (Offset 0x14)
    //uint32_t *GPIOA_ODR = (uint32_t*) (0x50000000 + 0x14);
    //*GPIOA_ODR |= (1u << 5);


    // 4. Infinite Loop (Prevents main from exiting)
    while (1) {
        // CPU stays here and keeps the LED lit!
    }
}
*/

/*#include <stdint.h>
#include "stm32l0xx.h"

// Simple busy-wait delay (not calibrated to exact ms, just a rough blink rate)
void delay(volatile uint32_t count) {
    while (count--) {
        __NOP();
    }
}

int main(void) {
    // 1. Enable clocks for GPIOA (LED) and GPIOC (button)
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;

    // 2. Configure PA5 (LD2) as output
    GPIOA->MODER &= ~GPIO_MODER_MODE5;      // clear mode bits for pin 5
    GPIOA->MODER |= GPIO_MODER_MODE5_0;     // set to '01' = general purpose output

    // 3. Configure PC13 (B1 button) as input
    // Input mode = '00', which is the reset value, but clear explicitly to be safe
    GPIOC->MODER &= ~GPIO_MODER_MODE13;

    // Nucleo B1 button is wired to pull the pin LOW when pressed,
    // and there's already an external pull-up on the board, so no
    // internal pull-up is strictly needed. But enabling one doesn't hurt:
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD13;
    GPIOC->PUPDR |= GPIO_PUPDR_PUPD13_0;    // '01' = pull-up

    // Start with LED off
    GPIOA->ODR &= ~GPIO_ODR_OD5;

    while (1) {
        // Button pressed => pin reads LOW (active-low)
        if ((GPIOC->IDR & GPIO_IDR_ID13) == 0) {
            // simple debounce
            delay(20000);
            if ((GPIOC->IDR & GPIO_IDR_ID13) == 0) {
                // toggle LED
                GPIOA->ODR ^= GPIO_ODR_OD5;
                delay(200000); // blink rate while held
            }
        }
    }
}*/

#include <stdint.h>
#include "stm32l0xx.h"

/* ---------------------------------------------------------------------
 * Software bit-band style single-bit read.
 *
 * The assignment specifies reading inputs via the Input Data Register
 * using bit banding. True ARM bit-banding is a Cortex-M3/M4/M7 feature;
 * the STM32L053R8 uses a Cortex-M0+ core, which does not implement it.
 * The L0's GPIO also sits at 0x5000_xxxx (AHB2), outside the classic
 * bit-band peripheral window of 0x4000_0000-0x400F_FFFF.
 *
 * This macro therefore reproduces the behaviour of a bit-band read -
 * isolate a single bit and return 0 or 1 - using a shift and mask.
 * ------------------------------------------------------------------- */
#define READ_BIT_BB(REG, BITNUM)   (((REG) >> (BITNUM)) & 0x1UL)

/* LEDs are on PB0, PB1, PB2 and PB10.
 * PB3 and PB4 are avoided: they carry SWO and NJTRST debug functions. */
#define LED_MASK   ((1U<<0) | (1U<<1) | (1U<<2) | (1U<<10))

static void delay(volatile uint32_t count) {
    while (count--) { __NOP(); }
}

/* Free-running counter; its value when a button is pressed acts as
 * the seed, which makes the result unpredictable in practice. */
static volatile uint32_t free_counter = 0;

/* xorshift32 PRNG - the STM32L053R8 has no hardware RNG */
static uint32_t xorshift32(uint32_t seed) {
    uint32_t x = seed ? seed : 1U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

int main(void)
{
    /* ---- Clocks: GPIOB for LEDs + red button, GPIOC for blue button ---- */
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;

    /* ---- LED outputs: PB0, PB1, PB2, PB10 ('01' = output mode) ---- */
    GPIOB->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1 |
                      GPIO_MODER_MODE2 | GPIO_MODER_MODE10);
    GPIOB->MODER |=  (GPIO_MODER_MODE0_0 | GPIO_MODER_MODE1_0 |
                      GPIO_MODER_MODE2_0 | GPIO_MODER_MODE10_0);

    GPIOB->ODR &= ~LED_MASK;                  /* all LEDs off at startup */

    /* ---- Red button on PB5: input with internal pull-up, active-low ---- */
    GPIOB->MODER &= ~GPIO_MODER_MODE5;
    GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD5;
    GPIOB->PUPDR |=  GPIO_PUPDR_PUPD5_0;

    /* ---- Blue button B1 on PC13: on-board pull-up, active-low ---- */
    GPIOC->MODER &= ~GPIO_MODER_MODE13;

    while (1)
    {
        free_counter++;

        /* ============================================================
         * BLUE BUTTON -> display the binary notation of a random number
         * Input read from IDR using the bit-band style macro.
         * ========================================================== */
        if (READ_BIT_BB(GPIOC->IDR, 13) == 0U)          /* pressed */
        {
            delay(20000);                                /* debounce */

            if (READ_BIT_BB(GPIOC->IDR, 13) == 0U)
            {
                uint32_t rnd = xorshift32(free_counter) & 0x0FU;   /* 0-15 */

                /* Map the 4-bit value onto the LED pins.
                 * Bits 0-2 go straight to PB0-PB2; bit 3 moves to PB10. */
                uint32_t pattern = (rnd & 0x7U) | (((rnd >> 3) & 1U) << 10);

                /* OUTPUT via the Output Data Register */
                GPIOB->ODR = (GPIOB->ODR & ~LED_MASK) | pattern;

                /* wait for release so one press gives one number */
                while (READ_BIT_BB(GPIOC->IDR, 13) == 0U) { __NOP(); }
                delay(20000);
            }
        }

        /* ============================================================
         * RED BUTTON -> turn all the LEDs off
         * ========================================================== */
        if (READ_BIT_BB(GPIOB->IDR, 5) == 0U)           /* pressed */
        {
            delay(20000);                                /* debounce */

            if (READ_BIT_BB(GPIOB->IDR, 5) == 0U)
            {
                /* OUTPUT via the Output Data Register */
                GPIOB->ODR &= ~LED_MASK;

                while (READ_BIT_BB(GPIOB->IDR, 5) == 0U) { __NOP(); }
                delay(20000);
            }
        }
    }
}
