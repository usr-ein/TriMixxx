// Two sticks, one deck: SAM1 goes into the first socket, SAM2 into the
// second, and the root list grows a row for each, numbered by its port. Then
// a CDJ joins on the link, and on its screen the deck has two slots lit: USB
// for the first stick and SD for the second, a slot every CDJ already knows.
import {
  C,
  type G,
  type Scene,
  box,
  cdj,
  clear,
  during,
  lerp,
  row,
  span,
  text,
  topbar,
} from "./kit";

const LENGTH = 9.5;

// The deck's group (sticks, housing, screen), centred until the CDJ comes.
const EDGE = 92;
const SOCKETS = [122, 180] as const;
const SCREEN = { dx: 30, y: 48, w: 164, h: 274 } as const;
const S = 1.75;
const CX = 300;
const CY = 104;

const IN = [
  [0.4, 1.1],
  [1.9, 2.6],
] as const;
const ROWS = [1.25, 2.75] as const;
const COUNTED = [1.8, 3.3] as const;
const MOVE = [3.6, 4.2] as const;
const SLOTS = [5.0, 5.6] as const;

const STICKS = [
  { name: "SAM1", tracks: "412 tracks", slot: "USB" },
  { name: "SAM2", tracks: "96 tracks", slot: "SD" },
] as const;

type Mark = "stick" | "autoplay" | "history" | "record" | "power";

/** The deck's line marks, about 14 units across. */
function mark(g: G, kind: Mark, x: number, y: number, colour: string): void {
  g.strokeStyle = colour;
  g.fillStyle = colour;
  g.lineWidth = 1.4;
  g.beginPath();
  switch (kind) {
    case "stick":
      g.roundRect(x - 4.5, y - 2, 9, 10, 1.5);
      g.moveTo(x - 2.3, y - 2);
      g.lineTo(x - 2.3, y - 7);
      g.lineTo(x + 2.3, y - 7);
      g.lineTo(x + 2.3, y - 2);
      g.stroke();
      break;
    case "autoplay":
      g.arc(x, y, 7, 0, Math.PI * 2);
      g.stroke();
      g.beginPath();
      g.moveTo(x - 2, y - 3.5);
      g.lineTo(x + 3.5, y);
      g.lineTo(x - 2, y + 3.5);
      g.closePath();
      g.stroke();
      break;
    case "history":
      g.arc(x, y, 7, 0, Math.PI * 2);
      g.moveTo(x, y - 4);
      g.lineTo(x, y);
      g.lineTo(x + 3.5, y);
      g.stroke();
      break;
    case "record":
      g.arc(x, y, 7, 0, Math.PI * 2);
      g.stroke();
      g.beginPath();
      g.arc(x, y, 3.5, 0, Math.PI * 2);
      g.fill();
      break;
    case "power":
      g.arc(x, y + 0.5, 6.5, -Math.PI * 0.32, Math.PI * 1.32);
      g.moveTo(x, y - 7);
      g.lineTo(x, y);
      g.stroke();
      break;
  }
}

/** A stick lying on its side, its plug pointing right; `x` is where plug meets body. */
function stickSide(g: G, x: number, y: number, label: string): void {
  box(g, x - 68, y - 19, 68, 38, C.ground, C.text, 6);
  box(g, x, y - 12, 24, 24, C.black, C.text, 1);
  g.fillStyle = C.text;
  g.fillRect(x + 11, y - 7, 5, 3);
  g.fillRect(x + 11, y + 4, 5, 3);
  text(g, label, x - 34, y, 12, C.text, { weight: 700, align: "center" });
}

