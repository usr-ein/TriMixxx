// The deck view's two waveforms, drawn as the deck's skin draws them: three
// bands stacked, the lows blue at the back, the mids yellow over them, the
// highs white in front; a line on every beat, red on the first of each bar;
// the CUE mark; the playhead in the middle.

export interface Track {
  title: string;
  artist: string;
  key: string;
  bpm: number;
  firstBeat: number;
  cue: number;
  duration: number;
  /** Waveform points per second. */
  rate: number;
  points: number;
}

export interface Bands {
  low: Uint8Array;
  mid: Uint8Array;
  high: Uint8Array;
}

const COLOURS = ["#2266ff", "#ffcc00", "#ffffff"] as const;
// How far each band reaches towards the edges, as on the deck.
const GAINS = [1, 0.78, 0.5] as const;

export async function fetchBands(url: string, points: number): Promise<Bands> {
  const bytes = new Uint8Array(await (await fetch(url)).arrayBuffer());
  return {
    low: bytes.subarray(0, points),
    mid: bytes.subarray(points, points * 2),
    high: bytes.subarray(points * 2, points * 3),
  };
}

/** The loudest point of a band between two times. */
function peak(band: Uint8Array, rate: number, from: number, to: number): number {
  const a = from * rate;
  const b = to * rate;
  if (b - a < 1) {
    // Closer in than one point a pixel: between the two points around it.
    const i = Math.floor(a);
    const f = a - i;
    return ((band[i] ?? 0) * (1 - f) + (band[i + 1] ?? 0) * f) / 255;
  }
  let max = 0;
  const end = Math.min(band.length, Math.ceil(b));
  for (let i = Math.max(0, Math.floor(a)); i < end; i++) max = Math.max(max, band[i] ?? 0);
  return max / 255;
}

/** Fills one band as a shape mirrored about the middle. */
function band(
  g: CanvasRenderingContext2D,
  values: Float32Array,
  step: number,
  middle: number,
  reach: number,
): void {
  g.beginPath();
  g.moveTo(0, middle);
  for (let i = 0; i < values.length; i++) g.lineTo(i * step, middle - (values[i] ?? 0) * reach);
  for (let i = values.length - 1; i >= 0; i--) {
    g.lineTo(i * step, middle + (values[i] ?? 0) * reach);
  }
  g.closePath();
  g.fill();
}

/**
 * The scrolling waveform: `span` seconds across, the playhead in the middle
 * at `position`.
 */
export function drawScrolling(
  g: CanvasRenderingContext2D,
  width: number,
  height: number,
  track: Track,
  bands: Bands | undefined,
  position: number,
  span: number,
): void {
  g.fillStyle = "#000";
  g.fillRect(0, 0, width, height);
  const middle = height / 2;
  const reach = height * 0.47;
  const perPixel = span / width;
  const start = position - span / 2;
  // Marks are sized by the deck's proportions, not by a tall phone's height.
  const unit = Math.min(height, width * 0.62);

  if (bands) {
    const step = Math.max(1, Math.round(width / 900));
    const columns = Math.ceil(width / step) + 1;
    const values = new Float32Array(columns);
    [bands.low, bands.mid, bands.high].forEach((data, b) => {
      for (let i = 0; i < columns; i++) {
        const t = start + i * step * perPixel;
        values[i] =
          t < 0 || t > track.duration
            ? 0
            : peak(data, track.rate, t, t + step * perPixel) * (GAINS[b] ?? 1);
      }
      g.fillStyle = COLOURS[b] ?? "#fff";
      band(g, values, step, middle, reach);
    });
  }

  // The grid, over the waveform.
  const beat = 60 / track.bpm;
  const lineWidth = Math.max(1, Math.round(unit / 200));
  // The grid runs from the track's first beat, before the first downbeat too.
  const first = Math.max(
    Math.ceil((start - track.firstBeat) / beat),
    -Math.floor(track.firstBeat / beat),
  );
  const last = Math.floor((start + span - track.firstBeat) / beat);
  for (let n = first; n <= last; n++) {
    const t = track.firstBeat + n * beat;
    if (t > track.duration) break;
    const x = Math.round((t - start) / perPixel - lineWidth / 2);
    g.fillStyle = n % 4 === 0 ? "#ff0000" : "rgb(204 204 204 / 85%)";
    g.fillRect(x, 0, lineWidth, height);
  }

  // The CUE mark.
  const cueX = Math.round((track.cue - start) / perPixel);
  if (cueX > -80 && cueX < width + 80) {
    const label = Math.round(unit / 26);
    g.fillStyle = "#ff0033";
    g.fillRect(cueX - 1, 0, 2, height);
    g.font = `700 ${String(label)}px Meslo, monospace`;
    const text = "CUE";
    const textWidth = g.measureText(text).width;
    const pad = Math.round(label * 0.3);
    g.fillRect(cueX, 0, textWidth + pad * 2, label + pad * 1.4);
    g.fillStyle = "#ffffff";
    g.textBaseline = "top";
    g.fillText(text, cueX + pad, pad * 0.75);
  }

  // The playhead, and its little arrow.
  const px = Math.round(width / 2);
  const head = Math.max(2, Math.round(unit / 220));
  g.fillStyle = "#ffffff";
  g.fillRect(px - Math.floor(head / 2), 0, head, height);
  const arrow = Math.round(unit / 45);
  g.beginPath();
  g.moveTo(px - arrow, 0);
  g.lineTo(px + arrow, 0);
  g.lineTo(px, arrow);
  g.closePath();
  g.fill();
}

