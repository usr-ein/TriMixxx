// Draws the A/B diagrams in src/assets/figures/ for the site: the SVG figures
// of pi-qemu/rauc-pi-4-report.html, in the report's own dark palette with its
// blue-greys swapped for the site's green-greys and its sheet for the site's
// near-black (--diagram), so a box reads as an outline, as it does on paper.
// Each is captured at 2x, 2096 px wide, and saved as lossless WebP.
//
//   npm install --no-save puppeteer-core@25.12.0
//   node scripts/dark-figures.mjs
//
// Needs Chrome (CHROME=/path/to/chrome elsewhere than macOS) and cwebp.
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

import puppeteer from "puppeteer-core";

const site = fileURLToPath(new URL("..", import.meta.url));
const report = new URL("../../pi-qemu/rauc-pi-4-report.html", import.meta.url);
const chrome =
  process.env["CHROME"] ?? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
// The report's figures, in order, and the ones the site shows.
const names = {
  1: "ab-1-card",
  2: "ab-2-boot",
  4: "ab-4-update-states",
  5: "ab-5-update-sequence",
  6: "ab-6-emulated-vs-real",
  7: "ab-7-workflow",
};

const work = mkdtempSync(join(tmpdir(), "dark-figures-"));
const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 1000, deviceScaleFactor: 2 });
  await page.emulateMediaFeatures([{ name: "prefers-color-scheme", value: "dark" }]);
  await page.goto(report.href, { waitUntil: "networkidle0" });
  await page.evaluate(() => {
    document.documentElement.dataset["theme"] = "dark";
    const style = document.createElement("style");
    style.textContent = `
      :root[data-theme="dark"] {
        --paper: #0b0c0b;
        --sheet: #0b0c0b;
        --ink: #eef1ea;
        --ink-2: #a5ada1;
        --rule: #353d35;
        --code-bg: #171b17;
      }
      .dia-wrap { width: 1048px; border: 0; border-radius: 0; overflow: visible; }
      svg.dia { min-width: 0; }
    `;
    document.head.append(style);
    return document.fonts.ready;
  });
  const figures = await page.$$(".dia-wrap");
  for (const [index, figure] of figures.entries()) {
    const name = names[index + 1];
    if (!name) continue;
    const png = join(work, `${name}.png`);
    await figure.screenshot({ path: png });
    execFileSync("cwebp", [
      "-quiet",
      "-lossless",
      "-z",
      "9",
      png,
      "-o",
      join(site, "src/assets/figures", `${name}.webp`),
    ]);
    process.stdout.write(`${name}.webp\n`);
  }
} finally {
  await browser.close();
  rmSync(work, { recursive: true, force: true });
}
