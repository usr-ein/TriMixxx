# OneButton Node Bring-Up — CH32V003 first light, Pro Micro flashing, and the all-white WS2812 saga

Validation log for a single **OneButton** ring node: a pre-assembled CH32V003F4P6
board carrying two WS2812 LEDs (data on **PC0**) and one button (**PC1**), destined
to be one of ~50 nodes in a UART cut-through ring for the TriMixxx CDJ project.

This entry covers proving **one** board end-to-end — before any ring, any second
node, or the real protocol. The goal was to remove every single-board unknown so
that the first two-board ring test only has to debug the *protocol*, not the
hardware or the LED timing.

---

## What was validated

| Item | Result | How |
|---|---|---|
| Board accepts a flash | ✅ | Full write cycle over SWIO; proves the SWIO pad, MCU solder joints, and the whole programmer chain |
| Both WS2812 LEDs light | ✅ | Colour cycle test; LED1 lighting proves data passes *through* LED0 down the chain |
| Individual addressing | ✅ | LED0 and LED1 shown different colours simultaneously |
| GRB colour order | ✅ | Red/green/blue render as intended after the RGB→GRB reorder in `ws2812_send` |
| Per-LED brightness | ✅ | Dim-grey phase (0x08) renders dim, not off, not full |
| Button reads | ✅ | PC1 as floating input; external 10k + 100nF + the pin's Schmitt trigger debounce in hardware |
| WS2812 bit timing centred | ✅ | Swept the timing to find the working window, sat in it with margin |
| Timing thermally robust | ✅ | Held glitch-free at **150 °C for 1 minute** on the mostly-zeros pattern |
| printf-over-SWIO debug link | ✅ | Works through the Pro Micro / Ardulink programmer — became the key debug tool |

**Bottom line:** the entire single-node signal chain is proven on real hardware.
The final tuned WS2812 constant is `WS0_HIGH = 8`.

---

## Setup

- **Target:** CH32V003F4P6 (48 MHz QingKe RV32EC, 16 KB flash, 2 KB RAM), whole
  ring runs at **5 V**. The '003's GPIO is 5 V-tolerant, so no level shifting on
  the SWIO or LED lines.
- **Firmware framework:** ch32fun (bare-metal, no HAL).
- **Toolchain (macOS):** `riscv-none-elf-gcc` (xPack), `libusb` (Homebrew), and
  `minichlink` built from the ch32fun repo. Firmware builds to a raw `.bin` —
  minichlink flashes `.bin`, **not** elf/hex.
- **Programmer:** no WCH-LinkE on hand yet, so an **Arduino Pro Micro** was used
  as a SWIO programmer via ch32fun's **Ardulink** backend (see struggles below).
- **Flash command:**
  ```
  minichlink -c /dev/cu.usbmodemXXXX -w firmware.bin flash -b
  ```
- **Debug channel:** `minichlink -c /dev/cu.usbmodemXXXX -T` — printf-over-SWIO,
  which (verified in the minichlink source) rides on the generic register
  read/write that Ardulink implements, so it works through the Pro Micro.

---

## Struggles (the actual story)

### 1. Flashing with a Pro Micro instead of a WCH-LinkE

The WCH-LinkE was ordered but not yet delivered, so the Pro Micro filled in. This
was not plug-and-play:

- The Ardulink **firmware** isn't in ch32fun — it lives in a separate repo
  (`gitlab.com/BlueSyncLine/arduino-ch32v003-swio`), and it's written for an
  Arduino **Uno** (ATmega328P). `ardulink.c` in ch32fun is only the *host* side.
- On the Uno, SWIO is hardcoded to **PB0**, which happens to be pin D8. But on
  the Pro Micro (ATmega32U4), **PB0 is the RX-LED / SS pin and isn't broken out**
  to a header — so the stock firmware can't drive it.
