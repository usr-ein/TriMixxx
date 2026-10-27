// The landing: the deck in lines, turning by itself, waiting on its boot
// splash. Drag spins it, pinch or the wheel zooms it, two fingers move it. A
// tap on it switches it on: the camera falls into its screen until the splash
// fills the window, and the deck view takes over from it. Coming back from
// Shut down plays the same thing backwards, out of a dark screen.
import { navigate } from "astro:transitions/client";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

import { handOver, reducedMotion, setArrival, showSplash, takeArrival, warmUp } from "../deck/crt";
import { engine } from "../deck/engine";
import { loadDeck } from "./deck-file";
import { type DeckModel, build } from "./model";
import { standin } from "./standin";

/** How much of the screen's width the boot splash takes, on the model and on the tube alike. */
const SPLASH = 0.94;

/** The deck waiting, as after boot: its splash on the tube, the cursor blinking. */
function drawSplash(
  g: CanvasRenderingContext2D,
  width: number,
  height: number,
  splash: HTMLImageElement,
  now: number,
): void {
  const ground = g.createRadialGradient(
    width / 2,
    height / 2,
    0,
    width / 2,
    height / 2,
    width * 0.6,
  );
  ground.addColorStop(0, "#04130a");
  ground.addColorStop(0.65, "#010603");
  ground.addColorStop(1, "#000000");
  g.fillStyle = ground;
  g.fillRect(0, 0, width, height);
  if (splash.complete && splash.naturalWidth > 0) {
    const w = width * SPLASH;
    const h = (w * 250) / 690;
    const x = (width - w) / 2;
    const y = (height - h) / 2;
    g.drawImage(splash, x, y, w, h);
    // The cursor, covered every other half-second.
    if (Math.floor(now / 530) % 2 === 1) {
      const s = w / 690;
      g.fillStyle = "#030c07";
      g.fillRect(x + 543 * s, y + 74 * s, 24 * s, 52 * s);
    }
  }
  g.fillStyle = "rgb(0 0 0 / 30%)";
  for (let y = 0; y < height; y += 3) g.fillRect(0, y, width, 1);
}

const ease = (t: number): number => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2);
const smooth = (a: number, b: number, t: number): number => {
  const x = Math.max(0, Math.min(1, (t - a) / (b - a)));
  return x * x * (3 - 2 * x);
};

function animate(ms: number, step: (k: number) => void): Promise<void> {
  return new Promise((resolve) => {
    const start = performance.now();
    const tick = (now: number): void => {
      const t = Math.min(1, (now - start) / ms);
      step(ease(t));
      if (t < 1) requestAnimationFrame(tick);
      else resolve();
    };
    requestAnimationFrame(tick);
  });
}

