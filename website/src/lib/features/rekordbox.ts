// Play rekordbox USB sticks: a stick goes into the deck's socket and its row
// appears at the top of the root list, reading, then counted. In it, a
// playlist fills in row by row, each track with its cover, BPM and key, and
// the selected track's waveform draws itself in, a band at a time, with the
// beat grid and hot cues that came on the stick.
import {
  C,
  type G,
  type Scene,
  bandsAt,
  box,
  clamp,
  clear,
  during,
  lerp,
  line,
  row,
  span,
  text,
  topbar,
} from "./kit";

const LENGTH = 10;

// The deck: its housing, the socket in its side, the screen.
const EDGE = 104;
const SOCKET_Y = 150;
const SX = 136;
const SY = 18;
const SW = 328;
const SH = 324;

const IN = [0.4, 1.3] as const;
const GROWS = 1.5;
const COUNTED = 2.2;
const PICKED = 2.5;
const OPENS = 2.9;
const FILLS = 3.1;
const SECOND = 4.6;
const DRAWS = 4.9;
const GRID = 6.4;

interface Track {
  title: string;
  bpm: string;
  key: string;
  cover: (g: G, x: number, y: number, s: number) => void;
}

// Made-up tracks, their covers in flat colours.
const TRACKS: readonly Track[] = [
  {
    title: "Low Tide",
    bpm: "122.0",
    key: "8A",
    cover: (g, x, y, s) => {
      box(g, x, y, s, s, C.low);
      g.beginPath();
      g.arc(x + s * 0.68, y + s * 0.34, s * 0.18, 0, Math.PI * 2);
      g.fillStyle = C.mid;
      g.fill();
    },
  },
  {
    title: "Copper Wire",
    bpm: "124.0",
    key: "9A",
    cover: (g, x, y, s) => {
      box(g, x, y, s, s, C.segment);
      g.beginPath();
      g.moveTo(x, y + s * 0.7);
      g.lineTo(x + s * 0.7, y);
      g.lineTo(x + s, y);
      g.lineTo(x + s, y + s * 0.15);
      g.lineTo(x + s * 0.15, y + s);
      g.lineTo(x, y + s);
      g.closePath();
      g.fillStyle = C.ground;
      g.fill();
    },
  },
  {
    title: "Night Bus",
    bpm: "124.0",
    key: "4A",
    cover: (g, x, y, s) => {
      box(g, x, y, s, s, C.keySync);
      box(g, x + s * 0.2, y + s * 0.56, s * 0.6, s * 0.2, C.accent);
    },
  },
  {
    title: "Glass Floor",
    bpm: "126.0",
    key: "11B",
    cover: (g, x, y, s) => {
      box(g, x, y, s, s, C.cdj);
      box(g, x + s * 0.3, y + s * 0.3, s * 0.4, s * 0.4, C.high);
    },
  },
  {
    title: "Slow Orbit",
    bpm: "126.0",
    key: "6A",
    cover: (g, x, y, s) => {
      box(g, x, y, s, s, "#1a1a1a");
      g.beginPath();
      g.arc(x + s / 2, y + s / 2, s * 0.28, 0, Math.PI * 2);
      g.strokeStyle = C.green;
      g.lineWidth = 2;
      g.stroke();
    },
  },
];

type Mark = "stick" | "autoplay" | "history" | "record" | "diagnostics" | "power";

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
    case "diagnostics":
      g.arc(x, y + 2, 7, Math.PI, Math.PI * 2);
      g.moveTo(x, y + 2);
      g.lineTo(x + 3.5, y - 2.5);
      g.stroke();
      break;
    case "power":
      g.arc(x, y + 0.5, 6.5, -Math.PI * 0.32, Math.PI * 1.32);
      g.moveTo(x, y - 7);
      g.lineTo(x, y);
      g.stroke();
      break;
  }
}

/** A row of the root list: › and its mark, then its name; a stick has its port's number. */
function rootRow(
  g: G,
  y: number,
  h: number,
  kind: Mark,
  label: string,
  detail: string,
  selected: boolean,
  dimDetail = false,
): void {
  row(g, SX, y, SW, h, "", "", selected);
  const ink = selected ? C.onAccent : C.text;
  const cy = y + h / 2;
  if (kind !== "power") text(g, ">", SX + 10, cy, 12, ink);
  mark(g, kind, SX + 34, cy, ink);
  if (kind === "stick") {
    text(g, "1", SX + 50, cy + 1, 11, selected ? C.onAccent : C.dim);
    text(g, label, SX + 66, cy, 13, ink);
  } else {
    text(g, label, SX + 52, cy, 13, ink);
  }
  if (detail) {
    const colour = selected ? C.onAccent : dimDetail ? C.faint : C.dim;
    text(g, detail, SX + SW - 10, cy, 10, colour, { align: "right" });
  }
}