- The fix (a Pro Micro fork) changed several things:
  - SWIO moved **PB0 → PB4** (Pro Micro pin 8).
  - Host protocol moved onto **USB CDC serial** (the 32U4 has native USB) instead
    of the hardware USART.
  - **Power-control instead of reset-control**: the 32U4 doesn't auto-reset on
    DTR the way the Uno does, so the target is power-cycled via a GPIO (PB5/pin 9)
    rather than a DTR-driven RESET line.
  - It also fixed a **real latent bug** in `swio.c`: `DDR &= ~SWIO_BIT` should be
    `DDR &= ~_BV(SWIO_BIT)`. On the Uno's bit 0 this was harmless by accident; on
    any non-zero bit (like PB4) the original would have cleared the wrong pin and
    never released the line. This bug fix is *why* flashing is reliable.
- A **1 kΩ series resistor** on the SWIO line was needed for stability (a known
  Ardulink gotcha).
- **Power:** flash the board from a real 5 V rail, **not** the programmer's GPIO
  power pin — two WS2812s at full white draw ~120 mA, well over a GPIO's ~20 mA.

A successful flash was the first real win: it proves the SWIO pad, the MCU's hand
solder joints, and the entire Pro-Micro→minichlink chain all work.

### 2. The lone board "does nothing"

The **ring firmware** (`onebutton_node.c`) waits for UART frames and relays them,
so on a single board with nothing driving the ring it looks completely dead — no
LED activity. That's expected, not a fault. To make progress on a single board, a
standalone **self-test** was written (LED colour cycle + button), independent of
the ring.

### 3. All-white LEDs, and a button that "did nothing"

The self-test came up showing **constant full white** on both LEDs, and the button
appeared to do nothing. Two hypotheses:

1. **Timing** — the WS2812 bit timing is wrong (zeros read as ones).
2. **Data** — the code is actually sending 0xFF everywhere (a logic bug).

These need completely different fixes, so guessing would waste time.

**printf-over-SWIO settled it.** First, a button-only printf firmware confirmed the
button reads fine on its own — clearing it as a suspect entirely. Then the
self-test was instrumented to print the exact bytes it shifts out. The printout
showed **correct data** (`00 FF 00` for red, `08 08 08` for dim, etc.) while the
LEDs were still white. Correct data + white LEDs = **purely a timing problem**:
the "0" bits were being read as "1".

That single fact also explained the "dead" button: pressing sends magenta
(`FF 00 FF`), which differs from white only in the *green* channel being 0 — so
with zeros-read-as-ones, magenta rendered as white too. The press *was*
registering; white-to-white just isn't visible. **One root cause, both symptoms.**

### 4. The delay loop was lying — optimization-dependent timing

The original bit-timing delay was a C loop (`while (n--) nop;`). Under the build's
`-Os -flto`, the compiler is free to reshape or unroll that loop, so the actual
pulse widths bore **no relation** to the intended counts. Sub-microsecond bit-bang
delays cannot be done with a loop the optimizer is allowed to touch.

Fix, in two steps:
1. Replaced it with a **hand-written asm delay** (deterministic across builds).
2. Then with an **`.rept` nop-sled** for single-cycle (~21 ns) resolution:
   ```c
   #define NOPS_(n) __asm__ volatile (".rept " #n "\n\t nop\n\t .endr\n\t")
   #define NOPS(n)  NOPS_(n)
   ```
   The assembler's `.rept` emits exactly *n* nops at *assembly* time, so `-Os`/
   `-flto` can't retime it, and each step is one CPU cycle.

### 5. Coarse resolution → couldn't centre

With the loop, each iteration was ~3–4 cycles (~65–85 ns), but the WS2812 "0"
tolerance window is only ~150 ns wide — so a single step was nearly the whole
window. Only **two** loop values worked (`1` and `2`), both hugging the edge; there
was no reachable middle.

The nop-sled's 1-cycle steps fixed that. Sweeping `WS0_HIGH`:

- **0 … 16 → clean colour.**
- **17 → white** (the "0" crosses into being read as a "1").
- Going below 0 was impossible (0 is the floor), and 0 still worked — because
  `NOPS(0)` still leaves the pin high for the ~40–60 ns it takes the *next*
  instruction (the store that pulls it low) to run. So the low edge was never
  reached; there's enormous margin on that side.

