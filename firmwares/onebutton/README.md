# OneButton firmware (CH32V003)

Two firmwares for the OneButton ring board (`../../boards/one_button/`):

| Project | What it is |
|---|---|
| `onebutton_node/` | Ring node: cut-through UART relay + 2× WS2812 + button. Every node runs this identical binary. |
| `onebutton_selftest/` | Single-board bring-up test. Flash this first to prove a board (LEDs, button, SWIO). |

Both are bare-metal [ch32fun](https://github.com/cnlohr/ch32fun) projects. ch32fun
is a **git submodule** at the repo root (`../../ch32fun`), pinned to a known commit —
the stock ch32fun tree is not committed into this repo, only a pointer to it.

## One-time setup on a new machine

1. **Get the submodules** (ch32fun + the SWIO adapter). Either clone with
   `git clone --recursive`, or in an existing clone run:
   ```sh
   git submodule update --init
   ```
   (If you forget, `make` auto-runs `git submodule update --init ch32fun` for you.)

2. **Install the RISC-V toolchain** — the xPack `riscv-none-elf-gcc` (bundles
   newlib; ch32fun auto-detects it). On macOS (Apple Silicon):
   ```sh
   ver=15.2.0-1
   curl -fL "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$ver/xpack-riscv-none-elf-gcc-$ver-darwin-arm64.tar.gz" \
     | tar -xz -C "$HOME/opt"
   # add to PATH (put this in your shell config):
   export PATH="$HOME/opt/xpack-riscv-none-elf-gcc-$ver/bin:$PATH"
   ```
   Linux/Windows: grab the matching asset from the
   [xPack releases](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases).
   > Homebrew's `riscv64-elf-gcc` will **not** work — it ships no C library.

## Build

```sh
cd onebutton_node        # or onebutton_selftest
make                     # -> onebutton_node.bin  (build only, no flash)
make clean
```

## Flash (Pro Micro + Ardulink, no dedicated programmer)

The SWIO adapter firmware lives in `../swio-adapter/` (flash it onto a Pro Micro
first — see that folder's README). Then wire adapter → target and flash:

```
Pro Micro pin 8 (PB4) --[1k]--> CH32V003 SWIO
Pro Micro pin 9 (PB5) ---------> CH32V003 VDD
GND -------> GND
```
```sh
cd onebutton_node
../../../ch32fun/minichlink/minichlink -c /dev/cu.usbmodemXXXX -w onebutton_node.bin flash -b
```
`ls /dev/cu.*` to find the port. `minichlink` builds itself on first use. Every
ring node gets the same `onebutton_node.bin`.

## Notes

- `-march=rv32ec_zicsr` in both Makefiles: the node uses CSR instructions
  (`csrci/csrsi mstatus`), which modern GCC gates behind the `zicsr` extension.
  Kept in both Makefiles so the build pipelines are identical.
- The watchdog (`iwdg_init()`) resets the chip if you pause on a SWIO
  breakpoint — comment it out for single-step debugging.
