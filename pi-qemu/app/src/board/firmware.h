#pragma once
// The VideoCore firmware's job, done on the host: pick the boot partition
// (autoboot.txt), read config.txt, load the kernel, initramfs and device tree,
// apply overlays and parameters, write the board's own facts into the tree, and
// build the kernel command line. QEMU's raspi4b only boots what it is handed.

#include <QString>
#include <QStringList>

class Card;

struct FirmwareOptions {
    bool    tryboot = false;        // a trial start: the reboot flags' bit 0
    int     requestedPartition = 0; // `reboot N` (the reset status); 0: none
    QString mac;            // eth0's, into the device tree as the firmware writes the real one
    int     fbWidth = 1280, fbHeight = 800, fbDepth = 16;
    QString dtmerge;        // Raspberry Pi's tool, built by pi-qemu/build.sh
    QString runDir;
};

struct BootPlan {
    int         partition = 0;
    QString     kernel, initramfs, dtb, cmdline; // files in the run directory
    QStringList skipped;     // overlays for hardware nothing here emulates
    QStringList notes;       // what the firmware would log on the way
    // The UART the S3 is on: whichever the card's config.txt gives the pins
    // it is wired to (s3UartOf).
    enum class S3Uart { Pl011, Mini, Uart3 };
    S3Uart      s3Uart = S3Uart::Pl011;
    bool        debugConsole = false; // a console on the mini UART (run/console.sock)
    int         bootWatchdog = 0; // kernel_watchdog_timeout: left running at power-on, seconds
};

bool prepareBoot(Card& card, const FirmwareOptions& o, BootPlan* plan, QString* error);

// Which UART a board's device tree, config.txt applied, puts the S3 on: UART3
// (GPIO 4/5, where the decks' S3 has been since its move, pi_config/
// fresh-install.md 1.3) when the tree enables it, else serial0's UART, the
// PL011 or the mini UART on GPIO 14/15, where it was before. Both, unlike a
// deck, so that a card from before the move still has its S3 in the emulator.
BootPlan::S3Uart s3UartOf(const QString& dtb);
