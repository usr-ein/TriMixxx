// Share them over the Ethernet cable: the deck and a CDJ, one cable between
// them, the link chattering both ways. The CDJ browses the deck's stick as it
// would another CDJ's, picks a track and loads it, the data running down the
// cable to it. Then the other way round, quicker: the deck plays a track off
// the CDJ's stick. Pink is the deck's, grey-blue the CDJ's.
import {
  C,
  type G,
  type Scene,
  box,
  cdj,
  clear,
  deck,
  during,
  line,
  span,
  text,
  waveform,
} from "./kit";

const LENGTH = 10;
const S = 1.85;

const DX = 22;
const DY = 90;
const CX = 273;
const CY = 90;

// The screens, where the kit's outlines put them.
const DECK_SCREEN = { x: DX + 8 * S, y: DY + 6 * S, w: 84 * S, h: 40 * S } as const;
const CDJ_SCREEN = { x: CX + 20 * S, y: CY + 8 * S, w: 60 * S, h: 30 * S } as const;

// The cable: out of the back of one, over, into the back of the other.
const P0 = [DX + 76 * S, DY] as const;
const P1 = [DX + 76 * S, DY - 78] as const;
const P2 = [CX + 24 * S, CY - 78] as const;
const P3 = [CX + 24 * S, CY] as const;

const LISTS = 1.3;
const NEXT = 2.4;
const PICK = 3.0;
const PLAYS = 3.8;
const THEIRS = 5.5;
const PICK_THEIRS = 6.2;
const PLAYS_THEIRS = 6.9;

const at = (u: number): [number, number] => {
  const v = 1 - u;
  const a = v * v * v;
  const b = 3 * v * v * u;
  const c = 3 * v * u * u;
  const d = u * u * u;
  return [
    a * P0[0] + b * P1[0] + c * P2[0] + d * P3[0],
    a * P0[1] + b * P1[1] + c * P2[1] + d * P3[1],
  ];
};

/** Packets sent from `from` to `to`, `every` seconds apart, each 0.9 s on the cable. */
function packets(
  g: G,
  t: number,
  from: number,
  to: number,
  every: number,
  colour: string,
  toCdj: boolean,
): void {
  for (let sent = from; sent < to; sent += every) {
    const u = (t - sent) / 0.9;
    if (u < 0 || u > 1) continue;
    const [x, y] = at(toCdj ? u : 1 - u);
    box(g, x - 3.5, y - 2.5, 7, 5, colour, undefined, 1.5);
  }
}

/** A screen's contents, clipped to it, and its bezel drawn again over their edges. */
function inside(
  g: G,
  screen: { x: number; y: number; w: number; h: number },
  bezel: string,
  r: number,
  paint: () => void,
): void {
  g.save();
  g.beginPath();
  g.roundRect(screen.x, screen.y, screen.w, screen.h, r);
  g.clip();
  paint();
  g.restore();
  box(g, screen.x, screen.y, screen.w, screen.h, undefined, bezel, r);
}