/** The whole track, drawn once into its own canvas. */
export function paintOverview(
  width: number,
  height: number,
  track: Track,
  bands: Bands,
): HTMLCanvasElement {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const g = canvas.getContext("2d");
  if (!g) return canvas;
  g.fillStyle = "#0c0c0c";
  g.fillRect(0, 0, width, height);
  const middle = height / 2;
  const reach = height * 0.46;
  const perPixel = track.duration / width;
  const values = new Float32Array(width);
  [bands.low, bands.mid, bands.high].forEach((data, b) => {
    for (let x = 0; x < width; x++) {
      values[x] = peak(data, track.rate, x * perPixel, (x + 1) * perPixel) * (GAINS[b] ?? 1);
    }
    g.fillStyle = COLOURS[b] ?? "#fff";
    band(g, values, 1, middle, reach);
  });
  g.fillStyle = "#333333";
  g.fillRect(0, Math.round(middle), width, 1);
  return canvas;
}

/** The overview, with where the playhead is and the cue. */
export function drawOverview(
  g: CanvasRenderingContext2D,
  painted: HTMLCanvasElement,
  track: Track,
  position: number,
): void {
  const { width, height } = painted;
  g.drawImage(painted, 0, 0);
  const x = Math.round((position / track.duration) * width);
  // What has played, a shade darker.
  g.fillStyle = "rgb(0 0 0 / 38%)";
  g.fillRect(0, 0, x, height);
  const cueX = Math.round((track.cue / track.duration) * width);
  const label = Math.round(height / 4.5);
  g.fillStyle = "#ff0033";
  g.fillRect(cueX, 0, 2, height);
  g.font = `700 ${String(label)}px Meslo, monospace`;
  g.textBaseline = "top";
  g.fillRect(cueX, 0, label * 0.9, label * 1.15);
  g.fillStyle = "#ffffff";
  g.fillText("C", cueX + label * 0.15, label * 0.08);
  const w = Math.max(2, Math.round(height / 36));
  g.fillRect(x - Math.floor(w / 2), 0, w, height);
  const arrow = Math.round(height / 9);
  g.beginPath();
  g.moveTo(x - arrow, 0);
  g.lineTo(x + arrow, 0);
  g.lineTo(x, arrow);
  g.closePath();
  g.moveTo(x - arrow, height);
  g.lineTo(x + arrow, height);
  g.lineTo(x, height - arrow);
  g.closePath();
  g.fill();
}

/** "1:01.01", as the deck writes it. */
export function clock(seconds: number): string {
  const s = Math.max(0, seconds);
  const minutes = Math.floor(s / 60);
  const rest = s - minutes * 60;
  const whole = Math.floor(rest);
  const hundredths = Math.floor((rest - whole) * 100);
  return `${String(minutes)}:${String(whole).padStart(2, "0")}.${String(hundredths).padStart(2, "0")}`;
}
