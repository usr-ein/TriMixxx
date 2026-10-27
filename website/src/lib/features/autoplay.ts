// Autoplay: under its genre badge, the deck plays the genre's tracks back to
// back, each the nearest in BPM to the last and none twice. A track ends and
// the next starts at once, with no crossfade, as Autoplay has none: the
// overview's playhead reaches the end, and the title, the overview and the
// seven segments switch over. Time runs fast here, a track in a few seconds.
// The queue, by BPM, crosses off what has played; the jog turns with nobody at
// it, from the top again with each track.
import {
  C,
  FONT,
  type G,
  type Scene,
  box,
  circle,
  clamp,
  clear,
  during,
  line,
  random,
  segments,
  text,
} from "./kit";

const LENGTH = 10.5;
// Seconds of track per second of loop.
const RATE = 90;

interface Track {
  title: string;
  bpm: number;
  seconds: number;
  /** Where the kick is out (intro, breakdowns, outro), as parts of the track. */
  quiet: readonly (readonly [number, number])[];
  /** How much there is besides the kick. */
  body: number;
}

// The genre's tracks, nearest in BPM first. The names are made up.
const QUEUE: readonly Track[] = [
  {
    title: "Copper",
    bpm: 122,
    seconds: 360,
    quiet: [
      [0, 0.1],
      [0.36, 0.46],
      [0.7, 0.76],
      [0.9, 1],
    ],
    body: 0.3,
  },
  {
    title: "Night Bus",
    bpm: 122.5,
    seconds: 315,
    quiet: [
      [0, 0.22],
      [0.52, 0.64],
      [0.86, 1],
    ],
    body: 0.42,
  },
  {
    title: "Glass Floor",
    bpm: 123.1,
    seconds: 450,
    quiet: [
      [0, 0.05],
      [0.26, 0.31],
      [0.56, 0.72],
      [0.95, 1],
    ],
    body: 0.24,
  },
  {
    title: "Dusk Run",
    bpm: 124,
    seconds: 402,
    quiet: [
      [0, 0.12],
      [0.44, 0.56],
      [0.9, 1],
    ],
    body: 0.34,
  },
  {
    title: "Low Tide",
    bpm: 125.3,
    seconds: 378,
    quiet: [
      [0, 0.08],
      [0.6, 0.7],
      [0.92, 1],
    ],
    body: 0.3,
  },
];
// The loop joins the first track 2 min 6 s in, so it ends at 2.6 s.
const JOINED = 126;
const ENDS = QUEUE.reduce<number[]>((ends, track, i) => {
  const from = ends[i - 1] ?? -JOINED / RATE;
  ends.push(from + track.seconds / RATE);
  return ends;
}, []);

const X0 = 16;
const X1 = 464;
const OVERVIEW_Y = 78;
const OVERVIEW_H = 92;
const COLUMNS = (X1 - X0) / 2;

/** Each track's overview, a column every 2 units: lows, mids, highs. */
const OVERVIEWS = QUEUE.map((track, k) => {
  const rand = random(31 + k * 7);
  return Array.from({ length: COLUMNS }, (_, i): [number, number, number] => {
    const u = (i + 0.5) / COLUMNS;
    const kick = !track.quiet.some(([from, to]) => u >= from && u < to);
    return [
      kick ? 0.7 + 0.12 * rand() : 0.1 + 0.05 * rand(),
      track.body + 0.14 * rand() + (kick ? 0 : 0.1),
      0.12 + 0.12 * rand(),
    ];
  });
});

const clock = (seconds: number): string =>
  `${String(Math.floor(seconds / 60))}:${String(Math.floor(seconds % 60)).padStart(2, "0")}`;

const scene: Scene = {
  length: LENGTH,
  still: 7.2,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    const now = Math.max(
      0,
      ENDS.findIndex((end) => t < end),
    );
    const track = QUEUE[now];
    const overview = OVERVIEWS[now];
    if (!track || !overview) return;
    const started = ENDS[now - 1] ?? -JOINED / RATE;
    const elapsed = (t - started) * RATE;
    const p = clamp(elapsed / track.seconds);

    // The genre, as the deck shows it over its waveform.
    box(g, X0, 16, 100, 46, C.ground, C.ruleStrong, 4);
    text(g, "AUTOPLAY", X0 + 12, 30, 10, C.accent, { weight: 700 });
    text(g, "House", X0 + 12, 49, 15, C.text);

    // What plays, and how long it has left.
    text(g, track.title, 134, 30, 16, C.text, { weight: 700 });
    text(g, `-${clock(track.seconds - elapsed)}`, 134, 51, 12, C.dim);
    segments(g, track.bpm.toFixed(2), 344, 16, 30);
    text(g, "+0.00%", X1, 60, 10, C.dim, { align: "right" });

    // The whole track, the part played dimmer, and the playhead on its way to the end.
    const mid = OVERVIEW_Y + OVERVIEW_H / 2;
    const head = X0 + p * (X1 - X0);
    const bands = [C.low, C.mid, C.high];
    const gains = [1, 0.78, 0.5];
    overview.forEach((column, i) => {
      const x = X0 + i * 2;
      g.globalAlpha = fade * (x < head ? 0.45 : 1);
      for (let b = 0; b < 3; b++) {
        const v = (column[b] ?? 0) * (gains[b] ?? 1) * OVERVIEW_H * 0.5;
        g.fillStyle = bands[b] ?? C.high;
        g.fillRect(x, mid - v, 2, v * 2);
      }
    });
    g.globalAlpha = fade;
    line(g, X1 + 0.5, OVERVIEW_Y - 4, X1 + 0.5, OVERVIEW_Y + OVERVIEW_H + 4, C.faint, 1);
    g.fillStyle = C.high;
    g.fillRect(head - 1, OVERVIEW_Y - 6, 2, OVERVIEW_H + 12);

    // The queue, by BPM: what has played crossed off, what plays lit.
    QUEUE.forEach((entry, i) => {
      const y = 194 + i * 31;
      const playing = i === now;
      const played = i < now;
      box(g, 0, y, 306, 31, playing ? C.accent : C.row);
      line(g, 0, y + 31, 306, y + 31, C.rule, 1);
      const colour = playing ? C.onAccent : played ? C.faint : C.text;
      text(g, entry.title, 16, y + 16, 13, colour);
      text(
        g,
        entry.bpm.toFixed(2),
        294,
        y + 16,
        11,
        playing ? C.onAccent : played ? C.faint : C.dim,
        {
          align: "right",
        },
      );
      if (played) {
        g.font = `400 13px ${FONT}`;
        line(g, 12, y + 16, 20 + g.measureText(entry.title).width, y + 16, C.faint, 1.5);
      }
    });

    // The jog, turning by itself, from the top again with each track.
    const jx = 394;
    const jy = 272;
    const turn = (t - Math.max(0, started)) * Math.PI * 2 * 0.55 - Math.PI / 2;
    circle(g, jx, jy, 66, undefined, C.line);
    circle(g, jx, jy, 58, undefined, C.ruleStrong, 1);
    for (let k = 0; k < 24; k++) {
      const a = turn + (k / 24) * Math.PI * 2;
      circle(g, jx + Math.cos(a) * 62, jy + Math.sin(a) * 62, 1.2, C.faint);
    }
    circle(g, jx, jy, 24, "rgb(68 255 136 / 25%)", C.green, 1);
    line(
      g,
      jx + Math.cos(turn) * 30,
      jy + Math.sin(turn) * 30,
      jx + Math.cos(turn) * 54,
      jy + Math.sin(turn) * 54,
      C.text,
      2,
    );
  },
};

export default scene;
