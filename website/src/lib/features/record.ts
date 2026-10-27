// Record set: while you play, the deck records the mixer's output. The red dot
// blinks, the time runs, and the night's waveform piles up, the whole set
// squeezed into one strip; below it, the home list's End recording row and its
// meters. Pushed, the deck writes the set to the stick, SAM1, and two files
// are there: the recording, and the History's track list beside it, by the
// names the deck gives them.
import {
  C,
  type G,
  type Scene,
  box,
  circle,
  clamp,
  clear,
  during,
  line,
  random,
  row,
  segments,
  span,
  stick,
  text,
} from "./kit";

const LENGTH = 10.5;
// 1:42:17 of set, recorded between these two moments of the loop.
const SET = 3600 + 42 * 60 + 17;
const REC_FROM = 0.15;
const REC_TO = 5;
const WRITE_FROM = 5.25;
const WRITE_TO = 7.25;
const FILES_AT = 7.6;

const X0 = 20;
const X1 = 460;
const STRIP_Y = 68;
const STRIP_H = 104;
const COLUMNS = (X1 - X0) / 2;

// The tracks of the set, by when each came in (seconds), and the strip they
// make, a column every 28 seconds or so: each track at its own level, its
// kick dropping out for a breakdown, and the mixes between them.
const rand = random(4207);
const STARTS: number[] = [0];
for (let at = 0; at + 640 < SET;) {
  at += 380 + rand() * 160;
  STARTS.push(at);
}
const LEVELS = STARTS.map(() => 0.56 + 0.26 * rand());
const BREAKS = STARTS.map(() => 0.34 + 0.24 * rand());
const RAW = Array.from({ length: COLUMNS }, (_, i): [number, number, number] => {
  const s = ((i + 0.5) / COLUMNS) * SET;
  const k = STARTS.findLastIndex((start) => start <= s);
  const from = STARTS[k] ?? 0;
  const to = STARTS[k + 1] ?? SET;
  const u = (s - from) / (to - from);
  const level = LEVELS[k] ?? 0.7;
  const into = BREAKS[k] ?? 0.45;
  const inBreak = u > into && u < into + 0.16;
  const mixing = Math.min(1, u / 0.12, (1 - u) / 0.12);
  return [
    inBreak ? 0.1 : level * (0.8 + 0.2 * mixing) + 0.1 * (rand() - 0.5),
    0.36 + 0.14 * rand() + (inBreak ? 0.14 : 0),
    0.16 + 0.1 * rand() + (inBreak ? 0 : 0.06 * level),
  ];
});
// Smoothed a little, so the strip reads as a set squeezed small, not as noise.
const STRIP = RAW.map((column, i): [number, number, number] => {
  const before = RAW[i - 1] ?? column;
  const after = RAW[i + 1] ?? column;
  const soft = (b: number): number =>
    ((before[b] ?? 0) + 2 * (column[b] ?? 0) + (after[b] ?? 0)) / 4;
  return [soft(0), soft(1), soft(2)];
});

const TRACK_LIST = ["0:00:00  Low Tide", "0:07:31  Copper", "0:14:58  Night Bus"];

/** The time so far, H:MM:SS in seven segments, right-aligned on `right`. */
function clock(g: G, seconds: number, right: number, y: number, h: number, colour: string): void {
  const whole = Math.floor(seconds);
  const groups = [
    String(Math.floor(whole / 3600)),
    String(Math.floor((whole % 3600) / 60)).padStart(2, "0"),
    String(whole % 60).padStart(2, "0"),
  ];
  const pitch = h * 0.73;
  const colon = pitch * 0.45;
  let x = right - (5 * pitch + 2 * colon);
  groups.forEach((group, i) => {
    if (i > 0) {
      // Lit like the digits, leaning with them.
      g.save();
      g.transform(1, 0, -0.12, 1, 0, 0);
      g.fillStyle = colour;
      const dot = h * 0.12;
      g.fillRect(x + colon / 2 - dot * 1.4, y + h * 0.28, dot, dot);
      g.fillRect(x + colon / 2 - dot * 1.4, y + h * 0.66, dot, dot);
      g.restore();
      x += colon;
    }
    segments(g, group, x, y, h, colour);
    x += group.length * pitch;
  });
}