const scene: Scene = {
  length: LENGTH,
  still: 8.6,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    text(g, "TRIMIXXX", DX, DY - 16, 12, C.text, { weight: 700 });
    text(g, "CDJ 2", CX + 100 * S, CY - 16, 12, C.cdj, { weight: 700, align: "right" });

    // The cable, and its plugs.
    g.beginPath();
    g.moveTo(P0[0], P0[1]);
    g.bezierCurveTo(P1[0], P1[1], P2[0], P2[1], P3[0], P3[1]);
    g.strokeStyle = C.faint;
    g.lineWidth = 1.5;
    g.stroke();
    box(g, P0[0] - 5, P0[1] - 9, 10, 9, C.black, C.line, 1);
    box(g, P3[0] - 5, P3[1] - 9, 10, 9, C.black, C.cdj, 1);

    deck(g, DX, DY, S);
    cdj(g, CX, CY, S);

    // The link: a trickle both ways all along, and a burst for each load.
    packets(g, t, 0.4, LENGTH, 0.55, C.accent, true);
    packets(g, t, 0.65, LENGTH, 0.55, C.cdj, false);
    packets(g, t, PICK, PLAYS - 0.5, 0.1, C.accent, true);
    packets(g, t, PICK_THEIRS, PLAYS_THEIRS - 0.5, 0.1, C.cdj, false);

    // The CDJ: the deck's stick, listed; a track picked; then playing it,
    // still under the name of where it came from.
    inside(g, CDJ_SCREEN, C.cdj, S, () => {
      const { x, y, w, h } = CDJ_SCREEN;
      const header = span(t, LISTS, LISTS + 0.2);
      if (header > 0) {
        g.globalAlpha = fade * header;
        box(g, x, y, w, 14, C.bar);
        text(g, "TRIMIXXX · USB", x + 5, y + 7, 9.5, C.accent, { weight: 700 });
        g.globalAlpha = fade;
      }
      const list = during(t, LISTS, PLAYS, 0.2);
      if (list > 0) {
        g.globalAlpha = fade * list;
        const titles = ["Low Tide", "Copper Wire", "Night Bus"];
        const picked = t >= NEXT ? 1 : 0;
        titles.forEach((title, i) => {
          const ry = y + 14 + i * 13.8;
          const selected = i === picked;
          const flash = selected && t >= PICK && Math.floor((t - PICK) / 0.12) % 2 === 0;
          if (selected) box(g, x, ry, w, 13.8, flash ? C.high : C.cdj);
          text(g, title, x + 5, ry + 7, 9.5, selected ? C.onAccent : C.text);
        });
        g.globalAlpha = fade;
      }
      const playing = span(t, PLAYS, PLAYS + 0.3);
      if (playing > 0) {
        g.globalAlpha = fade * playing;
        waveform(g, x, y + 14, w, h - 14, 20 + t, { seed: 6, bpm: 124, seconds: 3 });
        g.globalAlpha = fade;
      }
    });

    // The deck: its own stick; then the CDJ's, which it opens and plays from.
    inside(g, DECK_SCREEN, C.line, 2 * S, () => {
      const { x, y, w, h } = DECK_SCREEN;
      const list = 1 - span(t, PLAYS_THEIRS, PLAYS_THEIRS + 0.3);
      if (list > 0) {
        g.globalAlpha = fade * list;
        box(g, x, y, w, h, C.row);
        const rh = h / 4;
        const grow = span(t, THEIRS, THEIRS + 0.3);
        const second = t >= PICK_THEIRS;
        // SAM1: this deck's own stick, in its port 1.
        box(g, x, y, w, rh, second ? C.row : C.accent);
        text(g, "1", x + 8, y + rh / 2, 9, second ? C.dim : C.onAccent);
        text(g, "SAM1", x + 20, y + rh / 2, 11, second ? C.text : C.onAccent);
        text(g, "412 tracks", x + w - 6, y + rh / 2, 9, second ? C.dim : C.onAccent, {
          align: "right",
        });
        // Player 2's stick, over the link, led by the player's number.
        let ry = y + rh;
        if (grow > 0) {
          g.save();
          g.beginPath();
          g.rect(x, ry, w, rh * grow);
          g.clip();
          const flash =
            second &&
            t >= PICK_THEIRS + 0.3 &&
            Math.floor((t - PICK_THEIRS - 0.3) / 0.12) % 2 === 0;
          box(g, x, ry, w, rh, second ? (flash ? C.high : C.accent) : C.row);
          text(g, "2  USB", x + 20, ry + rh / 2, 11, second ? C.onAccent : C.cdj);
          text(g, "86 tracks", x + w - 6, ry + rh / 2, 9, second ? C.onAccent : C.dim, {
            align: "right",
          });
          g.restore();
          ry += rh * grow;
        }
        for (const label of ["Autoplay", "History", "Record set"]) {
          line(g, x, ry, x + w, ry, C.rule, 1);
          text(g, label, x + 20, ry + rh / 2, 11, C.dim);
          ry += rh;
        }
        g.globalAlpha = fade;
      }
      const playing = span(t, PLAYS_THEIRS, PLAYS_THEIRS + 0.3);
      if (playing > 0) {
        g.globalAlpha = fade * playing;
        waveform(g, x, y + 16, w, h - 16, 40 + t, { seed: 11, bpm: 124, seconds: 3 });
        box(g, x, y, w, 16, C.bar);
        text(g, "Night Bus", x + 6, y + 8, 10, C.text, { weight: 700 });
        text(g, "CDJ 2 · USB", x + w - 6, y + 8, 9, C.cdj, { align: "right" });
        g.globalAlpha = fade;
      }
    });
  },
};

export default scene;
