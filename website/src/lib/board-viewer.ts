// The 3D board viewer: a board's GLB (scripts/export-boards.sh) in a canvas,
// lit like a product shot, turned and zoomed with the mouse, a finger or the
// keyboard. Imported on demand by ModelViewer.astro, so three.js is only
// downloaded when someone opens a board.
import {
  Box3,
  DirectionalLight,
  Mesh,
  MeshStandardMaterial,
  NeutralToneMapping,
  PerspectiveCamera,
  PMREMGenerator,
  Scene,
  Spherical,
  SRGBColorSpace,
  Vector3,
  WebGLRenderer,
  type Material,
  type Object3D,
} from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";

export type Side = "top" | "bottom";

export interface BoardViewer {
  /** Back to the first view. */
  reset: () => void;
  /** Turn the board's other side towards the camera. */
  flip: () => void;
  /** Orbit by angles in radians: around the board, and over it. */
  turn: (around: number, over: number) => void;
  /** Move closer (factor below 1) or further away. */
  zoom: (factor: number) => void;
  dispose: () => void;
}

export interface ViewerOptions {
  side: Side;
  /** What the canvas is, for screen readers. */
  label: string;
  signal: AbortSignal;
  /** How much of the model has arrived, or undefined when the size is unknown. */
  onProgress: (fraction: number | undefined) => void;
}

/** Thrown when the browser can't draw WebGL. */
export class NoWebGL extends Error {
  override name = "NoWebGL";
}

// The first view: the board seen from the front, tilted back like its render.
const TILT = 0.85;
const AZIMUTH = -0.35;
const FOV = 30;

// The light, matched against KiCad's own renders: a dim studio for
// reflections, and one key light for the shape.
const EXPOSURE = 0.9;
const ENVIRONMENT = 0.7;
const KEY = 1.8;

async function download(
  url: string,
  signal: AbortSignal,
  onProgress: ViewerOptions["onProgress"],
): Promise<ArrayBuffer> {
  const response = await fetch(url, { signal });
  if (!response.ok || !response.body) throw new Error(`HTTP ${String(response.status)}`);
  const total = Number(response.headers.get("content-length") ?? 0);
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let loaded = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    loaded += value.byteLength;
    onProgress(total > 0 ? Math.min(loaded / total, 1) : undefined);
  }
  const bytes = new Uint8Array(loaded);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return bytes.buffer;
}

function materials(mesh: Mesh): Material[] {
  return Array.isArray(mesh.material) ? mesh.material : [mesh.material];
}

// Moves a layer towards the camera (negative) or away from it in the depth
// test, so layers KiCad draws almost in one plane never fight.
function nudge(material: Material, steps: number): void {
  material.polygonOffset = true;
  material.polygonOffsetFactor = steps;
  material.polygonOffsetUnits = steps;
}

// KiCad exports colours only, which glTF reads as bare metal: gold and grey
// metal stay metal (pads, pins, shields), everything else becomes a
// dielectric. It also exports the board's body (98 % opaque), its solder mask
// (83 %) and its silkscreen (90 %) as blended layers, which three.js would
// draw in any order and which lie almost on the copper: the body goes opaque
// and a step back, the mask comes a step forward over the copper, and the
// silkscreen two, as KiCad stacks them.
function finish(mesh: Mesh): void {
  for (const material of materials(mesh)) {
    if (!(material instanceof MeshStandardMaterial)) continue;
    const { h, s, l } = material.color.getHSL({ h: 0, s: 0, l: 0 }, SRGBColorSpace);
    const gold = h > 0.07 && h < 0.16 && s > 0.3 && l > 0.25 && l < 0.85;
    const steel = s < 0.06 && l > 0.45 && l < 0.9;
    material.metalness = gold || steel ? 1 : 0;
    material.roughness = gold || steel ? 0.32 : 0.55;
    if (!material.transparent) continue;
    if (material.opacity >= 0.95) {
      material.transparent = false;
      material.opacity = 1;
      material.depthWrite = true;
      nudge(material, 1);
    } else if (l > 0.8) {
      mesh.renderOrder = 2;
      nudge(material, -2);
    } else {
      mesh.renderOrder = 1;
      nudge(material, -1);
    }
  }
}

// `instanceof Mesh` alone types the mesh as Mesh<any>.
function isMesh(node: Object3D): node is Mesh {
  return node instanceof Mesh;
}

