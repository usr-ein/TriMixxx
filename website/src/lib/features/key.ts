// KEY SYNC on the Camelot wheel, minor keys inside (A), major outside (B). The
// track is in 8A, the master's in 3A. A press moves the track round to 3A,
// keeping its mode, and its key turns KEY SYNC purple: what it has been put
// into, against the accent's what it is, whose outline stays on 8A. Held, the
// pad lets you pick any of the twelve: the key steps round, 4A to 11A, and is
// in the accent again only where it passes the track's own key.
import { C, type G, type Scene, box, circle, clamp, clear, during, ease, lerp, text } from "./kit";

const LENGTH = 10.5;
const OWN = 8;
const MASTER = 3;
const PRESS = 1.4;
const MOVED = 2.4;
const HOLD = 4.4;
const LIST = 5;
const STEP = 0.52;
const STEPS = 8;
const RELEASE = LIST + STEPS * STEP + 0.4;

const CX = 172;
const CY = 180;
const R_IN = 58;
const R_MID = 104;
const R_OUT = 148;

/** Where a Camelot number sits: 12 at the top, round like a clock. */
const angle = (n: number): number => -Math.PI / 2 + (n * Math.PI) / 6;

function sector(g: G, n: number, inner: number, outer: number): void {
  const a = angle(n);
  g.beginPath();
  g.arc(CX, CY, outer, a - Math.PI / 12, a + Math.PI / 12);
  g.arc(CX, CY, inner, a + Math.PI / 12, a - Math.PI / 12, true);
  g.closePath();
}

/** The Camelot number, 1 to 12, nearest a point on the wheel. */
const nearest = (n: number): number => ((((Math.round(n) - 1) % 12) + 12) % 12) + 1;

/** Where the track's key is on the wheel at `t`. */
function keyAt(t: number): number {
  if (t < PRESS) return OWN;
  if (t < LIST) return lerp(OWN, MASTER, ease(clamp((t - PRESS) / (MOVED - PRESS))));
  const steps = (t - LIST) / STEP;
  const whole = Math.min(STEPS, Math.floor(steps));
  return MASTER + whole + (whole < STEPS ? ease(clamp((steps - whole) / 0.25)) : 0);
}

const scene: Scene = {
  length: LENGTH,
  still: 3.4,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    const n = keyAt(t);
    const key = nearest(n);
    const shifted = t >= PRESS && key !== OWN;
    const colour = shifted ? C.keySync : C.accent;

    // The wheel, the track's key under its marker.
    for (let k = 1; k <= 12; k++) {
      sector(g, k, R_IN, R_MID);
      g.strokeStyle = C.ruleStrong;
      g.lineWidth = 1;
      g.stroke();
      sector(g, k, R_MID, R_OUT);
      g.stroke();
    }
    sector(g, n, R_IN + 2, R_MID - 2);
    g.fillStyle = colour;
    g.fill();
    // Its own key, outlined in the accent; the master's in the CDJ's grey-blue.
    sector(g, OWN, R_IN, R_MID);
    g.strokeStyle = C.accent;
    g.lineWidth = 2;
    g.stroke();
    sector(g, MASTER, R_IN - 4, R_MID + 4);
    g.strokeStyle = C.cdj;
    g.setLineDash([4, 3]);
    g.lineWidth = 1.5;
    g.stroke();
    g.setLineDash([]);

    const minor = (ink: string): void => {
      for (let k = 1; k <= 12; k++) {
        const a = angle(k);
        const r = (R_IN + R_MID) / 2;
        text(g, `${String(k)}A`, CX + Math.cos(a) * r, CY + Math.sin(a) * r + 1, 12, ink, {
          align: "center",
          weight: 700,
        });
      }
    };
    minor(C.dim);
    // Dark where the marker lies over them, even half over as it moves.
    g.save();
    sector(g, n, R_IN + 2, R_MID - 2);
    g.clip();
    minor(C.onAccent);
    g.restore();
    for (let k = 1; k <= 12; k++) {
      const a = angle(k);
      const r = (R_MID + R_OUT) / 2;
      text(g, `${String(k)}B`, CX + Math.cos(a) * r, CY + Math.sin(a) * r + 1, 11, C.faint, {
        align: "center",
      });
    }
    circle(g, CX, CY, R_IN - 10, undefined, C.rule, 1);

    // The track's key, as the deck's header gives it.
    text(g, "TRACK", 404, 52, 10, C.dim, { align: "center", weight: 700 });
    box(g, 346, 62, 116, 54, undefined, colour, 4);
    text(g, `${String(key)}A`, 404, 90, 30, colour, { align: "center", weight: 700 });
    text(g, "MASTER", 404, 140, 10, C.dim, { align: "center", weight: 700 });
    box(g, 346, 150, 116, 36, undefined, C.cdj, 4);
    text(g, `${String(MASTER)}A`, 404, 169, 20, C.cdj, { align: "center", weight: 700 });

    // KEY SYNC: lit purple while the track is out of its own key.
    const px = 404;
    const py = 262;
    const flash = (at: number): number => (t >= at && t < at + 0.25 ? 1 - (t - at) / 0.25 : 0);
    const pressed = Math.max(flash(PRESS), flash(HOLD));
    circle(g, px, py, 30, shifted ? C.keySync : C.black, shifted ? C.keySync : C.line);
    if (pressed > 0) {
      g.globalAlpha = fade * pressed;
      circle(g, px, py, 30, C.high);
      g.globalAlpha = fade;
    }
    const on = shifted || pressed > 0;
    text(g, "KEY", px, py - 7, 11, on ? C.onAccent : C.text, { align: "center", weight: 700 });
    text(g, "SYNC", px, py + 8, 11, on ? C.onAccent : C.text, { align: "center", weight: 700 });
    if (t >= HOLD && t < RELEASE) {
      const held = clamp((t - HOLD) / (LIST - HOLD));
      g.beginPath();
      g.arc(px, py, 37, -Math.PI / 2, -Math.PI / 2 + held * Math.PI * 2);
      g.strokeStyle = C.keySync;
      g.lineWidth = 2.5;
      g.stroke();
      g.globalAlpha = fade * clamp((t - HOLD) / 0.3);
      text(g, "PICK ANY KEY", px, py + 54, 10, C.keySync, { align: "center", weight: 700 });
      g.globalAlpha = fade;
    }
  },
};

export default scene;
