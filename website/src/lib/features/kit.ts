// What the "What can it do?" animations are drawn with: the deck's colours
// and font, its seven segments, its rows, a three-band waveform, and the
// outlines of the things in the stories (the deck, a CDJ, a stick, a phone).
// Every scene draws in the same 480 × 360 space; the runner scales it.

export const W = 480;
export const H = 360;
export const FONT = "Meslo, Menlo, monospace";

export const C = {
  black: "#000000",
  ground: "#0c0c0c",
  row: "#0a0a0a",
  bar: "#111312",
  rule: "#1c1c1c",
  ruleStrong: "#2a2a2a",
  text: "#e0e0e0",
  dim: "#909090",
  faint: "#5a5a5a",
  accent: "#ff7092",
  onAccent: "#0c0c0c",
  low: "#2266ff",
  mid: "#ffcc00",
  high: "#ffffff",
  beat: "#cccccc",
  downbeat: "#ff0000",
  cue: "#ff0033",
  segment: "#ff8800",
  range: "#66cc66",
  keySync: "#b06cff",
  pink: "#e2559a",
  green: "#44ff88",
  line: "#a3a3a3",
  cdj: "#7f8ea0",
} as const;

/** A scene: a loop `length` seconds long; `still` is the one moment that tells it alone. */
export interface Scene {
  length: number;
  still: number;
  draw(g: CanvasRenderingContext2D, t: number): void;
}

export type G = CanvasRenderingContext2D;

// ---- Time ------------------------------------------------------------------

export const clamp = (x: number, a = 0, b = 1): number => Math.max(a, Math.min(b, x));
export const lerp = (a: number, b: number, k: number): number => a + (b - a) * k;
export const ease = (t: number): number => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2);
/** How far through [a, b] t is, 0 to 1, eased. */
export const span = (t: number, a: number, b: number): number => ease(clamp((t - a) / (b - a)));
/** 1 inside [a, b], 0 outside, with `fade` seconds of fade at each end. */
export const during = (t: number, a: number, b: number, fade = 0.25): number =>
  clamp(Math.min((t - a) / fade, (b - t) / fade));

// ---- Drawing -----------------------------------------------------------------

export function clear(g: G, colour: string = C.black): void {
  g.fillStyle = colour;
  g.fillRect(0, 0, W, H);
}

export function text(
  g: G,
  value: string,
  x: number,
  y: number,
  size: number,
  colour: string = C.text,
  options: { weight?: 400 | 700; align?: CanvasTextAlign; base?: CanvasTextBaseline } = {},
): void {
  g.font = `${String(options.weight ?? 400)} ${String(size)}px ${FONT}`;
  g.fillStyle = colour;
  g.textAlign = options.align ?? "left";
  g.textBaseline = options.base ?? "middle";
  g.fillText(value, x, y);
}

export function box(
  g: G,
  x: number,
  y: number,
  w: number,
  h: number,
  fill?: string,
  stroke?: string,
  r = 0,
  width = 1.5,
): void {
  g.beginPath();
  g.roundRect(x, y, w, h, r);
  if (fill) {
    g.fillStyle = fill;
    g.fill();
  }
  if (stroke) {
    g.strokeStyle = stroke;
    g.lineWidth = width;
    g.stroke();
  }
}

export function circle(
  g: G,
  x: number,
  y: number,
  r: number,
  fill?: string,
  stroke?: string,
  width = 1.5,
): void {
  g.beginPath();
  g.arc(x, y, r, 0, Math.PI * 2);
  if (fill) {
    g.fillStyle = fill;
    g.fill();
  }
  if (stroke) {
    g.strokeStyle = stroke;
    g.lineWidth = width;
    g.stroke();
  }
}

export function line(
  g: G,
  x1: number,
  y1: number,
  x2: number,
  y2: number,
  colour: string,
  width = 1.5,
): void {
  g.beginPath();
  g.moveTo(x1, y1);
  g.lineTo(x2, y2);
  g.strokeStyle = colour;
  g.lineWidth = width;
  g.stroke();
}

