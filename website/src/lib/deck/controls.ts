// The deck's controls, on every screen: the browse knob, BACK, the keys that
// stand in for them, and touch. What they do is the deck's own
// (docs/browser-prd.md §4): the knob moves the one selection and a push goes
// in; BACK goes up a level, and from the root list back to the track; held,
// it goes straight back to the track.
import { navigate } from "astro:transitions/client";

import { collapse, handOver, reducedMotion, setArrival, takeArrival, wait } from "./crt";
import { engine } from "./engine";

type Kind = "list" | "info" | "page" | "deck";

const HOLD_MS = 600;
/** One detent of the knob, in degrees. */
const DETENT = 15;

/** The knob's angle survives page changes, as a real knob's would. */
let angle = 0;

const ui = (): HTMLElement | null => document.querySelector<HTMLElement>("[data-ui]");

function kind(): Kind {
  const value = ui()?.dataset.kind;
  return value === "list" || value === "info" || value === "deck" ? value : "page";
}

const rows = (): HTMLElement[] => [...document.querySelectorAll<HTMLElement>("[data-row]")];
const memory = (): string => `trimixxx:selection:${location.pathname}`;

function selectedIndex(): number {
  return rows().findIndex((row) => row.classList.contains("is-selected"));
}

/** Moves the selection, keeps it on screen, and remembers it for BACK. */
function select(index: number, how: { scroll?: boolean; focus?: boolean } = {}): void {
  const list = rows();
  if (list.length === 0) return;
  const i = Math.max(0, Math.min(list.length - 1, index));
  list.forEach((row, n) => row.classList.toggle("is-selected", n === i));
  const row = list[i];
  if (!row) return;
  if (how.scroll) row.scrollIntoView({ block: "nearest", behavior: "auto" });
  if (how.focus) row.focus({ preventScroll: !how.scroll });
  try {
    sessionStorage.setItem(memory(), String(i));
  } catch {
    // Without storage, BACK lands on the first row.
  }
  document.dispatchEvent(new CustomEvent("deck:select", { detail: row }));
}

function restoreSelection(): void {
  if (rows().length === 0) return;
  let index = 0;
  try {
    index = Number(sessionStorage.getItem(memory()) ?? 0) || 0;
  } catch {
    // The first row, then.
  }
  select(index, { scroll: true });
}

function spin(detents: number): void {
  angle += detents * DETENT;
  document
    .querySelector<HTMLElement>("[data-knob] .knob__body")
    ?.style.setProperty("--angle", `${String(angle)}deg`);
}

/** One detent of the knob: +1 clockwise. */
function turn(detents: number, fromKeys = false): void {
  spin(detents);
  switch (kind()) {
    case "list":
    case "info":
      select(selectedIndex() + detents, { scroll: true, focus: fromKeys });
      break;
    case "page": {
      const body = document.querySelector<HTMLElement>("[data-scroll]");
      body?.scrollBy({
        top: detents * body.clientHeight * 0.22,
        behavior: reducedMotion() ? "auto" : "smooth",
      });
      break;
    }
    case "deck":
      document.dispatchEvent(new CustomEvent("deck:zoom", { detail: detents }));
      break;
  }
}

function pressed(element: Element | null, ms = 140): void {
  element?.classList.add("is-pressed");
  setTimeout(() => element?.classList.remove("is-pressed"), ms);
}

/** The knob pushed: go in. */
function push(): void {
  pressed(document.querySelector("[data-knob]"));
  if (kind() === "deck") {
    void navigate("/browse/");
    return;
  }
  const row = rows()[selectedIndex()];
  if (row) activate(row);
}

/** A row's own business: Shut down, or whatever the page makes of "deck:activate". */
function activate(row: HTMLElement): void {
  const action = row.dataset.action;
  if (action === "shutdown") {
    void shutDown();
    return;
  }
  if (action) {
    document.dispatchEvent(new CustomEvent("deck:activate", { detail: row }));
    return;
  }
  if (row instanceof HTMLAnchorElement) void navigate(row.href);
  else row.click();
}

