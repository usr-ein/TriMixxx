// The deck's one deck: the track, the platter it plays on, and where the
// needle is. One for the whole visit: the client router keeps this module
// alive from page to page, so the music carries on while you browse.
import type { FromPlatter, ToPlatter } from "./processor";
import platterUrl from "./processor.ts?worker&url";

declare global {
  interface Navigator {
    /** Safari's: "playback" plays through the mute switch, as music should. */
    audioSession?: { type: string };
  }
}

type Listener = () => void;

class Engine {
  private context: AudioContext | undefined;
  private platter: AudioWorkletNode | undefined;
  private ready: Promise<AudioWorkletNode> | undefined;
  private loading: Promise<void> | undefined;
  private loadedUrl: string | undefined;
  private listeners = new Set<Listener>();

  /** Whether the track is playing, or will as soon as it has loaded. */
  playing = false;
  loaded = false;
  held = false;
  duration = 0;

  private reported = 0;
  private reportedSpeed = 0;
  private reportedAt = 0;
  private heldAt = 0;

  /** Wakes the sound up. Call it from a tap or a key: browsers ask for one. */
  unlock(): void {
    if (navigator.audioSession) navigator.audioSession.type = "playback";
    const context = this.ensureContext();
    if (context.state !== "running") void context.resume();
  }

  private ensureContext(): AudioContext {
    this.context ??= new AudioContext({ latencyHint: "interactive" });
    return this.context;
  }

  private platterNode(): Promise<AudioWorkletNode> {
    this.ready ??= (async () => {
      const context = this.ensureContext();
      await context.audioWorklet.addModule(platterUrl);
      const node = new AudioWorkletNode(context, "platter", { outputChannelCount: [2] });
      node.connect(context.destination);
      node.port.onmessage = (event: MessageEvent<FromPlatter>) => {
        this.hear(event.data);
      };
      this.platter = node;
      return node;
    })();
    return this.ready;
  }

  /** Fetches and decodes the track onto the platter, once. */
  load(url: string): Promise<void> {
    if (this.loadedUrl === url && this.loading) return this.loading;
    this.loadedUrl = url;
    this.loading = (async () => {
      const [, bytes] = await Promise.all([
        this.platterNode(),
        fetch(url).then((response) => response.arrayBuffer()),
      ]);
      const audio = await this.ensureContext().decodeAudioData(bytes);
      const toInt16 = (channel: Float32Array): Int16Array => {
        const out = new Int16Array(channel.length);
        for (let i = 0; i < channel.length; i++) {
          out[i] = Math.max(-32768, Math.min(32767, Math.round((channel[i] ?? 0) * 32767)));
        }
        return out;
      };
      const left = toInt16(audio.getChannelData(0));
      const right = audio.numberOfChannels > 1 ? toInt16(audio.getChannelData(1)) : left.slice();
      this.duration = audio.duration;
      this.send({ type: "load", left, right }, [left.buffer, right.buffer]);
      this.send({ type: "seek", seconds: this.reported });
      this.loaded = true;
      if (this.playing) this.send({ type: "play" });
      this.emit();
    })();
    return this.loading;
  }

  private send(message: ToPlatter, transfer: Transferable[] = []): void {
    this.platter?.port.postMessage(message, transfer);
  }

  private hear(message: FromPlatter): void {
    if (message.type === "position") {
      this.reported = message.seconds;
      this.reportedSpeed = message.speed;
      this.reportedAt = message.time;
    } else {
      this.playing = false;
      this.emit();
    }
  }

  /** Where the needle is, in seconds, as of now. */
  position(): number {
    if (this.held) return this.heldAt;
    const context = this.context;
    if (!context || !this.loaded) return this.reported;
    const since = Math.max(0, Math.min(0.05, context.currentTime - this.reportedAt));
    return Math.max(0, Math.min(this.duration, this.reported + this.reportedSpeed * since));
  }

  play(): void {
    this.unlock();
    this.playing = true;
    this.send({ type: "play" });
    this.emit();
  }

  stop(): void {
    this.playing = false;
    this.send({ type: "stop" });
    this.emit();
  }

  toggle(): void {
    if (this.playing) this.stop();
    else this.play();
  }

  seek(seconds: number): void {
    const to = Math.max(0, Math.min(this.duration || seconds, seconds));
    this.reported = to;
    this.reportedAt = this.context?.currentTime ?? 0;
    this.send({ type: "seek", seconds: to });
  }

  /** A hand on the record: it stops under the finger. */
  hold(): void {
    this.unlock();
    this.heldAt = this.position();
    this.held = true;
    this.send({ type: "hold", seconds: this.heldAt });
  }

  /** The hand moves the record to here. */
  move(seconds: number): void {
    this.heldAt = Math.max(0, Math.min(this.duration || seconds, seconds));
    this.send({ type: "move", seconds: this.heldAt });
  }

  /** The hand lets go; the record carries on as it was. */
  release(): void {
    this.held = false;
    this.reported = this.heldAt;
    this.reportedSpeed = 0;
    this.reportedAt = this.context?.currentTime ?? 0;
    this.send({ type: "release", play: this.playing });
  }

  /** Off: the screen goes dark and the music stops. */
  powerOff(): void {
    this.stop();
    this.seek(0);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private emit(): void {
    for (const listener of this.listeners) listener();
  }
}

export const engine = new Engine();