/** The deck's breadcrumb: the house, then where you are. */
export function topbar(g: G, x: number, y: number, w: number, crumbs: readonly string[]): void {
  box(g, x, y, w, 26, C.bar);
  line(g, x, y + 26, x + w, y + 26, "#1e201f", 1);
  g.fillStyle = C.accent;
  g.beginPath();
  const hx = x + 12;
  const hy = y + 13;
  g.moveTo(hx, hy - 7);
  g.lineTo(hx + 7, hy - 1);
  g.lineTo(hx + 5, hy - 1);
  g.lineTo(hx + 5, hy + 6);
  g.lineTo(hx - 5, hy + 6);
  g.lineTo(hx - 5, hy - 1);
  g.lineTo(hx - 7, hy - 1);
  g.closePath();
  g.fill();
  let cx = x + 26;
  for (const crumb of crumbs) {
    text(g, "›", cx, y + 13, 12, "rgb(255 112 146 / 50%)");
    cx += 12;
    text(g, crumb, cx, y + 13, 12, C.accent, { weight: 700 });
    g.font = `700 12px ${FONT}`;
    cx += g.measureText(crumb).width + 10;
  }
}

/** A row of a deck list; with `sub`, a second, dimmer line under the label. */
export function row(
  g: G,
  x: number,
  y: number,
  w: number,
  h: number,
  label: string,
  meta = "",
  selected = false,
  sub = "",
): void {
  box(g, x, y, w, h, selected ? C.accent : C.row);
  line(g, x, y + h, x + w, y + h, C.rule, 1);
  const labelY = sub ? y + h * 0.36 : y + h / 2;
  text(g, label, x + 12, labelY, 13, selected ? C.onAccent : C.text);
  if (sub) text(g, sub, x + 12, y + h * 0.74, 9, selected ? C.onAccent : C.faint);
  if (meta)
    text(g, meta, x + w - 10, y + h / 2, 11, selected ? C.onAccent : C.dim, { align: "right" });
}

const GLYPHS: Record<string, string> = {
  "0": "abcdef",
  "1": "bc",
  "2": "abged",
  "3": "abgcd",
  "4": "fgbc",
  "5": "afgcd",
  "6": "afgedc",
  "7": "abc",
  "8": "abcdefg",
  "9": "abcdfg",
  "-": "g",
  " ": "",
};

/** Seven-segment digits, as on the deck's tempo display. `h` is a digit's height. */
export function segments(
  g: G,
  value: string,
  x: number,
  y: number,
  h: number,
  colour: string = C.segment,
): void {
  const w = h * 0.55;
  const t = h * 0.12;
  const pitch = w + h * 0.18;
  let cx = x;
  // Unlit segments show faintly, as on an LCD; both follow the scene's fade.
  const fade = g.globalAlpha;
  g.save();
  g.transform(1, 0, -0.12, 1, 0, 0);
  for (const char of value) {
    if (char === ".") {
      g.fillStyle = colour;
      g.fillRect(cx - pitch * 0.12, y + h - t, t, t);
      cx += pitch * 0.25;
      continue;
    }
    const lit = GLYPHS[char] ?? "";
    const parts: Record<string, [number, number, number, number]> = {
      a: [cx + t, y, w - 2 * t, t],
      g: [cx + t, y + h / 2 - t / 2, w - 2 * t, t],
      d: [cx + t, y + h - t, w - 2 * t, t],
      f: [cx, y + t, t, h / 2 - 1.5 * t + t / 2],
      b: [cx + w - t, y + t, t, h / 2 - 1.5 * t + t / 2],
      e: [cx, y + h / 2 + t / 2, t, h / 2 - 1.5 * t],
      c: [cx + w - t, y + h / 2 + t / 2, t, h / 2 - 1.5 * t],
    };
    for (const [name, [px, py, pw, ph]] of Object.entries(parts)) {
      g.globalAlpha = fade * (lit.includes(name) ? 1 : 0.08);
      g.fillStyle = colour;
      g.fillRect(px, py, pw, ph);
    }
    g.globalAlpha = fade;
    cx += pitch;
  }
  g.restore();
}

