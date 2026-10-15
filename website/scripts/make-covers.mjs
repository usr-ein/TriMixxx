// Draws the build log's generated covers, for posts no photograph fits: SVG
// written here, rasterised with rsvg-convert, saved as WebP in
// src/assets/covers/. Run on a Mac after changing a cover, then commit the
// WebP files:   node scripts/make-covers.mjs
// Needs rsvg-convert and cwebp (brew install librsvg webp). Menlo and
// Helvetica Neue are macOS's own fonts.
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const W = 1600;
const H = 900;
const out = new URL("../src/assets/covers/", import.meta.url).pathname;

const C = {
  bg: "#090a09",
  panel: "#131613",
  line: "#2a302a",
  text: "#eef1ea",
  muted: "#a5ada1",
  faint: "#6f776c",
  lime: "#88ff00",
  amber: "#ffb000",
  orange: "#ff6a00",
  cyan: "#00c8ff",
};

const mono = "Menlo, monospace";
const sans = "Helvetica Neue, Helvetica, sans-serif";

const esc = (s) => s.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");

const frame = (
  body,
) => `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
  <defs>
    <pattern id="dots" width="24" height="24" patternUnits="userSpaceOnUse">
      <circle cx="1" cy="1" r="1" fill="${C.lime}" opacity="0.12"/>
    </pattern>
    <radialGradient id="glow" cx="72%" cy="30%" r="70%">
      <stop offset="0" stop-color="${C.lime}" stop-opacity="0.10"/>
      <stop offset="1" stop-color="${C.lime}" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <rect width="${W}" height="${H}" fill="${C.bg}"/>
  <rect width="${W}" height="${H}" fill="url(#dots)"/>
  <rect width="${W}" height="${H}" fill="url(#glow)"/>
  ${body}
</svg>`;

const text = (
  x,
  y,
  s,
  { size = 28, fill = C.text, family = mono, weight = 400, anchor = "start", spacing = 0 } = {},
) =>
  `<text x="${x}" y="${y}" font-family="${family}" font-size="${size}" font-weight="${weight}" fill="${fill}" text-anchor="${anchor}" letter-spacing="${spacing}">${esc(s)}</text>`;

const box = (x, y, w, h, { stroke = C.line, fill = C.panel, rx = 18, dash = "" } = {}) =>
  `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${rx}" fill="${fill}" stroke="${stroke}" stroke-width="2" ${dash ? `stroke-dasharray="${dash}"` : ""}/>`;

const arrow = (x1, y1, x2, y2, color = C.lime) => {
  const a = Math.atan2(y2 - y1, x2 - x1);
  const h = 14;
  const p1 = [x2 - h * Math.cos(a - 0.4), y2 - h * Math.sin(a - 0.4)];
  const p2 = [x2 - h * Math.cos(a + 0.4), y2 - h * Math.sin(a + 0.4)];
  return `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${color}" stroke-width="3"/>
  <path d="M${x2} ${y2} L${p1[0]} ${p1[1]} L${p2[0]} ${p2[1]} z" fill="${color}"/>`;
};

const kicker = (s) =>
  `<rect x="96" y="92" width="14" height="14" fill="${C.lime}"/>${text(126, 106, s.toUpperCase(), { size: 22, fill: C.lime, spacing: 4 })}`;