function back(): void {
  if (kind() === "deck") {
    void navigate("/browse/");
    return;
  }
  void navigate(ui()?.dataset.back ?? "/deck/");
}

function backToTrack(): void {
  if (kind() !== "deck") void navigate("/deck/");
}

/** Shut down, as the root list's last row says: dark, then the deck again. */
async function shutDown(): Promise<void> {
  engine.powerOff();
  await collapse(document.querySelector("[data-screen]"));
  setArrival("shutdown");
  await navigate("/");
}

// ---- The knob ---------------------------------------------------------------

function bindKnob(knob: HTMLElement): void {
  spin(0);
  let grip: { cx: number; cy: number; last: number; turned: number; rest: number } | undefined;
  const angleOf = (event: PointerEvent, cx: number, cy: number): number =>
    (Math.atan2(event.clientY - cy, event.clientX - cx) * 180) / Math.PI;

  knob.addEventListener("pointerdown", (event) => {
    const box = knob.getBoundingClientRect();
    const cx = box.left + box.width / 2;
    const cy = box.top + box.height / 2;
    knob.setPointerCapture(event.pointerId);
    knob.classList.add("is-pressed");
    grip = { cx, cy, last: angleOf(event, cx, cy), turned: 0, rest: 0 };
    event.preventDefault();
  });
  knob.addEventListener("pointermove", (event) => {
    if (!grip) return;
    const now = angleOf(event, grip.cx, grip.cy);
    let delta = now - grip.last;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    grip.last = now;
    grip.turned += Math.abs(delta);
    grip.rest += delta;
    while (grip.rest >= DETENT) {
      grip.rest -= DETENT;
      turn(1);
    }
    while (grip.rest <= -DETENT) {
      grip.rest += DETENT;
      turn(-1);
    }
  });
  const letGo = (event: PointerEvent): void => {
    if (!grip) return;
    const wasPush = grip.turned < 10 && event.type === "pointerup";
    grip = undefined;
    knob.classList.remove("is-pressed");
    if (wasPush) push();
  };
  knob.addEventListener("pointerup", letGo);
  knob.addEventListener("pointercancel", letGo);
  // The keyboard's Enter and Space reach the knob as a click with no pointer.
  knob.addEventListener("click", (event) => {
    if (event.detail === 0) push();
  });

  let wheel = 0;
  knob.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      wheel += event.deltaMode === 1 ? event.deltaY * 16 : event.deltaY;
      while (wheel >= 40) {
        wheel -= 40;
        turn(1);
      }
      while (wheel <= -40) {
        wheel += 40;
        turn(-1);
      }
    },
    { passive: false },
  );
}

// ---- BACK ----------------------------------------------------------------------

function bindBack(button: HTMLElement): void {
  let timer: ReturnType<typeof setTimeout> | undefined;
  let held = false;
  button.addEventListener("pointerdown", () => {
    held = false;
    timer = setTimeout(() => {
      held = true;
      backToTrack();
    }, HOLD_MS);
  });
  const cancel = (): void => {
    clearTimeout(timer);
  };
  button.addEventListener("pointerup", cancel);
  button.addEventListener("pointercancel", cancel);
  button.addEventListener("pointerleave", cancel);
  button.addEventListener("click", () => {
    if (held) {
      held = false;
      return;
    }
    pressed(button);
    back();
  });
}

// ---- Rows ------------------------------------------------------------------------

function bindRows(): void {
  rows().forEach((row, index) => {
    // A mouse that moves over a row selects it; touch selects by tapping.
    row.addEventListener("pointermove", (event) => {
      if (event.pointerType === "mouse" && !row.classList.contains("is-selected")) select(index);
    });
    row.addEventListener("click", (event) => {
      if (event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey) return;
      // In the info layout a first tap only selects, as on the deck.
      if (kind() === "info" && !row.classList.contains("is-selected")) {
        event.preventDefault();
        select(index);
        return;
      }
      select(index);
      if (row.dataset.action) {
        event.preventDefault();
        activate(row);
      }
    });
  });
}

// ---- Keys --------------------------------------------------------------------------

