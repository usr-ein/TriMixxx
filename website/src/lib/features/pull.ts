// Pull the stick out: the deck is playing off a stick when it is yanked out of
// its socket, mid-track, no eject. The waveform scrolls on without a hitch,
// the clock keeps counting, and the deck says what its toast says. The stick
// leaves with a lock on it: it was mounted read-only, so there was nothing
// half-written on it to corrupt.
import {
  C,
  type G,
  type Scene,
  box,
  clamp,
  clear,
  during,
  lerp,
  line,
  random,
  segments,
  span,
  text,
  waveform,
} from "./kit";

const LENGTH = 9;
const PULL = 2.7;
const OUT = 0.22;
const TOAST = [2.95, 7.2] as const;

const EDGE = 128;
const SOCKET_Y = 150;
const SX = 160;
const SY = 18;
const SW = 304;
const SH = 324;

// Where the track is: 1:02.4 in when the loop starts, 5:33.6 long.
const START = 62.4;
const TOTAL = 333.6;

const clock = (seconds: number): string => {
  const whole = Math.floor(seconds);
  const tenths = Math.floor((seconds - whole) * 10);
  return `${String(Math.floor(whole / 60))}:${String(whole % 60).padStart(2, "0")}.${String(tenths)}`;
};

/** A padlock, about 10 units across. */
function lock(g: G, x: number, y: number, colour: string): void {
  g.beginPath();
  g.arc(x, y - 2, 3.4, Math.PI, Math.PI * 2);
  g.lineTo(x + 3.4, y + 1);
  g.moveTo(x - 3.4, y + 1);
  g.lineTo(x - 3.4, y - 2);
  g.strokeStyle = colour;
  g.lineWidth = 1.5;
  g.stroke();
  box(g, x - 5, y + 1, 10, 7.5, colour, undefined, 1.5);
}

/** A stick lying on its side, its plug pointing right; `x` is where plug meets body. */
function stickSide(g: G, x: number, y: number, label: string): void {
  box(g, x - 68, y - 19, 68, 38, C.ground, C.text, 6);
  box(g, x, y - 12, 24, 24, C.black, C.text, 1);
  g.fillStyle = C.text;
  g.fillRect(x + 11, y - 7, 5, 3);
  g.fillRect(x + 11, y + 4, 5, 3);
  text(g, label, x - 36, y, 12, C.text, { weight: 700, align: "center" });
}

// The whole track, as the overview strip shows it: quiet intro, two drops.
const OVERVIEW = (() => {
  const next = random(41);
  const columns: [number, number, number][] = [];
  for (let i = 0; i < 150; i++) {
    const p = i / 150;
    const energy =
      p < 0.12 ? 0.35 : p < 0.3 ? 0.55 + p : p < 0.58 ? 0.95 : p < 0.68 ? 0.4 : p < 0.9 ? 1 : 0.45;
    const n = 0.85 + next() * 0.15;
    columns.push([energy * n, energy * n * 0.7, energy * n * 0.38]);
  }
  return columns;
})();

