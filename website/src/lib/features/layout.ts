// Buttons where a CDJ has them: a CDJ's top panel, its controls lighting one
// after another, CUE, PLAY, the hot cues, the loop, the tempo fader, the jog.
// Then the panel turns into the TriMixxx's around them, and the controls
// don't move: they were already where the deck has them. Positions in
// millimetres, from the deck's dimensions (as the landing's stand-in model
// has them) and a CDJ-2000NXS2's top, laid over each other at the jog.
import {
  C,
  type G,
  type Scene,
  box,
  circle,
  clamp,
  clear,
  during,
  lerp,
  line,
  span,
  text,
} from "./kit";

const LENGTH = 10;
const LIGHT = 0.6;
const MORPH = [3.9, 6.3] as const;

const K = 0.74;
const OX = 240 - 7 * K;
const OY = 180 - 2 * K;
const X = (x: number): number => OX + x * K;
const Y = (z: number): number => OY + z * K;

type Point = [number, number];
type Corner = readonly [number, number, number];

// The bodies, clockwise from the middle of the back edge; each corner with its radius.
const CDJ_BODY: readonly Corner[] = [
  [18, -198, 0],
  [133, -198, 4],
  [133, -181, 0],
  [178, -181, 6],
  [178, 216, 6],
  [-142, 216, 6],
  [-142, -181, 6],
  [-97, -181, 0],
  [-97, -198, 4],
];
const DECK_BODY: readonly Corner[] = [
  [0, -211.5, 0],
  [164.5, -211.5, 26],
  [164.5, 211.5, 26],
  [-164.5, 211.5, 26],
  [-164.5, -211.5, 26],
];

/** A closed outline through its corners, each rounded off, as a dense polyline. */
function trace(corners: readonly Corner[]): Point[] {
  const out: Point[] = [];
  const n = corners.length;
  corners.forEach((corner, i) => {
    const before = corners[(i - 1 + n) % n] ?? corner;
    const after = corners[(i + 1) % n] ?? corner;
    const [cx, cz, r] = corner;
    if (r === 0) {
      out.push([cx, cz]);
      return;
    }
    const towards = (p: Corner): Point => {
      const dx = p[0] - cx;
      const dz = p[1] - cz;
      const d = Math.hypot(dx, dz) || 1;
      return [cx + (dx / d) * r, cz + (dz / d) * r];
    };
    const [ax, az] = towards(before);
    const [bx, bz] = towards(after);
    for (let k = 0; k <= 8; k++) {
      const u = k / 8;
      const v = 1 - u;
      out.push([
        v * v * ax + 2 * v * u * cx + u * u * bx,
        v * v * az + 2 * v * u * cz + u * u * bz,
      ]);
    }
  });
  return out;
}

/** `count` points, evenly spaced along a closed polyline, from its first point on. */
function resample(points: readonly Point[], count: number): Point[] {
  const lengths = [0];
  let total = 0;
  for (let i = 0; i < points.length; i++) {
    const a = points[i] ?? [0, 0];
    const b = points[(i + 1) % points.length] ?? a;
    total += Math.hypot(b[0] - a[0], b[1] - a[1]);
    lengths.push(total);
  }
  const out: Point[] = [];
  let segment = 0;
  for (let k = 0; k < count; k++) {
    const d = (k / count) * total;
    while ((lengths[segment + 1] ?? total) < d) segment++;
    const a = points[segment] ?? [0, 0];
    const b = points[(segment + 1) % points.length] ?? a;
    const from = lengths[segment] ?? 0;
    const to = lengths[segment + 1] ?? total;
    const u = to > from ? (d - from) / (to - from) : 0;
    out.push([lerp(a[0], b[0], u), lerp(a[1], b[1], u)]);
  }
  return out;
}

const CDJ_OUTLINE = resample(trace(CDJ_BODY), 240);
const DECK_OUTLINE = resample(trace(DECK_BODY), 240);

function outline(g: G, points: readonly Point[]): void {
  g.beginPath();
  points.forEach(([x, z], i) => {
    if (i === 0) g.moveTo(X(x), Y(z));
    else g.lineTo(X(x), Y(z));
  });
  g.closePath();
}

/** A colour between two "#rrggbb" colours. */
function mix(a: string, b: string, k: number): string {
  const channel = (hex: string, i: number): number => parseInt(hex.slice(1 + i * 2, 3 + i * 2), 16);
  const parts = [0, 1, 2].map((i) => Math.round(lerp(channel(a, i), channel(b, i), k)));
  return `rgb(${parts.join(" ")})`;
}

