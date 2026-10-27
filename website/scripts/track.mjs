// The demo track: the audio the deck view plays, and what its screen draws
// from it (three bands, the beat grid, the cue).
//
//   node scripts/track.mjs --standin
//       A loop synthesised here, written to public/track/ (ignored by git):
//       what plays until the real track is in the media repository.
//
//   node scripts/track.mjs song.mp3 --bpm 126 --title "…" --artist "…" --key 8A \
//       [--first-beat 0.312] [--cue 0.312] [--cover cover.jpg]
//       A real track, written to media/trimixxx/track/, to commit there only
//       if the site may publish it. Without --first-beat, the grid is found
//       here: the BPM refined to within a hundredth around --bpm, the first
//       downbeat put on the first kick. Without --cover, the file's own art.
//
//   --if-missing: do nothing when a track is already there (npm's predev and
//   prebuild run --standin with it).
//
// The waveform is the deck's: the signal split at 250 Hz and 3 kHz, each band's
// peak taken 150 times a second and scaled to a byte. Needs ffmpeg.
import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { parseArgs } from "node:util";

const RATE = 44100;
const POINTS_PER_SECOND = 150;

const { values, positionals } = parseArgs({
  allowPositionals: true,
  options: {
    standin: { type: "boolean", default: false },
    "if-missing": { type: "boolean", default: false },
    bpm: { type: "string" },
    "first-beat": { type: "string" },
    cue: { type: "string" },
    title: { type: "string" },
    artist: { type: "string" },
    key: { type: "string" },
    cover: { type: "string" },
    ffmpeg: { type: "string", default: process.env.FFMPEG ?? "ffmpeg" },
  },
});

const site = new URL("..", import.meta.url).pathname;

if (
  values["if-missing"] &&
  (existsSync(join(site, "public/track/track.json")) ||
    existsSync(join(site, "media/trimixxx/track/track.json")))
) {
  process.exit(0);
}

function ffmpeg(args, input) {
  const run = spawnSync(values.ffmpeg, ["-hide_banner", "-loglevel", "error", ...args], {
    input,
    maxBuffer: 1 << 30,
  });
  if (run.status !== 0) {
    throw new Error(`ffmpeg failed: ${run.stderr.toString()}`);
  }
  return run.stdout;
}

// ---- A loop of our own -----------------------------------------------------

