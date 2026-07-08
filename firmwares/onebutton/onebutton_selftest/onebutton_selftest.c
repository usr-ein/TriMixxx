// onebutton_selftest.c  -  OneButton board bring-up test (CH32V003F4P6)
// ---------------------------------------------------------------------------
// Standalone. No UART, no ring, no interrupts. Watch it over SWIO with:
//     minichlink -c /dev/cu.usbmodemXXXX -T           (keep this open!)
//
// This build is instrumented for debugging the WS2812 timing. For every frame
// it prints the exact bytes it shifts out (already reordered to G R B), so you
// can tell the two failure modes apart:
//
//   * TX shows 00 FF 00 ...  but the LED is WHITE  -> data is right, TIMING is
//         wrong (a "0" is read as a "1"). Fix: lower WS0_HIGH.
//   * TX shows FF FF FF FF FF FF                    -> the DATA is wrong (bug in
//         set_colors / the table). No timing change will help.
//
// Expected TX per phase (G R B for LED0, then LED1):
//   phase 0  red / off   -> TX 00 FF 00  00 00 00
//   phase 1  off / red   -> TX 00 00 00  00 FF 00
//   phase 2  grn / blu   -> TX FF 00 00  00 00 FF
//   phase 3  white       -> TX FF FF FF  FF FF FF
//   phase 4  dim grey    -> TX 08 08 08  08 08 08
//   button down (magenta)-> TX 00 FF FF  00 FF FF
//
// ws2812_send() is left BYTE-FOR-BYTE identical to onebutton_node.c, so the
// WS0_HIGH/WS1_HIGH values you settle on here drop straight into the ring code.
// ---------------------------------------------------------------------------
#include "ch32fun.h"
#include <stdint.h>
#include <stdio.h>

#define BTN_PORT  GPIOC
#define BTN_PIN   1            // PC1: switch to GND, external 10k pull-up + 100nF
#define WS_PORT   GPIOC
#define WS_PIN    0            // PC0: WS2812 data out (both LEDs in series)

// ---- WS2812 bit timing (nop sled, single-cycle resolution) ----------------
// NOPS(n) emits EXACTLY n nop instructions via the assembler's .rept, so the
// count survives -Os/-flto untouched and each step is one CPU cycle (~21 ns
// @48MHz) -- fine enough to sit in the MIDDLE of the tolerance window instead
// of clinging to an edge (the old wdelay loop stepped ~65-85 ns at a time).
//
// WS0_HIGH is the critical one (narrow window). Sweep it (see header):
//   * too LONG  -> a "0" reads as "1": LEDs go WHITE
//   * too SHORT -> the "0" pulse is missed: colors glitch / flicker
// WS1_HIGH ("1" high) has wide margin -- longer is safe up to the ~50us latch
// gap -- so it rarely needs sweeping; just keep it generous.

// Experiments: WS0_HIGH works between [0;16] breaks at 17
#define WS0_HIGH  8    // "0" high  -- CRITICAL, sweep to find the safe middle
#define WS1_HIGH  30   // "1" high  -- wide margin, keep generous
#define WS0_LOW   24   // low times are non-critical
#define WS1_LOW   18

// Emit n literal nops. Two-level macro so NOPS(WS0_HIGH) stringizes the VALUE.
#define NOPS_(n) __asm__ volatile (".rept " #n "\n\t nop\n\t .endr\n\t")
#define NOPS(n)  NOPS_(n)

