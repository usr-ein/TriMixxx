// Usage: node scripts/kicad-glb-colors.mjs BOARD.glb
//
// KiCad's GLB export writes the colours of the board it draws itself (its
// body, copper, solder mask and silkscreen) as sRGB values, where glTF wants
// linear ones: read as linear, its solder mask green (20 51 36) comes out
// washed out. The parts' colours, which come from their STEP models, are
// already linear. This decodes the board's own colours, in place: those of the
// materials only KiCad's shapes use, whose nodes it names "=>[...]".
import { readFileSync, writeFileSync } from "node:fs";

const file = process.argv[2];
if (!file) {
  process.stderr.write("usage: node scripts/kicad-glb-colors.mjs BOARD.glb\n");
  process.exit(2);
}

const glb = readFileSync(file);
if (glb.readUInt32LE(0) !== 0x46546c67) throw new Error(`${file}: not a GLB`);
const jsonLength = glb.readUInt32LE(12);
const json = JSON.parse(glb.subarray(20, 20 + jsonLength).toString("utf8"));
const rest = glb.subarray(20 + jsonLength);

const board = new Set();
const parts = new Set();
for (const node of json.nodes ?? []) {
  if (node.mesh === undefined) continue;
  const users = (node.name ?? "").startsWith("=>") ? board : parts;
  for (const primitive of json.meshes[node.mesh].primitives) {
    if (primitive.material !== undefined) users.add(primitive.material);
  }
}

const linear = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
let decoded = 0;
for (const index of board) {
  if (parts.has(index)) continue;
  const pbr = json.materials[index].pbrMetallicRoughness;
  if (!pbr?.baseColorFactor) continue;
  const [r, g, b, a] = pbr.baseColorFactor;
  pbr.baseColorFactor = [linear(r), linear(g), linear(b), a];
  decoded += 1;
}

// The JSON chunk is padded with spaces to a multiple of four bytes.
let text = Buffer.from(JSON.stringify(json), "utf8");
text = Buffer.concat([text, Buffer.alloc((4 - (text.length % 4)) % 4, 0x20)]);
const header = Buffer.alloc(20);
header.writeUInt32LE(0x46546c67, 0);
header.writeUInt32LE(2, 4);
header.writeUInt32LE(20 + text.length + rest.length, 8);
header.writeUInt32LE(text.length, 12);
header.writeUInt32LE(0x4e4f534a, 16);
writeFileSync(file, Buffer.concat([header, text, rest]));
process.stdout.write(`${file}: decoded ${String(decoded)} board colours\n`);