const scene: Scene = {
  length: LENGTH,
  still: 9.6,
  draw(g: G, t: number) {
    clear(g);
    const fade = during(t, 0, LENGTH, 0.35);
    g.globalAlpha = fade;

    // How much of the set is down: slow at first, so the seconds are seen to
    // tick, then the night goes by.
    const p = clamp((t - REC_FROM) / (REC_TO - REC_FROM)) ** 1.7;
    const recording = t >= REC_FROM && t < REC_TO;
    const blink = recording && (t - REC_FROM) % 0.8 < 0.5;

    // REC, and the time so far.
    circle(g, 30, 34, 9, blink ? C.cue : undefined, recording ? C.cue : C.faint, 1.5);
    text(g, "REC", 48, 35, 16, recording ? C.cue : C.faint, { weight: 700 });
    clock(g, p * SET, 460, 18, 32, C.segment);

    // The set's waveform, piling up behind the recording head.
    const head = X0 + p * (X1 - X0);
    const mid = STRIP_Y + STRIP_H / 2;
    line(g, head, mid, X1, mid, C.ruleStrong, 1);
    const bands = [C.low, C.mid, C.high];
    const gains = [1, 0.78, 0.5];
    STRIP.forEach((column, i) => {
      const x = X0 + i * 2;
      if (x >= head) return;
      for (let b = 0; b < 3; b++) {
        const v = (column[b] ?? 0) * (gains[b] ?? 1) * STRIP_H * 0.5;
        g.fillStyle = bands[b] ?? C.high;
        g.fillRect(x, mid - v, 2, v * 2);
      }
    });
    // Where each track came in.
    for (const start of STARTS.slice(1)) {
      const x = X0 + (start / SET) * (X1 - X0);
      if (x < head) line(g, x, STRIP_Y + STRIP_H + 5, x, STRIP_Y + STRIP_H + 11, C.faint, 1);
    }
    if (recording) line(g, head, STRIP_Y - 6, head, STRIP_Y + STRIP_H + 6, C.cue, 1.5);

    // The home list's row while it records, with its meters; pushed to end it.
    if (t < WRITE_FROM) {
      const pushed = t >= REC_TO;
      box(g, 0, 222, 480, 70, pushed ? C.high : C.accent);
      circle(g, 30, 246, 10, C.onAccent);
      circle(g, 30, 246, 6, pushed ? C.faint : C.cue);
      text(g, "End recording", 52, 246, 15, C.onAccent);
      const tick = Math.floor(t * 11);
      ["L", "R"].forEach((side, i) => {
        const y = 266 + i * 12;
        const level = recording ? 0.62 + 0.3 * random(tick * 2 + i)() : 0;
        text(g, side, 52, y + 3, 9, C.onAccent);
        box(g, 66, y, 394, 6, "rgb(12 12 12 / 22%)");
        box(g, 66, y, 394 * level, 6, C.onAccent);
      });
    }

    // Written to the stick, as the deck says it: 16 MB at a time.
    if (t >= WRITE_FROM && t < FILES_AT) {
      const done = Math.floor(clamp((t - WRITE_FROM) / (WRITE_TO - WRITE_FROM)) * 16) / 16;
      text(g, "Write the set to", 20, 208, 12, C.accent, { weight: 700 });
      box(g, 0, 222, 480, 70, C.accent);
      stick(g, 30, 241, 0.66, "", C.onAccent);
      text(g, "SAM1", 52, 244, 15, C.onAccent, { weight: 700 });
      text(g, "Don't remove it until it's written", 52, 266, 10, C.onAccent);
      const state = done < 1 ? `Writing ${String(Math.round(done * 100))}%` : "Written";
      text(g, state, 466, 254, 11, C.onAccent, { align: "right" });
      box(g, 12, 282, 456, 3, "rgb(12 12 12 / 22%)");
      box(g, 12, 282, 456 * done, 3, C.onAccent);
    }

    // On the stick: the recording, and its track list.
    if (t >= FILES_AT) {
      stick(g, 28, 196, 0.5, "", C.accent);
      text(g, "SAM1", 44, 205, 13, C.accent, { weight: 700 });
      g.globalAlpha = fade * span(t, FILES_AT, FILES_AT + 0.25);
      row(g, 0, 222, 480, 36, "recorded-set-00004207.mp3", "245 MB");
      g.globalAlpha = fade * span(t, FILES_AT + 0.3, FILES_AT + 0.55);
      row(g, 0, 258, 480, 36, "recorded-set-track-list-00004207.txt", "1 kB");
      TRACK_LIST.forEach((entry, i) => {
        const at = FILES_AT + 0.8 + i * 0.25;
        g.globalAlpha = fade * span(t, at, at + 0.2);
        text(g, entry, 32, 310 + i * 15, 11, C.dim);
      });
      g.globalAlpha = fade;
    }
  },
};

export default scene;
