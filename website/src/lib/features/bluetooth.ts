// The Bluetooth receiver: a phone pairs, and while the receiver is open the
// deck is its speaker. The deck's screen shows the phone's track as the deck
// draws it, cover and all; PLAY on the deck pauses the phone and plays it
// again, and loop double skips to its next track. Under the screen, the deck's
// own buttons sit in the order of the page's, previous, play, next.
import { C, type G, type Scene, box, clamp, clear, during, phone, text, topbar } from "./kit";

const LENGTH = 10.5;
const PAIRED = 1.6;
const PAUSE = 3.7;
const RESUME = 5.1;
const NEXT = 6.6;

// The deck's screen, and the page in it.
const SX = 128;
const SY = 18;
const SW = 340;
const SH = 212;

interface Track {
  title: string;
  length: number;
  from: number;
  cover: (i: number, j: number) => string;
}

/** A palette colour, darker: `k` of its light. */
const dark = (hex: string, k: number): string => {
  const n = Number.parseInt(hex.slice(1), 16);
  const part = (shift: number): string => String(Math.round(((n >> shift) & 255) * k));
  return `rgb(${part(16)} ${part(8)} ${part(0)})`;
};

// Covers 20 pixels square: a sun going down over the sea, and a bus at night.
const sunset = (i: number, j: number): string => {
  if (j < 13) {
    if ((i - 9.5) ** 2 + (j - 11.5) ** 2 <= 26) return C.mid;
    if (j < 5) return dark(C.keySync, 0.2 + j * 0.03);
    if (j < 9) return dark(C.pink, 0.36 + (j - 5) * 0.04);
    return dark(C.accent, 0.56 + (j - 9) * 0.07);
  }
  // The sun on the water: broken lines, wider than the sun near it.
  const reach = [7, 0, 5.5, 0, 4, 0, 2.5][j - 13] ?? 0;
  if (Math.abs(i - 9.5) < reach && (i + j) % 5 !== 0) return dark(C.mid, 0.5 - (j - 13) * 0.04);
  return dark(C.pink, 0.24);
};
const STARS = ["3,2", "8,5", "11,1", "18,9", "1,8"];
const nightBus = (i: number, j: number): string => {
  if ((i - 15) ** 2 + (j - 4) ** 2 <= 5) return dark(C.high, 0.92);
  if (STARS.includes(`${String(i)},${String(j)}`)) return dark(C.high, 0.5);
  if (j >= 8 && j <= 14 && i >= 2 && i <= 17) {
    if ((j === 10 || j === 11) && i >= 3 && i <= 16 && i % 3 !== 2) return C.mid;
    return dark(C.cue, 0.8);
  }
  if (j === 15 && (i === 5 || i === 6 || i === 13 || i === 14)) return dark(C.line, 0.6);
  if (j >= 15) return j === 17 && i % 4 < 2 ? dark(C.line, 0.42) : dark(C.line, 0.18);
  return dark(C.low, 0.12 + j * 0.022);
};

const TRACKS: readonly Track[] = [
  { title: "Dusk Run", length: 245, from: 63, cover: sunset },
  { title: "Night Bus", length: 228, from: 0, cover: nightBus },
];

function cover(g: G, x: number, y: number, size: number, paint: Track["cover"]): void {
  const px = size / 20;
  for (let j = 0; j < 20; j++) {
    for (let i = 0; i < 20; i++) {
      g.fillStyle = paint(i, j);
      // A hair over a pixel, so no seams show between them when scaled.
      g.fillRect(x + i * px, y + j * px, px + 0.3, px + 0.3);
    }
  }
}

const clock = (seconds: number): string =>
  `${String(Math.floor(seconds / 60))}:${String(Math.floor(seconds % 60)).padStart(2, "0")}`;

/** The transport icons: previous, play, pause, next. */
function icon(
  g: G,
  kind: "previous" | "play" | "pause" | "next",
  x: number,
  y: number,
  s: number,
  colour: string,
): void {
  g.fillStyle = colour;
  g.beginPath();
  if (kind === "pause") {
    g.rect(x - s * 0.55, y - s * 0.6, s * 0.38, s * 1.2);
    g.rect(x + s * 0.17, y - s * 0.6, s * 0.38, s * 1.2);
  } else if (kind === "play") {
    g.moveTo(x - s * 0.45, y - s * 0.6);
    g.lineTo(x + s * 0.6, y);
    g.lineTo(x - s * 0.45, y + s * 0.6);
  } else {
    const d = kind === "next" ? 1 : -1;
    g.moveTo(x - d * s * 0.5, y - s * 0.6);
    g.lineTo(x + d * s * 0.35, y);
    g.lineTo(x - d * s * 0.5, y + s * 0.6);
    g.rect(d > 0 ? x + s * 0.35 : x - s * 0.55, y - s * 0.6, s * 0.2, s * 1.2);
  }
  g.fill();
}

/** How lit a pressed button still is, `at` seconds after its press. */
const flash = (t: number, at: number): number => (t >= at && t < at + 0.3 ? 1 - (t - at) / 0.3 : 0);

