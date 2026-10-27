// The landing's deck, from Fusion's OBJ export of the whole V2 assembly: each
// body's feature edges (where its faces meet at an angle) become the lines
// the landing draws, and the outer bodies, coarsely, the solid that hides the
// lines behind it and catches taps.
//
//   node scripts/deck-model.mjs ~/Downloads/Trimixxx-V2.obj
//
// Writes media/trimixxx/models/deck-v2.gzbin, to commit to the media repository
// like the boards' GLBs. The file, once unpacked: a little-endian u32 with the length of a
// JSON header, the header (padded to 4 bytes), then the arrays it describes.
// Positions are millimetres in hundredths, as int16: x across, y up, z
// towards the DJ, the deck's footprint centred on the origin.
import { createReadStream, mkdirSync, writeFileSync } from "node:fs";
import { createInterface } from "node:readline";
import { join } from "node:path";
import { gzipSync } from "node:zlib";

const file = process.argv[2];
if (!file) {
  process.stderr.write("usage: deck-model.mjs Trimixxx-V2.obj\n");
  process.exit(2);
}
const site = new URL("..", import.meta.url).pathname;

/** How sharp a crease has to be to be drawn, in degrees. */
const CREASE = 26;
/**
 * Bodies smaller than this (bounding-box diagonal, cm) are left out. Inside
 * the deck, where they only show faintly through the top, only the bigger
 * ones are worth their weight: the boards, the Pi, the power supply.
 */
const SMALLEST = 1.0;
const SMALLEST_INSIDE = 3.0;
const INSIDE = new Set(["parts", "metal", "boards"]);
/** int16 units per millimetre. */
const UNITS = 100;

// What each body is drawn as. First match wins.
const PALETTE = [
  { name: "shell", colour: "#a3a3a3" },
  { name: "pink", colour: "#e2559a" },
  { name: "glass", colour: "#ffffff" },
  { name: "controls", colour: "#d4d8dc" },
  { name: "boards", colour: "#4fa77c" },
  { name: "metal", colour: "#7f8ea0" },
  { name: "parts", colour: "#6b6b6b" },
  { name: "jog", colour: "#c8ccd0" },
  { name: "jog-centre", colour: "#44ff88" },
];
const index = Object.fromEntries(PALETTE.map((p, i) => [p.name, i]));

const SHELL =
  /^(top_half_jog|bottom_half_jog|display_flat_top|bottom_half_screen_edge|display_curve_base)$/;
const CONTROLS =
  /^(button(:\d+)?|loop_start|loop_end|reloop|playcue_btn|btn_\w+|led_\w+|tempo_fader_\w+|clip_\w+)$/;
function classify(name, materials) {
  if (name === "danger_zone") return undefined;
  if (materials.has("Opaque(218,205,0)") && materials.size === 1) return undefined; // copper
  if (SHELL.test(name)) return "shell";
  if (name === "viewing_area") return "glass";
  if ([...materials].some((m) => m.startsWith("Pink_PETG"))) return "pink";
  if (CONTROLS.test(name)) return "controls";
  if (materials.has("Opaque(80,124,105)") || name.startsWith("pcb_")) return "boards";
  if ([...materials].some((m) => /Steel|Aluminum/.test(m)) || name === "screen_assy")
    return "metal";
  return "parts";
}
/** Bodies that hide what is behind them, and that a tap lands on. */
const SOLID = new Set(["shell", "pink", "glass", "controls"]);

// ---- Read ------------------------------------------------------------------

const positions = [];
const groups = new Map();
let group;
let material = "";
const enter = (name) => {
  group = groups.get(name);
  if (!group) {
    group = { name, materials: new Set(), faces: [] };
    groups.set(name, group);
  }
};
enter("(none)");
const rl = createInterface({ input: createReadStream(file), crlfDelay: Infinity });
for await (const line of rl) {
  const c = line.charCodeAt(0);
  if (c === 118 && line.charCodeAt(1) === 32) {
    const parts = line.split(/\s+/);
    positions.push(Number(parts[1]), Number(parts[2]), Number(parts[3]));
  } else if (c === 102 && line.charCodeAt(1) === 32) {
    const ids = line
      .slice(2)
      .trim()
      .split(/\s+/)
      .map((token) => {
        const i = Number(token.split("/")[0]);
        return i < 0 ? positions.length / 3 + i : i - 1;
      });
    for (let k = 1; k + 1 < ids.length; k++) group.faces.push(ids[0], ids[k], ids[k + 1]);
    group.materials.add(material);
  } else if (line.startsWith("g ")) {
    enter(line.slice(2).trim());
  } else if (line.startsWith("usemtl ")) {
    material = line.slice(7).trim();
  }
}

// Fusion writes Z up, in centimetres, the deck's front-left corner at the
// origin. The site wants y up, millimetres, centred, the front towards +z.
let minX = Infinity;
let maxX = -Infinity;
let minY = Infinity;
let maxY = -Infinity;
for (let i = 0; i < positions.length; i += 3) {
  minX = Math.min(minX, positions[i]);
  maxX = Math.max(maxX, positions[i]);
  minY = Math.min(minY, positions[i + 1]);
  maxY = Math.max(maxY, positions[i + 1]);
}
const cx = (minX + maxX) / 2;
const toSite = (i) => [
  (positions[i * 3] - cx) * 10,
  positions[i * 3 + 2] * 10,
  -(positions[i * 3 + 1] - (minY + maxY) / 2) * 10,
];

