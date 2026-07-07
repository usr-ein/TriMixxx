// onebutton_node.c  -  TriMixxx OneButton ring node (CH32V003F4P6)
// ---------------------------------------------------------------------------
// Cut-through UART ring relay + 2x WS2812 + 1 button, ch32v003fun bare-metal.
//
// Ring:  S3(TX) -> node1.RX ; node1.TX -> node2.RX ; ... ; node50.TX -> S3(RX)
// Whole ring is 5V. USART1: RX = PD6 (DIN), TX = PD5 (DOUT). SWIO=PD1 (leave!).
//
// Every node runs the *identical* binary. Position in the chain is discovered
// at boot (ENUM frame), so there is no per-board config.
//
// Frame formats (fixed length, terminated by an inter-frame IDLE gap):
//
//   DATA  [0]=0xA5  [1]=SEQ  [2..351]=50 slots x 7B  [352]=CRC8 (master only)
//         slot i = R0 G0 B0  R1 G1 B1  BTN     (LEDs master->node, BTN node->master)
//   ENUM  [0]=0x5A  [1]=hopcount        (node takes hop as its index, forwards +1)
//
// The node NEVER buffers the 352B frame and NEVER checks CRC. It relays each
// byte the instant it arrives, editing only the 7 bytes in its own slot. The
// master authors SEQ/CRC and verifies the echo end-to-end.
// ---------------------------------------------------------------------------
#include "ch32fun.h"
#include <stdint.h>

// ==== Pins matched to the OneButton board ==================================
#define BTN_PORT   GPIOC
#define BTN_PIN    1            // PC1: switch to GND, external 10k pull-up + 100nF (HW debounce)
#define WS_PORT    GPIOC
#define WS_PIN     0            // PC0: WS2812 data out (drives both LEDs in series)
// (bit-bang on purpose: PC0 is not an SPI MOSI pin, and refresh runs in the
//  idle gap with IRQs masked, so it's deterministic. Tune nops() on a scope.)
// ===========================================================================

#define BAUD        500000u     // 48e6/500000 = 96 exactly -> 0% baud error
#define BRR_DIV     (48000000u / BAUD)

#define FRAME_DATA  0xA5
#define FRAME_ENUM  0x5A
#define HDR         2           // [type][seq]
#define SLOT        7           // 6 LED + 1 button
#define LEVEL       0x01        // btn bit0: currently held
#define STICKY      0x02        // btn bit1: pressed since last report (latched)

// ---- shared state (ISR <-> main) ------------------------------------------
static volatile uint16_t rxpos      = 0;        // byte index in current frame
static volatile uint8_t  synced     = 1;        // 0 = lost byte alignment, wait for gap
static volatile uint8_t  frame_type = 0;
static volatile uint8_t  enumerated = 0;
static volatile uint16_t led_off    = 0xFFFF;   // my first LED byte offset
static volatile uint16_t btn_off    = 0xFFFF;   // my button byte offset
static volatile uint8_t  ledbuf[6]  = {0};      // R0 G0 B0 R1 G1 B1 (snooped)
static volatile uint8_t  btn_state  = 0;        // LEVEL|STICKY
static volatile uint8_t  refresh    = 0;        // set on DATA-frame boundary

// ---- RISC-V global interrupt enable (mstatus.MIE = bit 3) -----------------
// Plain mask/unmask (never nested in this file), used for two tiny critical
// sections: the WS2812 blast, and the btn_state read-modify-write.
static inline void irq_off(void){ __asm__ volatile ("csrci mstatus, 8"); }
static inline void irq_on (void){ __asm__ volatile ("csrsi mstatus, 8"); }

// ===========================================================================
//  THE CUT-THROUGH RELAY  -  one byte in, one byte out, no frame buffering
// ===========================================================================
void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->STATR;

    // ---- a byte arrived (RXNE), or we overran and lost one (ORE) ----------
    if (sr & (USART_STATR_RXNE | USART_STATR_ORE)) {
        uint8_t  in  = USART1->DATAR;   // reading DATAR clears BOTH RXNE and ORE
        uint8_t  out = in;              // default: pass through unchanged
        uint16_t i   = rxpos;

        if (sr & USART_STATR_ORE) {
            // Overrun: at least one byte was missed while we were busy (e.g. the
            // masked LED blast). Our position is now meaningless, so stop
            // snooping/injecting for the rest of this frame. Still relay the
            // byte to keep the line fed; the next IDLE gap re-syncs us.
            synced = 0;
        } else if (synced) {
            if (i == 0) {
                frame_type = in;                    // decide DATA vs ENUM
            } else if (frame_type == FRAME_ENUM) {
                if (i == 1) {                        // this byte = my hop index
                    led_off    = HDR + (uint16_t)in * SLOT;
                    btn_off    = led_off + 6;
                    enumerated = 1;
                    out        = in + 1;             // hand next node its index
                }
            } else { // FRAME_DATA
                if (enumerated) {
                    if (i >= led_off && i < led_off + 6) {
                        ledbuf[i - led_off] = in;    // snoop my LED byte (still relay it)
                    } else if (i == btn_off) {
                        out = btn_state;             // inject my button
                        btn_state &= ~STICKY;        // clear latch: reported once
                    }
                }
            }
        }

        // Bounded spin: RX and TX run the same baud, so by the time a byte has
        // fully arrived the previous one has finished going out. TXE is
        // essentially always ready here; this never blocks more than a bit-time.
        while (!(USART1->STATR & USART_STATR_TXE)) { }
        USART1->DATAR = out;
        rxpos = i + 1;
        return;
    }

    // ---- inter-frame gap = frame boundary, re-arm for next frame ----------
    if (sr & USART_STATR_IDLE) {
        (void)USART1->DATAR;                         // read STATR(above)+DATAR clears IDLE
        if (synced && frame_type == FRAME_DATA && enumerated) refresh = 1;
        synced = 1;                                  // fresh frame: trust offsets again
        rxpos  = 0;
    }
}