// ---- A track ---------------------------------------------------------------------

/** A seeded random, so every loop draws the same track. */
export function random(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let q = s;
    q = Math.imul(q ^ (q >>> 15), q | 1);
    q ^= q + Math.imul(q ^ (q >>> 7), q | 61);
    return ((q ^ (q >>> 14)) >>> 0) / 4294967296;
  };
}

/**
 * A made-up track's three bands at time `s` (seconds), 0 to 1, at `bpm`:
 * a kick on every beat and a bassline under it (lows), chords that come and
 * go (mids), hats on the off-beats (highs). Drawn stacked, that is the deck's
 * look: blue around yellow around a white core.
 */
export function bandsAt(s: number, seed: number, bpm = 128): [number, number, number] {
  const beat = 60 / bpm;
  const inBeat = (((s % beat) + beat) % beat) / beat;
  const n = Math.floor(s / beat);
  const r = random(seed * 9973 + n)();
  const kick = Math.exp(-inBeat * 4.2) * (0.62 + r * 0.2);
  const bass = 0.3 + 0.12 * Math.sin(inBeat * Math.PI * 2 + seed);
  const off = Math.abs(inBeat - 0.5) < 0.16 ? 1 - Math.abs(inBeat - 0.5) / 0.16 : 0;
  const bar = Math.floor(n / 4) % 4;
  const chord =
    (0.14 + 0.2 * Math.exp(-((inBeat * 4) % 1) * 3)) * (bar === 1 || bar === 3 ? 1 : 0.65);
  return [
    Math.min(1, kick + bass),
    chord + 0.06,
    0.06 + off * 0.16 + 0.04 * random(seed + n * 31)(),
  ];
}

/**
 * A strip of three-band waveform, `seconds` across, centred on `position`,
 * with the beat grid over it and the playhead in the middle.
 */
export function waveform(
  g: G,
  x: number,
  y: number,
  w: number,
  h: number,
  position: number,
  options: {
    seed?: number;
    bpm?: number;
    seconds?: number;
    grid?: boolean;
    playhead?: boolean;
    phase?: number;
  } = {},
): void {
  const { seed = 1, bpm = 128, seconds = 4, grid = true, playhead = true, phase = 0 } = options;
  g.save();
  g.beginPath();
  g.rect(x, y, w, h);
  g.clip();
  box(g, x, y, w, h, C.black);
  const mid = y + h / 2;
  const colours = [C.low, C.mid, C.high];
  const gains = [1, 0.78, 0.5];
  for (let b = 0; b < 3; b++) {
    g.beginPath();
    g.moveTo(x, mid);
    const top: [number, number][] = [];
    for (let i = 0; i <= w; i += 2) {
      const s = position - seconds / 2 + (i / w) * seconds - phase;
      const v = s < 0 ? 0 : (bandsAt(s, seed, bpm)[b] ?? 0) * (gains[b] ?? 1);
      top.push([x + i, v]);
      g.lineTo(x + i, mid - v * h * 0.46);
    }
    for (let i = top.length - 1; i >= 0; i--) {
      const point = top[i];
      if (point) g.lineTo(point[0], mid + point[1] * h * 0.46);
    }
    g.closePath();
    g.fillStyle = colours[b] ?? C.high;
    g.fill();
  }
  if (grid) {
    const beat = 60 / bpm;
    const start = position - seconds / 2 - phase;
    for (let n = Math.ceil(start / beat); n * beat <= start + seconds; n++) {
      if (n < 0) continue;
      const bx = x + ((n * beat - start) / seconds) * w;
      g.fillStyle = n % 4 === 0 ? C.downbeat : "rgb(204 204 204 / 85%)";
      g.fillRect(bx - 1, y, 2, h);
    }
  }
  if (playhead) {
    g.fillStyle = C.high;
    g.fillRect(x + w / 2 - 1, y, 2, h);
  }
  g.restore();
}

