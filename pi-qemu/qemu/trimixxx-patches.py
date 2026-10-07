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

# ---- 7. usb-audio named as the deck's DAC ----------------------------------------
# Mixxx's soundconfig.xml picks its device by name: the Behringer UCA222's
# "USB Audio CODEC". ALSA names the card after the USB product string, so the
# emulated card takes that name and the same config works on both. Vendor and
# product IDs stay QEMU's, so no kernel quirk for the real chip applies.
edit("hw/usb/dev-audio.c",
     """    [STRING_MANUFACTURER]       = "QEMU",
    [STRING_PRODUCT]            = "QEMU USB Audio",""",
     """    [STRING_MANUFACTURER]       = "Burr-Brown from TI",
    [STRING_PRODUCT]            = "USB Audio CODEC",""")
# ... and at its rate: soundconfig.xml runs the deck at 44.1 kHz, which the
# UCA222 offers and QEMU's 48 kHz-only card refused. A 1 ms packet is then 44
# or 45 frames, inside the 96-frame maximum the descriptors give.
edit("hw/usb/dev-audio.c",
     "#define USBAUDIO_SAMPLE_RATE     48000",
     "#define USBAUDIO_SAMPLE_RATE     44100")
# Its intake took only packets of exactly 48 frames, and dropped the rest
# without a word: at 44.1 kHz, all of them. It takes any whole-frame packet
# that fits now, wrapping round the ring as it goes.
edit("hw/usb/dev-audio.c",
     """static int streambuf_put(struct streambuf *buf, USBPacket *p, uint32_t channels)
{
    int64_t free = buf->size - (buf->prod - buf->cons);

    if (free < USBAUDIO_PACKET_SIZE(channels)) {
        return 0;
    }
    if (p->iov.size != USBAUDIO_PACKET_SIZE(channels)) {
        return 0;
    }

    /* can happen if prod overflows */
    assert(buf->prod % USBAUDIO_PACKET_SIZE(channels) == 0);
    usb_packet_copy(p, buf->data + (buf->prod % buf->size),
                    USBAUDIO_PACKET_SIZE(channels));
    buf->prod += USBAUDIO_PACKET_SIZE(channels);
    return USBAUDIO_PACKET_SIZE(channels);
}""",
     """static int streambuf_put(struct streambuf *buf, USBPacket *p, uint32_t channels)
{
    int64_t free = buf->size - (buf->prod - buf->cons);
    size_t len = p->iov.size, off, first;

    /*
     * Whole frames, up to the descriptors' maximum: at 44.1 kHz a 1 ms
     * packet is 44 or 45 frames, not the fixed 48 of 48 kHz.
     */
    if (len > USBAUDIO_PACKET_SIZE(channels) || len % (channels * 2) ||
        free < (int64_t)len) {
        return 0;
    }
    off = buf->prod % buf->size;
    first = MIN(len, buf->size - off);
    usb_packet_copy(p, buf->data + off, first);
    if (first < len) {
        usb_packet_copy(p, buf->data, len - first);
    }
    buf->prod += len;
    return len;
}""")

# ---- 8. usb-audio can be saved -----------------------------------------------------
# It was marked unmigratable, which blocks saving the whole machine -- and a
# saved, booted deck is how pi-qemu starts one in seconds. Its state is small:
# the USB device, the alternate setting (the stream on or off) and the volume.
edit("hw/usb/dev-audio.c",
     """    /* properties */
    uint32_t debug;
    uint32_t buffer_user, buffer;
    bool multi;
};""",
     """    uint32_t mig_altset; /* out.altset, for migration */

    /* properties */
    uint32_t debug;
    uint32_t buffer_user, buffer;
    bool multi;
};""")
edit("hw/usb/dev-audio.c",
     """static const VMStateDescription vmstate_usb_audio = {
    .name = TYPE_USB_AUDIO,
    .unmigratable = 1,
};""",
     """static int usb_audio_pre_save(void *opaque)
{
    USBAudioState *s = opaque;

    s->mig_altset = s->out.altset;
    return 0;
}

static int usb_audio_post_load(void *opaque, int version_id)
{
    USBAudioState *s = opaque;

    /*
     * The stream as the guest left it, volume and all; the few
     * milliseconds of samples that were in flight are not kept.
     */
    s->out.altset = ALTSET_OFF;
    usb_audio_set_output_altset(s, s->mig_altset);
    audio_be_set_volume_out(s->audio_be, s->out.voice, &s->out.vol);
    return 0;
}

static const VMStateDescription vmstate_usb_audio = {
    .name = TYPE_USB_AUDIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = usb_audio_pre_save,
    .post_load = usb_audio_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBAudioState),
        VMSTATE_UINT32(mig_altset, USBAudioState),
        VMSTATE_BOOL(out.vol.mute, USBAudioState),
        VMSTATE_UINT8_ARRAY(out.vol.vol, USBAudioState, AUDIO_MAX_CHANNELS),
        VMSTATE_END_OF_LIST()
    }
};""")

