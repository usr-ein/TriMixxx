// The deck's own model: the lines and the solid scripts/deck-model.mjs makes
// from Fusion's export of the V2, with the CDJ jog it leaves out drawn in.
import * as THREE from "three";

import type { DeckModel, Part } from "./model";
import { jogParts } from "./standin";

interface Header {
  version: number;
  units: number;
  palette: readonly { name: string; colour: string }[];
  layout: Record<
    "lines" | "lineClass" | "solid" | "solidIndex",
    { offset: number; length: number; type: string }
  >;
  screen: {
    centre: [number, number, number];
    normal: [number, number, number];
    up: [number, number, number];
    width: number;
    height: number;
  };
  jog: { centre: [number, number, number]; radius: number };
}

export async function loadDeck(url: string): Promise<DeckModel> {
  // Stored gzipped, unpacked here.
  const response = await fetch(url);
  if (!response.ok || !response.body) throw new Error(`No model at ${url}`);
  const buffer = await new Response(
    response.body.pipeThrough(new DecompressionStream("gzip")),
  ).arrayBuffer();
  const headerLength = new DataView(buffer).getUint32(0, true);
  const header = JSON.parse(
    new TextDecoder().decode(new Uint8Array(buffer, 4, headerLength)),
  ) as Header;
  const base = 4 + headerLength;
  const { layout, units } = header;
  const lines = new Int16Array(buffer, base + layout.lines.offset, layout.lines.length);
  const classes = new Uint8Array(buffer, base + layout.lineClass.offset, layout.lineClass.length);
  const solid = new Int16Array(buffer, base + layout.solid.offset, layout.solid.length);
  const indices =
    layout.solidIndex.type === "Uint16Array"
      ? new Uint16Array(buffer, base + layout.solidIndex.offset, layout.solidIndex.length)
      : new Uint32Array(buffer, base + layout.solidIndex.offset, layout.solidIndex.length);

  // One set of lines per colour.
  const counts = new Array<number>(header.palette.length).fill(0);
  for (const c of classes) counts[c] = (counts[c] ?? 0) + 6;
  const byClass = counts.map((n) => new Float32Array(n));
  const filled = new Array<number>(header.palette.length).fill(0);
  for (let s = 0; s < classes.length; s++) {
    const c = classes[s] ?? 0;
    const target = byClass[c];
    if (!target) continue;
    let at = filled[c] ?? 0;
    for (let k = 0; k < 6; k++) target[at++] = (lines[s * 6 + k] ?? 0) / units;
    filled[c] = at;
  }
  const parts: Part[] = byClass.map((points, i) => ({
    lines: Array.from(points),
    colour: Number.parseInt((header.palette[i]?.colour ?? "#ffffff").slice(1), 16),
  }));

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      Float32Array.from(solid, (v) => v / units),
      3,
    ),
  );
  geometry.setIndex(new THREE.BufferAttribute(indices, 1));
  parts.push({ mesh: geometry, colour: 0, solidOnly: true });

  const [jx, jy, jz] = header.jog.centre;
  parts.push(...jogParts(jx, jy, jz));

  const vector = (v: readonly number[]): THREE.Vector3 =>
    new THREE.Vector3(v[0] ?? 0, v[1] ?? 0, v[2] ?? 0);
  return {
    parts,
    screen: {
      centre: vector(header.screen.centre),
      normal: vector(header.screen.normal),
      up: vector(header.screen.up),
      width: header.screen.width,
      height: header.screen.height,
    },
    jog: { centre: new THREE.Vector3(jx, jy + 10.6, jz), radius: 40 },
    target: new THREE.Vector3(0, 70, 0),
  };
}
