// DOOM: hold PLAY while the deck boots, let go when PLAY and CUE blink, and
// the deck comes up as a DOOM cabinet. The view closes in on its screen: a
// corridor, drawn here as a little raycaster of our own (none of id's art),
// turns as the jog turns, and each pad, lit in its own colour, picks a weapon.
import {
  C,
  type G,
  type Scene,
  box,
  circle,
  clamp,
  clear,
  deck,
  during,
  lerp,
  line,
  segments,
  span,
  text,
} from "./kit";

const LENGTH = 10.5;
const HOLD_FROM = 0.5;
const HOLD_TO = 2;
const LET_GO = 2.45;
const BOOTED = 2.8;
const ZOOMED = 3.6;

// The weapons' pads, each lit in its own colour, and when each is pressed.
const PADS = [C.downbeat, C.segment, C.mid, C.range] as const;
const PICKS = [
  { at: 0, pad: 0 },
  { at: 4.7, pad: 1 },
  { at: 6.4, pad: 2 },
  { at: 8.1, pad: 3 },
] as const;

// Where the deck is drawn: a point on it, in its own units, on the canvas.
interface Place {
  x: number;
  y: number;
  s: number;
}
const at = (p: Place, u: number, v: number): [number, number] => [p.x + u * p.s, p.y + v * p.s];

// The corridor: a crossing halfway, a red door at the end.
const MAP = [
  "###########",
  "#####D#####",
  "####...####",
  "####...####",
  "#.........#",
  "####...####",
  "####...####",
  "####...####",
  "###########",
];
const cell = (x: number, y: number): string => MAP[y]?.[x] ?? "#";
const grey = (v: number): string => {
  const c = String(Math.round(v));
  return `rgb(${c} ${c} ${c})`;
};

function corridor(g: G, sx: number, sy: number, sw: number, sh: number, angle: number): void {
  const px = 5.5;
  const py = 7.3;
  const dx = Math.cos(angle);
  const dy = Math.sin(angle);
  const plane = 0.78;
  const focal = sw / 2 / plane;
  g.fillStyle = C.black;
  g.fillRect(sx, sy, sw, sh / 2);
  g.fillStyle = "#1a1a1a";
  g.fillRect(sx, sy + sh / 2, sw, sh / 2);
  const step = Math.max(1, sw / 220);
  const tops: [number, number][] = [];
  let last = "";
  for (let i = 0; i < sw; i += step) {
    const camera = (2 * (i + step / 2)) / sw - 1;
    const rx = dx - dy * plane * camera;
    const ry = dy + dx * plane * camera;
    let mx = Math.floor(px);
    let my = Math.floor(py);
    const ddx = Math.abs(1 / rx);
    const ddy = Math.abs(1 / ry);
    const sxStep = rx < 0 ? -1 : 1;
    const syStep = ry < 0 ? -1 : 1;
    let tx = (rx < 0 ? px - mx : mx + 1 - px) * ddx;
    let ty = (ry < 0 ? py - my : my + 1 - py) * ddy;
    let side = 0;
    let hit = "#";
    for (let n = 0; n < 40; n++) {
      if (tx < ty) {
        tx += ddx;
        mx += sxStep;
        side = 0;
      } else {
        ty += ddy;
        my += syStep;
        side = 1;
      }
      hit = cell(mx, my);
      if (hit !== ".") break;
    }
    const dist = Math.max(0.05, side === 0 ? tx - ddx : ty - ddy);
    const along = side === 0 ? py + dist * ry : px + dist * rx;
    const frac = along - Math.floor(along);
    // Taller than square, as the deck's screen is wide and corridors are high.
    const height = (focal * 1.35) / dist;
    const top = sy + sh / 2 - height / 2;
    // Nearer is lighter; the walls facing one way a little darker than the other.
    const k = clamp(1.3 - dist * 0.13, 0.28, 1);
    const x = sx + i;
    if (hit === "D") {
      const frame = frac < 0.12 || frac > 0.88;
      g.fillStyle = frame ? grey(78 * k) : `rgb(${String(Math.round(150 * k))} 18 28)`;
    } else {
      g.fillStyle = grey((side === 0 ? 62 : 44) * k);
    }
    g.fillRect(x, top, step + 0.5, height);
    // A red light strip along the walls, for depth.
    if (hit === "#") {
      g.fillStyle = `rgb(${String(Math.round(170 * k))} 0 20)`;
      g.fillRect(x, top + height * 0.3, step + 0.5, Math.max(1, height * 0.03));
    }
    // An edge where one wall gives way to another.
    const key = `${String(mx)},${String(my)},${String(side)}`;
    if (last && key !== last) {
      g.fillStyle = grey(110 * k);
      g.fillRect(x - 0.5, top, 1, height);
    }
    last = key;
    tops.push([x, height]);
  }
  for (const sign of [-1, 1]) {
    g.beginPath();
    tops.forEach(([x, height], i) => {
      const y = sy + sh / 2 + (sign * height) / 2;
      if (i === 0) g.moveTo(x, y);
      else g.lineTo(x, y);
    });
    g.strokeStyle = C.faint;
    g.lineWidth = 1;
    g.stroke();
  }
}