# ---- 9. usb-net can be saved -------------------------------------------------------
# The emulator's management NIC, unmigratable too; same reason as 8.
edit("hw/usb/dev-network.c",
     """    enum rndis_state rndis_state;
    uint32_t medium;""",
     """    enum rndis_state rndis_state;
    uint32_t mig_rndis_state; /* rndis_state, for migration */
    uint32_t medium;""")
edit("hw/usb/dev-network.c",
     """static const VMStateDescription vmstate_usb_net = {
    .name = "usb-net",
    .unmigratable = 1,
};""",
     """static int usb_net_pre_save(void *opaque)
{
    USBNetState *s = opaque;

    s->mig_rndis_state = s->rndis_state;
    return 0;
}

static int usb_net_post_load(void *opaque, int version_id)
{
    USBNetState *s = opaque;

    if (s->out_ptr > sizeof(s->out_buf) || s->in_len > sizeof(s->in_buf) ||
        s->in_ptr > s->in_len + 1) {
        return -EINVAL;
    }
    s->rndis_state = s->mig_rndis_state;
    return 0;
}

/*
 * The link as the guest set it up, and the frames half-way through the
 * device. RNDIS control responses not yet collected are not kept: a saved
 * machine is paused, and the guest asks again.
 */
static const VMStateDescription vmstate_usb_net = {
    .name = "usb-net",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = usb_net_pre_save,
    .post_load = usb_net_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBNetState),
        VMSTATE_UINT32(mig_rndis_state, USBNetState),
        VMSTATE_UINT32(medium, USBNetState),
        VMSTATE_UINT32(speed, USBNetState),
        VMSTATE_UINT32(media_state, USBNetState),
        VMSTATE_UINT16(filter, USBNetState),
        VMSTATE_UINT32(vendorid, USBNetState),
        VMSTATE_UINT16(connection, USBNetState),
        VMSTATE_UINT32(out_ptr, USBNetState),
        VMSTATE_BUFFER(out_buf, USBNetState),
        VMSTATE_UINT32(in_ptr, USBNetState),
        VMSTATE_UINT32(in_len, USBNetState),
        VMSTATE_BUFFER(in_buf, USBNetState),
        VMSTATE_END_OF_LIST()
    }
};""")

# ---- 10. qemu-xhci with msix=off can be loaded --------------------------------------
# Loading a saved machine walked the MSI-X vectors of an xHCI that has none
# (msix=off, as the Pi's PCIe root needs) and hit an assertion.
edit("hw/usb/hcd-xhci-pci.c",
     """    PCIDevice *pci_dev = PCI_DEVICE(s);
    int intr;

    for (intr = 0; intr < s->xhci.numintrs; intr++) {""",
     """    PCIDevice *pci_dev = PCI_DEVICE(s);
    int intr;

    if (!msix_present(pci_dev)) { /* msix=off: no vectors to restore */
        return 0;
    }
    for (intr = 0; intr < s->xhci.numintrs; intr++) {""")

# ---- 11. the PCIe root port is saved --------------------------------------------------
# It derives from the abstract pcie-root-port, which declares no saved state,
# so a saved machine silently dropped it: restored, the root port was at power
# on -- no bus numbers, no windows -- and the xHCI behind it unreachable. The
# guest's writes to the USB controller went nowhere and Linux declared it dead.
edit("hw/arm/bcm2838_pcie.c",
     """#include "hw/arm/bcm2838_pcie.h"
#include "trace.h"
""",
     """#include "hw/arm/bcm2838_pcie.h"
#include "hw/pci/pcie_aer.h"
#include "migration/vmstate.h"
#include "trace.h"
""")
edit("hw/arm/bcm2838_pcie.c",
     """static void bcm2838_pcie_root_class_init(ObjectClass *class, const void *data)
{""",
     """/*
 * What the root port has to keep across a saved machine: its config space --
 * the bridge's bus numbers and windows, which Linux programs at boot -- and
 * the Broadcom registers. The abstract pcie-root-port this derives from
 * declares nothing (only the concrete pcie-root-port does), so a restored
 * machine came back with a power-on root port and nothing behind it
 * reachable. Restored before the devices behind it, as pcie-root-port is.
 */
static const VMStateDescription vmstate_bcm2838_pcie_root = {
    .name = "bcm2838-pcie-root",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj.parent_obj,
                           BCM2838PcieRootState),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.parent_obj.exp.aer_log,
                       BCM2838PcieRootState, 0, vmstate_pcie_aer_log,
                       PCIEAERLog),
        VMSTATE_UINT8_ARRAY(regs, BCM2838PcieRootState,
                            BCM2838_PCIE_REGS_SIZE - PCIE_CONFIG_SPACE_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2838_pcie_root_class_init(ObjectClass *class, const void *data)
{""")
edit("hw/arm/bcm2838_pcie.c",
     """    dc->desc = "BCM2711 PCIe Bridge";""",
     """    dc->desc = "BCM2711 PCIe Bridge";
    dc->vmsd = &vmstate_bcm2838_pcie_root;""")