/** A seeded random, so the stand-in is the same on every machine. */
function random(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** Synthesises 64 bars at 128 BPM: intro, groove, break, drop, outro. */
function standin() {
  const bpm = 128;
  const beat = 60 / bpm;
  const bars = 64;
  const seconds = bars * 4 * beat + 1.5;
  const length = Math.ceil(seconds * RATE);
  const left = new Float32Array(length);
  const right = new Float32Array(length);
  const noise = random(2026);
  const at = (bar, step) => Math.round(((bar * 16 + step) * beat * RATE) / 4);

  const add = (start, voice, panL = 1, panR = 1) => {
    for (let i = 0; i < voice.length && start + i < length; i++) {
      left[start + i] += voice[i] * panL;
      right[start + i] += voice[i] * panR;
    }
  };

  // A kick: a sine falling from 170 Hz to 48 Hz, and a click.
  const kick = new Float32Array(Math.round(0.42 * RATE));
  {
    let phase = 0;
    for (let i = 0; i < kick.length; i++) {
      const t = i / RATE;
      phase += (2 * Math.PI * (48 + 122 * Math.exp(-t * 32))) / RATE;
      const body = Math.sin(phase) * Math.exp(-t * 7.5);
      const click = (noise() * 2 - 1) * Math.exp(-t * 900) * 0.35;
      kick[i] = Math.tanh((body + click) * 1.6) * 0.95;
    }
  }

  // Noise through two one-pole high-passes, for hats.
  const hat = (decay, cutoff, gain) => {
    const out = new Float32Array(Math.round((6 / decay) * RATE));
    const a = Math.exp((-2 * Math.PI * cutoff) / RATE);
    let x1 = 0;
    let h1 = 0;
    let h1Last = 0;
    let h2 = 0;
    for (let i = 0; i < out.length; i++) {
      const x = noise() * 2 - 1;
      h1 = a * (h1 + x - x1);
      x1 = x;
      h2 = a * (h2 + h1 - h1Last);
      h1Last = h1;
      out[i] = h2 * Math.exp((-i / RATE) * decay) * gain;
    }
    return out;
  };
  const closedHat = hat(70, 8000, 0.28);
  const openHat = hat(11, 7000, 0.2);

  // A clap: band-passed noise, three quick hits and a tail.
  const clap = new Float32Array(Math.round(0.3 * RATE));
  {
    const f = 1400;
    const q = 1.6;
    const w = (2 * Math.PI * f) / RATE;
    const alpha = Math.sin(w) / (2 * q);
    const b0 = alpha / (1 + alpha);
    const a1 = (-2 * Math.cos(w)) / (1 + alpha);
    const a2 = (1 - alpha) / (1 + alpha);
    let x1 = 0;
    let x2 = 0;
    let y1 = 0;
    let y2 = 0;
    for (let i = 0; i < clap.length; i++) {
      const t = i / RATE;
      const x = noise() * 2 - 1;
      const y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      const bursts = t < 0.03 ? Math.exp(-((t % 0.01) * 300)) : 0;
      clap[i] = y * (bursts * 0.9 + Math.exp(-t * 16) * 0.75) * 1.4;
    }
  }

  // A saw, low-passed by a one-pole filter whose cutoff decays.
  const note = (freq, seconds, cutoff, decay, detune = 0) => {
    const out = new Float32Array(Math.round(seconds * RATE));
    let p1 = 0;
    let p2 = 0.37;
    let y = 0;
    for (let i = 0; i < out.length; i++) {
      const t = i / RATE;
      p1 = (p1 + freq / RATE) % 1;
      p2 = (p2 + (freq * (1 + detune)) / RATE) % 1;
      const saw = (p1 * 2 - 1) * 0.5 + (p2 * 2 - 1) * 0.5;
      const c = cutoff * (0.25 + 0.75 * Math.exp(-t * decay));
      const k = 1 - Math.exp((-2 * Math.PI * c) / RATE);
      y += k * (saw - y);
      const env = Math.min(1, t * 400) * Math.exp(-t * (decay * 0.6));
      out[i] = y * env;
    }
    return out;
  };

  const A1 = 55;
  const bassLine = [A1, A1, A1 * 1.5, A1, A1 * 1.189, A1, A1 * 0.891, A1];
  const chord = [220, 261.63, 329.63, 392];
  const stabSteps = [3, 6, 10, 13];

  for (let bar = 0; bar < bars; bar++) {
    const intro = bar < 8;
    const groove = bar >= 8 && bar < 24;
    const brk = bar >= 24 && bar < 32;
    const drop = bar >= 32 && bar < 52;
    const outro = bar >= 56;
    const kickOn = !brk;
    const fade = outro ? 1 - (bar - 56) / 9 : 1;

    for (let step = 0; step < 16; step++) {
      const start = at(bar, step);
      if (kickOn && step % 4 === 0) add(start, kick, 0.9 * fade, 0.9 * fade);
      if (step % 4 === 2 && !brk) add(start, openHat, 0.75 * fade, 0.95 * fade);
      if (step % 2 === 1 || (step % 2 === 0 && (bar >= 8 || step % 4 === 2))) {
        const vel = step % 4 === 2 ? 1 : step % 2 === 1 ? 0.55 : 0.35;
        if (!brk || bar >= 30) add(start, closedHat, vel * fade * 0.9, vel * fade);
      }
      if ((groove || drop) && step % 8 === 4) add(start, clap, 0.85, 0.75);
      if ((groove || drop || bar >= 52) && step % 4 !== 0 && !outro) {
        const f = bassLine[(bar * 3 + step) % bassLine.length];
        add(start, note(f, 0.16, 520, 22), 0.55, 0.55);
      }
      if ((bar >= 16 && bar < 24) || drop || brk) {
        if (stabSteps.includes(step)) {
          const level = brk ? 0.13 : 0.2;
          for (const [i, f] of chord.entries()) {
            const v = note(f, 0.22, brk ? 2400 : 3400, 16, 0.006);
            add(start, v, level * (i % 2 ? 0.75 : 1), level * (i % 2 ? 1 : 0.75));
          }
        }
      }
    }
    // The break: a pad held over each bar, and noise rising into the drop.
    if (brk) {
      for (const f of chord) {
        const v = note(f / 2, 4 * beat, 1300, 0.4, 0.004);
        for (let i = 0; i < v.length; i++) v[i] *= Math.min(1, i / (0.5 * RATE)) * 0.11;
        add(at(bar, 0), v, 0.9, 1);
      }
      if (bar >= 28) {
        const span = at(32, 0) - at(28, 0);
        const from = at(bar, 0) - at(28, 0);
        const piece = new Float32Array(at(bar + 1, 0) - at(bar, 0));
        for (let i = 0; i < piece.length; i++) {
          const x = (from + i) / span;
          piece[i] = (noise() * 2 - 1) * x * x * 0.22;
        }
        add(at(bar, 0), piece, 1, 1);
      }
    }
    if (intro && bar >= 4) {
      for (let step = 0; step < 16; step += 2) {
        add(at(bar, step), note(A1, 0.1, 260, 30), 0.25, 0.25);
      }
    }
  }

  // Master: a little saturation, then normalised to -1 dBFS.
  let peak = 0;
  for (let i = 0; i < length; i++) {
    left[i] = Math.tanh(left[i] * 0.9);
    right[i] = Math.tanh(right[i] * 0.9);
    peak = Math.max(peak, Math.abs(left[i]), Math.abs(right[i]));
  }
  const gain = 0.891 / peak;
  for (let i = 0; i < length; i++) {
    left[i] *= gain;
    right[i] *= gain;
  }
  return {
    left,
    right,
    meta: {
      title: "Stand-in",
      artist: "TriMixxx",
      key: "8A",
      bpm,
      firstBeat: 0,
      cue: 0,
    },
  };
}

// ---- A real track ----------------------------------------------------------

function decode(file) {
  const raw = ffmpeg(["-i", file, "-f", "f32le", "-ac", "2", "-ar", String(RATE), "pipe:1"]);
  const samples = new Float32Array(raw.buffer, raw.byteOffset, raw.byteLength / 4);
  const length = samples.length / 2;
  const left = new Float32Array(length);
  const right = new Float32Array(length);
  for (let i = 0; i < length; i++) {
    left[i] = samples[i * 2];
    right[i] = samples[i * 2 + 1];
  }
  return { left, right };
}

// ---- The grid ------------------------------------------------------------------

/**
 * The beat grid of a track with a steady tempo: the BPM, refined to a
 * hundredth within half a BPM of `approx`, and the first downbeat, put on the
 * first kick. The kick is the lows' envelope, a thousand times a second, where
 * it rises; folded over one beat, the onsets pile up at the beat's phase.
 */
function findGrid(left, right, approx) {
  const mono = new Float32Array(left.length);
  for (let i = 0; i < mono.length; i++) mono[i] = (left[i] + right[i]) / 2;
  const low = biquad(biquad(mono, "low", 150), "low", 150);
  const step = RATE / 1000;
  const n = Math.floor(low.length / step);
  const envelope = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let sum = 0;
    const end = Math.round((i + 1) * step);
    for (let j = Math.round(i * step); j < end; j++) sum += Math.abs(low[j]);
    envelope[i] = sum / step;
  }
  const onset = new Float32Array(n);
  for (let i = 1; i < n; i++) onset[i] = Math.max(0, envelope[i] - envelope[i - 1]);

  let best = { score: -1, bpm: approx, phase: 0 };
  for (let bpm = approx - 0.5; bpm <= approx + 0.5001; bpm += 0.01) {
    const beat = 60000 / bpm;
    const bins = new Float64Array(Math.ceil(beat));
    for (let i = 0; i < n; i++) bins[Math.floor(i % beat)] += onset[i];
    for (let p = 0; p < bins.length; p++) {
      // A little smoothing, so a peak split over two milliseconds still wins.
      const score = bins[p] + 0.5 * ((bins[p - 1] ?? 0) + (bins[p + 1] ?? 0));
      if (score > best.score) best = { score, bpm, phase: p };
    }
  }
  // The first downbeat: the first beat with a kick on it worth the name.
  const beat = 60000 / best.bpm;
  const strengths = [];
  for (let t = best.phase; t < n; t += beat) {
    let max = 0;
    for (let i = Math.max(0, Math.round(t) - 15); i < Math.min(n, Math.round(t) + 15); i++)
      max = Math.max(max, envelope[i]);
    strengths.push(max);
  }
  const typical = [...strengths].sort((a, b) => a - b)[Math.floor(strengths.length * 0.75)] ?? 0;
  const first = Math.max(
    0,
    strengths.findIndex((s) => s > typical * 0.5),
  );
  const bpm = Math.round(best.bpm * 100) / 100;
  return { bpm, firstBeat: (best.phase + first * beat) / 1000 };
}