const scene: Scene = {
  length: LENGTH,
  still: 4.6,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;
    const now = START + t;

    // The housing and its socket.
    box(g, EDGE, 8, 344, 344, undefined, C.faint, 10);
    box(g, EDGE, SOCKET_Y - 14, 30, 28, C.black, C.line, 1);
    box(g, EDGE + 8, SOCKET_Y - 9, 22, 5, C.low);

    // The screen: the deck playing.
    g.save();
    g.beginPath();
    g.roundRect(SX, SY, SW, SH, 4);
    g.clip();
    box(g, SX, SY, SW, SH, C.black);

    // The header: cover, title, key; the time gone and the time left.
    box(g, SX, SY, SW, 44, C.bar);
    line(g, SX, SY + 44, SX + SW, SY + 44, "#1e201f", 1);
    box(g, SX + 9, SY + 8, 28, 28, C.keySync);
    box(g, SX + 14, SY + 23, 18, 6, C.accent);
    text(g, "Night Bus", SX + 46, SY + 15, 14, C.text, { weight: 700 });
    text(g, "4A", SX + 46, SY + 32, 11, C.accent, { weight: 700 });
    text(g, clock(now), SX + SW - 10, SY + 15, 14, C.text, { align: "right" });
    text(g, `-${clock(TOTAL - now)}`, SX + SW - 10, SY + 32, 11, C.dim, { align: "right" });

    // The waveform, scrolling on whatever happens to the stick.
    const wy = SY + 45;
    const wh = 214;
    waveform(g, SX, wy, SW, wh, now, { seed: 9, bpm: 124, seconds: 4 });
    box(g, SX + SW - 122, wy + wh - 50, 114, 42, "rgb(0 0 0 / 80%)", C.ruleStrong, 4);
    segments(g, "124.00", SX + SW - 110, wy + wh - 40, 22);

    // The overview: the whole track, and where it is.
    const oy = wy + wh + 6;
    const oh = SY + SH - oy - 6;
    const mid = oy + oh / 2;
    const played = (now / TOTAL) * SW;
    OVERVIEW.forEach((column, i) => {
      const x = SX + 2 + (i / OVERVIEW.length) * (SW - 4);
      const dim = x < SX + played ? 0.45 : 1;
      [C.low, C.mid, C.high].forEach((colour, b) => {
        const v = (column[b] ?? 0) * (oh / 2 - 2);
        g.globalAlpha = fade * dim;
        g.fillStyle = colour;
        g.fillRect(x, mid - v, 1.4, v * 2);
      });
    });
    g.globalAlpha = fade;
    g.fillStyle = C.high;
    g.fillRect(SX + played - 1, oy, 2, oh);

    // The deck's toast, top right, in its own words; under the header here,
    // so the clock stays in sight.
    const toast = during(t, TOAST[0], TOAST[1], 0.12);
    if (toast > 0) {
      g.globalAlpha = fade * toast;
      const tw = 232;
      const tx = SX + SW - tw - 10;
      const ty = SY + 54;
      box(g, tx, ty, tw, 52, C.rule);
      g.beginPath();
      g.roundRect(tx + 0.5, ty + 0.5, tw - 1, 51, 6);
      g.strokeStyle = C.accent;
      g.lineWidth = 1;
      g.stroke();
      text(g, "SAM1 removed — current track", tx + 12, ty + 18, 12, C.text);
      text(g, "is cached and stays playable.", tx + 12, ty + 35, 12, C.text);
      g.globalAlpha = fade;
    }
    g.restore();
    box(g, SX, SY, SW, SH, undefined, C.ruleStrong, 4);

    // The stick, yanked: fast, a few speed lines behind it, and a lock on it.
    const k = clamp((t - PULL) / OUT);
    const plug = lerp(EDGE, 70, 1 - (1 - k) ** 3);
    g.save();
    g.beginPath();
    g.rect(0, 0, EDGE, 360);
    g.clip();
    stickSide(g, plug, SOCKET_Y, "SAM1");
    const locked = span(t, PULL - 0.25, PULL + 0.1);
    if (locked > 0) {
      g.globalAlpha = fade * locked;
      lock(g, plug - 12, SOCKET_Y - 2, C.accent);
      g.globalAlpha = fade;
    }
    g.restore();
    const trail = t < PULL ? 0 : clamp(1 - (t - PULL - OUT) / 0.35);
    if (trail > 0) {
      g.globalAlpha = fade * trail;
      const from = plug + 30;
      for (const [dy, reach] of [
        [-11, 46],
        [0, 70],
        [11, 40],
      ] as const) {
        line(g, from, SOCKET_Y + dy, Math.min(EDGE - 4, from + reach), SOCKET_Y + dy, C.text, 1.2);
      }
      g.globalAlpha = fade;
    }
  },
};

export default scene;