# ---- 12. GENET is saved ------------------------------------------------------------
# The deck's CDJ port declared no saved state either: restored, eth0 was at
# power on under a driver that had set it up.
edit("hw/net/bcm2838_genet.c",
     """#include "hw/net/bcm2838_genet.h"
#include "trace.h"
""",
     """#include "hw/net/bcm2838_genet.h"
#include "migration/vmstate.h"
#include "trace.h"
""")
edit("hw/net/bcm2838_genet.c",
     """static void bcm2838_genet_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    dc->realize = bcm2838_genet_realize;""",
     """static int bcm2838_genet_post_load(void *opaque, int version_id)
{
    BCM2838GenetState *s = opaque;

    /* Frames that arrived while the machine was stopped */
    qemu_flush_queued_packets(qemu_get_queue(s->nic));
    return 0;
}

/*
 * GENET keeps everything in its register blocks (MAC, DMA rings, PHY and
 * its shadow registers): plain data, kept as they are. tx_packet and
 * rx_packet are scratch within one call. Host-endian: a saved machine is
 * restored on the host that saved it.
 */
static const VMStateDescription vmstate_bcm2838_genet = {
    .name = "bcm2838-genet",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = bcm2838_genet_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BUFFER_UNSAFE(regs, BCM2838GenetState, 0,
                              sizeof(BCM2838GenetRegs)),
        VMSTATE_BUFFER_UNSAFE(phy_regs, BCM2838GenetState, 0,
                              sizeof(BCM2838GenetPhyRegs)),
        VMSTATE_BUFFER_UNSAFE(phy_shd_regs, BCM2838GenetState, 0,
                              sizeof(BCM2838GenetPhyShdRegs)),
        VMSTATE_BUFFER_UNSAFE(phy_aux_ctl_shd_regs, BCM2838GenetState, 0,
                              sizeof(BCM2838GenetPhyAuxShdRegs)),
        VMSTATE_BUFFER_UNSAFE(phy_exp_shd_regs, BCM2838GenetState, 0,
                              sizeof(BCM2838GenetPhyExpShdRegs)),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2838_genet_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    dc->realize = bcm2838_genet_realize;
    dc->vmsd = &vmstate_bcm2838_genet;""")