// ---- Things --------------------------------------------------------------------------
//
// Drawn at a scale `s`, their lines keep the scene's widths: a stroke of w is
// drawn as w / s inside the scaled outline.

/** The TriMixxx seen from above, in lines: its screen, jog, pads, CUE and PLAY, fader. */
export function deck(
  g: G,
  x: number,
  y: number,
  s: number,
  options: { screen?: string; colour?: string } = {},
): void {
  const colour = options.colour ?? C.line;
  const k = 1 / s;
  g.save();
  g.translate(x, y);
  g.scale(s, s);
  box(g, 0, 0, 100, 140, C.black, colour, 7, 1.5 * k);
  box(g, 8, 6, 84, 40, options.screen ?? "#050505", colour, 2, 1.5 * k);
  circle(g, 52, 92, 30, undefined, colour, 1.5 * k);
  circle(g, 52, 92, 12, "rgb(68 255 136 / 25%)", C.green, k);
  for (let i = 0; i < 4; i++) circle(g, 12, 66 + i * 9, 3, undefined, colour, k);
  circle(g, 12, 116, 6, undefined, colour, 1.2 * k);
  circle(g, 12, 131, 6, undefined, colour, 1.2 * k);
  box(g, 89, 74, 4, 50, undefined, C.pink, 1, 1.5 * k);
  box(g, 0, 0, 100, 140, undefined, colour, 7, 1.5 * k);
  g.restore();
}

/** A CDJ seen from above, in its own grey-blue lines. */
export function cdj(
  g: G,
  x: number,
  y: number,
  s: number,
  options: { screen?: string } = {},
): void {
  const k = 1 / s;
  g.save();
  g.translate(x, y);
  g.scale(s, s);
  box(g, 0, 0, 100, 140, C.black, C.cdj, 5, 1.5 * k);
  box(g, 20, 8, 60, 30, options.screen ?? "#050505", C.cdj, 1, 1.5 * k);
  circle(g, 50, 92, 32, undefined, C.cdj, 1.5 * k);
  circle(g, 50, 92, 10, undefined, C.cdj, k);
  circle(g, 12, 116, 6, undefined, C.cdj, 1.2 * k);
  circle(g, 12, 131, 6, undefined, C.cdj, 1.2 * k);
  box(g, 88, 70, 4, 55, undefined, C.cdj, 1, 1.5 * k);
  g.restore();
}

/** A USB stick, its plug pointing up. */
export function stick(
  g: G,
  x: number,
  y: number,
  s: number,
  label = "",
  colour: string = C.text,
): void {
  const k = 1 / s;
  g.save();
  g.translate(x, y);
  g.scale(s, s);
  box(g, -9, 0, 18, 34, C.ground, colour, 3, 1.5 * k);
  box(g, -6, -12, 12, 12, undefined, colour, 1, 1.5 * k);
  g.fillStyle = colour;
  g.fillRect(-3, -9, 2, 3);
  g.fillRect(1, -9, 2, 3);
  if (label) text(g, label, 0, 20, 5, colour, { align: "center", weight: 700 });
  g.restore();
}

/** A phone, upright. */
export function phone(g: G, x: number, y: number, s: number, colour: string = C.text): void {
  g.save();
  g.translate(x, y);
  g.scale(s, s);
  box(g, 0, 0, 46, 92, C.black, colour, 7, 1.5 / s);
  box(g, 4, 9, 38, 72, "#080808", undefined, 2);
  box(g, 17, 3.5, 12, 2.5, colour, undefined, 1.2);
  g.restore();
}