// ---- The waveform ------------------------------------------------------------

/** An RBJ biquad, run over a whole signal. */
function biquad(signal, type, freq, q = Math.SQRT1_2) {
  const w = (2 * Math.PI * freq) / RATE;
  const cos = Math.cos(w);
  const alpha = Math.sin(w) / (2 * q);
  const a0 = 1 + alpha;
  const b =
    type === "low"
      ? [(1 - cos) / 2, 1 - cos, (1 - cos) / 2]
      : [(1 + cos) / 2, -(1 + cos), (1 + cos) / 2];
  const a1 = (-2 * cos) / a0;
  const a2 = (1 - alpha) / a0;
  const [b0, b1, b2] = b.map((x) => x / a0);
  const out = new Float32Array(signal.length);
  let x1 = 0;
  let x2 = 0;
  let y1 = 0;
  let y2 = 0;
  for (let i = 0; i < signal.length; i++) {
    const x = signal[i];
    const y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    out[i] = y;
  }
  return out;
}

function bands(left, right) {
  const mono = new Float32Array(left.length);
  for (let i = 0; i < mono.length; i++) mono[i] = (left[i] + right[i]) / 2;
  const low = biquad(biquad(mono, "low", 250), "low", 250);
  const high = biquad(biquad(mono, "high", 3000), "high", 3000);
  const mid = biquad(
    biquad(biquad(biquad(mono, "high", 250), "high", 250), "low", 3000),
    "low",
    3000,
  );
  const window = RATE / POINTS_PER_SECOND;
  const points = Math.ceil(mono.length / window);
  const peaks = [low, mid, high].map((band) => {
    const out = new Float32Array(points);
    for (let p = 0; p < points; p++) {
      let max = 0;
      const end = Math.min(band.length, Math.round((p + 1) * window));
      for (let i = Math.round(p * window); i < end; i++) max = Math.max(max, Math.abs(band[i]));
      out[p] = max;
    }
    return out;
  });
  // The three bands to bytes on one scale, as Mixxx keeps them, so a track's
  // lows outweigh its mids as they do on the deck: against the loudest
  // moments of whichever band is loudest (its 99.5th percentile, so one spike
  // doesn't flatten the rest), slightly compressed.
  const loudest = new Float32Array(points);
  for (let p = 0; p < points; p++) loudest[p] = Math.max(peaks[0][p], peaks[1][p], peaks[2][p]);
  loudest.sort();
  const ref = loudest[Math.floor(points * 0.995)] || 1;
  const bytes = new Uint8Array(points * 3);
  peaks.forEach((band, b) => {
    for (let p = 0; p < points; p++) {
      bytes[b * points + p] = Math.round(Math.min(1, (band[p] / ref) ** 0.8) * 255);
    }
  });
  return { bytes, points };
}