# ---- 13. a USB device keeps its configuration ---------------------------------------
# The generic USB device state had the address and the control transfer, not
# the configuration and alternate settings the guest chose. Upstream's
# migratable devices never look; usb-net does, and dropped every frame of a
# restored machine (no dev->config).
edit("hw/usb/desc.h",
     """void usb_desc_init(USBDevice *dev);
void usb_desc_attach(USBDevice *dev);""",
     """void usb_desc_init(USBDevice *dev);
void usb_desc_attach(USBDevice *dev);
int usb_desc_post_load(USBDevice *dev);""")
edit("hw/usb/desc.c",
     """static int usb_desc_set_config(USBDevice *dev, int value)
{""",
     """/*
 * After loading a saved machine: dev->configuration and dev->altsetting[]
 * came back with it; point the device at those descriptors and rebuild its
 * endpoints. The device model is not told -- its own state came back too.
 */
int usb_desc_post_load(USBDevice *dev)
{
    int i;

    if (!dev->device) {
        return 0;
    }
    dev->config = NULL;
    dev->ninterfaces = 0;
    if (dev->configuration) {
        for (i = 0; i < dev->device->bNumConfigurations; i++) {
            if (dev->device->confs[i].bConfigurationValue ==
                dev->configuration) {
                dev->config = dev->device->confs + i;
                dev->ninterfaces = dev->config->bNumInterfaces;
            }
        }
        if (!dev->config || dev->ninterfaces > USB_MAX_INTERFACES) {
            return -EINVAL;
        }
    }
    for (i = 0; i < USB_MAX_INTERFACES; i++) {
        dev->ifaces[i] = NULL;
        if (i < dev->ninterfaces) {
            dev->ifaces[i] = usb_desc_find_interface(dev, i,
                                                     dev->altsetting[i]);
            if (!dev->ifaces[i]) {
                return -EINVAL;
            }
        }
    }
    usb_desc_ep_init(dev);
    return 0;
}

static int usb_desc_set_config(USBDevice *dev, int value)
{""")
edit("hw/usb/bus.c",
     """#include "migration/vmstate.h"
""",
     """#include "migration/vmstate.h"
#include "desc.h"
""")
edit("hw/usb/bus.c",
     """const VMStateDescription vmstate_usb_device = {
    .name = "USBDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usb_device_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(addr, USBDevice),
        VMSTATE_INT32(state, USBDevice),
        VMSTATE_INT32(remote_wakeup, USBDevice),
        VMSTATE_INT32(setup_state, USBDevice),
        VMSTATE_INT32(setup_len, USBDevice),
        VMSTATE_INT32(setup_index, USBDevice),
        VMSTATE_UINT8_ARRAY(setup_buf, USBDevice, 8),
        VMSTATE_END_OF_LIST(),
    }
};""",
     """static bool usb_device_desc_needed(void *opaque)
{
    USBDevice *dev = opaque;

    return dev->configuration != 0;
}

static int usb_device_desc_post_load(void *opaque, int version_id)
{
    return usb_desc_post_load(opaque);
}

/*
 * The configuration and alternate settings the guest chose. Without them a
 * restored device is unconfigured under a driver that configured it: usb-net
 * then drops every frame (no dev->config), and endpoints lose their types.
 */
static const VMStateDescription vmstate_usb_device_desc = {
    .name = "USBDevice/desc",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = usb_device_desc_needed,
    .post_load = usb_device_desc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(configuration, USBDevice),
        VMSTATE_INT32_ARRAY(altsetting, USBDevice, USB_MAX_INTERFACES),
        VMSTATE_END_OF_LIST(),
    }
};

const VMStateDescription vmstate_usb_device = {
    .name = "USBDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usb_device_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(addr, USBDevice),
        VMSTATE_INT32(state, USBDevice),
        VMSTATE_INT32(remote_wakeup, USBDevice),
        VMSTATE_INT32(setup_state, USBDevice),
        VMSTATE_INT32(setup_len, USBDevice),
        VMSTATE_INT32(setup_index, USBDevice),
        VMSTATE_UINT8_ARRAY(setup_buf, USBDevice, 8),
        VMSTATE_END_OF_LIST(),
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_usb_device_desc,
        NULL
    }
};""")

# ---- 14. the SD card at the Mac's speed ------------------------------------------
# ADMA did 5 descriptors (typically 5 x 4 KiB) per round, then waited on a
# 100 ns timer -- which macOS's main loop serves in milliseconds: ~4 MB/s, a
# tenth of a real card. Nothing about a real card is modelled by it; it only
# keeps one huge transfer from starving the main loop, which 512 still does.
edit("hw/sd/sdhci-internal.h",
     """#define SDHC_ADMA_DESCS_PER_DELAY       5""",
     """#define SDHC_ADMA_DESCS_PER_DELAY       512""")

# ---- 15. the SD card reads ahead and gathers writes ---------------------------------
# The card model read and wrote the image one 512-byte block per blk_pread()/
# blk_pwrite(), each a thread-pool round trip: ~5 MB/s whatever the transfer.
# It reads 256 KiB ahead now, and gathers consecutive written blocks into one
# write that reaches the image before the next command and whenever the
# machine stops (a save, a power-off, quit).
edit("hw/sd/sd.c",
     '#include "qemu/module.h"\n#include "sdmmc-internal.h"',
     '#include "qemu/module.h"\n#include "system/runstate.h"\n#include "sdmmc-internal.h"')
