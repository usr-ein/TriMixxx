// onebutton_selftest.c  -  OneButton board bring-up test (CH32V003F4P6)
// ---------------------------------------------------------------------------
// Standalone. No UART, no ring, no interrupts. Flash this FIRST (Pro Micro +
// Ardulink is fine) to prove one physical board before touching the ring.
//
// It answers, by eye:
//   * Does the board take a flash at all?      -> SWIO pad + MCU joints OK
//   * Do both WS2812s light independently?     -> PC0 routing + chain OK
//   * Are red/green/blue actually right?        -> GRB reorder + nops() timing OK
//   * Does the button read?                     -> PC1 + RC network OK
//
// ws2812_send() and the pin setup are IDENTICAL to onebutton_node.c, so any
// nops() tuning you do here carries straight over to the ring firmware.
//
// What each phase shows (each ~0.6 s):
//   0: LED0 red,   LED1 off    -> LED0 works, first in the chain
//   1: LED0 off,   LED1 red    -> data passed THROUGH LED0 to LED1
//   2: LED0 green, LED1 blue   -> independent colors + correct R/G/B order
//   3: both white              -> all-bits-high timing stress (clean = T1H ok)
//   4: both dim grey           -> zero-bit timing (steady = T0H ok)
// Hold the BUTTON any time -> both LEDs go bright MAGENTA (distinct from every
// phase), so a press is unmistakable. Release -> cycle resumes.
// ---------------------------------------------------------------------------
#include "ch32fun.h"
#include <stdint.h>

#define BTN_PORT  GPIOC
#define BTN_PIN   1            // PC1: switch to GND, external 10k pull-up + 100nF
#define WS_PORT   GPIOC
#define WS_PIN    0            // PC0: WS2812 data out (both LEDs in series)

// ---- WS2812 output: same bit-bang as the ring firmware --------------------
// TIMING-CRITICAL: tune the nop counts by eye. If red looks green (etc.) the
// GRB reorder is wrong; if white flickers/pinks the T1H nops are off.
static inline void nops(int n){ while (n--) __asm__ volatile ("nop"); }

static void ws2812_send(const uint8_t *rgb)   // 6 bytes: R0 G0 B0 R1 G1 B1
{
    uint8_t seq[6] = { rgb[1],rgb[0],rgb[2],    // LED0 -> G R B
                       rgb[4],rgb[3],rgb[5] };   // LED1 -> G R B
    for (int b = 0; b < 6; b++) {
        uint8_t v = seq[b];
        for (int bit = 0; bit < 8; bit++) {
            WS_PORT->BSHR = (1u << WS_PIN);       // HIGH
            if (v & 0x80) { nops(9); WS_PORT->BSHR = (1u << (WS_PIN + 16)); nops(4); }  // T1H
            else          { nops(3); WS_PORT->BSHR = (1u << (WS_PIN + 16)); nops(9); }  // T0H
            v <<= 1;
        }
    }
    // >50us idle low afterwards latches the LEDs (the Delay_Ms below covers it)
}

static uint8_t button_pressed(void)
{
    return ((BTN_PORT->INDR >> BTN_PIN) & 1) ? 0 : 1;   // active-low: low = pressed
}

// fill the 6-byte RGB buffer for a given phase, or magenta if the button is held
static void set_colors(uint8_t *buf, uint8_t phase, uint8_t pressed)
{
    if (pressed) {                              // both LEDs bright magenta
        uint8_t m[6] = {255,0,255, 255,0,255};
        for (int i = 0; i < 6; i++) buf[i] = m[i];
        return;
    }
    static const uint8_t table[5][6] = {
        {255,0,0,   0,0,0  },   // 0: LED0 red,   LED1 off
        {0,0,0,     255,0,0},   // 1: LED0 off,   LED1 red
        {0,255,0,   0,0,255},   // 2: LED0 green, LED1 blue
        {255,255,255, 255,255,255}, // 3: both white
        {8,8,8,     8,8,8  },   // 4: both dim grey
    };
    for (int i = 0; i < 6; i++) buf[i] = table[phase][i];
}

static void gpio_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC;

    // PC0 WS2812 data: push-pull output, 50 MHz  (nibble 0b0011)
    WS_PORT->CFGLR &= ~(0xf << (4*WS_PIN));
    WS_PORT->CFGLR |=  (0x3 << (4*WS_PIN));
    WS_PORT->BSHR   =  (1u << (WS_PIN + 16));   // idle low

    // PC1 button: FLOATING input (nibble 0b0100). External 10k + 100nF own it;
    // no internal pull (it would parallel the 10k and shorten the debounce RC).
    BTN_PORT->CFGLR &= ~(0xf << (4*BTN_PIN));
    BTN_PORT->CFGLR |=  (0x4 << (4*BTN_PIN));
}

int main(void)
{
    SystemInit();                 // 48 MHz
    gpio_init();

    uint8_t phase = 0;
    uint16_t tick = 0;

    for (;;) {
        uint8_t buf[6];
        set_colors(buf, phase, button_pressed());
        ws2812_send(buf);
        Delay_Ms(20);             // refresh rate; also the WS2812 latch gap

        if (++tick >= 30) {       // 30 * 20 ms = ~0.6 s per phase
            tick = 0;
            phase = (phase + 1) % 5;
        }
    }
}