let escapeTimer: ReturnType<typeof setTimeout> | undefined;
let escapeHeld = false;

function typing(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName))
  );
}

document.addEventListener("keydown", (event) => {
  if (!ui() || typing(event.target) || event.altKey || event.metaKey || event.ctrlKey) return;
  switch (event.key) {
    case "ArrowDown":
      event.preventDefault();
      turn(1, true);
      break;
    case "ArrowUp":
      event.preventDefault();
      turn(-1, true);
      break;
    case "Enter":
      // A focused link or button does its own thing.
      if (event.target === document.body || event.target === ui()) {
        event.preventDefault();
        push();
      }
      break;
    case "Escape":
    case "Backspace":
      event.preventDefault();
      if (event.repeat) return;
      escapeHeld = false;
      escapeTimer = setTimeout(() => {
        escapeHeld = true;
        backToTrack();
      }, HOLD_MS);
      break;
  }
});

document.addEventListener("keyup", (event) => {
  if (event.key !== "Escape" && event.key !== "Backspace") return;
  clearTimeout(escapeTimer);
  if (!escapeHeld && ui() && !typing(event.target)) {
    pressed(document.querySelector("[data-back-button]"));
    back();
  }
  escapeHeld = false;
});

// ---- Swipe ---------------------------------------------------------------------------

/** A swipe from left to right is BACK, except on the waveform, which it scratches. */
function bindSwipe(screen: HTMLElement): void {
  let start: { x: number; y: number; t: number } | undefined;
  screen.addEventListener("pointerdown", (event) => {
    start =
      event.pointerType === "touch" && kind() !== "deck"
        ? { x: event.clientX, y: event.clientY, t: event.timeStamp }
        : undefined;
  });
  screen.addEventListener("pointerup", (event) => {
    if (!start) return;
    const dx = event.clientX - start.x;
    const dy = Math.abs(event.clientY - start.y);
    if (dx > 110 && dy < dx / 2 && event.timeStamp - start.t < 600) back();
    start = undefined;
  });
}

// ---- Arriving ----------------------------------------------------------------------

/** From the landing: the screen fills the window, then the frame comes into view. */
async function arrive(): Promise<void> {
  const arrival = takeArrival();
  const housing = document.querySelector<HTMLElement>("[data-housing]");
  const screen = document.querySelector<HTMLElement>("[data-screen]");
  if (arrival !== "poweron" || !housing || !screen || reducedMotion()) {
    await handOver();
    return;
  }
  const h = housing.getBoundingClientRect();
  const s = screen.getBoundingClientRect();
  const scale = Math.max(innerWidth / s.width, innerHeight / s.height);
  const originX = s.left + s.width / 2 - h.left;
  const originY = s.top + s.height / 2 - h.top;
  housing.style.setProperty("--origin", `${String(originX)}px ${String(originY)}px`);
  housing.style.transition = "none";
  housing.style.translate = `${String(innerWidth / 2 - (s.left + s.width / 2))}px ${String(innerHeight / 2 - (s.top + s.height / 2))}px`;
  housing.style.scale = String(scale);
  // Laid out zoomed in now, so that what follows is a transition from it.
  housing.getBoundingClientRect();
  await handOver();
  housing.style.transition =
    "scale 900ms cubic-bezier(0.6, 0, 0.2, 1), translate 900ms cubic-bezier(0.6, 0, 0.2, 1)";
  housing.style.scale = "1";
  housing.style.translate = "0 0";
  await wait(950);
  housing.style.removeProperty("transition");
  housing.style.removeProperty("scale");
  housing.style.removeProperty("translate");
}

document.addEventListener("astro:page-load", () => {
  if (!ui()) return;
  const knob = document.querySelector<HTMLElement>("[data-knob]");
  const backButton = document.querySelector<HTMLElement>("[data-back-button]");
  const screen = document.querySelector<HTMLElement>("[data-screen]");
  if (knob) bindKnob(knob);
  if (backButton) bindBack(backButton);
  if (screen) bindSwipe(screen);
  bindRows();
  restoreSelection();
  void arrive();
});