edit("hw/sd/sd.c",
     '    QEMUTimer *ocr_power_timer;\n    uint8_t dat_lines;\n    bool cmd_line;\n    char *preset_auth_key;\n};',
     '    QEMUTimer *ocr_power_timer;\n    uint8_t dat_lines;\n    bool cmd_line;\n    char *preset_auth_key;\n\n    /*\n     * The image in big pieces: read ahead, and consecutive written blocks\n     * gathered into one write. One blk_pread()/blk_pwrite() per 512-byte\n     * block -- a thread-pool round trip each -- made the card a few MB/s.\n     * Gathered blocks reach the image before the next command is handled\n     * and whenever the machine stops, so the guest never sees the difference.\n     */\n    uint8_t *ra_buf;            /* SD_IO_CHUNK bytes; ra_len valid from ra_start */\n    uint64_t ra_start;\n    uint32_t ra_len;\n    uint8_t *wb_buf;            /* written blocks not yet in the image */\n    uint64_t wb_start;\n    uint32_t wb_len;\n    VMChangeStateEntry *vmstate_change;\n};\n\n#define SD_IO_CHUNK (256 * KiB)')
edit("hw/sd/sd.c",
     'static void sd_blk_read(SDState *sd, uint64_t addr, uint32_t len)\n{\n    trace_sdcard_read_block(addr, len);\n    addr += sd_part_offset(sd);\n    if (!sd->blk || blk_pread(sd->blk, addr, len, sd->data, 0) < 0) {\n        fprintf(stderr, "sd_blk_read: read error on host side\\n");\n    }\n}\n\nstatic void sd_blk_write(SDState *sd, uint64_t addr, uint32_t len)\n{\n    trace_sdcard_write_block(addr, len);\n    addr += sd_part_offset(sd);\n    if (!sd->blk || blk_pwrite(sd->blk, addr, len, sd->data, 0) < 0) {\n        fprintf(stderr, "sd_blk_write: write error on host side\\n");\n    }\n}\n\n/* Erase [addr, addr + len): erased blocks read as zeroes */\nstatic void sd_blk_erase(SDState *sd, uint64_t addr, uint64_t len)\n{\n    addr += sd_part_offset(sd);\n    if (!sd->blk ||',
     '/* The gathered blocks into the image (host addresses, partition included) */\nstatic void sd_wb_flush(SDState *sd)\n{\n    if (!sd->wb_len) {\n        return;\n    }\n    if (!sd->blk ||\n        blk_pwrite(sd->blk, sd->wb_start, sd->wb_len, sd->wb_buf, 0) < 0) {\n        fprintf(stderr, "sd_blk_write: write error on host side\\n");\n    }\n    sd->wb_len = 0;\n}\n\n/* What is read ahead of [addr, addr + len) no longer matches the image */\nstatic void sd_ra_drop(SDState *sd, uint64_t addr, uint64_t len)\n{\n    if (sd->ra_len && addr < sd->ra_start + sd->ra_len &&\n        addr + len > sd->ra_start) {\n        sd->ra_len = 0;\n    }\n}\n\nstatic void sd_vm_state_change(void *opaque, bool running, RunState state)\n{\n    if (!running) {\n        sd_wb_flush(opaque);\n    }\n}\n\nstatic void sd_blk_read(SDState *sd, uint64_t addr, uint32_t len)\n{\n    trace_sdcard_read_block(addr, len);\n    addr += sd_part_offset(sd);\n    sd_wb_flush(sd);\n    if (sd->blk && len <= SD_IO_CHUNK) {\n        if (!(sd->ra_len && addr >= sd->ra_start &&\n              addr + len <= sd->ra_start + sd->ra_len)) {\n            int64_t end = blk_getlength(sd->blk);\n            uint64_t n = end > (int64_t)addr ? MIN(SD_IO_CHUNK, end - addr) : 0;\n\n            sd->ra_len = 0;\n            if (n >= len && blk_pread(sd->blk, addr, n, sd->ra_buf, 0) >= 0) {\n                sd->ra_start = addr;\n                sd->ra_len = n;\n            }\n        }\n        if (sd->ra_len) {\n            memcpy(sd->data, sd->ra_buf + (addr - sd->ra_start), len);\n            return;\n        }\n    }\n    if (!sd->blk || blk_pread(sd->blk, addr, len, sd->data, 0) < 0) {\n        fprintf(stderr, "sd_blk_read: read error on host side\\n");\n    }\n}\n\nstatic void sd_blk_write(SDState *sd, uint64_t addr, uint32_t len)\n{\n    trace_sdcard_write_block(addr, len);\n    addr += sd_part_offset(sd);\n    sd_ra_drop(sd, addr, len);\n    if (sd->blk && len <= SD_IO_CHUNK) {\n        if (sd->wb_len && (addr != sd->wb_start + sd->wb_len ||\n                           sd->wb_len + len > SD_IO_CHUNK)) {\n            sd_wb_flush(sd);\n        }\n        if (!sd->wb_len) {\n            sd->wb_start = addr;\n        }\n        memcpy(sd->wb_buf + sd->wb_len, sd->data, len);\n        sd->wb_len += len;\n        return;\n    }\n    if (!sd->blk || blk_pwrite(sd->blk, addr, len, sd->data, 0) < 0) {\n        fprintf(stderr, "sd_blk_write: write error on host side\\n");\n    }\n}\n\n/* Erase [addr, addr + len): erased blocks read as zeroes */\nstatic void sd_blk_erase(SDState *sd, uint64_t addr, uint64_t len)\n{\n    addr += sd_part_offset(sd);\n    sd_wb_flush(sd);\n    sd_ra_drop(sd, addr, len);\n    if (!sd->blk ||')