// Seven-segment digits, the same geometry as src/components/SevenSeg.astro.
const sevenSeg = (value, x, y, scale, color) => {
  const W7 = 12;
  const H7 = 22;
  const T = 2.6;
  const G = 0.55;
  const hz = (x1, x2, yy) => {
    const h = T / 2;
    const a = x1 + G;
    const b = x2 - G;
    return `${a},${yy} ${a + h},${yy - h} ${b - h},${yy - h} ${b},${yy} ${b - h},${yy + h} ${a + h},${yy + h}`;
  };
  const vt = (xx, y1, y2) => {
    const h = T / 2;
    const a = y1 + G;
    const b = y2 - G;
    return `${xx},${a} ${xx + h},${a + h} ${xx + h},${b - h} ${xx},${b} ${xx - h},${b - h} ${xx - h},${a + h}`;
  };
  const seg = {
    a: hz(T / 2, W7 - T / 2, T / 2),
    g: hz(T / 2, W7 - T / 2, H7 / 2),
    d: hz(T / 2, W7 - T / 2, H7 - T / 2),
    f: vt(T / 2, T / 2, H7 / 2),
    b: vt(W7 - T / 2, T / 2, H7 / 2),
    e: vt(T / 2, H7 / 2, H7 - T / 2),
    c: vt(W7 - T / 2, H7 / 2, H7 - T / 2),
  };
  const glyph = {
    0: "abcdef",
    1: "bc",
    2: "abged",
    3: "abgcd",
    4: "fgbc",
    5: "afgcd",
    6: "afgecd",
    7: "abc",
    8: "abcdefg",
    9: "abcdfg",
    "-": "g",
    " ": "",
  };
  let cx = 0;
  let parts = "";
  for (const ch of value) {
    if (ch === ".") {
      parts += `<rect x="${cx + 0.2 + H7 * 0.12}" y="${H7 - T}" width="${T}" height="${T}" fill="${color}"/>`;
      cx += 4;
      continue;
    }
    const lit = glyph[ch] ?? "";
    for (const name of "abcdefg") {
      parts += `<polygon points="${seg[name]}" transform="translate(${cx},0)" fill="${color}" opacity="${lit.includes(name) ? 1 : 0.08}"/>`;
    }
    cx += W7 + 4;
  }
  return `<g transform="translate(${x},${y}) scale(${scale}) skewX(-7)">${parts}</g>`;
};

