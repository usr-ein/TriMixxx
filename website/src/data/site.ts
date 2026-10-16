// What every page shares: the name, the pitch, the navigation and the repositories.

export const site = {
  name: "TriMixxx",
  url: "https://trimixxx.e1n.sh",
  tagline: "The open DJ deck",
  description:
    "TriMixxx is an all-in-one DJ player, open at every layer: custom PCBs, an ESP32-S3 controller, a Raspberry Pi running a Mixxx fork, and a Pro DJ Link stack that plays with real CDJs. Not a product: a platform.",
  author: "Samuel Prevost",
  email: "trimixxx@e1n.sh",
  repo: "https://github.com/usr-ein/TriMixxx",
  mixxx: "https://mixxx.org",
  /** Google Analytics' measurement ID (Base.astro loads gtag.js with it). */
  analytics: "G-BL1C2FDXBX",
} as const;

export interface NavItem {
  href: string;
  label: string;
}

export const nav: readonly NavItem[] = [
  { href: "/features/", label: "Features" },
  { href: "/architecture/", label: "Architecture" },
  { href: "/hardware/", label: "Hardware" },
  { href: "/gallery/", label: "Gallery" },
  { href: "/blog/", label: "Build log" },
  { href: "/about/", label: "About" },
];

export interface Repository {
  name: string;
  href: string;
  what: string;
  licence: string;
}

export const repositories: readonly Repository[] = [
  {
    name: "usr-ein/TriMixxx",
    href: "https://github.com/usr-ein/TriMixxx",
    what: "The deck: KiCad boards, firmwares, the Pi's system, the launcher, the deck's Mixxx setup, pi-qemu and this site.",
    licence: "GPL-3.0-or-later",
  },
  {
    name: "usr-ein/mixxx",
    href: "https://github.com/usr-ein/mixxx",
    what: "The Mixxx fork the deck runs, on Mixxx 2.5.6: the CDJ-style browser, waveforms, sync, History, Record set, KEY SYNC.",
    licence: "GPL-2.0-or-later",
  },
  {
    name: "usr-ein/prolink",
    href: "https://github.com/usr-ein/prolink",
    what: "An unofficial Rust implementation of Pro DJ Link, inside the fork: discovery, status, beat sync, NFS and dbserver, both ways.",
    licence: "GPL-3.0-only",
  },
  {
    name: "usr-ein/ttymidi",
    href: "https://github.com/usr-ein/ttymidi",
    what: "The bridge that turns the controller's UART into an ALSA MIDI port, so Mixxx sees an ordinary MIDI device.",
    licence: "GPL-3.0",
  },
  {
    name: "usr-ein/cdj2000-emulator",
    href: "https://github.com/usr-ein/cdj2000-emulator",
    what: "A fork of cdj2k-revival's emulator: a CDJ-2000NXS booting its own firmware in QEMU, for testing without a booth. No firmware included.",
    licence: "GPL-2.0-or-later",
  },
];