export async function openBoard(
  stage: HTMLElement,
  url: string,
  options: ViewerOptions,
): Promise<BoardViewer> {
  const loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder);
  const data = await download(url, options.signal, options.onProgress);
  const gltf = await loader.parseAsync(data, "");
  options.signal.throwIfAborted();

  let renderer: WebGLRenderer;
  try {
    renderer = new WebGLRenderer({ antialias: true, alpha: true });
  } catch {
    throw new NoWebGL();
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.toneMapping = NeutralToneMapping;
  renderer.toneMappingExposure = EXPOSURE;

  const scene = new Scene();
  const pmrem = new PMREMGenerator(renderer);
  const environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  scene.environment = environment;
  scene.environmentIntensity = ENVIRONMENT;

  const board = gltf.scene;
  board.traverse((node) => {
    if (isMesh(node)) finish(node);
  });
  const box = new Box3().setFromObject(board);
  board.position.sub(box.getCenter(new Vector3()));
  scene.add(board);
  const size = box.getSize(new Vector3());
  const radius = size.length() / 2;

  const key = new DirectionalLight(0xffffff, KEY);
  key.position.set(radius, radius * 3, radius * 2);
  scene.add(key);

  const camera = new PerspectiveCamera(FOV, 1, radius / 100, radius * 100);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = !window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  controls.dampingFactor = 0.1;
  controls.screenSpacePanning = true;
  controls.zoomToCursor = true;
  controls.minDistance = radius * 0.12;
  controls.maxDistance = radius * 8;

  const offset = new Vector3();
  const spherical = new Spherical();
  const place = (distance: number, phi: number, theta: number): void => {
    spherical.set(distance, phi, theta);
    camera.position.copy(controls.target).add(offset.setFromSpherical(spherical));
    camera.lookAt(controls.target);
  };

  // Far enough that the board fits the first view, with a margin. It lies in
  // the XZ plane, top up: across the screen goes its width as turned by the
  // azimuth, and up the screen its depth, foreshortened by the tilt, plus
  // its parts' height.
  const fit = (): number => {
    const vertical = Math.tan((FOV * Math.PI) / 360);
    const horizontal = vertical * camera.aspect;
    const [c, s] = [Math.abs(Math.cos(AZIMUTH)), Math.abs(Math.sin(AZIMUTH))];
    const across = (c * size.x + s * size.z) / 2;
    const up = ((s * size.x + c * size.z) / 2) * Math.cos(TILT) + (size.y / 2) * Math.sin(TILT);
    return Math.max(across / horizontal, up / vertical) * 1.3;
  };

  let frame = 0;
  const draw = (): void => {
    frame = 0;
    if (controls.update()) request();
    renderer.render(scene, camera);
  };
  const request = (): void => {
    if (frame === 0) frame = requestAnimationFrame(draw);
  };
  controls.addEventListener("change", request);

  let placed = false;
  const resize = (): void => {
    const { clientWidth: width, clientHeight: height } = stage;
    if (width === 0 || height === 0) return;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    if (!placed) {
      placed = true;
      place(fit(), options.side === "top" ? TILT : Math.PI - TILT, AZIMUTH);
      controls.update();
      controls.saveState();
    }
    request();
  };
  const observer = new ResizeObserver(resize);

  const canvas = renderer.domElement;
  canvas.classList.add("viewer__canvas");
  canvas.setAttribute("role", "img");
  canvas.setAttribute("aria-label", options.label);
  stage.append(canvas);
  observer.observe(stage);
  resize();

  const turn = (around: number, over: number): void => {
    spherical.setFromVector3(offset.copy(camera.position).sub(controls.target));
    place(
      spherical.radius,
      Math.min(Math.max(spherical.phi - over, 0.01), Math.PI - 0.01),
      spherical.theta + around,
    );
    request();
  };

  return {
    reset: () => {
      controls.reset();
      request();
    },
    flip: () => {
      spherical.setFromVector3(offset.copy(camera.position).sub(controls.target));
      place(spherical.radius, Math.PI - spherical.phi, spherical.theta);
      request();
    },
    turn,
    zoom: (factor) => {
      spherical.setFromVector3(offset.copy(camera.position).sub(controls.target));
      const distance = Math.min(
        Math.max(spherical.radius * factor, controls.minDistance),
        controls.maxDistance,
      );
      place(distance, spherical.phi, spherical.theta);
      request();
    },
    dispose: () => {
      cancelAnimationFrame(frame);
      observer.disconnect();
      controls.dispose();
      board.traverse((node) => {
        if (isMesh(node)) {
          node.geometry.dispose();
          for (const material of materials(node)) material.dispose();
        }
      });
      environment.dispose();
      pmrem.dispose();
      renderer.dispose();
      renderer.forceContextLoss();
      canvas.remove();
    },
  };
}