edit("hw/sd/sd.c",
     '        addr = lduw_be_p(&frame->address) * RPMB_DATA_LEN + sd_part_offset(sd);\n        if (blk_pwrite(sd->blk, addr, RPMB_DATA_LEN, frame->data, 0) < 0) {',
     '        addr = lduw_be_p(&frame->address) * RPMB_DATA_LEN + sd_part_offset(sd);\n        sd_ra_drop(sd, addr, RPMB_DATA_LEN);\n        if (blk_pwrite(sd->blk, addr, RPMB_DATA_LEN, frame->data, 0) < 0) {')
edit("hw/sd/sd.c",
     '    int last_state;\n    sd_rsp_type_t rtype;\n    int rsplen;\n\n    if (!sd->blk || !blk_is_inserted(sd->blk)) {\n        return 0;\n    }',
     '    int last_state;\n    sd_rsp_type_t rtype;\n    int rsplen;\n\n    /* Every command sees the image as the guest last wrote it */\n    sd_wb_flush(sd);\n    if (!sd->blk || !blk_is_inserted(sd->blk)) {\n        return 0;\n    }')
edit("hw/sd/sd.c",
     '    sd->proto = sc->proto;\n    sd->last_cmd_name = "UNSET";\n    sd->ocr_power_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sd_ocr_powerup, sd);\n}\n\nstatic void sd_instance_finalize(Object *obj)\n{\n    SDState *sd = SDMMC_COMMON(obj);\n\n    timer_free(sd->ocr_power_timer);\n}',
     '    sd->proto = sc->proto;\n    sd->last_cmd_name = "UNSET";\n    sd->ocr_power_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sd_ocr_powerup, sd);\n    sd->ra_buf = g_malloc(SD_IO_CHUNK);\n    sd->wb_buf = g_malloc(SD_IO_CHUNK);\n    /* gathered writes reach the image whenever the machine stops */\n    sd->vmstate_change = qemu_add_vm_change_state_handler(sd_vm_state_change, sd);\n}\n\nstatic void sd_instance_finalize(Object *obj)\n{\n    SDState *sd = SDMMC_COMMON(obj);\n\n    qemu_del_vm_change_state_handler(sd->vmstate_change);\n    sd_wb_flush(sd);\n    g_free(sd->ra_buf);\n    g_free(sd->wb_buf);\n    timer_free(sd->ocr_power_timer);\n}')

# ---- 16. the SD card moves a block per call ---------------------------------------
# Multi-block reads and writes (CMD18, CMD25) went one byte per call, each with
# the state checks, a block-layer media check and a trace check again: about
# 75 us a 512-byte block, a few MB/s however the image is read. They move the
# rest of the current block per call now; the callers already loop for more.
edit("hw/sd/sd.c",
     '    case 18:  /* CMD18:  READ_MULTIPLE_BLOCK */\n        /*\n         * We will only read one byte at a time. We will be called again with\n         * the remaining buffer.\n         */\n        length = 1;\n\n        if (sd->data_offset == 0) {\n            if (!address_in_range(sd, "READ_MULTIPLE_BLOCK",\n                                  sd->data_start, io_len)) {\n                *value = dummy_byte;\n                return length;\n            }',
     '    case 18:  /* CMD18:  READ_MULTIPLE_BLOCK */\n        /*\n         * At most the rest of this block; we will be called again with the\n         * remaining buffer. (It was one byte per call, ~75 us a block.)\n         */\n        if (sd->data_offset == 0) {\n            if (!address_in_range(sd, "READ_MULTIPLE_BLOCK",\n                                  sd->data_start, io_len)) {\n                *value = dummy_byte;\n                return 1;\n            }')
edit("hw/sd/sd.c",
     '        *value = sd->data[sd->data_offset++];\n\n        if (sd->data_offset >= io_len) {\n            sd->data_start += io_len;',
     '        length = MIN(length, io_len - sd->data_offset);\n        memcpy(value, sd->data + sd->data_offset, length);\n        sd->data_offset += length;\n\n        if (sd->data_offset >= io_len) {\n            sd->data_start += io_len;')