const scene: Scene = {
  length: LENGTH,
  still: 7.6,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    // The deck's group moves aside for the CDJ.
    const move = span(t, MOVE[0], MOVE[1]);
    const ox = lerp(84, 0, move);
    const edge = EDGE + ox;
    const sx = edge + SCREEN.dx;
    const { y: sy, w: sw, h: sh } = SCREEN;

    // The housing and its two sockets.
    text(g, "TRIMIXXX", edge, 26, 12, C.text, { weight: 700 });
    box(g, edge, 40, 204, 290, undefined, C.faint, 10);
    for (const y of SOCKETS) {
      box(g, edge, y - 14, 30, 28, C.black, C.line, 1);
      box(g, edge + 8, y - 9, 22, 5, C.low);
    }

    // The sticks, one after the other.
    g.save();
    g.beginPath();
    g.rect(0, 0, edge, 360);
    g.clip();
    STICKS.forEach((stick, i) => {
      const [a, b] = IN[i] ?? [0, 0];
      const y = SOCKETS[i] ?? 0;
      stickSide(g, lerp(-40, edge, span(t, a, b)), y, stick.name);
    });
    g.restore();

    // The root list: a row for each stick, by its port, above the rest.
    g.save();
    g.beginPath();
    g.roundRect(sx, sy, sw, sh, 4);
    g.clip();
    box(g, sx, sy, sw, sh, C.row);
    const h = 34;
    let y = sy + 26;
    const picked = t >= COUNTED[1] + 0.2 ? 1 : t >= COUNTED[0] + 0.1 ? 0 : -1;
    STICKS.forEach((stick, i) => {
      const grow = span(t, ROWS[i] ?? 0, (ROWS[i] ?? 0) + 0.35);
      if (grow <= 0) return;
      g.save();
      g.beginPath();
      g.rect(sx, y, sw, h * grow);
      g.clip();
      const selected = picked === i;
      const ink = selected ? C.onAccent : C.text;
      const counted = t >= (COUNTED[i] ?? 0);
      row(g, sx, y, sw, h, "", "", selected);
      text(g, ">", sx + 8, y + h / 2, 12, ink);
      mark(g, "stick", sx + 28, y + h / 2, ink);
      text(g, String(i + 1), sx + 40, y + h / 2 + 1, 11, selected ? C.onAccent : C.dim);
      text(g, stick.name, sx + 54, y + h / 2, 13, ink);
      text(
        g,
        counted ? stick.tracks : "reading…",
        sx + sw - 8,
        y + h / 2,
        10,
        selected ? C.onAccent : counted ? C.dim : C.faint,
        {
          align: "right",
        },
      );
      g.restore();
      y += h * grow;
    });
    const rest: readonly [Mark, string][] = [
      ["autoplay", "Autoplay"],
      ["history", "History"],
      ["record", "Record set"],
      ["power", "Shut down"],
    ];
    rest.forEach(([kind, label], i) => {
      const ry = y + i * h;
      const selected = picked < 0 && i === 0;
      const ink = selected ? C.onAccent : C.text;
      row(g, sx, ry, sw, h, "", "", selected);
      if (kind !== "power") text(g, ">", sx + 8, ry + h / 2, 12, ink);
      mark(g, kind, sx + 28, ry + h / 2, ink);
      text(g, label, sx + 44, ry + h / 2, 13, ink);
    });
    topbar(g, sx, sy, sw, []);
    g.restore();
    box(g, sx, sy, sw, sh, undefined, C.ruleStrong, 4);

    // The CDJ, on the link.
    if (move > 0) {
      const cx = lerp(500, CX, move);
      const cable = span(t, MOVE[1] - 0.1, MOVE[1] + 0.3);
      if (cable > 0) {
        g.globalAlpha = fade * cable;
        const x0 = edge + 170;
        const x1 = cx + 42;
        g.beginPath();
        g.moveTo(x0, 40);
        g.bezierCurveTo(x0, 6, x1, 14, x1, CY);
        g.strokeStyle = C.faint;
        g.lineWidth = 1.5;
        g.stroke();
        // A packet now and then, each way.
        for (let n = 0; n < 6; n++) {
          const u = ((((t - MOVE[1]) * 0.9 + n / 6) % 1) + 1) % 1;
          const toCdj = n % 2 === 0;
          const k = toCdj ? u : 1 - u;
          const v = 1 - k;
          const px = v * v * v * x0 + 3 * v * v * k * x0 + 3 * v * k * k * x1 + k * k * k * x1;
          const py = v * v * v * 40 + 3 * v * v * k * 6 + 3 * v * k * k * 14 + k * k * k * CY;
          box(g, px - 3.5, py - 2.5, 7, 5, toCdj ? C.accent : C.cdj, undefined, 1.5);
        }
        g.globalAlpha = fade;
      }
      cdj(g, cx, CY, S);
      text(g, "CDJ 2", cx + 100 * S, CY - 14, 12, C.cdj, { weight: 700, align: "right" });

      // Its screen: the deck, with two slots lit, one per stick.
      const list = span(t, SLOTS[0] - 0.4, SLOTS[0] - 0.2);
      if (list > 0) {
        const x = cx + 20 * S;
        const top = CY + 8 * S;
        const w = 60 * S;
        g.globalAlpha = fade * list;
        box(g, x, top, w, 13, C.bar);
        text(g, "TRIMIXXX", x + 5, top + 7, 9.5, C.accent, { weight: 700 });
        STICKS.forEach((stick, i) => {
          const lit = span(t, SLOTS[i] ?? 0, (SLOTS[i] ?? 0) + 0.15);
          const bx = x + 5 + i * (w / 2 - 2);
          const bw = w / 2 - 8;
          box(
            g,
            bx,
            top + 16,
            bw,
            20,
            lit > 0 ? C.accent : undefined,
            lit > 0 ? C.accent : C.faint,
            2,
          );
          text(g, stick.slot, bx + bw / 2, top + 26, 11, lit > 0 ? C.onAccent : C.faint, {
            weight: 700,
            align: "center",
          });
          g.globalAlpha = fade * list * lit;
          text(g, stick.name, bx + bw / 2, top + 44.5, 9.5, C.text, { align: "center" });
          g.globalAlpha = fade * list;
        });
        // The bezel again, over the header's edge.
        g.globalAlpha = fade;
        box(g, x, top, w, 30 * S, undefined, C.cdj, S);
      }
    }
  },
};

export default scene;