/** A stick lying on its side, its plug pointing right; `x` is where plug meets body. */
function stickSide(g: G, x: number, y: number, label: string): void {
  box(g, x - 76, y - 19, 76, 38, C.ground, C.text, 6);
  box(g, x, y - 12, 26, 24, C.black, C.text, 1);
  g.fillStyle = C.text;
  g.fillRect(x + 12, y - 7, 5, 3);
  g.fillRect(x + 12, y + 4, 5, 3);
  text(g, label, x - 38, y, 12, C.text, { weight: 700, align: "center" });
}

/** The three bands of a track, each drawn in only as far as its own sweep has got. */
function bands(
  g: G,
  x: number,
  y: number,
  w: number,
  h: number,
  reveal: readonly [number, number, number],
): void {
  const mid = y + h / 2;
  const colours = [C.low, C.mid, C.high];
  const gains = [1, 0.78, 0.5];
  const seconds = 6;
  for (let b = 0; b < 3; b++) {
    const reach = w * clamp(reveal[b] ?? 0);
    if (reach <= 0) continue;
    const top: [number, number][] = [];
    for (let i = 0; i <= reach; i += 2) {
      const s = 30 + (i / w) * seconds;
      top.push([x + i, (bandsAt(s, 5, 124)[b] ?? 0) * (gains[b] ?? 1)]);
    }
    g.beginPath();
    g.moveTo(x, mid);
    for (const [px, v] of top) g.lineTo(px, mid - v * h * 0.46);
    for (let i = top.length - 1; i >= 0; i--) {
      const point = top[i];
      if (point) g.lineTo(point[0], mid + point[1] * h * 0.46);
    }
    g.closePath();
    g.fillStyle = colours[b] ?? C.high;
    g.fill();
  }
}