edit("hw/sd/sd.c",
     '    case 25:  /* CMD25:  WRITE_MULTIPLE_BLOCK */\n        /*\n         * Only read one byte at a time. We will be called again with the\n         * remaining.\n         */\n        length = 1;\n\n        if (sd->data_offset == 0) {',
     '    case 25:  /* CMD25:  WRITE_MULTIPLE_BLOCK */\n        /*\n         * At most the rest of this block; we will be called again with the\n         * remaining. (It was one byte per call.)\n         */\n        if (sd->data_offset == 0) {')
edit("hw/sd/sd.c",
     '        sd->data[sd->data_offset++] = value[0];\n        if (sd->data_offset >= sd->blk_len) {',
     '        length = MIN(length, sd->blk_len - sd->data_offset);\n        memcpy(sd->data + sd->data_offset, value, length);\n        sd->data_offset += length;\n        if (sd->data_offset >= sd->blk_len) {')

# ---- 17. a WAV recording is valid while it is made -------------------------------
# The header's lengths were written only at teardown, which QEMU skips at exit:
# every --audio wav: file said it held nothing, and Python's wave refused it.
edit("audio/wavaudio.c",
     'static size_t wav_write_out(HWVoiceOut *hw, void *buf, size_t len)\n{\n    WAVVoiceOut *wav = (WAVVoiceOut *) hw;',
     'static void wav_store_lengths(WAVVoiceOut *wav);\n\nstatic size_t wav_write_out(HWVoiceOut *hw, void *buf, size_t len)\n{\n    WAVVoiceOut *wav = (WAVVoiceOut *) hw;')
edit("audio/wavaudio.c",
     '    wav->total_samples += bytes / hw->info.bytes_per_frame;\n    return bytes;\n}',
     '    wav->total_samples += bytes / hw->info.bytes_per_frame;\n    if (bytes) {\n        wav_store_lengths(wav);\n    }\n    return bytes;\n}')
edit("audio/wavaudio.c",
     'static int wav_init_out(HWVoiceOut *hw, struct audsettings *as)',
     '/*\n * The RIFF and data lengths in the header, kept right after every write: the\n * file is a valid WAV at every moment. They used to be written only by\n * wav_fini_out(), which QEMU does not run at exit -- the WAV of any run that\n * ended said it held nothing.\n */\nstatic void wav_store_lengths(WAVVoiceOut *wav)\n{\n    uint8_t len[4];\n    uint32_t datalen = wav->total_samples * wav->hw.info.bytes_per_frame;\n\n    le_store(len, datalen + 36, 4);\n    if (fseek(wav->f, 4, SEEK_SET) || fwrite(len, 4, 1, wav->f) != 1) {\n        goto out;\n    }\n    le_store(len, datalen, 4);\n    if (fseek(wav->f, 40, SEEK_SET) || fwrite(len, 4, 1, wav->f) != 1) {\n        goto out;\n    }\nout:\n    if (fseek(wav->f, 0, SEEK_END)) {\n        error_report("wav: fseek to the end failed: %s", strerror(errno));\n    }\n}\n\nstatic int wav_init_out(HWVoiceOut *hw, struct audsettings *as)')

# ---- 18. the reboot flags: tryboot ---------------------------------------------------
# `vcmailbox 0x00038064 4 0 1`, or Linux asked to `reboot "0 tryboot"`, sets bit 0:
# the next start is a trial (pi-qemu/PLAN.md, phase 1, T1 and T3). The firmware
# keeps the flags until that start or a power-off. Here they live in the mailbox's
# property device, and pi-qemu reads them over QMP (`reboot-flags`) when the guest
# resets, then powers on again accordingly. Saved with the machine only while set,
# so snapshots made before this patch still load.
edit("include/hw/misc/bcm2835_property.h",
     "    bool pending;\n};",
     "    bool pending;\n    uint32_t reboot_flags; /* SET_REBOOT_FLAGS; bit 0: tryboot */\n};")
