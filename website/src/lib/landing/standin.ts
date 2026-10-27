// A stand-in for the deck's model, drawn from its dimensions
// (ref_sizes/dims_v2_model.md) and its photographs, until the STEP of the
// whole deck replaces it. Millimetres; x across, y up from the floor, z from
// the back (negative) to the DJ (positive).
import * as THREE from "three";

import type { DeckModel, Part } from "./model";

const BODY_W = 329;
const BODY_D = 423;
const TOP = 85;

const GREY = 0x9a9a9a;
const SILVER = 0xd2d6da;
const PINK = 0xe2559a;
const GREEN = 0x44ff88;
const WHITE = 0xffffff;

/** A rounded rectangle, w × d, centred. */
function rounded(w: number, d: number, r: number): THREE.Shape {
  const shape = new THREE.Shape();
  const x = -w / 2;
  const y = -d / 2;
  shape.moveTo(x + r, y);
  shape.lineTo(x + w - r, y);
  shape.quadraticCurveTo(x + w, y, x + w, y + r);
  shape.lineTo(x + w, y + d - r);
  shape.quadraticCurveTo(x + w, y + d, x + w - r, y + d);
  shape.lineTo(x + r, y + d);
  shape.quadraticCurveTo(x, y + d, x, y + d - r);
  shape.lineTo(x, y + r);
  shape.quadraticCurveTo(x, y, x + r, y);
  return shape;
}

/** A slab: a rounded rectangle extruded upwards from `bottom`. */
function slab(w: number, d: number, r: number, bottom: number, height: number, bevel = 0) {
  const geometry = new THREE.ExtrudeGeometry(rounded(w, d, r), {
    depth: height - bevel * 2,
    bevelEnabled: bevel > 0,
    bevelThickness: bevel,
    bevelSize: bevel,
    bevelSegments: 1,
    curveSegments: 10,
  });
  geometry.rotateX(-Math.PI / 2);
  geometry.translate(0, bottom + bevel, 0);
  return geometry;
}

/** A circle of line segments, flat at height y. */
function ring(cx: number, cz: number, r: number, y: number, segments = 64): number[] {
  const out: number[] = [];
  for (let i = 0; i < segments; i++) {
    const a = (i / segments) * Math.PI * 2;
    const b = ((i + 1) / segments) * Math.PI * 2;
    out.push(cx + Math.cos(a) * r, y, cz + Math.sin(a) * r);
    out.push(cx + Math.cos(b) * r, y, cz + Math.sin(b) * r);
  }
  return out;
}

/**
 * The CDJ-1000MK3's jog, which no model of the deck contains: its dimpled
 * ring sunk into the cut-out, the platter, the window at its centre.
 */
export function jogParts(x: number, top: number, z: number): Part[] {
  const dimples: number[] = [];
  for (let i = 0; i < 30; i++) {
    const a = (i / 30) * Math.PI * 2;
    dimples.push(...ring(x + Math.cos(a) * 96, z + Math.sin(a) * 96, 4.6, top - 1.6, 12));
  }
  const platter = new THREE.CylinderGeometry(90, 90, 12, 96);
  platter.translate(x, top + 4, z);
  return [
    { lines: ring(x, z, 101, top - 1.8, 128), colour: 0x6a6a6a },
    { lines: dimples, colour: 0x5c5c5c },
    { mesh: platter, colour: SILVER },
    { lines: ring(x, z, 86, top + 10.2, 96), colour: 0x7a7a7a },
    { lines: ring(x, z, 40, top + 10.4, 64), colour: GREEN },
  ];
}

/** A round button standing `h` proud of the top plate. */
function button(parts: Part[], x: number, z: number, r: number, h: number, colour = SILVER): void {
  parts.push({
    lines: [...ring(x, z, r, TOP + h, 32), ...ring(x, z, r + 1.2, TOP + 0.3, 32)],
    colour,
  });
  const cylinder = new THREE.CylinderGeometry(r, r, h, 24);
  cylinder.translate(x, TOP + h / 2, z);
  parts.push({ mesh: cylinder, colour, solidOnly: true });
}

