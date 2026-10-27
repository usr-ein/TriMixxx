// History: a set, played by the deck and a CDJ, laid out as it happens. Each
// track a bar from when it started to when it stopped; where two overlap, a
// mix. Under it, History's own list fills in, a row a track and a row a mix
// ("A | B"), each with how long it lasted.
import {
  C,
  type G,
  type Scene,
  box,
  clear,
  clamp,
  during,
  line,
  row,
  span,
  text,
  topbar,
} from "./kit";

interface Play {
  title: string;
  on: "TRIMIXXX" | "CDJ 2";
  from: number;
  to: number;
}

// Minutes into the set. The names are made up.
const PLAYS: readonly Play[] = [
  { title: "Low Tide", on: "CDJ 2", from: 0, to: 9 },
  { title: "Copper", on: "TRIMIXXX", from: 7.5, to: 16 },
  { title: "Night Bus", on: "CDJ 2", from: 14.5, to: 24 },
  { title: "Glass Floor", on: "TRIMIXXX", from: 22.5, to: 31 },
  { title: "Dusk Run", on: "CDJ 2", from: 29.5, to: 38 },
];
const SET = 40;
const SWEEP = 6.5;
const X0 = 26;
const X1 = 454;

const at = (minutes: number): number => X0 + (minutes / SET) * (X1 - X0);
const length = (minutes: number): string => {
  const whole = Math.floor(minutes);
  return `${String(whole)}m${String(Math.round((minutes - whole) * 60)).padStart(2, "0")}s`;
};

const scene: Scene = {
  length: 10.5,
  still: 9,
  draw(g: G, t: number) {
    clear(g);
    g.globalAlpha = during(t, 0, 10.5, 0.35);
    topbar(g, 0, 0, 480, ["History", "This session"]);
    const now = clamp(t / SWEEP) * SET;

    // The minutes along the top.
    for (let m = 0; m <= SET; m += 10) {
      line(g, at(m), 40, at(m), 46, C.faint, 1);
      text(g, `${String(m)}'`, at(m), 52, 9, C.faint, { align: "center" });
    }

    // The bars, growing as the set plays.
    PLAYS.forEach((play, i) => {
      if (now <= play.from) return;
      const y = 62 + i * 25;
      const end = Math.min(now, play.to);
      const colour = play.on === "TRIMIXXX" ? C.accent : C.cdj;
      box(g, at(play.from), y, Math.max(2, at(end) - at(play.from)), 18, undefined, colour, 3);
      g.globalAlpha *= 0.22;
      box(g, at(play.from), y, Math.max(2, at(end) - at(play.from)), 18, colour, undefined, 3);
      g.globalAlpha = during(t, 0, 10.5, 0.35);
      if (at(end) - at(play.from) > 60) text(g, play.title, at(play.from) + 6, y + 9, 10, C.text);
      // Where it overlaps the one before: the mix.
      const before = PLAYS[i - 1];
      if (before && now > play.from) {
        const a = play.from;
        const b = Math.min(before.to, now);
        if (b > a) {
          g.fillStyle = "rgb(255 255 255 / 85%)";
          g.fillRect(at(a), y - 7, at(b) - at(a), 3);
        }
      }
    });

    // Now.
    if (t < SWEEP + 0.4) {
      line(g, at(now), 56, at(now), 192, C.high, 1.5);
    }

    // History's list: newest first, as each track or mix ends.
    const done: { label: string; sub: string; time: string; end: number }[] = [];
    PLAYS.forEach((play, i) => {
      done.push({
        label: play.title,
        sub: play.on,
        time: length(play.to - play.from),
        end: play.to,
      });
      const next = PLAYS[i + 1];
      if (next && next.from < play.to) {
        done.push({
          label: `${play.title} | ${next.title}`,
          sub: `${play.on} | ${next.on}`,
          time: length(play.to - next.from),
          end: play.to + 0.01,
        });
      }
    });
    const shown = done
      .filter((entry) => entry.end <= now)
      .sort((a, b) => b.end - a.end)
      .slice(0, 4);
    shown.forEach((entry, i) => {
      const y = 200 + i * 40;
      const fresh = span(t, (entry.end / SET) * SWEEP, (entry.end / SET) * SWEEP + 0.35);
      g.globalAlpha = during(t, 0, 10.5, 0.35) * (i === 0 ? fresh : 1);
      row(g, 0, y, 480, 40, entry.label, entry.time, i === 0, entry.sub);
    });
  },
};

export default scene;