const scene: Scene = {
  length: LENGTH,
  still: 2.9,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    const paired = t >= PAIRED;
    const playing = paired && (t < PAUSE || t >= RESUME);
    const track = t >= NEXT ? TRACKS[1] : TRACKS[0];
    if (!track) return;
    // Where the phone is in its track: it only moves while it plays.
    const since = t >= NEXT ? NEXT : PAIRED;
    const stopped = t >= NEXT || t < PAUSE ? 0 : Math.min(t, RESUME) - PAUSE;
    const position = track.from + Math.max(0, t - since - stopped);

    // The phone: what it connects to, then playing the same thing.
    phone(g, 18, 109, 1.55, C.line);
    if (paired) {
      cover(g, 31, 132, 46, track.cover);
      text(g, track.title, 54, 192, 9, C.text, { align: "center" });
      icon(g, playing ? "pause" : "play", 54, 214, 7, C.text);
    } else {
      text(g, "TRIMIXXX", 54, 172, 9, C.text, { align: "center", weight: 700 });
      g.globalAlpha = fade * (0.35 + 0.65 * clamp(1 - ((t * 3) % 1) * 1.6));
      text(g, "connecting", 54, 186, 9, C.dim, { align: "center" });
      g.globalAlpha = fade;
    }

    // The signal: quick while they pair, slow while it plays, still when paused.
    for (let k = 0; k < 3; k++) {
      const speed = paired ? 1.2 : 3;
      const wave = (t * speed - k / 3) % 1;
      const lit = !paired || playing ? clamp(1 - wave * 1.6) : 0;
      g.globalAlpha = fade * (0.18 + 0.82 * lit);
      g.beginPath();
      g.arc(94, 180, 8 + k * 8, -0.7, 0.7);
      g.strokeStyle = C.low;
      g.lineWidth = 2;
      g.stroke();
    }
    g.globalAlpha = fade;

    // The deck's screen, and the receiver page in it.
    box(g, SX - 6, SY - 6, SW + 12, SH + 12, C.black, C.line, 6);
    g.save();
    g.beginPath();
    g.rect(SX, SY, SW, SH);
    g.clip();
    box(g, SX, SY, SW, SH, C.black);
    topbar(g, SX, SY, SW, ["Bluetooth receiver"]);
    if (!paired) {
      text(g, "Connecting to iPhone…", SX + SW / 2, SY + 118, 12, C.dim, { align: "center" });
    } else {
      cover(g, SX + 14, SY + 42, 112, track.cover);
      const tx = SX + 140;
      text(g, track.title, tx, SY + 58, 17, C.text, { weight: 700 });
      text(g, "The Stand-ins", tx, SY + 80, 12, C.dim);
      text(g, clock(position), tx, SY + 106, 10, C.text);
      box(g, tx + 34, SY + 105, 120, 2, C.ruleStrong);
      box(g, tx + 34, SY + 105, 120 * (position / track.length), 2, C.accent);
      text(g, clock(track.length), SX + SW - 12, SY + 106, 10, C.text, { align: "right" });
      const buttons = [
        { kind: "previous", lit: false, pressed: 0 },
        {
          kind: playing ? "pause" : "play",
          lit: true,
          pressed: Math.max(flash(t, PAUSE), flash(t, RESUME)),
        },
        { kind: "next", lit: false, pressed: flash(t, NEXT) },
      ] as const;
      buttons.forEach((button, i) => {
        const bx = tx + 8 + i * 56;
        box(
          g,
          bx,
          SY + 124,
          46,
          30,
          button.lit ? C.accent : C.rule,
          button.lit ? undefined : C.ruleStrong,
          4,
        );
        if (button.pressed > 0) {
          g.globalAlpha = fade * button.pressed;
          box(g, bx, SY + 124, 46, 30, C.high, undefined, 4);
          g.globalAlpha = fade;
        }
        icon(g, button.kind, bx + 23, SY + 139, 9, button.lit ? C.onAccent : C.text);
      });
      if (playing) {
        g.fillStyle = C.accent;
        g.beginPath();
        g.arc(tx + 4, SY + 176, 3.5, 0, Math.PI * 2);
        g.fill();
        // The page names the device, as the deck does (bluetoothstate.cpp).
        text(g, "Playing from iPhone", tx + 14, SY + 176, 10, C.accent);
      } else {
        text(g, "Connected to iPhone", tx, SY + 176, 10, C.dim);
      }
    }
    g.restore();

    // The deck's buttons, in the order of the page's: loop halve, PLAY, loop
    // double, printed as a CDJ prints them.
    const controls = [
      { label: "LOOP", print: "1/2X", x: SX + 171, pressed: 0, round: false },
      {
        label: "PLAY",
        print: "",
        x: SX + 227,
        pressed: Math.max(flash(t, PAUSE), flash(t, RESUME)),
        round: true,
      },
      { label: "LOOP", print: "2X", x: SX + 283, pressed: flash(t, NEXT), round: false },
    ];
    for (const control of controls) {
      const y = 280;
      const colour = control.pressed > 0 ? C.accent : C.line;
      if (control.round) {
        // PLAY's light says whether the phone plays.
        if (playing) {
          g.beginPath();
          g.arc(control.x, y, 21, 0, Math.PI * 2);
          g.strokeStyle = C.green;
          g.lineWidth = 2;
          g.stroke();
        }
        g.beginPath();
        g.arc(control.x, y, 17, 0, Math.PI * 2);
        g.fillStyle = control.pressed > 0 ? C.accent : C.black;
        g.fill();
        g.strokeStyle = colour;
        g.lineWidth = 1.5;
        g.stroke();
        icon(g, "play", control.x - 5, y, 6, control.pressed > 0 ? C.onAccent : C.text);
        icon(g, "pause", control.x + 6, y, 6, control.pressed > 0 ? C.onAccent : C.text);
      } else {
        box(g, control.x - 17, y - 17, 34, 34, control.pressed > 0 ? C.accent : C.black, colour, 5);
        text(g, control.print, control.x, y + 1, 10, control.pressed > 0 ? C.onAccent : C.text, {
          align: "center",
          weight: 700,
        });
      }
      text(g, control.label, control.x, y + 34, 10, control.pressed > 0 ? C.accent : C.dim, {
        align: "center",
        weight: 700,
      });
    }
  },
};

export default scene;