// ---- Edges -------------------------------------------------------------------

const lines = [];
const lineClass = [];
const solidPositions = [];
const solidIndices = [];
const stats = {};

for (const g of groups.values()) {
  if (g.faces.length === 0) continue;
  const kind = classify(g.name, g.materials);
  if (kind === undefined) continue;
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  for (const v of g.faces) {
    for (let k = 0; k < 3; k++) {
      lo[k] = Math.min(lo[k], positions[v * 3 + k]);
      hi[k] = Math.max(hi[k], positions[v * 3 + k]);
    }
  }
  const smallest = INSIDE.has(kind) ? SMALLEST_INSIDE : SMALLEST;
  if (Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) < smallest) continue;

  // Weld by position: CAD exports repeat vertices along every face's edge.
  const weld = new Map();
  const welded = [];
  const at = [];
  const idOf = (v) => {
    const p = toSite(v);
    const key = `${Math.round(p[0] * 100)},${Math.round(p[1] * 100)},${Math.round(p[2] * 100)}`;
    let id = weld.get(key);
    if (id === undefined) {
      id = welded.length / 3;
      weld.set(key, id);
      welded.push(...p);
    }
    return id;
  };
  for (const v of g.faces) at.push(idOf(v));

  const normals = [];
  for (let f = 0; f < at.length; f += 3) {
    const [a, b, c] = [at[f], at[f + 1], at[f + 2]];
    const ux = welded[b * 3] - welded[a * 3];
    const uy = welded[b * 3 + 1] - welded[a * 3 + 1];
    const uz = welded[b * 3 + 2] - welded[a * 3 + 2];
    const vx = welded[c * 3] - welded[a * 3];
    const vy = welded[c * 3 + 1] - welded[a * 3 + 1];
    const vz = welded[c * 3 + 2] - welded[a * 3 + 2];
    const nx = uy * vz - uz * vy;
    const ny = uz * vx - ux * vz;
    const nz = ux * vy - uy * vx;
    const length = Math.hypot(nx, ny, nz);
    normals.push(length > 1e-9 ? [nx / length, ny / length, nz / length] : undefined);
  }
  const edges = new Map();
  for (let f = 0; f < at.length; f += 3) {
    if (!normals[f / 3]) continue;
    for (let k = 0; k < 3; k++) {
      const a = at[f + k];
      const b = at[f + ((k + 1) % 3)];
      if (a === b) continue;
      const key = a < b ? a * 4194304 + b : b * 4194304 + a;
      const entry = edges.get(key);
      if (entry) entry.push(f / 3);
      else edges.set(key, [f / 3]);
    }
  }
  const cos = Math.cos((CREASE * Math.PI) / 180);
  let count = 0;
  for (const [key, faces] of edges) {
    let keep = faces.length !== 2;
    if (!keep) {
      const n1 = normals[faces[0]];
      const n2 = normals[faces[1]];
      keep = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2] < cos;
    }
    if (!keep) continue;
    const a = Math.floor(key / 4194304);
    const b = key % 4194304;
    lines.push(...welded.slice(a * 3, a * 3 + 3), ...welded.slice(b * 3, b * 3 + 3));
    lineClass.push(index[kind]);
    count++;
  }
  stats[kind] ??= { bodies: 0, segments: 0, triangles: 0 };
  stats[kind].bodies++;
  stats[kind].segments += count;

  if (SOLID.has(kind)) {
    const base = solidPositions.length / 3;
    solidPositions.push(...welded);
    for (const id of at) solidIndices.push(base + id);
    stats[kind].triangles += at.length / 3;
  }
}

// ---- The screen, and where the jog goes ---------------------------------------

const glass = groups.get("viewing_area");
const corners = [...new Set(glass.faces)].map(toSite);
const centre = [0, 1, 2].map((k) => corners.reduce((s, p) => s + p[k], 0) / corners.length);
// The glass's plane: its normal from its largest triangle; up, along its slope.
let normal = [0, 1, 0];
{
  let best = 0;
  for (let f = 0; f < glass.faces.length; f += 3) {
    const [a, b, c] = [0, 1, 2].map((k) => toSite(glass.faces[f + k]));
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    const area = Math.hypot(...n);
    if (area > best) {
      best = area;
      normal = n.map((x) => x / area);
    }
  }
  if (normal[1] < 0) normal = normal.map((x) => -x);
}
// Up on the screen is the world's up, made square to the normal.
const dot = normal[1];
let up = [-normal[0] * dot, 1 - normal[1] * dot, -normal[2] * dot];
up = up.map((x) => x / Math.hypot(...up));
const right = [
  up[1] * normal[2] - up[2] * normal[1],
  up[2] * normal[0] - up[0] * normal[2],
  up[0] * normal[1] - up[1] * normal[0],
];
const extent = (axis) => {
  const values = corners.map(
    (p) =>
      (p[0] - centre[0]) * axis[0] + (p[1] - centre[1]) * axis[1] + (p[2] - centre[2]) * axis[2],
  );
  return Math.max(...values) - Math.min(...values);
};