const scene: Scene = {
  length: LENGTH,
  still: 8,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    // The housing, and the socket in its side.
    box(g, EDGE, 8, 368, 344, undefined, C.faint, 10);
    box(g, EDGE, SOCKET_Y - 14, 30, 28, C.black, C.line, 1);
    box(g, EDGE + 8, SOCKET_Y - 9, 22, 5, C.low);

    // The stick slides in; its plug disappears into the socket.
    const plug = lerp(-140, EDGE, span(t, IN[0], IN[1]));
    g.save();
    g.beginPath();
    g.rect(0, 0, EDGE, 360);
    g.clip();
    stickSide(g, plug, SOCKET_Y, "SAM1");
    g.restore();
    // A click, as it seats.
    const click = during(t, IN[1], IN[1] + 0.3, 0.1);
    if (click > 0) {
      g.globalAlpha = fade * click;
      line(g, EDGE - 3, SOCKET_Y - 26, EDGE - 3, SOCKET_Y - 20, C.accent, 1.5);
      line(g, EDGE - 3, SOCKET_Y + 20, EDGE - 3, SOCKET_Y + 26, C.accent, 1.5);
      g.globalAlpha = fade;
    }

    // The screen.
    g.save();
    g.beginPath();
    g.roundRect(SX, SY, SW, SH, 4);
    g.clip();
    box(g, SX, SY, SW, SH, C.row);

    const open = span(t, OPENS, OPENS + 0.35);
    // The root list, sliding away to the left once the stick is opened.
    if (open < 1) {
      g.save();
      g.translate(-SW * open, 0);
      const grow = span(t, GROWS, GROWS + 0.4);
      const h = 40;
      let y = SY + 26;
      if (grow > 0) {
        g.save();
        g.beginPath();
        g.rect(SX, y, SW, h * grow);
        g.clip();
        const counted = t >= COUNTED;
        rootRow(
          g,
          y,
          h,
          "stick",
          "SAM1",
          counted ? "412 tracks · 9 playlists" : "reading…",
          t >= PICKED,
          !counted,
        );
        g.restore();
        y += h * grow;
      }
      const rest: readonly [Mark, string][] = [
        ["autoplay", "Autoplay"],
        ["history", "History"],
        ["record", "Record set"],
        ["diagnostics", "Diagnostics"],
        ["power", "Shut down"],
      ];
      rest.forEach(([kind, label], i) => {
        rootRow(g, y + i * h, h, kind, label, "", i === 0 && t < PICKED);
      });
      g.restore();
    }

    // The playlist, coming in from the right.
    if (open > 0) {
      g.save();
      g.translate(SW * (1 - open), 0);
      const h = 36;
      TRACKS.forEach((track, i) => {
        const k = span(t, FILLS + i * 0.18, FILLS + i * 0.18 + 0.25);
        if (k <= 0) return;
        const y = SY + 26 + i * h;
        const selected = t >= SECOND ? i === 1 : i === 0;
        g.globalAlpha = fade * k;
        row(g, SX, y, SW, h, "", "", selected);
        const ink = selected ? C.onAccent : C.text;
        const dx = (1 - k) * 14;
        track.cover(g, SX + 8 + dx, y + 5, 26);
        text(g, track.title, SX + 44 + dx, y + h / 2, 13, ink);
        text(g, track.bpm, SX + SW - 46 + dx, y + h / 2, 11, selected ? C.onAccent : C.dim, {
          align: "right",
        });
        text(g, track.key, SX + SW - 10 + dx, y + h / 2, 11, ink, { align: "right" });
      });
      g.globalAlpha = fade;

      // The selected track's waveform, off the stick: lows, then mids, then highs.
      const wx = SX + 10;
      const wy = SY + 26 + 5 * h + 16;
      const ww = SW - 20;
      const wh = SH - (wy - SY) - 12;
      if (t >= DRAWS - 0.2) {
        box(g, wx, wy, ww, wh, C.black, C.ruleStrong, 2);
        g.save();
        g.beginPath();
        g.rect(wx, wy, ww, wh);
        g.clip();
        const reveal = [
          span(t, DRAWS, DRAWS + 0.9),
          span(t, DRAWS + 0.25, DRAWS + 1.15),
          span(t, DRAWS + 0.5, DRAWS + 1.4),
        ] as const;
        bands(g, wx, wy, ww, wh, reveal);
        // The first sweep's leading edge; the others show against it.
        if (reveal[0] > 0 && reveal[0] < 1) {
          line(g, wx + ww * reveal[0], wy, wx + ww * reveal[0], wy + wh, C.high, 1);
        }
        // The beat grid, then the cues, as rekordbox left them.
        const grid = span(t, GRID, GRID + 0.4);
        if (grid > 0) {
          g.globalAlpha = fade * grid;
          const beat = 60 / 124;
          const seconds = 6;
          for (let n = Math.ceil(30 / beat); n * beat <= 30 + seconds; n++) {
            const bx = wx + ((n * beat - 30) / seconds) * ww;
            g.fillStyle = n % 4 === 0 ? C.downbeat : "rgb(204 204 204 / 85%)";
            g.fillRect(bx - 1, wy, 2, wh);
          }
          const cues: readonly [number, string, string][] = [
            [64, C.cue, "CUE"],
            [68, C.green, "A"],
            [72, C.keySync, "B"],
          ];
          for (const [n, colour, label] of cues) {
            const bx = wx + ((n * beat - 30) / seconds) * ww;
            const pop = span(t, GRID + 0.3 + (n - 64) * 0.04, GRID + 0.5 + (n - 64) * 0.04);
            g.globalAlpha = fade * pop;
            g.fillStyle = colour;
            g.beginPath();
            g.moveTo(bx - 6, wy);
            g.lineTo(bx + 6, wy);
            g.lineTo(bx, wy + 8);
            g.closePath();
            g.fill();
            const tw = label.length * 6.6 + 8;
            box(g, bx + 3, wy + wh - 15, tw, 13, colour, undefined, 2);
            text(g, label, bx + 7, wy + wh - 8.5, 9, C.onAccent, { weight: 700 });
          }
          g.globalAlpha = fade;
        }
        g.restore();
      }
      g.restore();
    }

    // The breadcrumb: the house, then, once in, where on the stick.
    topbar(g, SX, SY, SW, []);
    if (open > 0) {
      g.globalAlpha = fade * open;
      topbar(g, SX, SY, SW, ["USB 1", "Playlists", "Warm-up"]);
      g.globalAlpha = fade;
    }
    g.restore();
    box(g, SX, SY, SW, SH, undefined, C.ruleStrong, 4);
  },
};

export default scene;
