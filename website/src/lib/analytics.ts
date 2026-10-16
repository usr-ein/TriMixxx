// Google Analytics, with the visitor's consent only: until they accept in
// Consent.astro's banner, nothing is loaded and nothing reaches Google. The
// choice is kept in this browser; the footer's "Analytics settings" asks again.

declare global {
  interface Window {
    dataLayer?: unknown[];
    gtag?: (...command: unknown[]) => void;
  }
}

export type Choice = "granted" | "denied";

const KEY = "trimixxx-analytics";

/** The choice made in this browser, if any. */
export function storedChoice(): Choice | undefined {
  try {
    const value = localStorage.getItem(KEY);
    return value === "granted" || value === "denied" ? value : undefined;
  } catch {
    return undefined;
  }
}

export function store(choice: Choice): void {
  try {
    localStorage.setItem(KEY, choice);
  } catch {
    // Storage refused (a private window): the banner asks again next time.
  }
}

// gtag.js's own switch: true stops it sending anything from this page on.
function disable(id: string, off: boolean): void {
  Reflect.set(window, `ga-disable-${id}`, off);
}

let loaded = false;

/** Loads gtag.js and counts this page view. */
export function start(id: string): void {
  disable(id, false);
  if (loaded) return;
  loaded = true;
  const dataLayer = (window.dataLayer ??= []);
  // gtag.js reads only arguments objects from the data layer, not arrays.
  window.gtag = function gtag() {
    // eslint-disable-next-line prefer-rest-params -- see above
    dataLayer.push(arguments);
  };
  window.gtag("consent", "default", {
    ad_storage: "denied",
    ad_user_data: "denied",
    ad_personalization: "denied",
    analytics_storage: "granted",
  });
  window.gtag("js", new Date());
  window.gtag("config", id);
  const script = document.createElement("script");
  script.async = true;
  script.src = `https://www.googletagmanager.com/gtag/js?id=${id}`;
  document.head.append(script);
}

/** Stops counting, and deletes the cookies gtag.js set. */
export function stop(id: string): void {
  disable(id, true);
  const domains = [location.hostname, `.${location.hostname.split(".").slice(-2).join(".")}`];
  for (const cookie of document.cookie.split(";")) {
    const name = cookie.split("=")[0]?.trim() ?? "";
    if (!name.startsWith("_ga")) continue;
    for (const domain of domains) {
      document.cookie = `${name}=; Max-Age=0; path=/; domain=${domain}`;
    }
    document.cookie = `${name}=; Max-Age=0; path=/`;
  }
}