### 6. Tuning to the safe middle (which isn't the geometric middle)

The window is **asymmetric**: the only reachable failure is the **high cliff at 17**.
And the enemy is **RC-oscillator drift** — the '003's internal oscillator slows as
it warms, which *lengthens* every pulse, pushing the effective value *up* toward
that cliff. So the right place to sit is **well below 17**, not at the midpoint of
`[0, 16]`.

Chosen: **`WS0_HIGH = 8`** — ~9 steps (~190 ns) of headroom under the cliff, while
still a clearly-long-enough "0". `WS1_HIGH` was kept generous at 30; a "1" only
fails by being too *short* (LEDs go dark) and has a huge window up to the ~50 µs
latch gap, so it needs no trimming.

### 7. Heat test — proving the margin

With `WS0_HIGH = 8` on the mostly-zeros pattern (the case nearest the cliff), the
chip was held at **150 °C for one minute** — no glitches, no stray white pixels.
That decisively proves thermal margin: 150 °C is far beyond the enclosure's
expected 40–60 °C, and it's near the chip's absolute-max junction temperature, so
it was a hard stress. Chip-to-chip RC spread across the 50 boards is expected to be
smaller than that thermal swing, so `WS0_HIGH = 8` should absorb part-to-part
variation without per-board tuning.

---

## Key learnings (transferable)

- **Never time a bit-bang with a C loop under `-O`/`-flto`.** The optimizer will
  reshape it and the timing becomes meaningless. Use an `.rept` nop-sled — it's
  resolved at assembly time, immune to the optimizer, and gives 1-cycle resolution.
- **printf-over-SWIO is the debugging keystone.** Printing the exact bytes on the
  wire instantly separated "data wrong" from "timing wrong" and pointed straight
  at the real cause. It works through the Ardulink/Pro-Micro programmer, no
  WCH-LinkE needed.
- **Tune to margin, not to "it works."** Characterise the full window, find which
  edge can actually bite (here: only the high cliff, worsened by thermal drift),
  and sit far from it — not at the geometric middle of a lopsided window.
- **One symptom, one cause.** "All white" *and* "button dead" turned out to be a
  single timing bug (magenta differs from white only in a zero channel). Look for
  the unifying cause before debugging two things.
- **Prove one board before scaling.** The standalone self-test isolated single-node
  behaviour (LEDs, button, timing) from all ring/protocol complexity, so the timing
  was nailed with nothing else in the way.
- **Ardulink on a Pro Micro needs work:** the stock firmware is Uno-only. The
  32U4 needs SWIO on PB4, USB-CDC transport, power-control instead of DTR-reset,
  and a 1 kΩ series resistor on SWIO.

---

## Final validated timing

```c
#define WS0_HIGH  8    // "0" high  -- window 0..16, cliff at 17, sit at 8 (150 C ok)
#define WS1_HIGH  30   // "1" high  -- wide margin, kept generous
#define WS0_LOW   24   // low times are non-critical
#define WS1_LOW   18

#define NOPS_(n) __asm__ volatile (".rept " #n "\n\t nop\n\t .endr\n\t")
#define NOPS(n)  NOPS_(n)
```

These constants, the `NOPS` macro, and `ws2812_send` are now ported verbatim into
`onebutton_node.c`. The **one** difference in the ring firmware: the send runs with
interrupts masked (`irq_off()/irq_on()` in `main`), because the cut-through relay
ISR is live and an interrupt mid-send would stretch a pulse. The pulse timing
itself is identical.

---

## What's next

1. **(done)** Port the validated timing into `onebutton_node.c`, keeping the
   interrupt-masking around the send.
2. **Flash two nodes** with the ring firmware.
3. **Chain them** and drive the ring with the USB-serial adapter as a stand-in
   master — send an ENUM frame, then a DATA frame.
4. **Watch the first relay hop** on the logic analyzer.

That two-board test is where the actual protocol — cut-through relay, positional
enumeration, and the circulating summation frame — finally gets exercised. The
hard, instrument-free part (proving the board and nailing the WS2812 timing by
eye) is now behind us.
