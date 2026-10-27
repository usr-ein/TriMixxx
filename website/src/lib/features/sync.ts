// Beat sync: a CDJ is master at 128.00; the deck plays at 124.00, its beats
// drifting under the CDJ's. SYNC lights, the deck's tempo rolls over to the
// master's and its grid slides into step, downbeat on downbeat.
import {
  C,
  type G,
  type Scene,
  box,
  clear,
  during,
  lerp,
  line,
  segments,
  span,
  text,
  waveform,
} from "./kit";

const SYNC_AT = 3;
const LOCKED = 3.9;

const scene: Scene = {
  length: 8.5,
  still: 6,
  draw(g: G, t: number) {
    clear(g);
    g.globalAlpha = during(t, 0, 8.5, 0.35);
    const k = span(t, SYNC_AT, LOCKED);
    const bpm = lerp(124, 128, k);
    // Out of step by a little over a third of a beat, until SYNC.
    const phase = lerp(0.17, 0, k);
    const position = 20 + t;

    // The master: a CDJ.
    text(g, "CDJ 2", 20, 32, 13, C.cdj, { weight: 700 });
    box(g, 74, 23, 62, 18, undefined, C.accent, 3);
    text(g, "MASTER", 105, 32, 10, C.accent, { weight: 700, align: "center" });
    segments(g, "128.00", 352, 22, 22);
    waveform(g, 20, 52, 440, 86, position, { seed: 3, bpm: 128, seconds: 4 });

    // The deck.
    text(g, "TRIMIXXX", 20, 176, 13, C.text, { weight: 700 });
    const lit = t >= SYNC_AT;
    box(g, 98, 167, 50, 18, lit ? C.accent : undefined, lit ? C.accent : C.faint, 3);
    text(g, "SYNC", 123, 176, 10, lit ? C.onAccent : C.faint, { weight: 700, align: "center" });
    segments(g, bpm.toFixed(2), 352, 166, 22);
    waveform(g, 20, 196, 440, 86, position, { seed: 8, bpm, seconds: 4, phase });

    // In step: the downbeats, joined.
    if (k > 0.98) {
      const beat = 60 / 128;
      const start = position - 2;
      g.globalAlpha *= span(t, LOCKED, LOCKED + 0.6) * 0.55;
      for (let n = Math.ceil(start / beat); n * beat <= start + 4; n++) {
        if (n % 4 !== 0) continue;
        const x = 20 + ((n * beat - start) / 4) * 440;
        g.setLineDash([3, 4]);
        line(g, x, 138, x, 196, C.accent, 1.5);
        g.setLineDash([]);
      }
      g.globalAlpha = during(t, 0, 8.5, 0.35);
    }

    // The phase meter: where the deck's beat falls against the master's.
    const offset = lit ? lerp(Math.sin(SYNC_AT * 1.7) * 0.4, 0, k) : Math.sin(t * 1.7) * 0.4;
    box(g, 120, 312, 240, 10, "#111111", C.ruleStrong, 2);
    line(g, 240, 306, 240, 328, C.faint, 1);
    box(g, 240 + offset * 110 - 5, 309, 10, 16, lit && k > 0.98 ? C.accent : C.low, undefined, 2);
    text(g, "PHASE", 112, 317, 9, C.faint, { align: "right" });
  },
};

export default scene;