export function standin(): DeckModel {
  const parts: Part[] = [];

  // The top plate, its edge chamfered, and the tray under it.
  parts.push({ mesh: slab(BODY_W, BODY_D, 26, 60, TOP - 60, 3), colour: GREY });
  parts.push({ mesh: slab(BODY_W - 34, BODY_D - 34, 18, 26, 34), colour: GREY });

  // Four pink legs, splayed.
  for (const sx of [-1, 1]) {
    for (const sz of [-1, 1]) {
      const leg = new THREE.CylinderGeometry(12, 21, 32, 32, 1, true);
      leg.rotateZ(sx * -0.2);
      leg.rotateX(sz * 0.2);
      leg.translate(sx * (BODY_W / 2 - 46), 15, sz * (BODY_D / 2 - 52));
      parts.push({ mesh: leg, colour: PINK });
      // A few lines down the cone, so it reads as one.
      const lines: number[] = [];
      for (let i = 0; i < 6; i++) {
        const a = (i / 6) * Math.PI * 2;
        const top = new THREE.Vector3(Math.cos(a) * 12, 16, Math.sin(a) * 12);
        const bottom = new THREE.Vector3(Math.cos(a) * 21, -16, Math.sin(a) * 21);
        const m = new THREE.Matrix4()
          .makeTranslation(sx * (BODY_W / 2 - 46), 15, sz * (BODY_D / 2 - 52))
          .multiply(new THREE.Matrix4().makeRotationX(sz * 0.2))
          .multiply(new THREE.Matrix4().makeRotationZ(sx * -0.2));
        top.applyMatrix4(m);
        bottom.applyMatrix4(m);
        lines.push(top.x, top.y, top.z, bottom.x, bottom.y, bottom.z);
      }
      parts.push({ lines, colour: PINK });
    }
  }

  // The screen housing: a wedge rising towards the back, the glass in its top.
  const front = -60;
  const lip = TOP + 10;
  const slope = (20 * Math.PI) / 180;
  const length = 172;
  const backZ = front - Math.cos(slope) * length;
  const backY = lip + Math.sin(slope) * length;
  const xl = -BODY_W / 2;
  const xr = BODY_W / 2;
  const p = [
    [front, TOP],
    [front, lip],
    [backZ, backY],
    [backZ, TOP - 25],
  ] as const;
  const v = (x: number, i: number): number[] => [x, p[i]?.[1] ?? 0, p[i]?.[0] ?? 0];
  const quad = (a: number[], b: number[], c: number[], d: number[]): number[] => [
    ...a,
    ...b,
    ...c,
    ...a,
    ...c,
    ...d,
  ];
  const wedge = new THREE.BufferGeometry();
  wedge.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      [
        ...quad(v(xl, 0), v(xl, 1), v(xl, 2), v(xl, 3)),
        ...quad(v(xr, 0), v(xr, 3), v(xr, 2), v(xr, 1)),
        ...quad(v(xl, 0), v(xr, 0), v(xr, 1), v(xl, 1)),
        ...quad(v(xl, 1), v(xr, 1), v(xr, 2), v(xl, 2)),
        ...quad(v(xl, 2), v(xr, 2), v(xr, 3), v(xl, 3)),
        ...quad(v(xl, 3), v(xr, 3), v(xr, 0), v(xl, 0)),
      ],
      3,
    ),
  );
  wedge.computeVertexNormals();
  parts.push({ mesh: wedge, colour: GREY });

  // Along the slope (u), and out of it (n).
  const origin = new THREE.Vector3(0, lip, front);
  const u = new THREE.Vector3(0, Math.sin(slope), -Math.cos(slope));
  const n = new THREE.Vector3(0, Math.cos(slope), Math.sin(slope));
  const onSlope = (x: number, s: number, lift = 0.4): THREE.Vector3 =>
    origin.clone().addScaledVector(u, s).addScaledVector(n, lift).setX(x);
  const rect = (x0: number, x1: number, s0: number, s1: number, lift = 0.4): number[] => {
    const a = onSlope(x0, s0, lift);
    const b = onSlope(x1, s0, lift);
    const c = onSlope(x1, s1, lift);
    const d = onSlope(x0, s1, lift);
    return [a, b, b, c, c, d, d, a].flatMap((q) => [q.x, q.y, q.z]);
  };
  // The glass, 239 × 147, and the picture inside its black border.
  const glassX = xl + 46;
  parts.push({ lines: rect(glassX, glassX + 239, 12.5, 159.5), colour: WHITE });
  parts.push({ lines: rect(glassX + 12, glassX + 228, 18, 153.6, 0.5), colour: 0x5a5a5a });
  const slopeRing = (x: number, s: number, r: number, lift = 0.5): number[] => {
    const out: number[] = [];
    const side = new THREE.Vector3(1, 0, 0);
    for (let i = 0; i < 32; i++) {
      const a = (i / 32) * Math.PI * 2;
      const b = ((i + 1) / 32) * Math.PI * 2;
      for (const t of [a, b]) {
        const q = onSlope(x, s, lift)
          .addScaledVector(side, Math.cos(t) * r)
          .addScaledVector(u, Math.sin(t) * r);
        out.push(q.x, q.y, q.z);
      }
    }
    return out;
  };
  // SORT/INFO and the USB port to the left, BACK and the knob to the right.
  parts.push({ lines: slopeRing(xl + 23, 118, 7.5), colour: SILVER });
  parts.push({ lines: rect(xl + 16, xl + 30, 50, 66, 0.5), colour: 0x6f8fff });
  parts.push({ lines: slopeRing(xr - 22, 96, 7.5), colour: SILVER });
  parts.push({
    lines: [...slopeRing(xr - 22, 52, 7), ...slopeRing(xr - 22, 52, 7, 9)],
    colour: WHITE,
  });

  const screenCentre = onSlope(glassX + 12 + 108, 85.8, 0.6);

  // The jog: its cut-out, the dimpled ring, the platter and its glowing centre.
  const jx = 18;
  const jz = 72;
  parts.push({ lines: ring(jx, jz, 105, TOP + 0.3, 128), colour: GREY });
  parts.push(...jogParts(jx, TOP, jz));

  // CUE and PLAY, the hot cues, the loop row, the right-hand column.
  button(parts, -117, 128, 16.5, 4);
  button(parts, -117, 176, 16.5, 4);
  for (const z of [-22, -2, 18, 38]) button(parts, -112, z, 7.5, 3);
  button(parts, -74, -18, 7.5, 3);
  for (const x of [-92, -60]) button(parts, x, -44, 7.5, 3);
  button(parts, -28, -42, 4.5, 2.5);
  for (const x of [34, 53, 72, 91]) button(parts, x, -42, 7.5, 3);
  for (const z of [-26, -6, 30]) button(parts, 128, z, 7.5, 3);
  button(parts, 88, -12, 5, 11, WHITE);

  // The tempo fader: its pink slot and its cap.
  const slot: number[] = [];
  const fx = 142;
  for (const [a, b] of [
    [-6, 38],
    [6, 38],
    [6, 172],
    [-6, 172],
  ] as const) {
    slot.push(fx + a, TOP + 0.4, b);
  }
  parts.push({
    lines: [0, 1, 1, 2, 2, 3, 3, 0].flatMap((i) => slot.slice(i * 3, i * 3 + 3)),
    colour: PINK,
  });
  const cap = new THREE.BoxGeometry(17, 11, 10);
  cap.translate(fx, TOP + 5.5, 108);
  parts.push({ mesh: cap, colour: SILVER });

  return {
    parts,
    screen: {
      centre: screenCentre,
      normal: n.clone(),
      up: u.clone(),
      width: 216,
      height: 135.6,
    },
    jog: { centre: new THREE.Vector3(jx, TOP + 10.6, jz), radius: 40 },
    target: new THREE.Vector3(0, 70, 0),
  };
}