// What differs between the two, as pairs that turn into each other:
// [x, z, w, h] for the rectangles, [x, z, r] for the round things.
const SCREEN = { cdj: [-63, -166, 157.5, 98], deck: [-118.5, -202, 239, 128] } as const;
const LIP = { cdj: [-97, 133, -66], deck: [-164.5, 164.5, -60] } as const;
const USB = { cdj: [-127, -161, 19, 17], deck: [-141.5, -114.5, 14, 15] } as const;
const ROUND = [
  // The browse knob; DISC EJECT into BACK; USB STOP into SORT/INFO.
  { cdj: [115.7, -137, 17], deck: [142.5, -109, 7.5] },
  { cdj: [154, -144.5, 12], deck: [142.5, -150, 7.5] },
  { cdj: [-108, -151.5, 5], deck: [-141.5, -171, 7.5] },
] as const;

// The controls the two share, where the deck has them.
const HOT_CUES = [-22, -2, 18, 38];
const LOOP: readonly (readonly [number, number, number])[] = [
  [-92, -44, 7.5],
  [-60, -44, 7.5],
  [-28, -42, 4.5],
];
const OTHERS: readonly (readonly [number, number, number])[] = [
  [-74, -18, 7.5],
  [34, -42, 7.5],
  [53, -42, 7.5],
  [72, -42, 7.5],
  [91, -42, 7.5],
  [128, -26, 7.5],
  [128, -6, 7.5],
  [128, 30, 7.5],
  [88, -12, 5],
];
const JOG = [18, 72] as const;
const FADER = { x: 142, from: 38, to: 172, cap: 108 } as const;

const LABELS = ["CUE", "PLAY", "HOT CUES", "LOOP", "TEMPO", "JOG"] as const;