// ---- Out -----------------------------------------------------------------------

function wav(left, right) {
  const data = Buffer.alloc(left.length * 4);
  for (let i = 0; i < left.length; i++) {
    data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, left[i])) * 32767), i * 4);
    data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, right[i])) * 32767), i * 4 + 2);
  }
  const header = Buffer.alloc(44);
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + data.length, 4);
  header.write("WAVEfmt ", 8);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(2, 22);
  header.writeUInt32LE(RATE, 24);
  header.writeUInt32LE(RATE * 4, 28);
  header.writeUInt16LE(4, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(data.length, 40);
  return Buffer.concat([header, data]);
}

let signal;
let meta;
let out;
if (values.standin) {
  signal = standin();
  meta = signal.meta;
  out = join(site, "public/track");
} else {
  const [file] = positionals;
  if (!file || !values.bpm || !values.title || !values.artist || !values.key) {
    process.stderr.write(
      "usage: track.mjs --standin | track.mjs FILE --bpm N --title T --artist A --key K\n",
    );
    process.exit(2);
  }
  signal = decode(file);
  const grid =
    values["first-beat"] === undefined
      ? findGrid(signal.left, signal.right, Number(values.bpm))
      : { bpm: Number(values.bpm), firstBeat: Number(values["first-beat"]) };
  meta = {
    title: values.title,
    artist: values.artist,
    key: values.key,
    bpm: grid.bpm,
    firstBeat: grid.firstBeat,
    cue: Number(values.cue ?? grid.firstBeat),
  };
  out = join(site, "media/trimixxx/track");
  process.stdout.write(
    `grid: ${String(grid.bpm)} BPM, first downbeat at ${grid.firstBeat.toFixed(3)} s\n`,
  );
}

const { left, right } = signal;
mkdirSync(out, { recursive: true });
const temp = join(tmpdir(), `trimixxx-track-${String(process.pid)}.wav`);
writeFileSync(temp, wav(left, right));
ffmpeg([
  "-y",
  "-i",
  temp,
  "-c:a",
  "aac",
  "-b:a",
  "128k",
  "-movflags",
  "+faststart",
  join(out, "track.m4a"),
]);
rmSync(temp);

const { bytes, points } = bands(left, right);
writeFileSync(join(out, "track.wave"), bytes);
let cover = false;
if (values.cover) {
  copyFileSync(values.cover, join(out, "cover.jpg"));
  cover = true;
} else if (!values.standin) {
  // The file's own art, when it has some.
  try {
    ffmpeg([
      "-y",
      "-i",
      positionals[0],
      "-map",
      "0:v:0",
      "-frames:v",
      "1",
      "-vf",
      "scale=360:360",
      join(out, "cover.jpg"),
    ]);
    cover = true;
  } catch {
    cover = false;
  }
}
writeFileSync(
  join(out, "track.json"),
  `${JSON.stringify(
    {
      ...meta,
      duration: left.length / RATE,
      rate: POINTS_PER_SECOND,
      points,
      cover: cover ? "cover.jpg" : null,
    },
    null,
    2,
  )}\n`,
);
process.stdout.write(
  `track: ${out} (${(left.length / RATE).toFixed(1)} s, ${String(points)} points)\n`,
);
