// Runs the "What can it do?" animations: only the ones on screen, each from
// its start when it comes into view, each as one still frame for those who
// would rather nothing moved. A push of the knob on a feature plays it again.
import { H, W } from "./kit";
import { scenes } from "./scenes";

const started = new WeakMap<HTMLCanvasElement, number>();
let frame = 0;

const still = (): boolean => matchMedia("(prefers-reduced-motion: reduce)").matches;

function draw(canvas: HTMLCanvasElement, now: number): void {
  const scene = scenes[canvas.dataset.feature ?? ""];
  const g = canvas.getContext("2d");
  if (!scene || !g) return;
  const ratio = Math.min(devicePixelRatio, 2);
  const width = Math.round(canvas.clientWidth * ratio);
  const height = Math.round(canvas.clientHeight * ratio);
  if (width === 0 || height === 0) return;
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  let start = started.get(canvas);
  if (start === undefined) {
    start = now;
    started.set(canvas, now);
  }
  const t = still() ? scene.still : ((now - start) / 1000) % scene.length;
  g.setTransform(1, 0, 0, 1, 0, 0);
  g.fillStyle = "#000";
  g.fillRect(0, 0, width, height);
  const scale = Math.min(width / W, height / H);
  g.setTransform(scale, 0, 0, scale, (width - W * scale) / 2, (height - H * scale) / 2);
  g.save();
  scene.draw(g, t);
  g.restore();
}

function tick(now: number): void {
  frame = requestAnimationFrame(tick);
  for (const canvas of document.querySelectorAll<HTMLCanvasElement>("canvas[data-feature]")) {
    if (canvas.closest("[hidden]")) {
      started.delete(canvas);
      continue;
    }
    draw(canvas, now);
  }
}

document.addEventListener("astro:page-load", () => {
  cancelAnimationFrame(frame);
  if (document.querySelector("canvas[data-feature]")) frame = requestAnimationFrame(tick);
});
document.addEventListener("astro:before-swap", () => {
  cancelAnimationFrame(frame);
});
document.addEventListener("deck:activate", (event) => {
  const row = (event as CustomEvent<HTMLElement>).detail;
  const canvas = document.querySelector<HTMLCanvasElement>(
    `[data-art="${row.dataset.index ?? ""}"] canvas[data-feature]`,
  );
  if (canvas) started.delete(canvas);
});