const scene: Scene = {
  length: LENGTH,
  still: 8.2,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;
    const m = span(t, MORPH[0], MORPH[1]);
    const ink = mix(C.cdj, C.line, m);
    const lit = LABELS.map((_, i) => span(t, LIGHT + i * 0.5, LIGHT + i * 0.5 + 0.25));
    const on = (i: number): number => lit[i] ?? 0;

    // The body, turning from one into the other.
    const body = CDJ_OUTLINE.map(([x, z], i): Point => {
      const [dx, dz] = DECK_OUTLINE[i] ?? [x, z];
      return [lerp(x, dx, m), lerp(z, dz, m)];
    });
    outline(g, body);
    g.fillStyle = C.black;
    g.fill();
    g.strokeStyle = ink;
    g.lineWidth = 1.5;
    g.stroke();

    // What isn't a control: the screen, the edge of its housing, the ports and knobs.
    const [sx, sz, sw, sh] = SCREEN.cdj.map((v, i) => lerp(v, SCREEN.deck[i] ?? v, m));
    box(g, X(sx ?? 0), Y(sz ?? 0), (sw ?? 0) * K, (sh ?? 0) * K, "#050505", ink, lerp(2, 4, m));
    const [l0, l1, lz] = LIP.cdj.map((v, i) => lerp(v, LIP.deck[i] ?? v, m));
    line(g, X(l0 ?? 0), Y(lz ?? 0), X(l1 ?? 0), Y(lz ?? 0), ink, 1);
    const [ux, uz, uw, uh] = USB.cdj.map((v, i) => lerp(v, USB.deck[i] ?? v, m));
    box(
      g,
      X((ux ?? 0) - (uw ?? 0) / 2),
      Y((uz ?? 0) - (uh ?? 0) / 2),
      (uw ?? 0) * K,
      (uh ?? 0) * K,
      undefined,
      ink,
      1,
    );
    for (const pair of ROUND) {
      const [rx, rz, rr] = pair.cdj.map((v, i) => lerp(v, pair.deck[i] ?? v, m));
      circle(g, X(rx ?? 0), Y(rz ?? 0), (rr ?? 0) * K, undefined, ink, 1);
    }
    // The CDJ's SD slot, which the deck doesn't have.
    if (m < 1) {
      g.globalAlpha = fade * (1 - m);
      box(g, X(-136), Y(-137), 33 * K, 19 * K, undefined, ink, 1);
      g.globalAlpha = fade;
    }

    // The controls, which stay put. Lit, they turn pink.
    const control = (x: number, z: number, r: number, k: number, width = 1.2): void => {
      const colour = k > 0 ? mix(C.cdj, C.accent, k) : ink;
      if (k > 0) {
        g.globalAlpha = fade * k * 0.28;
        circle(g, X(x), Y(z), r * K, C.accent);
        g.globalAlpha = fade;
      }
      circle(g, X(x), Y(z), r * K, undefined, k > 0 ? colour : ink, width);
    };
    for (const [x, z, r] of OTHERS) control(x, z, r, 0, 1);
    control(-117, 128, 16.5, on(0), 1.5);
    control(-117, 176, 16.5, on(1), 1.5);
    for (const z of HOT_CUES) control(-112, z, 7.5, on(2));
    for (const [x, z, r] of LOOP) control(x, z, r, on(3));

    // The tempo fader: its slot, pink on the deck, and its cap.
    const tempo = on(4);
    const slot = tempo > 0 ? mix(C.cdj, C.accent, tempo) : mix(C.cdj, C.pink, m);
    box(g, X(FADER.x - 6), Y(FADER.from), 12 * K, (FADER.to - FADER.from) * K, undefined, slot, 2);
    if (tempo > 0) {
      g.globalAlpha = fade * tempo * 0.28;
      box(
        g,
        X(FADER.x - 6),
        Y(FADER.from),
        12 * K,
        (FADER.to - FADER.from) * K,
        C.accent,
        undefined,
        2,
      );
      g.globalAlpha = fade;
    }
    box(
      g,
      X(FADER.x - 9),
      Y(FADER.cap - 5),
      18 * K,
      10 * K,
      C.black,
      tempo > 0 ? C.accent : ink,
      1.5,
    );

    // The jog: its cut-out, the platter, the centre that glows green on the deck.
    const jog = on(5);
    const rim = jog > 0 ? mix(C.cdj, C.accent, jog) : ink;
    circle(g, X(JOG[0]), Y(JOG[1]), 105 * K, undefined, ink, 1);
    if (jog > 0) {
      g.globalAlpha = fade * jog * 0.1;
      circle(g, X(JOG[0]), Y(JOG[1]), 90 * K, C.accent);
      g.globalAlpha = fade;
    }
    circle(g, X(JOG[0]), Y(JOG[1]), 90 * K, undefined, rim, 1.5);
    g.globalAlpha = fade * m * 0.25;
    circle(g, X(JOG[0]), Y(JOG[1]), 40 * K, C.green);
    g.globalAlpha = fade;
    circle(g, X(JOG[0]), Y(JOG[1]), 40 * K, undefined, mix(C.cdj, C.green, m), 1);

    // The CDJ it was, left over it as a ghost.
    const ghost = span(t, MORPH[1] - 0.2, MORPH[1] + 0.5);
    if (ghost > 0) {
      g.globalAlpha = fade * ghost * 0.7;
      g.setLineDash([4, 4]);
      outline(g, CDJ_OUTLINE);
      g.strokeStyle = C.cdj;
      g.lineWidth = 1;
      g.stroke();
      g.setLineDash([]);
      g.globalAlpha = fade;
    }

    // The labels, each on its own leader.
    const label = (i: number, fromX: number, fromY: number, toY: number, left: boolean): void => {
      const k = on(i);
      if (k <= 0) return;
      const name = LABELS[i] ?? "";
      const end = left ? 104 : 376;
      g.globalAlpha = fade * k;
      const elbow = left ? Math.min(fromX - 8, X(-172)) : Math.max(fromX + 8, X(186));
      g.beginPath();
      g.moveTo(fromX, fromY);
      g.lineTo(elbow, toY);
      g.lineTo(end + (left ? 4 : -4), toY);
      g.strokeStyle = C.accent;
      g.lineWidth = 1;
      g.stroke();
      circle(g, fromX, fromY, 2, C.accent);
      text(g, name, end, toY, 11, C.accent, { weight: 700, align: left ? "right" : "left" });
      g.globalAlpha = fade;
    };
    label(0, X(-117 - 16.5), Y(128), Y(128), true);
    label(1, X(-117 - 16.5), Y(176), Y(176), true);
    // The hot cues, bracketed.
    if (on(2) > 0) {
      g.globalAlpha = fade * on(2);
      const bx = X(-124);
      g.beginPath();
      g.moveTo(bx + 3, Y(-29));
      g.lineTo(bx, Y(-29));
      g.lineTo(bx, Y(45));
      g.lineTo(bx + 3, Y(45));
      g.strokeStyle = C.accent;
      g.lineWidth = 1;
      g.stroke();
      g.globalAlpha = fade;
    }
    label(2, X(-124), Y(8), Y(8), true);
    label(3, X(-92 - 7.5), Y(-44), Y(-44), true);
    label(4, X(FADER.x + 9), Y(FADER.cap), Y(FADER.cap), false);
    const a = (-35 * Math.PI) / 180;
    label(5, X(JOG[0] + Math.cos(a) * 90), Y(JOG[1] + Math.sin(a) * 90), Y(12), false);

    // Which outline is which.
    const legend = clamp(ghost);
    g.globalAlpha = fade * (1 - legend);
    text(g, "CDJ", 16, 24, 12, C.cdj, { weight: 700 });
    g.globalAlpha = fade * legend;
    g.setLineDash([4, 4]);
    line(g, 16, 22, 34, 22, C.cdj, 1);
    g.setLineDash([]);
    text(g, "CDJ", 40, 22, 11, C.cdj, { weight: 700 });
    line(g, 16, 40, 34, 40, C.line, 1.5);
    text(g, "TRIMIXXX", 40, 40, 11, C.text, { weight: 700 });
    g.globalAlpha = fade;
  },
};

export default scene;
