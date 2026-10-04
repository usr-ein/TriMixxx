#!/usr/bin/env python3
"""Our changes to QEMU's raspi4b, applied on top of rpi-qemu's patch series.

    trimixxx-patches.py QEMU_SOURCE_DIR

Each change is an exact text replacement that must match once, so a QEMU or
rpi-qemu bump that moves the code fails loudly here instead of building
something half-patched. Found and checked on 2026-10-04 by booting the stock
Raspberry Pi OS trixie card on raspi4b (see ../PLAN.md, section 2):

  1. HVF: run the board on the host's own cores (no EL3/EL2, no Cortex-A72
     model), so an M1 runs the Pi's kernel at native speed.
  2. PCIe: report the link up, route root-port config accesses through the
     bridge's own handlers, and map the outbound window the device tree
     describes (CPU 0x6_0000_0000 -> PCI 0xc000_0000). With all three an
     xHCI on the root port works as the Pi 4's VL805 does.
  3. EMMC2 does ADMA2/SDMA on the GPU bus view, as on a real BCM2711,
     instead of PIO (one VM exit per word under HVF): boot 60 s -> 22 s.
  4. The second GiB of RAM is described to the kernel (it saw ~1 GiB).
  5. The device tree's serial aliases are left as the card's config makes them
     (rpi-qemu forces serial0 onto the PL011; with dtoverlay=disable-bt, as on
     the decks, it already is, and without it the firmware leaves it on the
     mini UART). pi-qemu wires the S3 to whichever UART serial0 names.
  6. HVF dirty tracking marks the host page that holds a write, inside the
     region. It used to mark a whole host page (16 KiB on Apple Silicon) from
     the faulting offset, unaligned and unclamped: the Pi's framebuffer is the
     top of RAM, so as soon as a window watched the screen, a write near the
     end ran past RAM's dirty bitmap and QEMU crashed (SIGSEGV).
"""

import sys
from pathlib import Path

root = Path(sys.argv[1])


def edit(path, old, new):
    p = root / path
    s = p.read_text()
    if s.count(old) != 1:
        sys.exit(f"trimixxx-patches: {path}: expected exactly one match of:\n{old}")
    p.write_text(s.replace(old, new, 1))


# ---- 1. HVF -------------------------------------------------------------------
edit("hw/arm/bcm2836.c",
     '#include "target/arm/gtimer.h"\n',
     '#include "target/arm/gtimer.h"\n#include "system/tcg.h"\n#include "system/qtest.h"\n')
edit("hw/arm/bcm2836.c",
     """    for (int n = 0; n < bc->core_count; n++) {
        object_initialize_child(OBJECT(dev), "cpu[*]", &s->cpu[n].core,
                                bc->cpu_type);
    }""",
     """    /*
     * Under hardware acceleration (HVF, KVM) the guest runs on the host's own
     * cores: there is no Cortex-A72 to model, and no EL3 or EL2 to give it.
     * The kernel then boots at EL1.
     */
    const char *cpu_type = bc->cpu_type;
    if (!tcg_enabled() && !qtest_enabled()) {
        cpu_type = ARM_CPU_TYPE_NAME("host");
    }
    for (int n = 0; n < bc->core_count; n++) {
        object_initialize_child(OBJECT(dev), "cpu[*]", &s->cpu[n].core,
                                cpu_type);
        if (!tcg_enabled() &&
            object_property_find(OBJECT(&s->cpu[n].core), "has_el2")) {
            object_property_set_bool(OBJECT(&s->cpu[n].core), "has_el2",
                                     false, &error_abort);
        }
    }""")
edit("hw/arm/bcm2838.c",
     """        /* set periphbase/CBAR value for CPU-local registers */
        object_property_set_int(OBJECT(&s_base->cpu[n].core), "reset-cbar",
                                bc_base->peri_base, &error_abort);""",
     """        /* set periphbase/CBAR value for CPU-local registers (a modelled
         * Cortex-A72 has it; the host CPU under HVF/KVM does not) */
        if (object_property_find(OBJECT(&s_base->cpu[n].core), "reset-cbar")) {
            object_property_set_int(OBJECT(&s_base->cpu[n].core), "reset-cbar",
                                    bc_base->peri_base, &error_abort);
        }""")

# ---- 2. PCIe ------------------------------------------------------------------
edit("hw/arm/bcm2838_pcie.c",
     '#include "hw/pci-host/gpex.h"\n',
     '#include "hw/pci-host/gpex.h"\n#include "hw/pci/pci_host.h"\n')
edit("hw/arm/bcm2838_pcie.c",
     """static uint32_t bcm2838_pcie_config_read(PCIDevice *d,
                                         uint32_t address, int len)
{
    return pci_default_read_config(d, address, len);
}

static void bcm2838_pcie_config_write(PCIDevice *d, uint32_t addr, uint32_t val,
                                      int len)
{
    return pci_default_write_config(d, addr, val, len);
}

""", "")
edit("hw/arm/bcm2838_pcie.c",
     """        value = pci_default_read_config(PCI_DEVICE(&s->root_port), offset,
                                        size);""",
     """        value = pci_host_config_read_common(PCI_DEVICE(&s->root_port), offset,
                                            PCIE_CONFIG_SPACE_SIZE, size);""")
edit("hw/arm/bcm2838_pcie.c",
     """        pci_default_write_config(PCI_DEVICE(&s->root_port), offset, value,
                                 size);""",
     """        pci_host_config_write_common(PCI_DEVICE(&s->root_port), offset,
                                     PCIE_CONFIG_SPACE_SIZE, value, size);""")