const covers = {
  "pro-dj-link": frame(`
    ${kicker("Pro DJ Link")}
    ${text(96, 196, "Every packet starts the same way.", { size: 54, family: sans, weight: 700 })}
    ${box(96, 262, 1408, 168, { stroke: C.lime })}
    ${text(136, 334, "51 73 70 74 31 57 6d 4a 4f 4c", { size: 46, fill: C.lime })}
    ${text(136, 392, "Q  s  p  t  1  W  m  J  O  L", { size: 34, fill: C.muted })}
    ${[
      ["UDP 50000", "who's here · a keep-alive every 2 s"],
      ["UDP 50001", "beats · tempo master hand-over"],
      ["UDP 50002", "status · every 200 ms, to announced players"],
      ["NFSv2", "the music itself, read 8 KB at a time"],
      ["TCP 1051", "dbserver · menus, metadata, waveforms"],
    ]
      .map(
        ([port, what], i) =>
          `${text(136, 520 + i * 62, port, { size: 30, fill: C.amber })}${text(420, 520 + i * 62, what, { size: 30, fill: C.text })}`,
      )
      .join("")}
  `),
  "sends-returns": frame(`
    ${kicker("Send and return")}
    ${text(96, 196, "The deck as an effects unit.", { size: 54, family: sans, weight: 700 })}
    ${box(96, 330, 270, 150)}${text(231, 395, "Xone:92", { size: 34, family: sans, weight: 700, anchor: "middle" })}${text(231, 440, "AUX SEND", { size: 24, fill: C.muted, anchor: "middle" })}
    ${arrow(366, 405, 456, 405)}
    ${box(456, 330, 230, 150)}${text(571, 395, "UCA222", { size: 34, family: sans, weight: 700, anchor: "middle" })}${text(571, 440, "RCA IN", { size: 24, fill: C.muted, anchor: "middle" })}
    ${arrow(686, 405, 776, 405)}
    ${box(776, 290, 360, 230, { stroke: C.lime, fill: "#0f150c" })}${text(956, 350, "TriMixxx", { size: 36, family: sans, weight: 700, anchor: "middle" })}
    ${["REVERB", "DELAY", "ECHO", "HPF · LPF"].map((m, i) => text(956, 396 + i * 32, m, { size: 22, fill: C.lime, anchor: "middle" })).join("")}
    ${arrow(1136, 405, 1226, 405)}
    ${box(1226, 330, 278, 150)}${text(1365, 395, "Xone:92", { size: 34, family: sans, weight: 700, anchor: "middle" })}${text(1365, 440, "RETURN", { size: 24, fill: C.muted, anchor: "middle" })}
    ${text(96, 700, "wet only · per-module mix · beat-synced delay", { size: 30, fill: C.muted })}
  `),
  "ab-updates": frame(`
    ${kicker("A/B updates")}
    ${text(96, 196, "Two of everything that changes.", { size: 54, family: sans, weight: 700 })}
    ${box(96, 280, 640, 150, { stroke: C.lime, fill: "#0f150c" })}${text(136, 348, "SLOT A", { size: 40, weight: 700 })}${text(136, 396, "running · the release before", { size: 26, fill: C.muted })}
    ${box(864, 280, 640, 150, { stroke: C.amber, fill: "#16120a", dash: "14 10" })}${text(904, 348, "SLOT B", { size: 40, weight: 700 })}${text(904, 396, "the update · a one-shot trial", { size: 26, fill: C.muted })}
    ${arrow(1184, 430, 1184, 548, C.lime)}${text(1184, 600, "network + ssh within 150 s", { size: 26, anchor: "middle" })}${text(1184, 640, "→ kept", { size: 30, fill: C.lime, anchor: "middle" })}
    ${arrow(864, 600, 600, 600, C.amber)}${text(416, 588, "otherwise", { size: 26, anchor: "middle" })}${text(416, 628, "→ back to A", { size: 30, fill: C.amber, anchor: "middle" })}
    ${text(96, 800, "p1 bootsel · p2 p3 boot · p5 p6 system · p7 identity · p8 state", { size: 26, fill: C.faint })}
  `),
  emulation: frame(`
    ${kicker("Emulation")}
    ${text(96, 196, "A booth on a laptop.", { size: 54, family: sans, weight: 700 })}
    ${box(96, 262, 1408, 470, { rx: 22 })}
    <circle cx="142" cy="302" r="10" fill="#ff5f57"/><circle cx="176" cy="302" r="10" fill="#febc2e"/><circle cx="210" cy="302" r="10" fill="#28c840"/>
    ${[
      ["pi-qemu deck up d --link booth", "an emulated deck, on the link “booth”"],
      ["pi-qemu cdj up a --link booth --window", "a CDJ-2000NXS, on its own firmware"],
      ["pi-qemu cdj up b --link booth --window", "and another"],
      ["pi-qemu link devices booth", "who announces itself there"],
    ]
      .map(
        ([cmd, note], i) =>
          `${text(142, 390 + i * 82, "$ ", { size: 32, fill: C.lime })}${text(186, 390 + i * 82, cmd, { size: 32 })}${text(186, 424 + i * 82, `# ${note}`, { size: 22, fill: C.faint })}`,
      )
      .join("")}
    ${text(96, 820, "QEMU's Raspberry Pi 4 · 21 patches · a deck up in ~3 s from a snapshot", { size: 26, fill: C.muted })}
  `),
  "boot-modes": frame(`
    ${kicker("Boot modes")}
    ${text(96, 196, "Hold PLAY at power-on.", { size: 54, family: sans, weight: 700 })}
    ${[
      ["jog wheel, turned", "turn"],
      ["jog wheel, touched", "fire"],
      ["tempo fader", "walk, run"],
      ["ring A, pads 1–7", "the seven weapons"],
      ["ring B", "the menu"],
      ["CUE", "use, open doors"],
    ]
      .map(
        ([control, does], i) =>
          `${text(96, 300 + i * 72, control, { size: 34, fill: C.text })}${text(620, 300 + i * 72, "→", { size: 34, fill: C.lime })}${text(700, 300 + i * 72, does, { size: 34, fill: C.amber })}`,
      )
      .join("")}
    ${text(96, 820, "hold CUE instead: a rescue console · LOOP IN + LOOP OUT for 2 s: back to the deck", { size: 24, fill: C.faint })}
  `),
  technobase: frame(`
    ${kicker("First gig")}
    ${text(96, 196, "A night at Technobase.", { size: 54, family: sans, weight: 700 })}
    ${sevenSeg("04.10", 96, 260, 15, C.orange)}
    ${text(96, 770, "trimixxx2 in the booth · a secret underground location", { size: 30, fill: C.muted })}
    ${text(96, 820, "4 October 2026", { size: 26, fill: C.faint })}
  `),
  "release-1-0": frame(`
    ${kicker("Release")}
    ${text(96, 196, "TriMixxx", { size: 54, family: sans, weight: 700 })}
    ${sevenSeg("1.0.0", 96, 270, 16, C.orange)}
    ${text(96, 780, "History · Record set · KEY SYNC · plain sticks both ways", { size: 30, fill: C.muted })}
    ${text(96, 830, "15 October 2026", { size: 26, fill: C.faint })}
  `),
};

