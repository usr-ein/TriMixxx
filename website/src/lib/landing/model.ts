// The deck as the landing draws it: lines, in the colours of its parts. The
// edges you can see are bright; the ones behind something, faint; and the
// solid parts, drawn black with a little light on their rims, are what hides
// them and what a tap lands on.
import * as THREE from "three";
import { toCreasedNormals } from "three/addons/utils/BufferGeometryUtils.js";

export interface Part {
  /** A solid: drawn as its edges (unless solidOnly), and it hides what is behind it. */
  mesh?: THREE.BufferGeometry;
  /** Line segments, drawn as given: pairs of points. */
  lines?: number[];
  colour: number;
  solidOnly?: boolean;
}

export interface DeckModel {
  parts: Part[];
  /** The picture area of the screen: where the camera dives in. */
  screen: {
    centre: THREE.Vector3;
    normal: THREE.Vector3;
    up: THREE.Vector3;
    width: number;
    height: number;
  };
  /** The jog's glowing centre. */
  jog: { centre: THREE.Vector3; radius: number };
  /** Where the camera looks. */
  target: THREE.Vector3;
}

export interface BuiltDeck {
  group: THREE.Group;
  solids: THREE.Group;
  /** Fades every line, for the dive into the screen. */
  fade(opacity: number): void;
}

const EDGE_ANGLE = 24;

/** Black, but for a rim of light where a surface turns away: the shape of what has no edges. */
function rimMaterial(): THREE.ShaderMaterial {
  return new THREE.ShaderMaterial({
    uniforms: { rim: { value: new THREE.Color(0x8c8c8c) }, strength: { value: 0.32 } },
    vertexShader: `
      varying vec3 vNormal;
      varying vec3 vView;
      void main() {
        vec4 eye = modelViewMatrix * vec4(position, 1.0);
        vNormal = normalize(normalMatrix * normal);
        vView = normalize(-eye.xyz);
        gl_Position = projectionMatrix * eye;
      }`,
    fragmentShader: `
      uniform vec3 rim;
      uniform float strength;
      varying vec3 vNormal;
      varying vec3 vView;
      void main() {
        float facing = abs(dot(normalize(vNormal), normalize(vView)));
        gl_FragColor = vec4(rim * pow(1.0 - facing, 3.0) * strength, 1.0);
      }`,
    polygonOffset: true,
    polygonOffsetFactor: 1,
    polygonOffsetUnits: 1,
  });
}

export function build(model: DeckModel): BuiltDeck {
  const positions: number[] = [];
  const colours: number[] = [];
  const solids = new THREE.Group();
  const black = rimMaterial();
  const colour = new THREE.Color();
  const push = (points: ArrayLike<number>, hex: number): void => {
    colour.setHex(hex);
    for (let i = 0; i < points.length; i += 3) {
      positions.push(points[i] ?? 0, points[i + 1] ?? 0, points[i + 2] ?? 0);
      colours.push(colour.r, colour.g, colour.b);
    }
  };
  for (const part of model.parts) {
    if (part.mesh) {
      solids.add(new THREE.Mesh(toCreasedNormals(part.mesh, Math.PI / 6), black));
      if (!part.solidOnly) {
        push(
          new THREE.EdgesGeometry(part.mesh, EDGE_ANGLE).getAttribute("position").array,
          part.colour,
        );
      }
    }
    if (part.lines) push(part.lines, part.colour);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute("color", new THREE.Float32BufferAttribute(colours, 3));

  const seen = new THREE.LineBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.95,
  });
  const behind = new THREE.LineBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.14,
    depthFunc: THREE.GreaterDepth,
    depthWrite: false,
  });
  const front = new THREE.LineSegments(geometry, seen);
  const back = new THREE.LineSegments(geometry, behind);
  solids.renderOrder = 0;
  front.renderOrder = 1;
  back.renderOrder = 2;

  const group = new THREE.Group();
  group.add(solids, front, back);
  return {
    group,
    solids,
    fade(opacity) {
      seen.opacity = 0.95 * opacity;
      behind.opacity = 0.14 * opacity;
    },
  };
}
