// The boards, from the first site's Hardware page: in Pictures › Boards, each
// render opens the board in 3D (its GLB, from the media repository) and links
// its schematics.
import type { ImageMetadata } from "astro";

import loopRender from "../assets/boards/loop.webp";
import midiLaserRender from "../assets/boards/midi-laser.webp";
import s3Render from "../assets/boards/midi-s3-mini.webp";
import oneButtonRender from "../assets/boards/one-button.webp";
import playCueRender from "../assets/boards/play-cue.webp";
import mainboardRender from "../assets/history/mainboard-3d.webp";

export interface Board {
  id: string;
  name: string;
  role: string;
  status: "current" | "abandoned" | "never built";
  image: ImageMetadata;
  imageAlt: string;
  schematics: readonly { label: string; href: string }[];
  date: string;
  /** The side its render shows. */
  side?: "top" | "bottom";
}

export const boards: readonly Board[] = [
  {
    id: "midi-s3-mini",
    name: "midi_s3_mini",
    role: "The controller. Every control on the deck lands here, and MIDI leaves for the Pi.",
    status: "current",
    image: s3Render,
    imageAlt:
      "A 3D render of the midi_s3_mini board: a green PCB with headers for the LOLIN S3 Mini, a row of white JST connectors along two edges, a large capacitor and a screw terminal.",
    schematics: [
      { label: "Main sheet", href: "/schematics/midi-s3-mini/midi_s3_mini.svg" },
      { label: "Power", href: "/schematics/midi-s3-mini/midi_s3_mini-Power.svg" },
      { label: "Jog board", href: "/schematics/midi-s3-mini/midi_s3_mini-JOGB_ASSY.svg" },
    ],
    date: "Jul 2026",
  },
  {
    id: "one-button",
    name: "one_button",
    role: "A ring node: one pad, two RGB LEDs, one RISC-V microcontroller. Every node is identical.",
    status: "current",
    image: oneButtonRender,
    imageAlt:
      "A 3D render of the OneButton board: a small square green PCB with a tact switch in the middle, two LEDs beside it, a tiny QFN chip and JST connectors on two sides.",
    schematics: [{ label: "Schematic", href: "/schematics/one-button/OneButton.svg" }],
    date: "Jul 2026",
  },
  {
    id: "play-cue",
    name: "play_cue_btn",
    role: "PLAY and CUE, each ringed by four LEDs that Mixxx lights.",
    status: "current",
    image: playCueRender,
    imageAlt:
      "A 3D render of the play/cue board: a bare copper PCB with two tact switches, each surrounded by four red 5 mm LEDs.",
    schematics: [{ label: "Schematic", href: "/schematics/play-cue/play_cue_btn.svg" }],
    date: "Jun 2026",
  },
  {
    id: "loop",
    name: "loop_btn",
    role: "LOOP IN, LOOP OUT and RELOOP, under the jog wheel.",
    status: "current",
    image: loopRender,
    imageAlt:
      "A 3D render of the loop board: a long bare copper PCB with three tact switches and two LEDs.",
    schematics: [{ label: "Schematic", href: "/schematics/loop/loop_btn.svg" }],
    date: "Jun 2026",
    side: "bottom",
  },
  {
    id: "midi-laser",
    name: "midi-laser-pcb",
    role: "The first standalone controller, May 2026: a Pro Micro and an I²C expander. Abandoned.",
    status: "abandoned",
    image: midiLaserRender,
    imageAlt:
      "A 3D render of the midi-laser board: a green PCB with an Arduino Pro Micro footprint, a DIP-28 expander and JST connectors.",
    schematics: [{ label: "Schematic", href: "/schematics/midi-laser/midi-laser-pcb.svg" }],
    date: "May 2026",
  },
  {
    id: "mainboard",
    name: "CDJ-MainBoard",
    role: "The monolithic first design, February to March 2026: everything on one board. Never built.",
    status: "never built",
    image: mainboardRender,
    imageAlt:
      "KiCad's 3D view of the monolithic main board: a long green PCB with RCA jacks, USB ports and the Compute Module 5.",
    schematics: [],
    date: "Mar 2026",
  },
];