// ===========================================================================
//  WS2812 OUTPUT  -  runs only in the idle gap, interrupts masked.
//  TIMING-CRITICAL: tune the TxH/TxL nop counts by eye/scope, OR swap this
//  whole function for a timer+DMA driver for jitter-free output on any pin.
//  Pin your -O level: the nops() loop's cycle count changes between -O0 / -O2.
//  Order on the wire is GRB; ledbuf holds RGB, so we reorder here.
// ===========================================================================
static inline void nops(int n){ while (n--) __asm__ volatile ("nop"); }

static void ws2812_send(const uint8_t *rgb)  // 6 bytes: R0 G0 B0 R1 G1 B1
{
    uint8_t seq[6] = { rgb[1],rgb[0],rgb[2],   // LED0 -> G R B
                       rgb[4],rgb[3],rgb[5] };  // LED1 -> G R B
    for (int b = 0; b < 6; b++) {
        uint8_t v = seq[b];
        for (int bit = 0; bit < 8; bit++) {
            WS_PORT->BSHR = (1u << WS_PIN);            // HIGH
            if (v & 0x80) { nops(9);  WS_PORT->BSHR = (1u << (WS_PIN + 16)); nops(4); }  // T1H~0.7us
            else          { nops(3);  WS_PORT->BSHR = (1u << (WS_PIN + 16)); nops(9); }  // T0H~0.35us
            v <<= 1;
        }
    }
    // latch happens automatically: the rest of the master's idle gap is the >50us low
}

// ---- button: pure edge detect. RC network (10k + 100nF) + the pin's Schmitt
//      trigger already debounce in hardware, so there is nothing to time here.
static void sample_button(void)
{
    static uint8_t last = 0;
    uint8_t level = ((BTN_PORT->INDR >> BTN_PIN) & 1) ? 0 : 1;  // 1 = pressed (active-low)

    irq_off();                                  // btn_state is shared with the ISR
    if (level && !last) btn_state |= STICKY;    // clean press edge -> latch the tap
    if (level) btn_state |= LEVEL;
    else       btn_state &= ~LEVEL;
    irq_on();

    last = level;
}

// ---- independent watchdog: a hung node reboots itself instead of freezing
//      the whole downstream ring. LSI ~128kHz, /16 -> 8kHz, 0xFFF -> ~0.5s.
//      NOTE: comment iwdg_init() out while single-stepping over SWIO, or it
//      will reset the part whenever you pause at a breakpoint.
static void iwdg_init(void)
{
    IWDG->CTLR = 0x5555;        // unlock PSCR/RLDR
    IWDG->PSCR = 2;             // prescaler /16
    IWDG->RLDR = 0x0FFF;        // reload = 4095  -> ~512 ms timeout
    IWDG->CTLR = 0xCCCC;        // start the watchdog
}
static inline void iwdg_feed(void){ IWDG->CTLR = 0xAAAA; }

static void gpio_usart_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_USART1;

    // PD5 = USART1 TX : alt-function push-pull, 50 MHz  (nibble 0b1011)
    GPIOD->CFGLR &= ~(0xf << (4*5));
    GPIOD->CFGLR |=  (0xB << (4*5));
    // PD6 = USART1 RX : input with pull-up  (nibble 0b1000 + OUTDR bit)
    GPIOD->CFGLR &= ~(0xf << (4*6));
    GPIOD->CFGLR |=  (0x8 << (4*6));
    GPIOD->OUTDR |=  (1 << 6);

    // PC1 button: FLOATING input (nibble 0b0100). The external 10k + 100nF fully
    // define the level; an internal pull would just parallel the 10k and shorten
    // the debounce RC. So no pull, no OUTDR write for this pin.
    BTN_PORT->CFGLR &= ~(0xf << (4*BTN_PIN));
    BTN_PORT->CFGLR |=  (0x4 << (4*BTN_PIN));
    // WS2812 data : push-pull output, 50 MHz  (nibble 0b0011)
    WS_PORT->CFGLR &= ~(0xf << (4*WS_PIN));
    WS_PORT->CFGLR |=  (0x3 << (4*WS_PIN));
    WS_PORT->BSHR   =  (1u << (WS_PIN + 16));   // idle low

    USART1->CTLR1 = 0;                          // 8 data bits, no parity
    USART1->CTLR2 = 0;                          // 1 stop bit
    USART1->CTLR3 = 0;
    USART1->BRR   = BRR_DIV;
    USART1->CTLR1 = USART_CTLR1_UE  | USART_CTLR1_TE  | USART_CTLR1_RE
                  | USART_CTLR1_RXNEIE | USART_CTLR1_IDLEIE;

    NVIC_EnableIRQ(USART1_IRQn);
}

int main(void)
{
    SystemInit();                 // 48 MHz
    gpio_usart_init();
    iwdg_init();                  // (comment out for SWIO single-step debugging)
    irq_on();

    for (;;) {
        iwdg_feed();              // superloop is healthy -> pet the dog
        sample_button();

        if (refresh) {
            uint8_t snap[6];
            refresh = 0;
            irq_off();                          // WS2812 needs uninterrupted timing;
            for (int k = 0; k < 6; k++) snap[k] = ledbuf[k];  // safe: we're in the gap
            ws2812_send(snap);
            irq_on();
        }
    }
}