/** The weapon in hand: a plain shape for each, nothing more. */
function weapon(
  g: G,
  sx: number,
  sy: number,
  sw: number,
  sh: number,
  pad: number,
  raise: number,
): void {
  const cx = sx + sw / 2;
  const base = sy + sh + (1 - raise) * sh * 0.4;
  const u = sh / 100;
  g.fillStyle = C.ruleStrong;
  g.strokeStyle = C.dim;
  g.lineWidth = 1;
  const shapes: Record<number, [number, number, number, number][]> = {
    0: [[-9, -30, 18, 30]],
    1: [
      [-5, -46, 10, 46],
      [-12, -16, 24, 16],
    ],
    2: [
      [-13, -38, 11, 38],
      [2, -38, 11, 38],
    ],
    3: [
      [-17, -34, 34, 34],
      [-8, -44, 16, 10],
    ],
  };
  for (const [x, y, w, h] of shapes[pad] ?? []) {
    g.beginPath();
    g.rect(cx + x * u, base + y * u, w * u, h * u);
    g.fill();
    g.stroke();
  }
}

const scene: Scene = {
  length: LENGTH,
  still: 7.2,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    // From the whole deck to its screen, once it is up.
    const zoom = span(t, BOOTED, ZOOMED);
    const p: Place = { x: lerp(120, 60, zoom), y: lerp(12, -10, zoom), s: lerp(2.4, 3.6, zoom) };
    const turn = t < ZOOMED ? 0 : 0.85 * Math.sin((t - ZOOMED) * 0.95);
    const pick = [...PICKS].reverse().find((entry) => t >= entry.at) ?? PICKS[0];

    // The deck, the kit's; over it what moves: the jog's dimples and mark, the
    // pads' lights, CUE and PLAY blinking, the screen.
    deck(g, p.x, p.y, p.s);
    const [jx, jy] = at(p, 52, 92);
    g.fillStyle = C.faint;
    for (let k = 0; k < 24; k++) {
      const a = turn + (k / 24) * Math.PI * 2;
      g.beginPath();
      g.arc(jx + Math.cos(a) * 26.5 * p.s, jy + Math.sin(a) * 26.5 * p.s, 1.3, 0, Math.PI * 2);
      g.fill();
    }
    const a = turn - Math.PI / 2;
    line(
      g,
      jx + Math.cos(a) * 14 * p.s,
      jy + Math.sin(a) * 14 * p.s,
      jx + Math.cos(a) * 24 * p.s,
      jy + Math.sin(a) * 24 * p.s,
      C.text,
      2,
    );

    // The pads, once the game is up: dim in their colours, the weapon's lit.
    if (t >= BOOTED) {
      PADS.forEach((colour, i) => {
        const [x, y] = at(p, 12, 66 + i * 9);
        const lit = pick.pad === i;
        const pressed = lit && pick.at > 0 && t - pick.at < 0.25;
        g.globalAlpha = fade * (lit ? 1 : 0.3);
        circle(g, x, y, 3 * p.s, pressed ? C.high : colour);
        g.globalAlpha = fade;
        circle(g, x, y, 3 * p.s, undefined, colour, 1);
        if (p.s > 3) {
          text(g, String(i + 1), x, y + 1, 10, lit ? C.onAccent : C.dim, {
            align: "center",
            weight: 700,
          });
        }
      });
    }

    // CUE and PLAY; held, then blinking: the moment to let go.
    if (t >= HOLD_TO && t < LET_GO && (t - HOLD_TO) % 0.15 < 0.08) {
      for (const [v, colour] of [
        [116, C.segment],
        [131, C.green],
      ] as const) {
        const [x, y] = at(p, 12, v);
        circle(g, x, y, 6 * p.s, colour, C.line, 1.2);
      }
    }
    const [bx, by] = at(p, 12, 131);
    if (t >= HOLD_FROM && t < LET_GO) {
      g.beginPath();
      const held = clamp((t - HOLD_FROM) / (HOLD_TO - HOLD_FROM));
      g.arc(bx, by, 6 * p.s + 6, -Math.PI / 2, -Math.PI / 2 + held * Math.PI * 2);
      g.strokeStyle = C.accent;
      g.lineWidth = 2.5;
      g.stroke();
    }
    g.globalAlpha = fade * during(t, 0.3, LET_GO + 0.3, 0.25);
    text(g, "HOLD PLAY", bx - 6 * p.s - 26, by - 6, 13, C.accent, { align: "right", weight: 700 });
    text(g, "as it boots", bx - 6 * p.s - 26, by + 10, 10, C.dim, { align: "right" });
    g.globalAlpha = fade;

    // The screen: dark, then up like a tube, then the game.
    if (t >= LET_GO + 0.05) {
      const [sx, sy] = at(p, 8, 6);
      const sw = 84 * p.s;
      const sh = 40 * p.s;
      g.save();
      g.beginPath();
      g.roundRect(sx, sy, sw, sh, 2 * p.s);
      g.clip();
      if (t < BOOTED - 0.12) {
        const w = span(t, LET_GO + 0.05, BOOTED - 0.2);
        g.fillStyle = C.high;
        g.fillRect(sx + (sw * (1 - w)) / 2, sy + sh / 2 - 1, sw * w, 2);
      } else {
        const open = span(t, BOOTED - 0.12, BOOTED + 0.05);
        g.beginPath();
        g.rect(sx, sy + (sh * (1 - open)) / 2, sw, sh * open);
        g.clip();
        corridor(g, sx, sy, sw, sh, -Math.PI / 2 + turn * 0.8);
        const raise = pick.at > 0 ? span(t, pick.at, pick.at + 0.3) : 1;
        weapon(g, sx, sy, sw, sh, pick.pad, raise);
        segments(
          g,
          String(pick.pad + 1),
          sx + sw - sh * 0.24,
          sy + sh * 0.08,
          sh * 0.2,
          PADS[pick.pad],
        );
      }
      g.restore();
      box(g, sx, sy, sw, sh, undefined, C.line, 2 * p.s);
    }
  },
};

export default scene;