edit("hw/arm/bcm2838_pcie.c",
     """    k->config_read = bcm2838_pcie_config_read;
    k->config_write = bcm2838_pcie_config_write;
""", "")
edit("hw/arm/bcm2838_pcie.c",
     "                  - PCIE_CONFIG_SPACE_SIZE) = BCM2838_PCIE_PCIE_PORT_MASK;",
     """                  - PCIE_CONFIG_SPACE_SIZE) = BCM2838_PCIE_PCIE_PORT_MASK
                                              | BCM2838_PCIE_DL_ACTIVE_MASK
                                              | BCM2838_PCIE_PHYLINKUP_MASK;""")
edit("include/hw/arm/bcm2838_pcie.h",
     "#define BCM2838_PCIE_PCIE_PORT_MASK     0x80    /* Bit 7: 1=RC, 0=EP */\n",
     "#define BCM2838_PCIE_PCIE_PORT_MASK     0x80    /* Bit 7: 1=RC, 0=EP */\n"
     "#define BCM2838_PCIE_DL_ACTIVE_MASK     0x20    /* data link layer active */\n"
     "#define BCM2838_PCIE_PHYLINKUP_MASK     0x10    /* physical link up */\n")
edit("hw/arm/bcm2838_peripherals.c",
     """    /* MMIO region */
    mmio_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie_host), 1);
    memory_region_add_subregion(get_system_memory(), PCIE_MMIO_ARM_OFFSET,
                                mmio_mr);""",
     """    /*
     * MMIO region: the outbound window the device tree describes, CPU
     * 0x6_0000_0000 onto PCI 0xc000_0000. Linux programs BARs with the
     * PCI-side address, so mapping PCI address 0 here leaves every BAR
     * reading all-ones.
     */
    mmio_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie_host), 1);
    memory_region_init_alias(&s->pcie_mmio_alias, OBJECT(s), "pcie-mmio-window",
                             mmio_mr, PCIE_MMIO_OFFSET, PCIE_MMIO_SIZE);
    memory_region_add_subregion(get_system_memory(), PCIE_MMIO_ARM_OFFSET,
                                &s->pcie_mmio_alias);""")
edit("include/hw/arm/bcm2838_peripherals.h",
     "    BCM2838PcieHostState pcie_host;\n",
     "    BCM2838PcieHostState pcie_host;\n    MemoryRegion pcie_mmio_alias;\n")

# ---- 3. EMMC2 DMA -------------------------------------------------------------
edit("hw/arm/bcm2838_peripherals.c",
     """    object_property_set_uint(OBJECT(&s->emmc2), "capareg",
                             BCM2835_SDHC_CAPAREG, &error_abort);""",
     """    /*
     * EMMC2 does SDMA and ADMA2 on a real BCM2711; without them Linux uses
     * PIO. Its DMA addresses are VideoCore bus addresses (dma-ranges map bus
     * 0xc000_0000 onto RAM 0), so it masters the GPU bus view.
     */
    object_property_set_uint(OBJECT(&s->emmc2), "capareg",
                             BCM2835_SDHC_CAPAREG | (1 << 19) | (1 << 22),
                             &error_abort);
    object_property_set_link(OBJECT(&s->emmc2), "dma",
                             OBJECT(&s_base->gpu_bus_mr), &error_abort);""")

# ---- 4. RAM -------------------------------------------------------------------
edit("hw/arm/raspi4b.c",
     "    if (info->ram_size > UPPER_RAM_BASE) {\n",
     "    /* info->ram_size is the boot RAM, below the VideoCore's share of the\n"
     "     * first GiB: compare the board's RAM, or the second GiB is lost. */\n"
     "    if (ram_size > UPPER_RAM_BASE) {\n")

# ---- 5. serial aliases as the card makes them -----------------------------------
edit("hw/arm/raspi4b.c",
     """    uint64_t ram_size = board_ram_size(info->board_id);
    int offset;
""",
     """    uint64_t ram_size = board_ram_size(info->board_id);
""")
edit("hw/arm/raspi4b.c",
     """
    /*
     * Fix serial aliases: ensure PL011 (uart0) is serial0.
     *
     * The Raspberry Pi firmware DTB swaps the serial aliases so that
     * serial0 points to the mini-UART (for the GPIO header console)
     * and serial1 points to the PL011 (used for Bluetooth on real
     * hardware).  QEMU does not emulate Bluetooth and the PL011 is
     * connected to the QEMU serial port, so restore the natural
     * mapping so that console=ttyAMA0 works as expected.
     */
    offset = fdt_path_offset(fdt, "/aliases");
    if (offset >= 0) {
        fdt_setprop_string(fdt, offset,
                           "serial0", "/soc/serial@7e201000");
        fdt_setprop_string(fdt, offset,
                           "serial1", "/soc/serial@7e215040");
    }
}""",
     """}""")

# ---- 6. HVF dirty tracking inside the region --------------------------------------
edit("target/arm/hvf/hvf.c",
     """                if (memory_region_get_dirty_log_mask(mr)) {
                    memory_region_set_dirty(mr, xlat, page_size);""",
     """                if (memory_region_get_dirty_log_mask(mr)) {
                    /* the host page holding the write, and no further than
                     * the region: RAM need not end on a host page */
                    hwaddr start = xlat & page_mask;
                    hwaddr len = MIN((hwaddr)page_size,
                                     memory_region_size(mr) - start);
                    memory_region_set_dirty(mr, start, len);""")

print("trimixxx-patches: applied")