edit("hw/misc/bcm2835_property.c",
     """        default:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: unhandled tag 0x%08x\\n", tag);""",
     """        case RPI_FWREQ_GET_REBOOT_FLAGS:
            stl_le_phys(&s->dma_as, value + 12, s->reboot_flags);
            resplen = 4;
            break;
        case RPI_FWREQ_SET_REBOOT_FLAGS:
            /* A Pi 4 answers 0, as measured on trimixxx3. */
            s->reboot_flags = ldl_le_phys(&s->dma_as, value + 12);
            stl_le_phys(&s->dma_as, value + 12, 0);
            resplen = 4;
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: unhandled tag 0x%08x\\n", tag);""")
edit("hw/misc/bcm2835_property.c",
     """static const VMStateDescription vmstate_bcm2835_property = {
    .name = TYPE_BCM2835_PROPERTY,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_MACADDR(macaddr, BCM2835PropertyState),
        VMSTATE_UINT32(addr, BCM2835PropertyState),
        VMSTATE_BOOL(pending, BCM2835PropertyState),
        VMSTATE_END_OF_LIST()
    }
};""",
     """static bool bcm2835_property_reboot_flags_needed(void *opaque)
{
    BCM2835PropertyState *s = opaque;

    return s->reboot_flags != 0;
}

static const VMStateDescription vmstate_bcm2835_property_reboot_flags = {
    .name = TYPE_BCM2835_PROPERTY "/reboot-flags",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = bcm2835_property_reboot_flags_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(reboot_flags, BCM2835PropertyState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_bcm2835_property = {
    .name = TYPE_BCM2835_PROPERTY,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_MACADDR(macaddr, BCM2835PropertyState),
        VMSTATE_UINT32(addr, BCM2835PropertyState),
        VMSTATE_BOOL(pending, BCM2835PropertyState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_bcm2835_property_reboot_flags,
        NULL
    }
};""")
edit("hw/misc/bcm2835_property.c",
     "    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mbox_irq);\n",
     "    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mbox_irq);\n"
     "    object_property_add_uint32_ptr(obj, \"reboot-flags\", &s->reboot_flags,\n"
     "                                   OBJ_PROP_FLAG_READ);\n")

# ---- 19. the reset status, and the watchdog the firmware leaves running -------------
# `rsts` holds the partition Linux asked for (`reboot N`: partition bit i in RSTS bit
# 2i): readable over QMP, for pi-qemu's next power-on. `boot-watchdog` (seconds) is
# what the firmware does for config.txt's kernel_watchdog_timeout: the watchdog left
# running at power-on, for as long as it counts (about 16 s), so Linux finds it
# running and keeps it fed until watchdog.open_timeout. Pi OS's initramfs (rpi_wd)
# then leaves it armed, and a start that hangs before systemd resets (phase 1, T8).
edit("hw/misc/bcm2835_powermgt.c",
     '#include "system/runstate.h"\n',
     '#include "system/runstate.h"\n#include "hw/core/qdev-properties.h"\n')
edit("include/hw/misc/bcm2835_powermgt.h",
     "    QEMUTimer wdt_timer;\n",
     "    QEMUTimer wdt_timer;\n    uint32_t boot_watchdog; /* seconds; 0: the watchdog starts stopped */\n")
edit("hw/misc/bcm2835_powermgt.c",
     """    timer_init_ns(&s->wdt_timer, QEMU_CLOCK_VIRTUAL,
                  bcm2835_powermgt_wdt_expired, s);
""",
     """    timer_init_ns(&s->wdt_timer, QEMU_CLOCK_VIRTUAL,
                  bcm2835_powermgt_wdt_expired, s);
    object_property_add_uint32_ptr(obj, "rsts", &s->rsts, OBJ_PROP_FLAG_READ);
""")
edit("hw/misc/bcm2835_powermgt.c",
     """    memset(s->asb_regs, 0, sizeof(s->asb_regs));
    timer_del(&s->wdt_timer);
}""",
     """    memset(s->asb_regs, 0, sizeof(s->asb_regs));
    timer_del(&s->wdt_timer);
    if (s->boot_watchdog) {
        s->rstc |= V_RSTC_RESET;
        s->wdog = V_WDOG_TIME_SET;
        bcm2835_powermgt_wdt_arm(s);
    }
}

static const Property bcm2835_powermgt_props[] = {
    DEFINE_PROP_UINT32("boot-watchdog", BCM2835PowerMgtState, boot_watchdog, 0),
};""")
edit("hw/misc/bcm2835_powermgt.c",
     """    device_class_set_legacy_reset(dc, bcm2835_powermgt_reset);
    dc->vmsd = &vmstate_bcm2835_powermgt;""",
     """    device_class_set_legacy_reset(dc, bcm2835_powermgt_reset);
    device_class_set_props(dc, bcm2835_powermgt_props);
    dc->vmsd = &vmstate_bcm2835_powermgt;""")

print("trimixxx-patches: applied")