// The jog's hole, which the model leaves empty (the CDJ's jog isn't in it):
// the narrowest round loop of the shell's lines, 200 to 240 mm across, flat.
let jog = { x: 0, z: 0, y: 0, r: 105 };
{
  const key = (s, end) =>
    `${Math.round(lines[s * 6 + end * 3] * 10)},${Math.round(lines[s * 6 + end * 3 + 2] * 10)}`;
  const parent = new Map();
  const find = (k) => {
    while (parent.get(k) !== k) k = parent.get(k);
    return k;
  };
  const flat = [];
  for (let s = 0; s < lineClass.length; s++) {
    if (lineClass[s] !== index.shell || Math.abs(lines[s * 6 + 1] - lines[s * 6 + 4]) > 0.5)
      continue;
    flat.push(s);
    for (const k of [key(s, 0), key(s, 1)]) if (!parent.has(k)) parent.set(k, k);
    parent.set(find(key(s, 0)), find(key(s, 1)));
  }
  const loops = new Map();
  for (const s of flat) {
    const root = find(key(s, 0));
    const loop = loops.get(root) ?? {
      lo: [Infinity, Infinity, Infinity],
      hi: [-Infinity, -Infinity, -Infinity],
    };
    for (let k = 0; k < 6; k++) {
      loop.lo[k % 3] = Math.min(loop.lo[k % 3], lines[s * 6 + k]);
      loop.hi[k % 3] = Math.max(loop.hi[k % 3], lines[s * 6 + k]);
    }
    loops.set(root, loop);
  }
  let narrowest = Infinity;
  for (const { lo, hi } of loops.values()) {
    const sx = hi[0] - lo[0];
    const sz = hi[2] - lo[2];
    if (sx < 200 || sx > 240 || Math.abs(sx - sz) > 6 || sx >= narrowest) continue;
    narrowest = sx;
    jog = { x: (lo[0] + hi[0]) / 2, z: (lo[2] + hi[2]) / 2, y: hi[1], r: (sx + sz) / 4 };
  }
  // The top of the plate, at the hole.
  for (const { lo, hi } of loops.values()) {
    const sx = hi[0] - lo[0];
    if (sx >= 200 && sx <= 240 && Math.abs(sx - (hi[2] - lo[2])) <= 6)
      jog.y = Math.max(jog.y, hi[1]);
  }
}

// ---- Write ------------------------------------------------------------------------

const toInt16 = (values) => {
  const out = new Int16Array(values.length);
  for (let i = 0; i < values.length; i++) out[i] = Math.round(values[i] * UNITS);
  return out;
};
const lineArray = toInt16(lines);
const classArray = Uint8Array.from(lineClass);
const solidArray = toInt16(solidPositions);
const indexArray =
  solidPositions.length / 3 < 65536
    ? Uint16Array.from(solidIndices)
    : Uint32Array.from(solidIndices);
const arrays = [
  ["lines", lineArray],
  ["lineClass", classArray],
  ["solid", solidArray],
  ["solidIndex", indexArray],
];
let offset = 0;
const layout = {};
for (const [name, array] of arrays) {
  layout[name] = { offset, length: array.length, type: array.constructor.name };
  offset += array.byteLength;
  offset = Math.ceil(offset / 4) * 4;
}
const header = {
  version: 1,
  units: UNITS,
  palette: PALETTE,
  layout,
  screen: { centre, normal, up, right, width: extent(right), height: extent(up) },
  jog: { centre: [jog.x, jog.y, jog.z], radius: jog.r },
  bounds: { width: (maxX - minX) * 10, depth: (maxY - minY) * 10 },
};
const json = Buffer.from(JSON.stringify(header));
const headerLength = Math.ceil(json.length / 4) * 4;
const out = Buffer.alloc(4 + headerLength + offset, 0x20);
out.writeUInt32LE(headerLength, 0);
json.copy(out, 4);
let at = 4 + headerLength;
for (const [, array] of arrays) {
  Buffer.from(array.buffer, array.byteOffset, array.byteLength).copy(out, at);
  at += Math.ceil(array.byteLength / 4) * 4;
}
const dir = join(site, "media/trimixxx/models");
mkdirSync(dir, { recursive: true });
// Gzipped (the browser unpacks it), under a name no server serves as gzip.
const packed = gzipSync(out, { level: 9 });
writeFileSync(join(dir, "deck-v2.gzbin"), packed);
process.stdout.write(`${JSON.stringify(stats)}\n`);
process.stdout.write(
  `segments ${String(lineArray.length / 6)}, solid triangles ${String(indexArray.length / 3)}\n`,
);
process.stdout.write(
  `screen ${JSON.stringify(header.screen)}\njog ${JSON.stringify(header.jog)}\n`,
);
process.stdout.write(
  `wrote ${join(dir, "deck-v2.gzbin")}: ${(packed.length / 1024).toFixed(0)} KB\n`,
);