// The social-preview card (public/og.png): the wordmark and the pitch beside the
// V2 render. The render is embedded as a JPEG, converted with macOS's sips.
const wordmark =
  "M114 55h50v10h-50z M134 65h10v60h-10z M174 75h30v10h-30z M204 85h10v10h-10z M174 85h10v40h-10z M234 55h10v10h-10z M234 75h10v50h-10z M224 115h30v10h-30z M264 55h10v70h-10z M304 55h10v70h-10z M274 65h10v10h-10z M294 65h10v10h-10z M284 75h10v10h-10z M334 55h10v10h-10z M334 75h10v50h-10z M324 115h30v10h-30z M364 75h10v10h-10z M404 75h10v10h-10z M374 85h10v10h-10z M394 85h10v10h-10z M384 95h10v10h-10z M374 105h10v10h-10z M394 105h10v10h-10z M364 115h10v10h-10z M404 115h10v10h-10z M424 75h10v10h-10z M464 75h10v10h-10z M434 85h10v10h-10z M454 85h10v10h-10z M444 95h10v10h-10z M434 105h10v10h-10z M454 105h10v10h-10z M424 115h10v10h-10z M464 115h10v10h-10z M484 75h10v10h-10z M524 75h10v10h-10z M494 85h10v10h-10z M514 85h10v10h-10z M504 95h10v10h-10z M494 105h10v10h-10z M514 105h10v10h-10z M484 115h10v10h-10z M524 115h10v10h-10z";

const ogCard = (
  renderJpegBase64,
) => `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1200" height="630" viewBox="0 0 1200 630">
  <defs>
    <pattern id="dots" width="20" height="20" patternUnits="userSpaceOnUse">
      <circle cx="1" cy="1" r="1" fill="${C.lime}" opacity="0.12"/>
    </pattern>
    <clipPath id="photo"><rect x="620" y="70" width="540" height="490" rx="22"/></clipPath>
  </defs>
  <rect width="1200" height="630" fill="${C.bg}"/>
  <rect width="1200" height="630" fill="url(#dots)"/>
  <image x="500" y="70" width="871" height="490" clip-path="url(#photo)" preserveAspectRatio="xMidYMid slice" xlink:href="data:image/jpeg;base64,${renderJpegBase64}"/>
  <rect x="620" y="70" width="540" height="490" rx="22" fill="none" stroke="${C.line}" stroke-width="2"/>
  <g transform="translate(60,150) scale(0.98)">
    <path d="${wordmark}" fill="${C.text}" transform="translate(-114,-55)"/>
    <rect x="430" y="20" width="22" height="50" fill="${C.lime}"/>
  </g>
  ${text(60, 300, "Open from the copper", { size: 44, family: sans, weight: 700 })}
  ${text(60, 352, "to the cue point.", { size: 44, family: sans, weight: 700 })}
  ${text(60, 430, "An open-source DJ deck: custom boards,", { size: 20, fill: C.muted })}
  ${text(60, 460, "an ESP32-S3, a Raspberry Pi running Mixxx,", { size: 20, fill: C.muted })}
  ${text(60, 490, "and Pro DJ Link with real CDJs.", { size: 20, fill: C.muted })}
  ${text(60, 568, "trimixxx.e1n.sh", { size: 22, fill: C.lime })}
</svg>`;

const work = mkdtempSync(join(tmpdir(), "covers-"));
try {
  for (const [name, svg] of Object.entries(covers)) {
    const svgPath = join(work, `${name}.svg`);
    const pngPath = join(work, `${name}.png`);
    writeFileSync(svgPath, svg);
    execFileSync("rsvg-convert", [
      "--width",
      String(W),
      "--height",
      String(H),
      svgPath,
      "--output",
      pngPath,
    ]);
    execFileSync("cwebp", ["-quiet", "-q", "90", pngPath, "-o", `${out}${name}.webp`]);
    process.stdout.write(`${name}.webp\n`);
  }

  const renderJpeg = join(work, "render.jpg");
  execFileSync(
    "sips",
    [
      "-s",
      "format",
      "jpeg",
      "-Z",
      "1400",
      new URL("../src/assets/renders/deck-v2-render.webp", import.meta.url).pathname,
      "--out",
      renderJpeg,
    ],
    { stdio: "ignore" },
  );
  const ogSvg = join(work, "og.svg");
  writeFileSync(ogSvg, ogCard(readFileSync(renderJpeg).toString("base64")));
  execFileSync("rsvg-convert", [
    "--width",
    "1200",
    "--height",
    "630",
    ogSvg,
    "--output",
    new URL("../public/og.png", import.meta.url).pathname,
  ]);
  process.stdout.write("og.png\n");
} finally {
  rmSync(work, { recursive: true, force: true });
}
