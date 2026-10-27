// The deck view at work: drawing the waveforms as the track plays, and the
// hands on it. Tap the waveform: play or stop. Drag it: scratch. Tap or drag
// the overview: jump there. The knob zooms; Space plays; ← and → move a beat.
import { engine } from "./engine";
import {
  type Bands,
  type Track,
  clock,
  drawOverview,
  drawScrolling,
  fetchBands,
  paintOverview,
} from "./waveform";

/** How many seconds the waveform shows, per zoom step. */
const SPANS = [2, 3, 4.5, 6.5, 9, 13, 19, 28] as const;
let zoom = 3;
let cached: { url: string; bands: Bands } | undefined;

function typing(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName))
  );
}

function mount(root: HTMLElement): () => void {
  const track = JSON.parse(root.dataset.track ?? "{}") as Track;
  const audioUrl = root.dataset.audio ?? "";
  const waveUrl = root.dataset.wave ?? "";
  const area = root.querySelector<HTMLElement>("[data-wave-area]");
  const wave = root.querySelector<HTMLCanvasElement>("[data-wave-canvas]");
  const over = root.querySelector<HTMLCanvasElement>("[data-overview-canvas]");
  const g = wave?.getContext("2d");
  const go = over?.getContext("2d");
  const elapsed = root.querySelector("[data-elapsed]");
  const remaining = root.querySelector("[data-remaining]");
  const cells = [...root.querySelectorAll(".dv__phase span")];
  if (!area || !wave || !over || !g || !go) return () => undefined;

  let bands = cached?.url === waveUrl ? cached.bands : undefined;
  let painted: HTMLCanvasElement | undefined;
  if (!bands) {
    void fetchBands(waveUrl, track.points).then((loaded) => {
      bands = loaded;
      cached = { url: waveUrl, bands: loaded };
      painted = undefined;
    });
  }
  engine.duration ||= track.duration;
  void engine.load(audioUrl);

  const resize = (): void => {
    const ratio = Math.min(devicePixelRatio, 2);
    for (const canvas of [wave, over]) {
      const box = canvas.getBoundingClientRect();
      canvas.width = Math.max(1, Math.round(box.width * ratio));
      canvas.height = Math.max(1, Math.round(box.height * ratio));
    }
    painted = undefined;
  };
  const observer = new ResizeObserver(resize);
  observer.observe(wave);
  observer.observe(over);
  resize();

  const span = (): number => SPANS[zoom] ?? 6.5;
  const beat = 60 / track.bpm;
  let frame = 0;
  let lastText = "";
  const draw = (): void => {
    frame = requestAnimationFrame(draw);
    const position = engine.position();
    drawScrolling(g, wave.width, wave.height, track, bands, position, span());
    if (bands) {
      painted ??= paintOverview(over.width, over.height, track, bands);
      drawOverview(go, painted, track, position);
    }
    const text = clock(position);
    if (text !== lastText) {
      lastText = text;
      if (elapsed) elapsed.textContent = text;
      if (remaining) remaining.textContent = `-${clock(track.duration - position)}`;
    }
    const n = Math.floor((position - track.firstBeat) / beat);
    cells.forEach((cell, i) => cell.classList.toggle("is-on", n >= 0 && n % 4 === i));
  };
  draw();

  // A hand on the waveform.
  let hand: { x: number; from: number; moved: boolean } | undefined;
  wave.addEventListener("pointerdown", (event) => {
    wave.setPointerCapture(event.pointerId);
    engine.unlock();
    hand = { x: event.clientX, from: 0, moved: false };
  });
  wave.addEventListener("pointermove", (event) => {
    if (!hand) return;
    if (!hand.moved && Math.abs(event.clientX - hand.x) > 6) {
      hand.moved = true;
      hand.x = event.clientX;
      engine.hold();
      hand.from = engine.position();
    }
    if (hand.moved) {
      engine.move(hand.from - ((event.clientX - hand.x) / wave.clientWidth) * span());
    }
  });
  const letGo = (event: PointerEvent): void => {
    if (!hand) return;
    if (hand.moved) engine.release();
    else if (event.type === "pointerup") engine.toggle();
    hand = undefined;
  };
  wave.addEventListener("pointerup", letGo);
  wave.addEventListener("pointercancel", letGo);

  // A trackpad's sideways swipe nudges the track; the wheel zooms.
  let wheel = 0;
  wave.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      if (Math.abs(event.deltaX) > Math.abs(event.deltaY)) {
        engine.seek(engine.position() + (event.deltaX / wave.clientWidth) * span());
        return;
      }
      wheel += event.deltaMode === 1 ? event.deltaY * 16 : event.deltaY;
      if (Math.abs(wheel) >= 60) {
        zoom = Math.max(0, Math.min(SPANS.length - 1, zoom + Math.sign(wheel)));
        wheel = 0;
      }
    },
    { passive: false },
  );

  // The needle search: the overview, tapped or dragged.
  let searching = false;
  const search = (event: PointerEvent): void => {
    const box = over.getBoundingClientRect();
    engine.seek(((event.clientX - box.left) / box.width) * track.duration);
  };
  over.addEventListener("pointerdown", (event) => {
    over.setPointerCapture(event.pointerId);
    searching = true;
    search(event);
  });
  over.addEventListener("pointermove", (event) => {
    if (searching) search(event);
  });
  over.addEventListener("pointerup", () => {
    searching = false;
  });
  over.addEventListener("pointercancel", () => {
    searching = false;
  });

  const onZoom = (event: Event): void => {
    const detents = (event as CustomEvent<number>).detail;
    zoom = Math.max(0, Math.min(SPANS.length - 1, zoom + detents));
  };
  const onKey = (event: KeyboardEvent): void => {
    if (typing(event.target) || event.metaKey || event.ctrlKey || event.altKey) return;
    const onButton = event.target instanceof HTMLButtonElement;
    if ((event.key === " " && !onButton) || (event.key === "Enter" && event.target === area)) {
      event.preventDefault();
      engine.toggle();
    } else if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
      event.preventDefault();
      engine.seek(engine.position() + (event.key === "ArrowLeft" ? -beat : beat));
    }
  };
  const pressed = (): void => {
    area.setAttribute("aria-pressed", String(engine.playing));
  };
  document.addEventListener("deck:zoom", onZoom);
  document.addEventListener("keydown", onKey);
  const unsubscribe = engine.subscribe(pressed);
  pressed();

  return () => {
    cancelAnimationFrame(frame);
    observer.disconnect();
    document.removeEventListener("deck:zoom", onZoom);
    document.removeEventListener("keydown", onKey);
    unsubscribe();
  };
}

let unmount: (() => void) | undefined;
document.addEventListener("astro:page-load", () => {
  unmount?.();
  unmount = undefined;
  const root = document.querySelector<HTMLElement>("[data-deck-view]");
  if (root) unmount = mount(root);
});
document.addEventListener("astro:before-swap", () => {
  unmount?.();
  unmount = undefined;
});
