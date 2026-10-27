// The platter: plays the track at any speed, backwards included, which is all
// scratching is. It runs in the audio thread (an AudioWorklet), so a hand on
// the waveform moves the sound without waiting for the page.
//
// The page tells it what the deck is doing: playing, stopped, or held at a
// position by a finger. Held, it chases the finger's position, and the speed
// it needs to get there is the speed the record plays at.

declare const sampleRate: number;
declare const currentTime: number;
declare class AudioWorkletProcessor {
  readonly port: MessagePort;
}
declare function registerProcessor(name: string, processor: typeof AudioWorkletProcessor): void;

export type ToPlatter =
  | { type: "load"; left: Int16Array; right: Int16Array }
  | { type: "play" }
  | { type: "stop" }
  | { type: "seek"; seconds: number }
  | { type: "hold"; seconds: number }
  | { type: "move"; seconds: number }
  | { type: "release"; play: boolean };

export type FromPlatter =
  { type: "position"; seconds: number; speed: number; time: number } | { type: "ended" };

const SCALE = 1 / 32768;

class Platter extends AudioWorkletProcessor {
  private left: Int16Array = new Int16Array(0);
  private right: Int16Array = new Int16Array(0);
  /** Where the needle is, in samples. */
  private pos = 0;
  /** How fast the record turns: 1 is playing, 0 stopped, negative backwards. */
  private speed = 0;
  private playing = false;
  private held = false;
  /** The finger's position, in samples, while held. */
  private target = 0;
  private blocks = 0;

  constructor() {
    super();
    this.port.onmessage = (event: MessageEvent<ToPlatter>) => {
      const message = event.data;
      switch (message.type) {
        case "load":
          this.left = message.left;
          this.right = message.right;
          this.pos = 0;
          this.speed = 0;
          break;
        case "play":
          this.playing = true;
          break;
        case "stop":
          this.playing = false;
          break;
        case "seek":
          this.pos = this.clamp(message.seconds * sampleRate);
          break;
        case "hold":
          this.held = true;
          this.target = this.clamp(message.seconds * sampleRate);
          break;
        case "move":
          this.target = this.clamp(message.seconds * sampleRate);
          break;
        case "release":
          this.held = false;
          this.playing = message.play;
          break;
      }
    };
  }

  private clamp(samples: number): number {
    return Math.min(Math.max(samples, 0), Math.max(this.left.length - 2, 0));
  }

  /** The sample at a fractional position, by cubic interpolation. */
  private read(channel: Int16Array, at: number): number {
    const i = Math.floor(at);
    const f = at - i;
    const last = channel.length - 1;
    const y0 = channel[Math.max(i - 1, 0)] ?? 0;
    const y1 = channel[Math.min(i, last)] ?? 0;
    const y2 = channel[Math.min(i + 1, last)] ?? 0;
    const y3 = channel[Math.min(i + 2, last)] ?? 0;
    const c1 = 0.5 * (y2 - y0);
    const c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
    const c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return (((c3 * f + c2) * f + c1) * f + y1) * SCALE;
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const out = outputs[0];
    const outLeft = out?.[0];
    if (!out || !outLeft) return true;
    const outRight = out[1] ?? outLeft;
    const length = this.left.length;
    // The motor reaches speed in about 20 ms; a hand, in about 2.
    const motor = 1 - Math.exp(-1 / (0.02 * sampleRate));
    const hand = 1 - Math.exp(-1 / (0.002 * sampleRate));
    for (let i = 0; i < outLeft.length; i++) {
      if (this.held) {
        const wanted = (this.target - this.pos) / (0.012 * sampleRate);
        this.speed += (Math.max(-12, Math.min(12, wanted)) - this.speed) * hand;
      } else {
        this.speed += ((this.playing ? 1 : 0) - this.speed) * motor;
      }
      if (length === 0) {
        outLeft[i] = 0;
        outRight[i] = 0;
        continue;
      }
      // Slow is quiet, as on a record.
      const level = Math.min(1, Math.abs(this.speed) / 0.12);
      outLeft[i] = this.read(this.left, this.pos) * level;
      outRight[i] = this.read(this.right, this.pos) * level;
      this.pos += this.speed;
      if (this.pos >= length - 2) {
        this.pos = length - 2;
        if (this.playing && !this.held) {
          this.playing = false;
          this.speed = 0;
          this.port.postMessage({ type: "ended" } satisfies FromPlatter);
        }
      } else if (this.pos < 0) {
        this.pos = 0;
      }
    }
    // About every 6 ms, where the needle is.
    if (++this.blocks % 2 === 0) {
      this.port.postMessage({
        type: "position",
        seconds: this.pos / sampleRate,
        speed: this.speed,
        time: currentTime,
      } satisfies FromPlatter);
    }
    return true;
  }
}

registerProcessor("platter", Platter);