async function mount(root: HTMLElement, gone: () => boolean): Promise<(() => void) | undefined> {
  const canvas = root.querySelector<HTMLCanvasElement>("[data-landing-canvas]");
  if (!canvas) return undefined;
  let renderer: THREE.WebGLRenderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  } catch {
    root.classList.add("is-flat");
    return undefined;
  }
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setClearColor(0x000000, 1);

  const audioUrl = root.dataset.audio ?? "";
  const modelUrl = root.dataset.model;
  let model: DeckModel;
  try {
    model = modelUrl ? await loadDeck(modelUrl) : standin();
  } catch {
    model = standin();
  }
  if (gone()) {
    renderer.dispose();
    return undefined;
  }
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(30, 1, 10, 8000);
  const deck = build(model);
  scene.add(deck.group);

  // The screen, the one lit surface.
  const splash = new Image();
  splash.src = "/splash.svg";
  const picture = document.createElement("canvas");
  picture.width = 768;
  picture.height = Math.round((768 * model.screen.height) / model.screen.width);
  const pg = picture.getContext("2d");
  const texture = new THREE.CanvasTexture(picture);
  texture.colorSpace = THREE.SRGBColorSpace;
  const screenMaterial = new THREE.MeshBasicMaterial({
    map: texture,
    transparent: true,
    depthWrite: false,
  });
  const screen = new THREE.Mesh(
    new THREE.PlaneGeometry(model.screen.width, model.screen.height),
    screenMaterial,
  );
  const right = new THREE.Vector3().crossVectors(model.screen.up, model.screen.normal).normalize();
  screen.quaternion.setFromRotationMatrix(
    new THREE.Matrix4().makeBasis(right, model.screen.up, model.screen.normal),
  );
  // A millimetre proud of the glass, so the glass doesn't hide it.
  screen.position.copy(model.screen.centre).addScaledVector(model.screen.normal, 1);
  screen.renderOrder = 3;
  scene.add(screen);

  // The jog's centre, lit green from inside, as on trimixxx2.
  const glow = document.createElement("canvas");
  glow.width = glow.height = 128;
  const gg = glow.getContext("2d");
  if (gg) {
    const gradient = gg.createRadialGradient(64, 64, 0, 64, 64, 64);
    gradient.addColorStop(0, "rgb(90 255 150 / 55%)");
    gradient.addColorStop(0.6, "rgb(40 220 110 / 20%)");
    gradient.addColorStop(1, "rgb(0 0 0 / 0%)");
    gg.fillStyle = gradient;
    gg.fillRect(0, 0, 128, 128);
  }
  const jogGlow = new THREE.Mesh(
    new THREE.CircleGeometry(model.jog.radius, 48),
    new THREE.MeshBasicMaterial({
      map: new THREE.CanvasTexture(glow),
      transparent: true,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    }),
  );
  jogGlow.rotation.x = -Math.PI / 2;
  jogGlow.position.copy(model.jog.centre);
  jogGlow.renderOrder = 3;
  scene.add(jogGlow);

  // Where the camera rests: far enough for the whole deck (a sphere of about
  // 270 mm around it) to fit the window's narrower side.
  const target = model.target.clone();
  const restDistance = (): number => {
    const vertical = THREE.MathUtils.degToRad(camera.fov / 2);
    const horizontal = Math.atan(Math.tan(vertical) * camera.aspect);
    return 270 / Math.sin(Math.min(vertical, horizontal));
  };
  const rest = (): THREE.Vector3 =>
    new THREE.Vector3()
      .setFromSphericalCoords(
        restDistance(),
        THREE.MathUtils.degToRad(57),
        THREE.MathUtils.degToRad(-30),
      )
      .add(target);
  const controls = new OrbitControls(camera, canvas);
  controls.target.copy(target);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.screenSpacePanning = true;
  controls.minDistance = 260;
  // A peek under the deck, but never upside down beneath it.
  controls.minPolarAngle = THREE.MathUtils.degToRad(4);
  controls.maxPolarAngle = THREE.MathUtils.degToRad(112);
  // One finger turns it; two pinch it and move it.
  controls.touches = { ONE: THREE.TOUCH.ROTATE, TWO: THREE.TOUCH.DOLLY_PAN };
  controls.autoRotateSpeed = 0.7;
  controls.autoRotate = !reducedMotion();
  let resume: ReturnType<typeof setTimeout> | undefined;
  let touched = false;
  controls.addEventListener("start", () => {
    touched = true;
    controls.autoRotate = false;
    clearTimeout(resume);
  });
  controls.addEventListener("end", () => {
    resume = setTimeout(() => {
      controls.autoRotate = !reducedMotion();
    }, 4000);
  });
  const bounds = new THREE.Box3(
    new THREE.Vector3(-260, -80, -300),
    new THREE.Vector3(260, 220, 300),
  );
  controls.addEventListener("change", () => {
    controls.target.clamp(bounds.min, bounds.max);
  });

  let busy = false;
  const resize = (): void => {
    const { clientWidth: w, clientHeight: h } = canvas;
    if (w === 0 || h === 0) return;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    // Pinching out goes as far again as the deck's resting distance, which a
    // narrow phone makes long.
    controls.maxDistance = Math.max(2600, restDistance() * 1.6);
    // Until someone moves it, the deck keeps fitting the window.
    if (!touched && !busy) {
      const offset = camera.position.clone().sub(controls.target);
      camera.position.copy(controls.target).addScaledVector(offset.normalize(), restDistance());
    }
  };
  const observer = new ResizeObserver(resize);
  observer.observe(canvas);
  camera.position.copy(rest());
  camera.lookAt(target);
  resize();

  renderer.setAnimationLoop((now: number) => {
    if (!busy) controls.update();
    if (pg) {
      drawSplash(pg, picture.width, picture.height, splash, now);
      texture.needsUpdate = true;
    }
    renderer.render(scene, camera);
  });

  // The camera square on to the screen, the picture filling the window.
  const facing = (): { position: THREE.Vector3; up: THREE.Vector3 } => {
    const half = THREE.MathUtils.degToRad(camera.fov / 2);
    const fitHeight = model.screen.height / 2 / Math.tan(half);
    const fitWidth = model.screen.width / 2 / (Math.tan(half) * camera.aspect);
    // Filling a wide window; across a tall one, where filling it would cut
    // the splash off at both ends.
    const distance = camera.aspect < 1 ? fitWidth * 0.98 : Math.min(fitHeight, fitWidth) * 0.96;
    return {
      position: model.screen.centre.clone().addScaledVector(model.screen.normal, distance),
      up: model.screen.up.clone(),
    };
  };

  const fly = async (
    to: { position: THREE.Vector3; up: THREE.Vector3; look: THREE.Vector3 },
    ms: number,
    lines: (k: number) => number,
  ): Promise<void> => {
    const fromPosition = camera.position.clone();
    const fromUp = camera.up.clone();
    const fromLook = controls.target.clone();
    const look = new THREE.Vector3();
    await animate(ms, (k) => {
      camera.position.lerpVectors(fromPosition, to.position, k);
      camera.up.lerpVectors(fromUp, to.up, k).normalize();
      camera.lookAt(look.lerpVectors(fromLook, to.look, k));
      deck.fade(lines(k));
    });
  };

  /** Where the screen's picture is in the window now, in CSS pixels. */
  const screenRect = (): { x: number; y: number; width: number } => {
    const box = canvas.getBoundingClientRect();
    const corner = (sx: number, sy: number): THREE.Vector3 =>
      model.screen.centre
        .clone()
        .addScaledVector(right, (sx * model.screen.width) / 2)
        .addScaledVector(model.screen.up, (sy * model.screen.height) / 2)
        .project(camera);
    const a = corner(-1, 0);
    const b = corner(1, 0);
    const c = model.screen.centre.clone().project(camera);
    return {
      x: box.left + ((c.x + 1) / 2) * box.width,
      y: box.top + ((1 - c.y) / 2) * box.height,
      width: (Math.abs(b.x - a.x) / 2) * box.width,
    };
  };

  const powerOn = async (): Promise<void> => {
    if (busy) return;
    busy = true;
    engine.unlock();
    void engine.load(audioUrl);
    controls.enabled = false;
    if (reducedMotion()) {
      await warmUp();
    } else {
      const end = facing();
      await fly({ ...end, look: model.screen.centre }, 1300, (k) => 1 - smooth(0.45, 0.95, k));
      const rect = screenRect();
      await showSplash(rect.x, rect.y, rect.width * SPLASH);
    }
    setArrival("poweron");
    await navigate("/deck/");
  };

  // A tap, not the end of a drag or a pinch, on the deck itself.
  const raycaster = new THREE.Raycaster();
  const aim = (event: PointerEvent): boolean => {
    const box = canvas.getBoundingClientRect();
    raycaster.setFromCamera(
      new THREE.Vector2(
        ((event.clientX - box.left) / box.width) * 2 - 1,
        -((event.clientY - box.top) / box.height) * 2 + 1,
      ),
      camera,
    );
    return raycaster.intersectObjects([...deck.solids.children, screen], false).length > 0;
  };
  const pointers = new Set<number>();
  let down: { x: number; y: number; t: number; many: boolean } | undefined;
  canvas.addEventListener("pointerdown", (event) => {
    pointers.add(event.pointerId);
    if (pointers.size === 1) {
      down = { x: event.clientX, y: event.clientY, t: event.timeStamp, many: false };
    } else if (down) {
      down.many = true;
    }
  });
  const lift = (event: PointerEvent): void => {
    pointers.delete(event.pointerId);
    if (!down || event.type !== "pointerup" || pointers.size > 0) {
      if (pointers.size === 0) down = undefined;
      return;
    }
    const moved = Math.hypot(event.clientX - down.x, event.clientY - down.y);
    const quick = event.timeStamp - down.t < 500;
    const single = !down.many;
    down = undefined;
    if (moved <= 8 && quick && single && !busy && aim(event)) void powerOn();
  };
  canvas.addEventListener("pointerup", lift);
  canvas.addEventListener("pointercancel", lift);
  // The canvas's touch-action already keeps pinches for the deck; older iOS
  // Safari zooms the page on its own gesture events regardless.
  for (const gesture of ["gesturestart", "gesturechange"]) {
    canvas.addEventListener(gesture, (event) => {
      event.preventDefault();
    });
  }
  canvas.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      void powerOn();
    }
  });
  // Over the deck, the pointer says it can be pressed.
  canvas.addEventListener("pointermove", (event) => {
    if (pointers.size === 0 && event.pointerType === "mouse") {
      canvas.classList.toggle("is-over", aim(event));
    }
  });

  if (takeArrival() === "shutdown" && !reducedMotion()) {
    // Back from Shut down: out of the dark screen, backwards.
    busy = true;
    controls.enabled = false;
    const start = facing();
    camera.position.copy(start.position);
    camera.up.copy(start.up);
    camera.lookAt(model.screen.centre);
    controls.target.copy(model.screen.centre);
    deck.fade(0);
    screenMaterial.opacity = 0;
    void (async () => {
      await handOver();
      await fly({ position: rest(), up: new THREE.Vector3(0, 1, 0), look: target }, 1700, (k) => {
        screenMaterial.opacity = smooth(0.5, 1, k);
        return smooth(0.15, 0.7, k);
      });
      controls.target.copy(target);
      controls.enabled = true;
      busy = false;
    })();
  } else {
    void handOver();
    // The lines come up out of the dark.
    if (!reducedMotion())
      void animate(1400, (k) => {
        deck.fade(k);
      });
  }

  return () => {
    renderer.setAnimationLoop(null);
    observer.disconnect();
    controls.dispose();
    renderer.dispose();
  };
}

let unmount: (() => void) | undefined;
let visit = 0;
document.addEventListener("astro:page-load", () => {
  unmount?.();
  unmount = undefined;
  const root = document.querySelector<HTMLElement>("[data-landing]");
  if (!root) return;
  const mine = ++visit;
  void mount(root, () => mine !== visit).then((stop) => {
    if (mine === visit) unmount = stop;
    else stop?.();
  });
});
document.addEventListener("astro:before-swap", () => {
  visit++;
  unmount?.();
  unmount = undefined;
});