// PURE: no printf in here, so timing stays identical to the ring firmware.
static void ws2812_send(const uint8_t *rgb)   // 6 bytes: R0 G0 B0 R1 G1 B1
{
    uint8_t seq[6] = { rgb[1],rgb[0],rgb[2],    // LED0 -> G R B
                       rgb[4],rgb[3],rgb[5] };   // LED1 -> G R B
    for (int b = 0; b < 6; b++) {
        uint8_t v = seq[b];
        for (int bit = 0; bit < 8; bit++) {
            // branch first, so the compare is NOT inside the HIGH pulse
            if (v & 0x80) {
                WS_PORT->BSHR = (1u << WS_PIN);          NOPS(WS1_HIGH);
                WS_PORT->BSHR = (1u << (WS_PIN + 16));   NOPS(WS1_LOW);
            } else {
                WS_PORT->BSHR = (1u << WS_PIN);          NOPS(WS0_HIGH);
                WS_PORT->BSHR = (1u << (WS_PIN + 16));   NOPS(WS0_LOW);
            }
            v <<= 1;
        }
    }
    // the Delay_Ms in the main loop provides the >50us idle that latches the LEDs
}

static uint8_t button_pressed(void)
{
    return ((BTN_PORT->INDR >> BTN_PIN) & 1) ? 0 : 1;   // active-low: low = pressed
}

// fill the 6-byte RGB buffer for a phase, or magenta if the button is held
static void set_colors(uint8_t *buf, uint8_t phase, uint8_t pressed)
{
    if (pressed) {                              // both LEDs bright magenta
        uint8_t m[6] = {255,0,255, 255,0,255};
        for (int i = 0; i < 6; i++) buf[i] = m[i];
        return;
    }
    static const uint8_t table[5][6] = {
        {255,0,0,   0,0,0  },       // 0: LED0 red,   LED1 off
        {0,0,0,     255,0,0},       // 1: LED0 off,   LED1 red
        {0,255,0,   0,0,255},       // 2: LED0 green, LED1 blue
        {255,255,255, 255,255,255}, // 3: both white
        {8,8,8,     8,8,8  },       // 4: both dim grey
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

    // PC1 button: FLOATING input (nibble 0b0100). External 10k + 100nF own it.
    BTN_PORT->CFGLR &= ~(0xf << (4*BTN_PIN));
    BTN_PORT->CFGLR |=  (0x4 << (4*BTN_PIN));
}

int main(void)
{
    SystemInit();                 // 48 MHz
    gpio_init();

    printf("\n=== onebutton self-test (printf LED debug) ===\n");
    printf("Compare each TX line (G R B on the wire) with the LEDs.\n");
    printf("WS0_HIGH=%d  WS1_HIGH=%d   (~4s per phase)\n\n", WS0_HIGH, WS1_HIGH);

    uint8_t  phase       = 0;
    uint8_t  lastphase   = 0xff;
    uint8_t  lastpressed = 0xff;
    uint8_t  lastbuf[6]  = {1,1,1,1,1,1};   // mismatch -> force first send
    uint16_t tick        = 0;

    for (;;) {
        uint8_t pressed = button_pressed();
        uint8_t buf[6];
        set_colors(buf, phase, pressed);

        if (phase != lastphase) {
            printf("-- phase %d --\n", phase);
            lastphase = phase;
        }
        if (pressed != lastpressed) {
            printf(pressed ? "[button DOWN -> magenta]\n" : "[button up]\n");
            lastpressed = pressed;
        }

        // Re-send + print ONLY when the frame changes. The WS2812 holds its last
        // value, so this keeps the terminal readable and keeps printf (which can
        // stall waiting on the host) safely OUT of the bit-banged send window.
        int changed = 0;
        for (int i = 0; i < 6; i++) if (buf[i] != lastbuf[i]) changed = 1;
        if (changed) {
            printf("TX %02x %02x %02x  %02x %02x %02x   (G R B per LED)\n",
                   buf[1], buf[0], buf[2], buf[4], buf[3], buf[5]);
            ws2812_send(buf);
            for (int i = 0; i < 6; i++) lastbuf[i] = buf[i];
        }

        Delay_Ms(30);                 // button stays responsive
        if (++tick >= 130) {          // ~4 s per phase
            tick = 0;
            phase = (phase + 1) % 5;
        }
    }
}
