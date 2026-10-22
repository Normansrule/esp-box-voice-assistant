/*
 * device3d.js - the hero: a stylised ESP32-S3-BOX-3 on its sensor dock,
 * built from primitives in three.js, with the simulated face as the screen.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * The model is an illustration, not a CAD export: proportions are close to
 * the real board but no dimension here is authoritative. If WebGL or the CDN
 * is unavailable the stage falls back to the flat 2D screen.
 */
const stage = document.getElementById("hero-stage");

function fallback(reason) {
  stage.classList.add("no-gl");
  console.info("3D hero unavailable, using 2D screen:", reason);
}

async function start() {
  const sim = window.DeskBuddySim;
  if (!sim) return fallback("simulator not loaded");
  let THREE, RoundedBoxGeometry, RoomEnvironment;
  try {
    THREE = await import("three");
    ({ RoundedBoxGeometry } = await import("three/addons/geometries/RoundedBoxGeometry.js"));
    ({ RoomEnvironment } = await import("three/addons/environments/RoomEnvironment.js"));
  } catch (e) {
    return fallback(e.message);
  }

  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
  } catch (e) {
    return fallback("no WebGL");
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.domElement.className = "gl";
  renderer.domElement.setAttribute("aria-hidden", "true");
  stage.prepend(renderer.domElement);

  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(renderer), 0.04).texture;

  const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 100);
  camera.position.set(0, 0.3, 10.4);
  camera.lookAt(0, -0.35, 0);

  const RED = new THREE.Color("#E8362D");
  const shell = new THREE.MeshPhysicalMaterial({ color: "#f4f4f6", roughness: 0.38, clearcoat: 0.6, clearcoatRoughness: 0.25 });
  const glass = new THREE.MeshPhysicalMaterial({ color: "#0c0d0f", roughness: 0.08, metalness: 0.1, clearcoat: 1 });
  const dockMat = new THREE.MeshPhysicalMaterial({ color: "#e6e7eb", roughness: 0.55, clearcoat: 0.2 });
  const grey = new THREE.MeshStandardMaterial({ color: "#a7aab1", roughness: 0.5 });

  const device = new THREE.Group();
  scene.add(device);

  /* Main body: 2.4-inch 4:3 panel in a rounded shell. */
  const body = new THREE.Mesh(new RoundedBoxGeometry(3.2, 2.7, 1.25, 8, 0.32), shell);
  body.castShadow = true;
  device.add(body);

  const bezel = new THREE.Mesh(new RoundedBoxGeometry(2.86, 2.2, 0.06, 6, 0.16), glass);
  bezel.position.set(0, 0.08, 0.63);
  device.add(bezel);

  const texture = new THREE.CanvasTexture(sim.heroCanvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 8;
  const screen = new THREE.Mesh(new THREE.PlaneGeometry(2.56, 1.92),
    new THREE.MeshBasicMaterial({ map: texture, toneMapped: false }));
  screen.position.set(0, 0.08, 0.665);
  device.add(screen);

  /* Two microphone holes above the screen and the mute switch on top. */
  [-0.32, 0.32].forEach((x) => {
    const mic = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 0.02, 20), grey);
    mic.rotation.x = Math.PI / 2;
    mic.position.set(x, 1.23, 0.626);
    device.add(mic);
  });
  const mute = new THREE.Mesh(new RoundedBoxGeometry(0.42, 0.1, 0.16, 4, 0.04), grey);
  mute.position.set(0.9, 1.37, 0.1);
  device.add(mute);

  /* Sensor dock underneath. */
  const dock = new THREE.Mesh(new RoundedBoxGeometry(3.25, 0.62, 1.55, 8, 0.24), dockMat);
  dock.position.set(0, -1.68, -0.1);
  dock.castShadow = true;
  device.add(dock);
  const led = new THREE.Mesh(new THREE.CircleGeometry(0.045, 20), new THREE.MeshBasicMaterial({ color: RED }));
  led.position.set(1.25, -1.68, 0.656);
  device.add(led);

  /* Soft contact shadow. */
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(14, 14), new THREE.ShadowMaterial({ opacity: 0.14 }));
  floor.rotation.x = -Math.PI / 2;
  floor.position.y = -2.0;
  floor.receiveShadow = true;
  scene.add(floor);

  const key = new THREE.DirectionalLight("#ffffff", 2.2);
  key.position.set(3, 5, 4);
  key.castShadow = true;
  key.shadow.mapSize.set(1024, 1024);
  key.shadow.radius = 6;
  scene.add(key);
  const rim = new THREE.PointLight(RED, 18, 12);
  rim.position.set(-3.5, 1.5, -2.5);
  scene.add(rim);
  scene.add(new THREE.AmbientLight("#ffffff", 0.35));

  /* Interaction: pointer tilt plus drag to turn, easing back to rest. */
  let targetX = 0, targetY = 0, dragY = 0, dragging = false, lastX = 0, spin = 0;
  window.addEventListener("pointermove", (e) => {
    targetY = (e.clientX / window.innerWidth - 0.5) * 0.5;
    targetX = (e.clientY / window.innerHeight - 0.5) * 0.25;
    if (dragging) { spin += (e.clientX - lastX) * 0.01; lastX = e.clientX; }
  }, { passive: true });
  renderer.domElement.addEventListener("pointerdown", (e) => { dragging = true; lastX = e.clientX; });
  window.addEventListener("pointerup", () => { dragging = false; });
  renderer.domElement.addEventListener("click", () => sim.hero.anim.playAntic(Math.floor(Math.random() * window.FIRMWARE.antics.length)));

  function resize() {
    const r = stage.getBoundingClientRect();
    renderer.setSize(r.width, r.height, false);
    camera.aspect = r.width / r.height;
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(stage);
  resize();

  const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const intro = performance.now();
  let visible = true;
  new IntersectionObserver(([e]) => { visible = e.isIntersecting; }).observe(stage);

  function frame(now) {
    requestAnimationFrame(frame);
    if (!visible) return;
    const t = (now - intro) / 1000;
    if (!dragging) spin *= 0.94;
    dragY += (spin - dragY) * 0.1;
    const enter = reduce ? 1 : Math.min(1, t / 1.6);
    const e = 1 - Math.pow(1 - enter, 4);
    device.rotation.y = (-0.55 + targetY) * (1 - e) + (targetY - 0.32) * e + dragY + (reduce ? 0 : Math.sin(t * 0.5) * 0.05);
    device.rotation.x = targetX * 0.6 + (reduce ? 0 : Math.sin(t * 0.7) * 0.02);
    device.position.y = (reduce ? 0 : Math.sin(t * 0.9) * 0.06) - (1 - e) * 0.6;
    texture.needsUpdate = true;
    renderer.render(scene, camera);
  }
  requestAnimationFrame(frame);
}

start().catch((e) => fallback(e.message));
