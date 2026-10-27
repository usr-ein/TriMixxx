// The screen switching on and off, like the tube the boot splash pretends it
// is. The tube (#crt, in Shell.astro) outlives page changes, so it can cover
// the moment one screen gives way to the next.

const ARRIVAL = "trimixxx:arrival";

/** How the next page was reached: through the power button, or the landing's tap. */
export type Arrival = "poweron" | "shutdown";

export function setArrival(arrival: Arrival): void {
  try {
    sessionStorage.setItem(ARRIVAL, arrival);
  } catch {
    // No storage: the next page simply appears.
  }
}

export function takeArrival(): Arrival | undefined {
  try {
    const value = sessionStorage.getItem(ARRIVAL);
    sessionStorage.removeItem(ARRIVAL);
    return value === "poweron" || value === "shutdown" ? value : undefined;
  } catch {
    return undefined;
  }
}

export const reducedMotion = (): boolean => matchMedia("(prefers-reduced-motion: reduce)").matches;

export const wait = (ms: number): Promise<void> =>
  new Promise((resolve) => setTimeout(resolve, ms));

const tube = (): HTMLElement | null => document.getElementById("crt");

/** The tube warms up into the boot splash; resolves once it has been seen. */
export async function warmUp(): Promise<void> {
  const crt = tube();
  if (!crt) return;
  crt.classList.remove("is-fading", "is-dark");
  crt.classList.add("is-on", "is-warming");
  await wait(reducedMotion() ? 350 : 1650);
}

/**
 * The splash, already lit, where the landing's camera left it: centred at
 * (x, y), `width` wide, in CSS pixels. It holds, flickers, and resolves.
 */
export async function showSplash(x: number, y: number, width: number): Promise<void> {
  const crt = tube();
  if (!crt) return;
  crt.style.setProperty("--splash-x", `${String(x)}px`);
  crt.style.setProperty("--splash-y", `${String(y)}px`);
  crt.style.setProperty("--splash-w", `${String(width)}px`);
  crt.classList.remove("is-fading", "is-dark", "is-warming");
  crt.classList.add("is-on", "is-steady");
  await wait(650);
  crt.classList.add("is-switching");
  await wait(260);
}

/** The page under the tube takes over. */
export async function handOver(): Promise<void> {
  const crt = tube();
  if (!crt?.classList.contains("is-on")) return;
  crt.classList.add("is-fading");
  await wait(340);
  crt.classList.remove("is-on", "is-warming", "is-fading", "is-dark", "is-steady", "is-switching");
}

/** The picture folds to a line, the line to a dot, the dot to dark. */
export async function collapse(screen: HTMLElement | null): Promise<void> {
  if (screen && !reducedMotion()) {
    screen.classList.add("is-collapsing");
    await wait(560);
  }
  const crt = tube();
  crt?.classList.remove("is-warming", "is-fading");
  crt?.classList.add("is-on", "is-dark");
  await wait(140);
}
