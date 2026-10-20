import * as THREE from 'three';
import * as CANNON from 'cannon-es';
import { mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';

/* ============================= Physics (cannon-es) ============================= */
// Whole instrument drops as one rigid box onto the floor on page load.
const physWorld = new CANNON.World({ gravity: new CANNON.Vec3(0, -9.82, 0) });
physWorld.broadphase = new CANNON.SAPBroadphase(physWorld);
physWorld.allowSleep = true;
const physGroundMat = new CANNON.Material('ground');
const physBoxMat = new CANNON.Material('box');
const physContactMat = new CANNON.ContactMaterial(physGroundMat, physBoxMat, {
  friction: 0.45,
  restitution: 0.28,
});
physWorld.addContactMaterial(physContactMat);
const physGround = new CANNON.Body({ mass: 0, material: physGroundMat, shape: new CANNON.Plane() });
physGround.quaternion.setFromEuler(-Math.PI / 2, 0, 0);
physWorld.addBody(physGround);

let physBox = null;
let physHalf = null; // collision half-extents currently on physBox
let physSettled = true; // true when body is sleeping / at rest
let physHasDropped = false; // only auto-drop from height on first (page-load) build
let physLastTime = 0;

function physDropPose(depth, phys){
  const dropHeight = Math.max(0.0, num(phys?.dropHeight, 1.0));
  const tilt = phys?.tilt || {};
  const tx = num(tilt.x, 0.10);
  const ty = num(tilt.y, 0.06);
  const tz = num(tilt.z, 0.12);
  const restY = depth / 2 + 0.03; // boundary is the z=0 plane, box half-thickness is depth/2
  const q = new THREE.Quaternion().setFromEuler(
    new THREE.Euler(-Math.PI / 2 + tx, ty, tz)
  );
  return { pos: new THREE.Vector3(0, restY + dropHeight, 0), quat: q };
}

function ensurePhysLoop(){
  if (animationActive) return;
  animationActive = true;
  physLastTime = performance.now();
  requestAnimationFrame(animate);
}

function syncRigFromBody(){
  rig.position.copy(physBox.position);
  rig.quaternion.copy(physBox.quaternion);
}

/* ============================= Three.js setup ============================= */
const host = document.getElementById('canvasHost');
const scene = new THREE.Scene();

// vertical gradient background for studio feel
(function(){
  const c = document.createElement('canvas');
  c.width = 8; c.height = 256;
  const ctx = c.getContext('2d');
  const g = ctx.createLinearGradient(0,0,0,256);
  g.addColorStop(0, '#1c1e24');
  g.addColorStop(0.55, '#131418');
  g.addColorStop(1, '#0c0d10');
  ctx.fillStyle = g; ctx.fillRect(0,0,8,256);
  scene.background = new THREE.CanvasTexture(c);
})();

const camera = new THREE.PerspectiveCamera(45, 1, 1.0, 50);
const renderer = new THREE.WebGLRenderer({ antialias: true, logarithmicDepthBuffer: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFShadowMap; // alt VSMShadowMap, PCFShadowMap, PCFSoftShadowMap
host.appendChild(renderer.domElement);

// Lights are driven entirely by config.spotlight (an array). The pool grows
// on demand and is reused across rebuilds.
const spotLights = [];

function ensureSpotLights(count){
  while (spotLights.length < count){
    const light = new THREE.DirectionalLight(0xffffff, 0);
    light.castShadow = false;
    light.shadow.mapSize.set(1024, 1024);
    light.shadow.camera.near = 1.0;
    light.shadow.camera.far = 10.0;
    light.shadow.bias = 0.001;
    light.shadow.normalBias = 0.01;
    light.shadow.radius = 6;
    //light.shadow.blurSamples = 16; // VSMShadowMap only
    scene.add(light);
    scene.add(light.target);
    spotLights.push(light);
  }
  // Park lights that are no longer configured.
  for (let i = count; i < spotLights.length; i++){
    spotLights[i].intensity = 0;
    spotLights[i].castShadow = false;
  }
}

// floor surface (settable via config.surface.material / surface.texture)
const SURFACE_SIZE = 60; // world units across the floor plane
const floorGeo = new THREE.PlaneGeometry(SURFACE_SIZE, SURFACE_SIZE);
const floorMat = makeMaterial({ color: '#1c1e24', roughness: 0.9, metalness: 0.05 });
const floor = new THREE.Mesh(floorGeo, floorMat);
floor.receiveShadow = true;
floor.rotation.x = -Math.PI / 2;
scene.add(floor);

let floorTexture = null;
let skyMesh = null;

const rig = new THREE.Group();
// lay the instrument flat: the panel face (where knobs/LCD sit) now points up (+Y)
rig.rotation.x = -Math.PI / 2;
scene.add(rig);

/* ---------- manual orbit controls ---------- */
let azimuth = 0, elevation = 1.0 / 3, radius = 8; // straight front, 60° elevation
let cameraInitialized = false;
let dragging = false, lastX = 0, lastY = 0;

/* ---------- direct manipulation of panel controls (raycast picking) ---------- */
const KNOB_SWEEP_DEG = 270; // full-sweep pot travel, value 0 = full ccw, 1 = full cw
// Vertical pointer travel, in canvas pixels, that sweeps a knob's whole range.
const KNOB_DRAG_PIXELS = 200;
// A track seen shorter than this on screen points (nearly) at the camera, so
// dragging along it can't be resolved reliably.
const SLIDER_MIN_PIXEL_LENGTH = 6;

let activeConfig = null;
let activeControl = null; // control currently being dragged or held
let hoveredControl = null;
// Debug view mode for the instrument only (floor/sky untouched):
// 'standard' | 'wireframe' | 'normals'. Survives rebuilds; set from the UI.
let viewMode = 'standard';
let normalMaterial = null; // shared MeshNormalMaterial singleton
const controlPress = { startX: 0, startY: 0, startValue: 0 };

function clamp01(v){ return v < 0 ? 0 : v > 1 ? 1 : v; }

// control.kind is 'knob' | 'slider' | 'button', so the config list is the plural form.
function storeControlValue(control, key, value){
  const list = activeConfig && activeConfig[control.kind + 's'];
  const index = control.configIndex;
  if (!list || !list[index]) return;
  const before = resolveArray(list).map(e => e[key]);
  list[index][key] = value;
  const after = resolveArray(list).map(e => e[key]);
  // The sparse array resolves by inheritance, so an entry that omits this key
  // inherits it from the entry before. Pin any later entry whose value only
  // changed as a side effect, so editing one control can't change its siblings.
  for (let j = 0; j < after.length; j++){
    if (j === index || !list[j]) continue;
    if (after[j] !== before[j] && list[j][key] === undefined){
      list[j][key] = before[j];
    }
  }
}

function applyKnobValue(control, value){
  const v = clamp01(value);
  control.value = v;
  control.capGroup.rotation.z = -THREE.MathUtils.degToRad((v - 0.5) * KNOB_SWEEP_DEG);
  storeControlValue(control, 'value', v);
}

function applySliderValue(control, value){
  const v = clamp01(value);
  control.value = v;
  control.capGroup.position.y = (v - 0.5) * control.travel;
  storeControlValue(control, 'value', v);
}

function applyButtonPressed(control, pressed){
  control.pressed = !!pressed;
  control.capGroup.position.z = control.pressed ? -control.travel : 0;
}

function toggleButton(control){
  applyButtonPressed(control, !control.pressed);
}

// Walks the scene live rather than caching, so picking stays correct if
// geometry is added or moved after build(). The graph is small (tens of
// nodes), so this is far cheaper than the intersection test itself.
function collectPickableMeshes(){
  const out = [];
  scene.traverse(o => {
    if (o.isMesh && o.visible !== false) out.push(o);
  });
  return out;
}

const controlRaycaster = new THREE.Raycaster();
const controlNdc = new THREE.Vector2();

// Raycasts the scene at a client point and returns the frontmost hit's control,
// but only when a control surface is the nearest mesh — anything closer (body,
// floor, another part sitting in front) blocks the pick and falls through to orbit.
function findControlAt(clientX, clientY){
  const rect = dom.getBoundingClientRect();
  if (!rect.width || !rect.height) return null;
  controlNdc.x = ((clientX - rect.left) / rect.width) * 2 - 1;
  controlNdc.y = -((clientY - rect.top) / rect.height) * 2 + 1;
  controlRaycaster.setFromCamera(controlNdc, camera);
  const hits = controlRaycaster.intersectObjects(collectPickableMeshes(), false);
  if (!hits.length) return null;
  const mesh = hits[0].object;
  if (!mesh.userData.control) return null; // obstructed
  return { mesh, control: mesh.userData.control };
}

const _trkA = new THREE.Vector3();
const _trkB = new THREE.Vector3();
const _trkQ = new THREE.Quaternion();

// Projects a slider's full travel into CSS pixels: p0 is the track centre and
// (dx,dy) is the on-screen vector the cap sweeps across its 0..1 range.
function sliderTrackScreen(control){
  const rect = dom.getBoundingClientRect();
  if (!rect.width || !rect.height) return null;
  const g = control.group;
  g.updateWorldMatrix(true, false);
  _trkA.setFromMatrixPosition(g.matrixWorld);
  _trkB.set(0, 1, 0).applyQuaternion(g.getWorldQuaternion(_trkQ));
  _trkB.multiplyScalar(control.travel).add(_trkA);
  _trkA.project(camera);
  _trkB.project(camera);
  return {
    p0x: (_trkA.x * 0.5 + 0.5) * rect.width,
    p0y: (-_trkA.y * 0.5 + 0.5) * rect.height,
    dx: (_trkB.x - _trkA.x) * 0.5 * rect.width,
    dy: (-_trkB.y + _trkA.y) * 0.5 * rect.height
  };
}

// Relative mapping: the pointer's travel, measured along the track's projected
// axis and scaled by that axis' on-screen length, offsets the value captured at
// mousedown. Dragging across the whole visible travel sweeps the full range, so
// pressing a slider without moving it leaves its value untouched.
function sliderValueFromDrag(control, clientX, clientY){
  const t = sliderTrackScreen(control);
  if (!t) return control.value;
  const len2 = t.dx * t.dx + t.dy * t.dy;
  if (len2 < SLIDER_MIN_PIXEL_LENGTH * SLIDER_MIN_PIXEL_LENGTH) return control.value;
  const rect = dom.getBoundingClientRect();
  const sx = controlPress.startX - rect.left, sy = controlPress.startY - rect.top;
  const px = clientX - rect.left, py = clientY - rect.top;
  const along = ((px - sx) * t.dx + (py - sy) * t.dy) / len2;
  return controlPress.startValue + along;
}

function controlCursor(control){
  if (!control) return dragging ? 'grabbing' : 'grab';
  if (control.kind === 'button') return 'pointer';
  if (control.kind === 'knob') return 'ns-resize';
  const t = sliderTrackScreen(control);
  if (!t) return 'move';
  if (Math.hypot(t.dx, t.dy) < SLIDER_MIN_PIXEL_LENGTH) return 'move';
  return Math.abs(t.dx) > Math.abs(t.dy) ? 'ew-resize' : 'ns-resize';
}

function updateCamera(){
  const x = radius * Math.cos(Math.PI * elevation) * Math.sin(Math.PI * azimuth);
  const y = radius * Math.sin(Math.PI * elevation);
  const z = radius * Math.cos(Math.PI * elevation) * Math.cos(Math.PI * azimuth);
  camera.position.set(x, y, z);
  camera.lookAt(0, 0, 0);
  scheduleRender();
}

const dom = renderer.domElement;
dom.style.cursor = 'grab';

function refreshCursor(){
  dom.style.cursor = controlCursor(activeControl || hoveredControl);
}

dom.addEventListener('mousedown', e => {
  const hit = findControlAt(e.clientX, e.clientY);
  if (hit){
    // Take over the gesture so it adjusts the control instead of orbiting.
    activeControl = hit.control;
    controlPress.startX = e.clientX;
    controlPress.startY = e.clientY;
    controlPress.startValue = activeControl.value;
    if (activeControl.kind === 'button'){
      toggleButton(activeControl); // latch on press, like hardware
      scheduleRender();
    }
    // Sliders hold their value until the pointer actually moves.
    refreshCursor();
    return;
  }
  dragging = true; lastX = e.clientX; lastY = e.clientY; dom.style.cursor = 'grabbing';
});

window.addEventListener('mouseup', () => {
  activeControl = null;
  dragging = false;
  refreshCursor();
});

dom.addEventListener('mousemove', e => {
  if (activeControl){
    const c = activeControl;
    if (c.kind === 'knob'){
      // Drag up increases the value (screen Y grows downward).
      const delta = (controlPress.startY - e.clientY) / KNOB_DRAG_PIXELS;
      applyKnobValue(c, controlPress.startValue + delta);
      scheduleRender();
    } else if (c.kind === 'slider'){
      applySliderValue(c, sliderValueFromDrag(c, e.clientX, e.clientY));
      scheduleRender();
    }
    // A button has already latched, so dragging on does nothing further.
    refreshCursor();
    return;
  }
  if (dragging){
    const dx = e.clientX - lastX, dy = e.clientY - lastY;
    lastX = e.clientX; lastY = e.clientY;
    azimuth -= dx * 0.002;
    elevation = Math.max(-0.4999, Math.min(0.4999, elevation + dy * 0.002));
    updateCamera();
    return;
  }
  // Not dragging: reflect what is under the cursor in the pointer style.
  const hit = findControlAt(e.clientX, e.clientY);
  hoveredControl = hit ? hit.control : null;
  refreshCursor();
});
dom.addEventListener('wheel', e => {
  e.preventDefault();
  radius = Math.max(2.5, Math.min(24, radius + e.deltaY * 0.01));
  updateCamera();
}, { passive: false });

let touchLast = null;
dom.addEventListener('touchstart', e => { if (e.touches.length===1) touchLast = [e.touches[0].clientX, e.touches[0].clientY]; }, {passive:true});
dom.addEventListener('touchmove', e => {
  if (e.touches.length===1 && touchLast){
    const dx = e.touches[0].clientX - touchLast[0];
    const dy = e.touches[0].clientY - touchLast[1];
    touchLast = [e.touches[0].clientX, e.touches[0].clientY];
    azimuth -= dx * 0.006;
    elevation += dy * 0.006;
    updateCamera();
  }
}, {passive:true});
dom.addEventListener('touchend', () => { touchLast = null; });

/* ---------- geometry helpers ---------- */
function roundedRectShape(w, h, r){
  r = Math.max(0, Math.min(r, w/2, h/2));
  const shape = new THREE.Shape();
  const x = -w/2, y = -h/2;
  if (r <= 1e-5){
    shape.moveTo(x, y);
    shape.lineTo(x, y + h);
    shape.lineTo(x + w, y + h);
    shape.lineTo(x + w, y);
    shape.closePath();
    return shape;
  }
  shape.moveTo(x, y + r);
  shape.lineTo(x, y + h - r);
  // True quarter-circle corners (absarc) instead of quadratic Bezier curves,
  // so the outline stays exactly circular at any segment count. Each arc runs
  // 90° clockwise between its two edges' tangent points; three's EllipseCurve
  // takes decreasing start/end angles for a clockwise sweep.
  shape.absarc(x + r, y + h - r, r, Math.PI, Math.PI / 2, true);        // top-left
  shape.lineTo(x + w - r, y + h);
  shape.absarc(x + w - r, y + h - r, r, Math.PI / 2, 0, true);          // top-right
  shape.lineTo(x + w, y + r);
  shape.absarc(x + w - r, y + r, r, 0, -Math.PI / 2, true);             // bottom-right
  shape.lineTo(x + r, y);
  shape.absarc(x + r, y + r, r, -Math.PI / 2, -Math.PI, true);          // bottom-left
  return shape;
}

function assignPlanarUVs(geo, w, h){
  const pos = geo.attributes.position;
  const uvs = new Float32Array(pos.count * 2);
  for (let i = 0; i < pos.count; i++){
    uvs[i * 2] = (pos.getX(i) + w / 2) / w;
    uvs[i * 2 + 1] = (pos.getY(i) + h / 2) / h;
  }
  geo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
}

function makeBody(width, height, depth, corner, bevel, material, cornerSegments, bevelSegments){
  const cornerFactor = Math.max(0, Math.min(1, num(corner, 0.15)));
  const bevelFactor = Math.max(0, Math.min(1, num(bevel, 0.15)));

  // Unit factors: 1 = the largest rounding the current dimensions allow.
  // Corner: up to half the shorter side (a fully pill-shaped outline).
  // Bevel:  up to half the depth (the whole cross-section rounded).
  const maxCorner = Math.min(width, height) * 0.5;
  const cornerR = cornerFactor * maxCorner;

  const maxBevel = depth * 0.5;
  const bevelVal = bevelFactor * maxBevel;

  const shape2 = roundedRectShape(width, height, cornerR);
  const cSegs = Math.max(1, Math.min(64, num(cornerSegments, 16)));
  const bSegs = Math.max(1, Math.min(64, num(bevelSegments, 16)));

  const hasBevel = bevelVal > 1e-5;
  const extrudeDepth = hasBevel ? Math.max(0.001, depth - bevelVal * 2) : depth;
  const extrudeSettings = {
    steps: 1,
    depth: extrudeDepth,
    bevelEnabled: hasBevel,
    bevelThickness: bevelVal,
    bevelSize: bevelVal * 0.85,
    bevelSegments: bSegs,
    curveSegments: cSegs
  };
  let geo = new THREE.ExtrudeGeometry(shape2, extrudeSettings);
  // ExtrudeGeometry bakes per-face uv/normal attributes, and mergeVertices
  // hashes *every* attribute — so as-is it welds almost nothing, the curves
  // keep per-face normals, and flatShading becomes a no-op (smooth shading
  // can't smooth what was never welded either). Drop both attributes so the
  // weld is purely positional, then rebuild truly smooth normals.
  // (No body map support to lose: body material never had UVs worth keeping.)
  geo.deleteAttribute('uv');
  geo.deleteAttribute('normal');
  geo = mergeVertices(geo, 1e-4);
  geo.computeVertexNormals();
  geo.center();
  const mat = makeMaterial(material);
  const mesh = new THREE.Mesh(geo, mat);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
}


// common transport/control labels rendered as icons when a button label sits "on" the cap
// \uFE0E forces monochrome "text" presentation instead of a colored emoji glyph
const BUTTON_EMOJI = {
  PLAY: '\u25B6\uFE0E', STOP: '\u23F9\uFE0E', PAUSE: '\u23F8\uFE0E',
  REC: '\u23FA\uFE0E', RECORD: '\u23FA\uFE0E',
  FF: '\u23E9\uFE0E', FASTFORWARD: '\u23E9\uFE0E', REW: '\u23EA\uFE0E', REWIND: '\u23EA\uFE0E',
  NEXT: '\u23ED\uFE0E', PREV: '\u23EE\uFE0E', PREVIOUS: '\u23EE\uFE0E',
  EJECT: '\u23CF\uFE0E', POWER: '\u23FB\uFE0E',
  UP: '\u2B06\uFE0E', DOWN: '\u2B07\uFE0E', HOLD: '\u23F8\uFE0E'
};

function textTexture(text, {
  width = 256, height = 128, bg = 'transparent', fg = '#ffffff',
  font = '600 34px ui-monospace, monospace', align = 'center'
} = {}){
  const c = document.createElement('canvas');
  c.width = width; c.height = height;
  const ctx = c.getContext('2d');
  if (bg !== 'transparent'){ ctx.fillStyle = bg; ctx.fillRect(0,0,width,height); }
  ctx.font = font;
  ctx.fillStyle = fg;
  ctx.textAlign = align;
  ctx.textBaseline = 'middle';
  const lines = String(text).split('\n');
  const lh = height / (lines.length + 1);
  lines.forEach((line, i) => {
    ctx.fillText(line, align === 'center' ? width/2 : 8, lh * (i+1));
  });
  const tex = new THREE.CanvasTexture(c);
  tex.needsUpdate = true;
  return tex;
}

function makeKnob(k, depth){
  const knobGroup = new THREE.Group();
  const radius = num(k.radius, 0.16);
  const kHeight = num(k.height, 0.22);
  const km = matProps(k, { color: '#cfd3d8', roughness: 0.4, metalness: 0.25 });
  applyFacetingShorthand(km, k);
  const value = Math.max(0, Math.min(1, num(k.value, 0.5))); // knob setting, 0–1
  const segments = Math.max(3, Math.min(64, num(k.segments, 32)));
  const topRounding = Math.max(0, Math.min(1, num(k.topRounding, 0)));
  const skirtFactor = Math.max(0, Math.min(1, num(k.skirtFactor, 0)));
  const skirtRounding = Math.max(0, Math.min(1, num(k.skirtRounding, 0)));

  // cap + indicator turn together to reflect the knob's value
  const capGroup = new THREE.Group();
  const knobInfo = { kind: 'knob', group: knobGroup, capGroup, value, configIndex: k.knobIndex };

  const bodyMat = makeMaterial(km);

  // Build the knob body as a surface of revolution. Unlike an extruded
  // circle, this keeps the wall's radial normals continuous and reflections even.
  const halfHeight = kHeight * 0.5;
  const topBevel = Math.min(topRounding * Math.min(radius, halfHeight), radius, halfHeight);
  const bevelSegments = Math.max(4, Math.ceil(segments / 2));
  const topRadius = radius * (1 - skirtFactor);

  // The profile is a cross-section in the (x = radius, y = height) half-plane,
  // revolved around the y axis. It is built from the bottom up:
  //   1. axis → outer base, then a short vertical segment down to the body face
  //   2. if a skirt is present: horizontal skirt at the base, with a fillet
  //      of radius `skirtRounding * (radius - topRadius)` rounding the inner
  //      corner where the skirt meets the vertical cylinder.
  //   3. vertical cylinder wall up to the top
  //   4. top edge (optionally rounded by `topRounding`) → axis
  const baseY = -halfHeight;
  const bottomY = -halfHeight * .9;
  const skirtFillR = skirtRounding * (radius - topRadius);

  const profile = [
    new THREE.Vector2(0, baseY),
    new THREE.Vector2(radius, baseY),
    new THREE.Vector2(radius, baseY+.001),
    new THREE.Vector2(radius, bottomY)
  ];

  if (skirtFactor > 0){
    // Small bevel at the outer edge of the skirt, rounding the corner between
    // the vertical connector and the horizontal skirt top. This softens the
    // 90° edge so the lighting transitions smoothly.
    {
      const outerBevelR = kHeight * 0.0;
      // Bevel arc: center at (radius - outerBevelR, bottomY + outerBevelR),
      // from (radius, bottomY + outerBevelR) to (radius - outerBevelR, bottomY).
      const bCx = radius - outerBevelR;
      const bCy = bottomY - outerBevelR;
      for (let i = 1; i <= bevelSegments; i++){
        const t = i / bevelSegments;
        const angle = t * (Math.PI / 2);
        profile.push(new THREE.Vector2(
          bCx + outerBevelR * Math.cos(angle),
          bCy + outerBevelR * Math.sin(angle)
        ));
      }
    }

    // Horizontal skirt at the base extends out to (radius - outerBevelR).
    // The fillet rounds the inner corner where the skirt meets the vertical cylinder;
    // its radius is `skirtRounding * (radius - topRadius)`, capped so it never
    // reaches the outer rim.
    // Always emit the arc so the profile topology is consistent for
    // skirtRounding=0 and skirtRounding=0.0001.
    {
      // Fillet arc: center at (topRadius + skirtFillR, bottomY + skirtFillR),
      // sweeping clockwise from below-center to left-of-center.
      // The arc starts at t=0 (the skirt end) so the skirt remains fully
      // horizontal right up to the skirtRounding.
      const filletCx = topRadius + skirtFillR;
      const filletCy = bottomY + skirtFillR;
      for (let i = 0; i <= bevelSegments; i++){
        const t = i / bevelSegments;
        const angle = -Math.PI / 2 - t * (Math.PI / 2);
        profile.push(new THREE.Vector2(
          filletCx + skirtFillR * Math.cos(angle),
          filletCy + skirtFillR * Math.sin(angle)
        ));
      }
    }

    // vertical cylinder wall up to the start of the top bevel
    profile.push(new THREE.Vector2(topRadius, halfHeight - topBevel));
  } else {
    // No skirt: the side wall goes straight up at the full radius.
    profile.push(new THREE.Vector2(radius, halfHeight - topBevel));
  }

  // top edge bevel (topRounding). Always emit the arc so the profile topology
  // is consistent for topRounding=0 and topRounding=0.0001.
  {
    const outerR = skirtFactor > 0 ? topRadius : radius;
    const innerR = Math.max(0, outerR - topBevel);
    for (let i = 1; i <= bevelSegments; i++){
      const angle = (i / bevelSegments) * (Math.PI / 2);
      profile.push(new THREE.Vector2(innerR + topBevel * Math.cos(angle), halfHeight - topBevel + topBevel * Math.sin(angle)));
    }
  }

  // top center (skip if profile already reaches the axis)
  if (profile[profile.length - 1].x > 0.0001){
    profile.push(new THREE.Vector2(0, halfHeight));
  }

  const knobGeo = new THREE.LatheGeometry(profile, segments);
  const knobMesh = new THREE.Mesh(knobGeo, bodyMat);
  knobMesh.rotation.x = Math.PI / 2;
  knobMesh.position.z = depth/2 + halfHeight;
  knobMesh.castShadow = true;
  knobMesh.receiveShadow = true;
  knobMesh.userData.control = knobInfo;
  capGroup.add(knobMesh);

  // indicator ridge — markLength/markWidth are proportional to the knob radius
  const markColor = k.markColor || '#15161a';
  const markLength = Math.max(0.01, num(k.markLength, 0.55));
  const markWidth = Math.max(0.01, num(k.markWidth, 0.14));
  const markDepth = kHeight * 1.1;
  const ridgeGeo = new THREE.BoxGeometry(radius * markWidth, radius * markLength, markDepth);
  const ridgeMat = makeMaterial({ color: markColor, roughness: 0.6, metalness: 0.2, opacity: km.opacity });
  const ridge = new THREE.Mesh(ridgeGeo, ridgeMat);
  ridge.position.set(0, radius * markLength, depth/2 + kHeight * 0.02 + markDepth * 0.5);
  ridge.castShadow = true;
  ridge.receiveShadow = true;
  ridge.userData.control = knobInfo;
  capGroup.add(ridge);

  const sweepDeg = KNOB_SWEEP_DEG; // full-sweep pot travel, value 0 = full ccw, 1 = full cw
  capGroup.rotation.z = -THREE.MathUtils.degToRad((value - 0.5) * sweepDeg);
  knobGroup.add(capGroup);

  // label plate
  if (k.label) {
    const labelSize = num(k.labelSize, 0.15);
    const labelColor = k.labelColor || '#c9ccd2';
    const tex = textTexture(String(k.label).toUpperCase(), {
      width: 220, height: 72, fg: labelColor,
      font: `600 ${Math.round(150 * labelSize)}px ui-monospace, monospace`
    });
    const labelGeo = new THREE.PlaneGeometry(5 * labelSize, labelSize);
    const labelMat = makeMaterial({ roughness: 0.5, metalness: 0.0, opacity: km.opacity }, { map: tex, transparent: true,
    polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4 });
    const labelMesh = new THREE.Mesh(labelGeo, labelMat);
    labelMesh.castShadow = false;
    labelMesh.receiveShadow = true;
    labelMesh.position.set(0, -radius * 1.1 - .7 * labelSize - .01, depth/2 + 0.006);
    knobGroup.add(labelMesh);
  }

  knobGroup.position.set(num(k.x,0), num(k.y,0), 0);
  return knobGroup;
}

function makeLCD(lcd, depth){
  const g = new THREE.Group();
  const w = num(lcd.width, 1.0);
  const h = num(lcd.height, 1.0);
  const bez = Math.max(0, num(lcd.bezel, 0.1*w));
  const bg = lcd.backgroundColor || '#08130b';
  const fg = lcd.textColor || '#7CFC98';
  const lm = matProps(lcd, { color: '#0d0d10', roughness: 0.6, metalness: 0.1 });
  const bezelColor = lcd.bezelColor || lm.color;
  const roundedness = Math.max(0, Math.min(1, num(lcd.roundedness, 0)));

  const sw = Math.max(0.01, w - 2 * bez);
  const sh = Math.max(0.01, h - 2 * bez);

  let bezelGeo, screenGeo;
  if (roundedness <= 1e-5){
    bezelGeo = new THREE.PlaneGeometry(w, h);
    screenGeo = new THREE.PlaneGeometry(sw, sh);
  } else {
    const maxBezelR = Math.min(w, h) * 0.5;
    const bezelR = roundedness * maxBezelR;
    const maxScreenR = Math.min(sw, sh) * 0.5;
    const screenR = bezelR > bez ? Math.min(maxScreenR, bezelR - bez) : roundedness * maxScreenR;

    const bezelShape = roundedRectShape(w, h, bezelR);
    bezelGeo = new THREE.ShapeGeometry(bezelShape, 16);
    assignPlanarUVs(bezelGeo, w, h);
    bezelGeo.computeVertexNormals();

    const screenShape = roundedRectShape(sw, sh, screenR);
    screenGeo = new THREE.ShapeGeometry(screenShape, 16);
    assignPlanarUVs(screenGeo, sw, sh);
    screenGeo.computeVertexNormals();
  }

  const bezelMat = makeMaterial({ color: bezelColor, roughness: lm.roughness, metalness: lm.metalness, opacity: lm.opacity });
  const bezel = new THREE.Mesh(bezelGeo, bezelMat);
  bezel.position.z = depth/2 + 0.003;
  bezel.castShadow = true;
  bezel.receiveShadow = true;
  g.add(bezel);

  const tex = textTexture(lcd.text || '', {
    width: 512, height: 256, bg: bg, fg: fg,
    font: '600 46px ui-monospace, monospace'
  });
  const screenMat = makeMaterial({ color: '#fff', roughness: lm.roughness, metalness: lm.metalness, opacity: lm.opacity }, { map: tex, emissiveMap: tex, emissive: "#fff", emissiveIntensity: 0.5 });
  const screen = new THREE.Mesh(screenGeo, screenMat);
  screen.position.z = depth/2 + 0.012;
  g.add(screen);

  g.position.set(num(lcd.x,0), num(lcd.y,0), 0);
  return g;
}

function makeButton(btn, depth){
  const g = new THREE.Group();
  const w = Math.max(0.05, num(btn.width, 0.4));
  const len = Math.max(0.05, num(btn.length, 0.4));
  const btnHeight = Math.max(0.01, num(btn.height, 0.12));
  const travel = Math.max(0, Math.min(btnHeight, num(btn.travel, btnHeight * 0.65)));
  const bm = matProps(btn, { color: '#3a3d44', roughness: 0.4, metalness: 0.25 });
  const roundedness = Math.max(0, Math.min(1, num(btn.roundedness, 0.4)));

  const capGroup = new THREE.Group();
  const buttonInfo = { kind: 'button', pressed: false, travel, capGroup, configIndex: btn.buttonIndex };

  // Cap geometry params, needed up front: the bevel flares a lip at the cap's
  // base that is bevelSize wider than the shape itself, and that lip — not the
  // shape — is what sits in the recess opening.
  const capRadius = Math.max(0.001, Math.min(w, len) * 0.5 * roundedness);
  // Cap the bevel so a high roundedness can't balloon the base into a rectangular
  // lip that dips below the recess and pokes out at the cap's base.
  const bevelThickness = Math.min(capRadius * 0.35, 0.022);
  const bevelSize = Math.min(capRadius * 0.3, Math.min(w, len) * 0.06);

  // Recessed bezel (the "hole" the cap sits in). Sized from the cap's actual
  // outline — lip included — and offset outward by a flat `margin`, so the
  // visible gap is the same on every side of every button no matter how big it
  // is or how round, and the cap always fits through the hole it drops into.
  const margin = 0.01;
  if (margin > 0) {
    const bezelW = w + 2 * bevelSize + 2 * margin;
    const bezelL = len + 2 * bevelSize + 2 * margin;
    // Offsetting a rounded rect outward by m grows each corner radius by exactly m.
    const bezelR = Math.max(0.001,
      Math.min(capRadius + bevelSize + margin, Math.min(bezelW, bezelL) * 0.5));
    const bezelShape = roundedRectShape(bezelW, bezelL, bezelR);
    const bezelGeo = new THREE.ExtrudeGeometry(bezelShape, { depth: 0.02, bevelEnabled: false, curveSegments: 10 });
    const bezelMat = makeMaterial({ color: '#444', roughness: 1.0, opacity: bm.opacity });
    const bezel = new THREE.Mesh(bezelGeo, bezelMat);
    bezel.position.z = depth/2 - 0.016;
    bezel.receiveShadow = true;
    g.add(bezel);
  }
  // rounded rectangular cap — corner radius driven by the roundedness parameter
  const shape = roundedRectShape(w, len, capRadius);
  const capGeo = new THREE.ExtrudeGeometry(shape, {
    depth: btnHeight, bevelEnabled: true,
    bevelThickness, bevelSize,
    bevelSegments: 3, curveSegments: 8
  });
  capGeo.center();
  const capMat = makeMaterial(bm);
  const capMesh = new THREE.Mesh(capGeo, capMat);
  capMesh.position.z = depth/2 + btnHeight/2 + 0.008;
  capMesh.castShadow = true;
  capMesh.receiveShadow = true;
  capMesh.userData.control = buttonInfo;
  capGroup.add(capMesh);
  g.add(capGroup);

  // label
  if (btn.label){
    const labelSize = num(btn.labelSize, 0.15);
    const labelColor = btn.labelColor || '#c9ccd2';
    const labelPosition = String(btn.labelPosition || 'on').toLowerCase(); // 'on' | 'above' | 'below'
    const upperLabel = String(btn.label).toUpperCase();
    const emoji = BUTTON_EMOJI[upperLabel];

    if (labelPosition === 'on'){
      // icon printed on the cap's top face, as a flat monochrome glyph (no emoji background)
      const display = emoji || upperLabel;
      const fontFamily = emoji
        ? "'Segoe UI Symbol','Noto Sans Symbols','Arial Unicode MS',sans-serif"
        : 'ui-monospace, monospace';
      const fontSize = emoji ? 1000 * labelSize : 400 * labelSize;
      const tex = textTexture(display, {
        width: 220, height: 220, fg: labelColor,
        font: `600 ${Math.round(fontSize)}px ${fontFamily}`
      });
      const plateSize = Math.min(w, len) * 5 * labelSize;
      const plateGeo = new THREE.PlaneGeometry(plateSize, plateSize);
      const plateMat = makeMaterial(bm, {
        map: tex, transparent: true,
        polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4
      });
      const plate = new THREE.Mesh(plateGeo, plateMat);
      const topZ = depth/2 + btnHeight + 0.008 + capRadius * 0.35;
      plate.position.set(0, 0, topZ + 0.015);
      plate.userData.control = buttonInfo;
      capGroup.add(plate);
    } else {
      // printed name plate above or below the button
      const fontPx = Math.round(Math.max(14, 200 * labelSize));
      const tex = textTexture(upperLabel, {
        width: 256, height: 72, fg: labelColor,
        font: `600 ${fontPx}px ui-monospace, monospace`
      });
      const labelW = Math.max(w * 1.2, 5 * labelSize);
      const labelH = Math.max(0.1, labelSize * 1.2);
      const labelGeo = new THREE.PlaneGeometry(labelW, labelH);
      const labelMat = makeMaterial({roughness: 0.5, metalness: 0.0, opacity: bm.opacity,
        map: tex, transparent: true, depthWrite: false,
        polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4
      });
      const labelMesh = new THREE.Mesh(labelGeo, labelMat);
      labelMesh.castShadow = false;
      labelMesh.receiveShadow = true;
      const sign = labelPosition === 'above' ? 1 : -1;
      const yOff = sign * (len * 0.6 + labelH * 0.6 + 0.02);
      labelMesh.position.set(0, yOff, depth/2 + 0.008);
      g.add(labelMesh);
    }
  }

  g.position.set(num(btn.x,0), num(btn.y,0), 0);
  return g;
}

function makeSliderTrackTexture(){
  const c = document.createElement('canvas');
  c.width = 128;
  c.height = 512;
  const ctx = c.getContext('2d');
  ctx.clearRect(0, 0, 128, 512);

  // Recessed center slot with subtle shadow
  const slotW = 10;
  const slotX = (128 - slotW) / 2;
  const slotY = 16;
  const slotH = 512 - 32;

  // Slot outer groove shadow
  ctx.fillStyle = 'rgba(0,0,0,0.6)';
  ctx.fillRect(slotX - 2, slotY - 2, slotW + 4, slotH + 4);

  // Slot interior cavity
  ctx.fillStyle = '#08090c';
  ctx.fillRect(slotX, slotY, slotW, slotH);

  // Minimal Roland SH-101 style graduation tick marks
  const numTicks = 9;
  for (let i = 0; i < numTicks; i++){
    const y = 30 + (i / (numTicks - 1)) * (512 - 60);
    const isMajor = (i === 0 || i === Math.floor(numTicks / 2) || i === numTicks - 1);
    const tickLen = isMajor ? 16 : 9;
    const tickH = isMajor ? 3 : 2;
    ctx.fillStyle = isMajor ? 'rgba(255,255,255,0.45)' : 'rgba(255,255,255,0.22)';

    // Left tick
    ctx.fillRect(slotX - 5 - tickLen, y - tickH / 2, tickLen, tickH);
    // Right tick
    ctx.fillRect(slotX + slotW + 5, y - tickH / 2, tickLen, tickH);
  }

  const tex = new THREE.CanvasTexture(c);
  tex.needsUpdate = true;
  return tex;
}

let cachedSliderTrackTex = null;
function getSliderTrackTexture(){
  if (!cachedSliderTrackTex){
    cachedSliderTrackTex = makeSliderTrackTexture();
  }
  return cachedSliderTrackTex;
}

function makeSlider(s, depth){
  const g = new THREE.Group();
  const trackLen = Math.max(0.15, num(s.length, 0.75));
  const orientationDeg = num(s.orientation, 0);
  const value = Math.max(0, Math.min(1, num(s.value, 0.5)));
  const sm = matProps(s, { color: '#202228', roughness: 0.4, metalness: 0.2 });
  const indicatorColor = s.indicatorColor || '#ffffff';

  const capWidth = Math.max(0.05, num(s.capWidth, 0.16));
  const capLength = Math.max(0.05, num(s.capLength, 0.22));
  const capHeight = Math.max(0.01, num(s.capHeight, 0.14));
  const capRoundedness = Math.max(0, Math.min(1, num(s.capRoundedness, 0.2)));
  const capRadius = Math.max(0.001, Math.min(capWidth, capLength) * 0.5 * capRoundedness);
  const plateWidth = capWidth * 1.85;
  const plateLength = trackLen + capLength * 0.55;

  // Track plate with recessed slot & ticks
  const trackTex = getSliderTrackTexture();
  const trackGeo = new THREE.PlaneGeometry(plateWidth, plateLength);
  const trackMat = makeMaterial({ roughness: 0.85, metalness: 0.1, opacity: sm.opacity,
    map: trackTex,
    transparent: true,
    depthWrite: false,
    polygonOffset: true,
    polygonOffsetFactor: -4,
    polygonOffsetUnits: -4
  });
  const trackMesh = new THREE.Mesh(trackGeo, trackMat);
  trackMesh.position.z = depth / 2 + 0.004;
  trackMesh.receiveShadow = true;
  g.add(trackMesh);

  // Slider Cap (Handle): minimalist beveled Roland SH-101 style
  const capGroup = new THREE.Group();

  const capShape = roundedRectShape(capWidth, capLength, capRadius);
  const capGeo = new THREE.ExtrudeGeometry(capShape, {
    depth: capHeight,
    bevelEnabled: true,
    bevelThickness: 0.012,
    bevelSize: 0.01,
    bevelSegments: 3
  });
  capGeo.center();
  const capMat = makeMaterial(sm);
  const capMesh = new THREE.Mesh(capGeo, capMat);
  capMesh.castShadow = true;
  capMesh.receiveShadow = true;
  capGroup.add(capMesh);

  // Position cap along track seated flush against the chassis
  const capTravel = trackLen - capLength * 0.3;
  const capY = (value - 0.5) * capTravel;
  capGroup.position.set(0, capY, depth / 2 + capHeight / 2 - 0.01);
  g.add(capGroup);

  // Both the cap and the track plate set the value, mapped onto the track axis.
  const sliderInfo = { kind: 'slider', group: g, capGroup, value, travel: capTravel, configIndex: s.sliderIndex };
  capMesh.userData.control = sliderInfo;
  trackMesh.userData.control = sliderInfo;

  // Label plate (optional)
  if (s.label){
    const rawSize = num(s.labelSize, 0.15);
    const labelColor = s.labelColor || '#c9ccd2';
    const labelPosition = String(s.labelPosition || 'below').toLowerCase();
    const effectiveSize = rawSize > 0.5 ? rawSize * 0.15 : rawSize;
    const fontPx = Math.round(Math.max(14, 200 * effectiveSize));
    const tex = textTexture(String(s.label).toUpperCase(), {
      width: 256,
      height: 72,
      fg: labelColor,
      font: `600 ${fontPx}px ui-monospace, monospace`
    });
    const labelW = Math.max(capWidth * 3.5, 5 * effectiveSize);
    const labelH = Math.max(0.1, effectiveSize * 1.2);
    const labelGeo = new THREE.PlaneGeometry(labelW, labelH);
    const labelMat = makeMaterial({opacity: sm.opacity,
      map: tex,
      transparent: true,
      depthWrite: false,
      polygonOffset: true,
      polygonOffsetFactor: -4,
      polygonOffsetUnits: -4
    });
    const labelMesh = new THREE.Mesh(labelGeo, labelMat);
    labelMesh.castShadow = false;
    labelMesh.receiveShadow = true;
    const sign = labelPosition === 'above' ? 1 : -1;
    const yOff = sign * (plateLength / 2 + labelH * 0.6 + 0.02);
    labelMesh.position.set(0, yOff, depth / 2 + 0.008);
    g.add(labelMesh);
  }

  // Rotation according to orientation in degrees (0 = vertical, 90 = horizontal)
  g.rotation.z = -THREE.MathUtils.degToRad(orientationDeg);
  g.position.set(num(s.x, 0), num(s.y, 0), 0);
  return g;
}

function makeLabel(lbl, depth){
  if (!lbl || lbl.text === undefined || lbl.text === null) return null;

  const textStr = String(lbl.text);
  const lines = textStr.split('\n');

  // Extract font, fontsize, fontweight, color, material properties
  const fontFam = lbl.font || 'ui-monospace, monospace';
  const fontWeight = lbl.fontweight ?? lbl.fontWeight ?? '600';
  const mat = matProps(lbl, { color: lbl.textColor || '#ffffff', roughness: 0.4, metalness: 0.25 });

  const rawSize = lbl.fontsize ?? lbl.fontSize ?? 28;
  let numFontSize = 28;
  if (typeof rawSize === 'number' && isFinite(rawSize)){
    numFontSize = rawSize;
  } else if (typeof rawSize === 'string'){
    const parsed = parseFloat(rawSize);
    if (!isNaN(parsed) && isFinite(parsed)) numFontSize = parsed;
  }

  // Calculate 3D world font height directly from fontsize
  let worldFontHeight = 0.22;
  if (numFontSize > 5){
    worldFontHeight = numFontSize * 0.008; // e.g. 24 -> 0.192, 28 -> 0.224, 72 -> 0.576
  } else if (numFontSize > 1 && numFontSize <= 5){
    worldFontHeight = numFontSize * 0.2;
  } else if (numFontSize > 0 && numFontSize <= 1){
    worldFontHeight = numFontSize;
  }

  // Canvas rasterization with ample padding so text is never cropped vertically or horizontally
  const canvasFontSize = 64; // base pixel size on canvas
  const canvasFont = `${fontWeight} ${canvasFontSize}px ${fontFam}`;

  const measureCanvas = document.createElement('canvas');
  const measureCtx = measureCanvas.getContext('2d');
  measureCtx.font = canvasFont;

  let maxLineWidth = 0;
  lines.forEach(line => {
    const m = measureCtx.measureText(line).width;
    if (m > maxLineWidth) maxLineWidth = m;
  });
  if (maxLineWidth <= 0) maxLineWidth = canvasFontSize;

  const canvasLineHeight = canvasFontSize * 1.25;
  const padX = canvasFontSize * 0.35;
  const padY = canvasFontSize * 0.3;

  const canvasWidth = Math.ceil(maxLineWidth + padX * 2);
  const canvasHeight = Math.ceil(lines.length * canvasLineHeight + padY * 2);

  const drawCanvas = document.createElement('canvas');
  drawCanvas.width = canvasWidth;
  drawCanvas.height = canvasHeight;
  const ctx = drawCanvas.getContext('2d');

  ctx.font = canvasFont;
  ctx.fillStyle = mat.color;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';

  const totalContentHeight = lines.length * canvasLineHeight;
  const startY = (canvasHeight - totalContentHeight) / 2 + canvasLineHeight / 2;

  lines.forEach((line, i) => {
    ctx.fillText(line, canvasWidth / 2, startY + i * canvasLineHeight);
  });

  const tex = new THREE.CanvasTexture(drawCanvas);
  tex.needsUpdate = true;

  // 3D Plane Geometry determined by font size & measured text dimensions
  const worldUnitsPerCanvasPixel = worldFontHeight / canvasFontSize;
  const w = canvasWidth * worldUnitsPerCanvasPixel;
  const h = canvasHeight * worldUnitsPerCanvasPixel;

  // Position (x, y, and optional z)
  let posX = num(lbl.x, 0);
  let posY = num(lbl.y, 0);
  let posZ = num(lbl.z, 0);
  if (lbl.position !== undefined){
    if (Array.isArray(lbl.position)){
      posX = num(lbl.position[0], posX);
      posY = num(lbl.position[1], posY);
      posZ = num(lbl.position[2], posZ);
    } else if (typeof lbl.position === 'object' && lbl.position !== null){
      posX = num(lbl.position.x, posX);
      posY = num(lbl.position.y, posY);
      posZ = num(lbl.position.z, posZ);
    }
  }

  // Horizontal alignment: "left"/"right" anchor the edge at x, "center" (default) anchors the middle
  const align = String(lbl.align || 'center').toLowerCase();
  const alignX = align === 'left' ? w / 2 : align === 'right' ? -w / 2 : 0;

  const labelGeo = new THREE.PlaneGeometry(w, h);
  const labelMat = makeMaterial({ ...mat,
    map: tex,
    transparent: true,
    depthWrite: false,
    polygonOffset: true,
    polygonOffsetFactor: -4,
    polygonOffsetUnits: -4
  });
  const labelMesh = new THREE.Mesh(labelGeo, labelMat);
  labelMesh.castShadow = false;
  labelMesh.position.set(alignX, 0, depth / 2 + 0.008 + posZ);

  const labelGroup = new THREE.Group();
  labelGroup.add(labelMesh);
  labelGroup.position.set(posX, posY, 0.0);
  return labelGroup;
}

// Paints one seamless tile for the floor and returns it as a repeating
// texture. The tile is drawn so its edges join: edge-centred lines and
// half-bands on opposite borders complete each other across neighbouring
// tiles, and corner dots become whole dots once repeated.
function makeSurfaceTexture(cfg, surfaceColor){
  const res = 256;
  const canvas = document.createElement('canvas');
  canvas.width = res;
  canvas.height = res;
  const ctx = canvas.getContext('2d');

  const pattern = String(cfg.pattern || 'grid').toLowerCase();
  const bg = cfg.background || surfaceColor || '#1c1e24';
  const ink = cfg.color || '#ffffff';
  // Tile size and feature width are world units (like every other length in
  // the config); convert them to pixels at this tile's resolution.
  const size = Math.max(0.01, num(cfg.size, 2));
  const width = Math.max(0.001, Math.min(size, num(cfg.width, 0.06)));
  const w = (width / size) * res;

  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, res, res);
  ctx.fillStyle = ink;
  ctx.strokeStyle = ink;

  if (pattern === 'checker'){
    const half = res / 2;
    ctx.fillRect(0, 0, half, half);
    ctx.fillRect(half, half, half, half);
  } else if (pattern === 'dots'){
    // One circle per tile, so the repeat (derived from `size`) spaces the
    // dots exactly `size` apart in x and y.
    const r = Math.max(0.5, w / 2);
    ctx.beginPath();
    ctx.arc(res / 2, res / 2, r, 0, Math.PI * 2);
    ctx.fill();
  } else if (pattern === 'stripes'){
    const half = Math.max(0.5, w) / 2;
    ctx.fillRect(0, 0, half, res);
    ctx.fillRect(res - half, 0, half, res);
  } else { // grid
    ctx.lineWidth = Math.max(1, w);
    for (const p of [0, res]){
      ctx.beginPath(); ctx.moveTo(p, 0); ctx.lineTo(p, res); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(0, p); ctx.lineTo(res, p); ctx.stroke();
    }
  }

  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.wrapS = THREE.RepeatWrapping;
  tex.wrapT = THREE.RepeatWrapping;
  const repeat = SURFACE_SIZE / size;
  tex.repeat.set(repeat, repeat);
  const [ox, oy] = pair(cfg.offset, 0);
  tex.offset.set(ox, oy);
  tex.anisotropy = maxAnisotropy();
  tex.needsUpdate = true;
  return tex;
}

// The circle color with its alpha dialed to zero. Fading to this (rather
// than transparent black) keeps the glow's hue clean through the falloff.
function transparentOf(color){
  const m = /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(String(color).trim());
  if (!m) return 'rgba(0,0,0,0)';
  const h = m[1];
  const full = h.length === 3 ? h[0]+h[0]+h[1]+h[1]+h[2]+h[2] : h;
  const r = parseInt(full.slice(0, 2), 16);
  const g = parseInt(full.slice(2, 4), 16);
  const b = parseInt(full.slice(4, 6), 16);
  return `rgba(${r},${g},${b},0)`;
}

// Paints the skybox texture: one flat color with a few light circles high up
// in the sky (upper part of the canvas maps toward the zenith; the canvas
// middle is the horizon, in the +X direction). The layout is deterministic,
// so rebuilding never reshuffles the circles.
function makeSkyTexture({ color, circleColor, circles, circleSize, spread, seed }){
  const W = 1024, H = 512;
  const canvas = document.createElement('canvas');
  canvas.width = W;
  canvas.height = H;
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = color;
  ctx.fillRect(0, 0, W, H);

  let s = seed | 0;
  const rnd = () => {
    s = (s + 0x6D2B79F5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  for (let i = 0; i < circles; i++){
    const cx = W * (0.5 + (rnd() - 0.5) * 0.35 * spread);
    // high above the horizon (canvas top = zenith) but clear of the pole pinch
    const cy = H * (0.22 + (rnd() - 0.5) * 0.25 * spread);
    const r = Math.max(1, circleSize * (0.5 + rnd()));
    // soft disc: full circle color at the middle, fading to the same hue at
    // zero alpha by r (no dark fringe the way transparent black gives)
    const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
    g.addColorStop(0, circleColor);
    g.addColorStop(1, transparentOf(circleColor));
    ctx.fillStyle = g;
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.fill();
  }

  const tex = new THREE.CanvasTexture(canvas);
  // Canvas bytes are sRGB; without this the renderer treats them as linear
  // and washes the fill out on output (white circles survive that untouched,
  // which is exactly the "circles but no fill" look).
  tex.colorSpace = THREE.SRGBColorSpace;
  // The equirect flag is what makes three compile the env-sampling shader
  // chunk for this texture — without it scene.environment is silently ignored
  // and lit materials go black when the lights do.
  tex.mapping = THREE.EquirectangularReflectionMapping;
  tex.needsUpdate = true;
  return tex;
}

function num(v, fallback){
  return (typeof v === 'number' && isFinite(v)) ? v : fallback;
}

// Reads a scalar or [x, y] pair, falling back per component.
function pair(v, fallback){
  if (Array.isArray(v)) return [num(v[0], fallback), num(v[1], fallback)];
  return [num(v, fallback), num(v, fallback)];
}

// Anisotropic filtering for the tiled floor; capped to keep the cost modest.
function maxAnisotropy(){
  const caps = renderer.capabilities || {};
  return Math.max(1, Math.min(8, num(caps.getMaxAnisotropy?.(), 1)));
}

/**
 * Resolves color/roughness/metalness/opacity for a config entry: the nested
 * `material` dict first, then flat fields on the entry itself, then defaults.
 * Any other field in the material dict passes straight through untouched.
 */
function matProps(obj, defaults){
  obj = obj || {};
  const m = (typeof obj.material === 'object' && obj.material !== null) ? obj.material : {};
  const props = { ...m,
    color: m.color ?? obj.color ?? defaults.color,
    roughness: Math.max(0, Math.min(1, num(m.roughness ?? obj.roughness, defaults.roughness))),
    metalness: Math.max(0, Math.min(1, num(m.metalness ?? obj.metalness, defaults.metalness))),
    opacity: Math.max(0, Math.min(1, num(m.opacity ?? obj.opacity, defaults.opacity ?? 1.0)))
  };
  // `smoothShading` is the inverse spelling of three's `flatShading` — honor
  // it wherever it shows up. An explicit flatShading always wins; the alias
  // is deleted so three never sees (and warns about) an unknown property.
  if (props.flatShading === undefined && props.smoothShading !== undefined)
    props.flatShading = !props.smoothShading;
  delete props.smoothShading;
  return props;
}

/**
 * Per-part faceting shorthand: `flatShading` / `smoothShading` sitting next
 * to the part (not inside its `material` dict). An explicit material-level
 * flatShading always wins; otherwise the shorthand decides.
 */
function applyFacetingShorthand(props, entry){
  if (props.flatShading === undefined){
    if (entry.flatShading !== undefined) props.flatShading = !!entry.flatShading;
    else if (entry.smoothShading !== undefined) props.flatShading = !entry.smoothShading;
  }
  return props;
}

/**
 * Centralizes MeshStandardMaterial creation for all panel parts.
 * `params` is a matProps-style object (partial objects allowed; missing fields
 * fall back to three.js defaults); `extra` holds per-part parameters such as
 * map, emissive, side, or polygonOffset. Opacity defaults to 1.0 (opaque) and
 * enables transparency automatically when set below 1.
 */
function makeMaterial(params = {}, extra = {}) {
  // Any JSON field passes straight through; `extra` is just fallback defaults.
  // (Built fresh each call so defaults written here never leak back into a
  // caller-owned object that gets reused for the next mesh.)
  const merged = { opacity: 1.0, roughness: 0.5, ...extra, ...params };
  // Explicit `field: undefined` entries (e.g. `{opacity: src.opacity}` where
  // the source omits it) would otherwise clobber the defaults above *and*
  // make three warn — coerce them back.
  if (merged.opacity === undefined) merged.opacity = 1.0;
  if (merged.roughness === undefined) merged.roughness = 0.5;
  if (merged.transparent === undefined) merged.transparent = merged.opacity < 1;
  return new THREE.MeshStandardMaterial(merged);
}

/**
 * Substitutes `constants` references throughout the config.
 * Any string value anywhere in the config (any depth, inside dicts or
 * arrays) that exactly matches a key of the top-level `constants` dict is
 * replaced with a copy of that constant's value. Constant values may be any
 * JSON data type. Chained references (a constant whose value is another
 * constant's key) resolve via repeated passes, capped to avoid infinite
 * loops on cyclic definitions. The input is never mutated.
 */
function resolveConstants(config){
  const defs = (config && typeof config.constants === 'object' && config.constants !== null && !Array.isArray(config.constants))
    ? config.constants
    : null;
  if (!defs || Object.keys(defs).length === 0) return config;
  const clone = (v) => JSON.parse(JSON.stringify(v));
  let root = clone(config);
  const MAX_DEPTH = 100;
  const substitute = (node, snapshot, depth) => {
    if (depth > MAX_DEPTH) return node;
    if (typeof node === 'string'){
      if (Object.prototype.hasOwnProperty.call(snapshot, node)) return clone(snapshot[node]);
      return node;
    }
    if (Array.isArray(node)) return node.map(item => substitute(item, snapshot, depth + 1));
    if (node && typeof node === 'object'){
      const out = {};
      for (const key of Object.keys(node)) out[key] = substitute(node[key], snapshot, depth + 1);
      return out;
    }
    return node;
  };
  for (let pass = 0; pass < 10; pass++){
    const snapshot = root.constants;
    const next = substitute(root, snapshot, 0);
    if (JSON.stringify(next) === JSON.stringify(root)) { root = next; break; }
    root = next;
  }
  return root;
}

/**
 * Resolves a sparse array of panel elements (knobs, buttons, ...):
 * - The first entry should define every field it needs; anything it omits
 *   stays undefined (caller functions apply their own final fallbacks).
 * - Any later entry that omits a field inherits that field's value from
 *   the previous (already-resolved) entry.
 * - For x/y specifically: if omitted, and two previous resolved entries
 *   exist, the value continues the linear sequence: v[n] = 2*v[n-1] - v[n-2].
 *   With only one previous entry, it falls back to that entry's value.
 */
function resolveArray(raw, extrapolateKeys = ['x', 'y']){
  if (!Array.isArray(raw)) return [];
  const out = [];
  raw.forEach((item, i) => {
    item = item || {};
    const prev = out[i - 1];
    const prev2 = out[i - 2];
    const resolved = { ...item };
    const keys = new Set([...Object.keys(item), ...(prev ? Object.keys(prev) : [])]);
    keys.forEach(key => {
      if (item[key] !== undefined) return; // explicit value always wins
      if (extrapolateKeys.includes(key) && prev && typeof prev[key] === 'number'){
        if (prev2 && typeof prev2[key] === 'number'){
          resolved[key] = 2 * prev[key] - prev2[key];
        } else {
          resolved[key] = prev[key];
        }
      } else if (prev && prev[key] !== undefined){
        resolved[key] = prev[key];
      }
    });
    // Deep-merge the material sub-dictionary so partial overrides inherit
    // the previous entry's color/roughness/metalness instead of replacing them.
    const pm = prev && typeof prev.material === 'object' && prev.material !== null && prev.material;
    const im = typeof item.material === 'object' && item.material !== null && item.material;
    if (pm || im) resolved.material = { ...(pm || {}), ...(im || {}) };
    out.push(resolved);
  });
  return out;
}

/* ---------- build from config ---------- */
const fps = document.getElementById('fps');
let lastFpsUpdate = performance.now();
let framesSinceFpsUpdate = 0;
let renderContinuously = false;
let animationActive = false;
let renderQueued = false;

function renderFrame(now){
  if (physBox && !physSettled){
    const dt = Math.min(0.05, Math.max(0.0001, (now - physLastTime) / 1000 || 1 / 60));
    physLastTime = now;
    physWorld.step(1 / 60, dt, 3);
    syncRigFromBody();
    if (physBox.sleepState === CANNON.Body.SLEEPING){
      physSettled = true;
      syncRigFromBody();
      if (!renderContinuously) fps.textContent = '';
    }
  }
  renderer.render(scene, camera);
  if (!renderContinuously && physSettled) return;
  framesSinceFpsUpdate++;
  const elapsed = now - lastFpsUpdate;
  if (elapsed >= 250){
    fps.textContent = `${Math.round(framesSinceFpsUpdate * 1000 / elapsed)} fps`;
    lastFpsUpdate = now;
    framesSinceFpsUpdate = 0;
  }
}

function renderOnce(now){
  renderQueued = false;
  renderFrame(now);
}

function scheduleRender(){
  if (renderContinuously || renderQueued || animationActive) return;
  renderQueued = true;
  requestAnimationFrame(renderOnce);
}

function animate(now){
  if (!renderContinuously && physSettled){
    animationActive = false;
    return;
  }
  requestAnimationFrame(animate);
  renderFrame(now);
}

function setRenderMode(continuous){
  renderContinuously = continuous;
  fps.textContent = continuous ? '-- fps' : '';
  if (continuous && !animationActive){
    animationActive = true;
    lastFpsUpdate = performance.now();
    framesSinceFpsUpdate = 0;
    requestAnimationFrame(animate);
  } else if (!continuous) {
    scheduleRender();
  }
}

const VIEW_MODES = ['standard', 'wireframe', 'normals'];

// Applies the current view mode to every mesh of the instrument. Original
// materials are stashed on first swap so 'standard' restores the exact same
// objects (including any wireframe flag the JSON itself specified).
function applyViewMode(){
  if (!normalMaterial){
    // toneMapped:false keeps the normal colors exact; fog isn't a
    // MeshNormalMaterial property (and the scene uses no fog anyway).
    normalMaterial = new THREE.MeshNormalMaterial({ toneMapped: false });
  }
  rig.traverse(object => {
    if (!object.isMesh || !object.material) return;
    if (viewMode === 'standard'){
      if (object.userData.savedMaterial){
        object.material = object.userData.savedMaterial;
        delete object.userData.savedMaterial;
      }
      const mats = Array.isArray(object.material) ? object.material : [object.material];
      mats.forEach(m => {
        if (m.userData.origWireframe !== undefined){
          m.wireframe = m.userData.origWireframe;
          delete m.userData.origWireframe;
        }
      });
    } else {
      if (!object.userData.savedMaterial) object.userData.savedMaterial = object.material;
      if (viewMode === 'wireframe'){
        // Flag the saved original, never whatever overlay is currently
        // assigned: the shared normalMaterial must stay unflagged, or every
        // later normals view would render as wireframe too.
        object.material = object.userData.savedMaterial;
        const mats = Array.isArray(object.material) ? object.material : [object.material];
        mats.forEach(m => {
          if (m.userData.origWireframe === undefined) m.userData.origWireframe = !!m.wireframe;
          m.wireframe = true;
        });
      } else { // normals
        normalMaterial.wireframe = false;
        delete normalMaterial.userData.origWireframe;
        object.material = normalMaterial;
      }
    }
  });
  scheduleRender();
}

// Switches the instrument's debug view. Unknown modes are ignored; the mode
// persists across build() calls (which rebuild all materials from scratch).
function setViewMode(mode){
  if (!VIEW_MODES.includes(mode)) return;
  if (mode === viewMode && rig.children.length) return; // already applied
  viewMode = mode;
  applyViewMode();
}

function build(config){
  config = resolveConstants(config);
  while (rig.children.length) rig.remove(rig.children[0]);

  const render = config.render || {};
  const pixelRatio = Math.max(0.5, Math.min(2, num(render.pixelRatio, 1.5)));
  const shadowsEnabled = render.shadows !== false;
  const detailShadows = render.detailShadows === true;
  const continuousRendering = render.continuous === true;
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, pixelRatio));
  renderer.shadowMap.enabled = shadowsEnabled;
  fps.style.display = render.showFps === false ? 'none' : '';
  setRenderMode(continuousRendering);

  const b = config.body || {};
  const width = Math.max(0.3, num(b.width, 6));
  const height = Math.max(0.3, num(b.height, 2));
  const depth = Math.max(0.1, num(b.depth, 1.2));
  const corner = Math.max(0, Math.min(1, num(b.corner ?? b.roundedness, 0.15)));
  const bevel = Math.max(0, Math.min(1, num(b.bevel ?? b.roundedness, 0.15)));
  const cornerSegments = num(b.cornerSegments ?? b.curveSegments ?? b.segments, 16);
  const bevelSegments = num(b.bevelSegments ?? b.roundingSegments ?? b.segments, 16);

  const bodyProps = matProps(b, { color: '#2a2c31', roughness: 0.55, metalness: 0.18 });
  applyFacetingShorthand(bodyProps, b);
  const bodyMesh = makeBody(width, height, depth, corner, bevel, bodyProps, cornerSegments, bevelSegments);
  rig.add(bodyMesh);

  activeConfig = config;
  // Any in-flight gesture referenced meshes that are about to be destroyed.
  activeControl = null;
  dragging = false;
  hoveredControl = null;
  refreshCursor();
  const knobs = resolveArray(config.knobs);
  knobs.forEach((k, i) => { k.knobIndex = i; rig.add(makeKnob(k, depth)); });

  const buttons = resolveArray(config.buttons);
  buttons.forEach((btn, i) => { btn.buttonIndex = i; rig.add(makeButton(btn, depth)); });

  const sliders = resolveArray(config.sliders);
  sliders.forEach((s, i) => { s.sliderIndex = i; rig.add(makeSlider(s, depth)); });

  const labels = resolveArray(config.labels);
  labels.forEach(lbl => { const m = makeLabel(lbl, depth); if (m) rig.add(m); });

  if (config.lcd && typeof config.lcd === 'object'){
    rig.add(makeLCD(config.lcd, depth));
  }

  applyViewMode(); // re-applies the UI-selected debug view to the fresh materials

  // The chassis casts the important broad shadow. Small controls can opt in
  // when their individual shadows are worth the additional shadow-map work.
  rig.traverse(object => {
    if (object.isMesh && object.castShadow !== false) object.castShadow = detailShadows || object === bodyMesh;
  });

  const surface = config.surface || {};
  // Height (z) at which the surface plane is drawn. Defaults to z=0, the
  // plane the panel rests on; raising/lowering it only moves the drawing,
  // never the physics boundary.
  const surfaceZ = num(surface.z, 0);
  const smat = matProps(surface, { color: '#1c1e24', roughness: 0.9, metalness: 0.05, opacity: 1.0 });
  const texCfg = surface.texture;
  const hasSurfaceTexture = !!(texCfg && typeof texCfg === 'object' && texCfg.enabled !== false);
  // With a texture the cells are painted in the surface color, so the
  // diffuse color must stay white — otherwise three multiplies the map by it
  // and the cells come out darker (and the grid lines take the tint too).
  floorMat.color.set(hasSurfaceTexture ? '#ffffff' : smat.color);
  floorMat.roughness = smat.roughness;
  floorMat.metalness = smat.metalness;
  floorMat.opacity = smat.opacity;
  const floorTransparent = smat.opacity < 1;
  if (floorMat.transparent !== floorTransparent) floorMat.needsUpdate = true;
  floorMat.transparent = floorTransparent;
  floor.receiveShadow = shadowsEnabled;
  floor.position.y = surfaceZ;

  // Surface texture: an optional procedural tile repeated across the whole
  // floor. Rebuilt from scratch each build so config edits take effect.
  if (floorTexture){
    floorTexture.dispose();
    floorTexture = null;
  }
  floorMat.map = null;
  if (hasSurfaceTexture){
    floorTexture = makeSurfaceTexture(texCfg, smat.color);
    floorMat.map = floorTexture;
  }
  floorMat.needsUpdate = true; // adding/removing the map changes the shader

  /* ---------- physics: whole instrument is one rigid box ---------- */
  const phys = config.physics || {};
  const physEnabled = phys.enabled !== false;
  // The collision boundary is always the z=0 plane, regardless of where the
  // surface is drawn.
  const floorY = 0;
  physGround.position.set(0, floorY, 0);
  if (phys.friction !== undefined) physContactMat.friction = Math.max(0, num(phys.friction, 0.45));
  if (phys.restitution !== undefined) physContactMat.restitution = Math.max(0, Math.min(1, num(phys.restitution, 0.28)));
  // gravity: { x, y, z } vector, or a scalar to scale the y component
  const grav = phys.gravity;
  if (grav !== undefined){
    if (typeof grav === 'object' && grav !== null){
      physWorld.gravity.set(num(grav.x, 0), num(grav.y, -9.82), num(grav.z, 0));
    } else {
      physWorld.gravity.set(0, num(grav, -9.82), 0);
    }
  }
  if (physEnabled){
    const half = new CANNON.Vec3(width / 2, height / 2, depth / 2);
    if (!physBox){
      // Page-load drop: start a small height above rest with a slight tilt.
      physBox = new CANNON.Body({
        mass: Math.max(0.1, num(phys.mass, 2)),
        material: physBoxMat,
        linearDamping: num(phys.linearDamping, 0.01),
        angularDamping: num(phys.angularDamping, 0.08),
      });
      physBox.addShape(new CANNON.Box(half));
      const pose = physDropPose(depth, phys);
      physBox.position.set(pose.pos.x, pose.pos.y, pose.pos.z);
      physBox.quaternion.set(pose.quat.x, pose.quat.y, pose.quat.z, pose.quat.w);
      physBox.allowSleep = true;
      physBox.sleepSpeedLimit = 0.2;
      physBox.sleepTimeLimit = 0.5;
      physWorld.addBody(physBox);
      physHalf = { x: half.x, y: half.y, z: half.z };
      syncRigFromBody();
      physHasDropped = true;
      physSettled = false;
      physLastTime = performance.now();
      ensurePhysLoop();
    } else if (physHalf && physHalf.x === half.x && physHalf.y === half.y && physHalf.z === half.z){
      // Unchanged collision footprint: leave the body exactly as it is, pose,
      // velocity and sleep state included. Edits that don't resize the chassis
      // — pressing a button, recolouring a knob — must not disturb it.
      syncRigFromBody();
      scheduleRender();
    } else {
      // The chassis was actually resized, so reshape the collision box and
      // re-check the resting pose.
      while (physBox.shapes.length) physBox.removeShape(physBox.shapes[0]);
      physBox.addShape(new CANNON.Box(half));
      physBox.updateBoundingRadius();
      physBox.aabbNeedsUpdate = true;
      physHalf = { x: half.x, y: half.y, z: half.z };
      // World-space half-height of the (possibly tilted) box.
      const q = physBox.quaternion;
      const tq = new THREE.Quaternion(q.x, q.y, q.z, q.w);
      const ax = new THREE.Vector3(1, 0, 0).applyQuaternion(tq);
      const ay = new THREE.Vector3(0, 1, 0).applyQuaternion(tq);
      const az = new THREE.Vector3(0, 0, 1).applyQuaternion(tq);
      const worldHalfY = Math.abs(ax.y) * half.x + Math.abs(ay.y) * half.y + Math.abs(az.y) * half.z;
      // Where the back face actually meets the floor. Threshold on the true
      // contact plane rather than a padded value, so a body already resting at
      // its correct height is never nudged for no reason.
      const contactY = floorY + worldHalfY;
      // A slimmer chassis sits *lower* than the old body, so it can be left
      // hovering in mid-air; a deeper one can end up buried. Either way the
      // body has to be woken so gravity puts it back on the floor — a sleeping
      // body would otherwise just hang there.
      if (physBox.position.y < contactY - 1e-4){
        physBox.position.y = contactY + 0.021; // pop clear, then drop back
        physBox.velocity.setZero();
        physBox.angularVelocity.setZero();
      }
      physBox.wakeUp();
      physSettled = false;
      physLastTime = performance.now();
      ensurePhysLoop();
      scheduleRender(); // draw the rebuilt chassis even before the body moves
    }
  } else {
    if (physBox){
      physWorld.removeBody(physBox);
      physBox = null;
      physHalf = null;
    }
    physSettled = true;
    rig.position.set(0, depth / 2, 0); // back face rests on the z=0 boundary
    rig.quaternion.setFromEuler(new THREE.Euler(-Math.PI / 2, 0, 0));
  }

  // Skybox: a BackSide sphere covering everything at/above the floor plane
  // (z >= 0), painted a single flat color with a few light circles near the
  // texture's middle. Unlit, so the color stays exactly as configured. The
  // same texture is fed to scene.environment, so every PBR material in the
  // scene reflects the sky (three samples equirect env maps directly, no
  // PMREM pass needed — and none possible headless).
  if (skyMesh){
    scene.remove(skyMesh);
    skyMesh.geometry.dispose();
    skyMesh.material.map.dispose();
    skyMesh.material.dispose();
    skyMesh = null;
  }
  const skyCfg = config.skybox || {};
  if (skyCfg.enabled !== false){
    // comfortably outside the max camera zoom (24) but inside the far plane (50)
    const skyRadius = Math.max(30, Math.min(48, num(skyCfg.radius, 40)));
    const skyCount = Math.max(0, Math.floor(num(skyCfg.circles, 5)));
    const skyTex = makeSkyTexture({
      color: skyCfg.color || '#14161c',
      circleColor: skyCfg.circleColor || '#ffffff',
      circles: skyCount,
      circleSize: Math.max(1, num(skyCfg.circleSize, 16)),
      spread: Math.max(0, num(skyCfg.circleSpread, 1)),
      seed: Math.floor(num(skyCfg.randSeed, 1337))
    });
    skyMesh = new THREE.Mesh(
      new THREE.SphereGeometry(skyRadius, 32, 16),
      new THREE.MeshBasicMaterial({ map: skyTex, side: THREE.BackSide, fog: false, toneMapped: false })
    );
    skyMesh.position.set(0, surfaceZ, 0); // equator glued to the floor plane
    scene.add(skyMesh);
    scene.environment = skyTex;
    // global multiplier on the image-based light; the sky is dark by default,
    // so values above 1 are the normal way to make its contribution visible
    scene.environmentIntensity = Math.max(0, num(skyCfg.intensity, 1));
  } else {
    scene.environment = null;
  }

  // Spotlights: config.spotlight is an array of independent lights. A single
  // object is accepted for older configs; omitting it leaves the scene dark
  // apart from the skybox environment.
  const spotRaw = config.spotlight;
  const spotConfigs = Array.isArray(spotRaw) ? spotRaw : (spotRaw ? [spotRaw] : []);
  const shadowLightCount = Math.max(0, Math.floor(num(render.softboxShadowCount, 1)));
  const ext = Math.max(width, height) * 0.9 + 2.5;
  ensureSpotLights(spotConfigs.length);

  spotConfigs.forEach((spotCfg, i) => {
    const spot = spotCfg || {};
    const light = spotLights[i];
    const shadowRadius = Math.max(0, num(spot.shadowRadius ?? spot.shadowSoftness ?? spot.radius, 8));
    const shadowBias = num(spot.shadowBias, -0.00006);
    const shadowNormalBias = num(spot.shadowNormalBias, 0.0015);
    const mapSize = Math.max(128, Math.floor(num(spot.shadowMapSize ?? spot.mapSize ?? render.shadowMapSize, 1024)));

    // Config y is depth and z is height for the flat-laid instrument.
    light.position.set(num(spot.x, -3), num(spot.z, 4), num(spot.y, -3));
    light.target.position.set(0, 0, 0);
    light.color.set(spot.color || '#bdf');
    light.intensity = Math.max(0, num(spot.intensity, 0.6));
    light.castShadow = shadowsEnabled && spot.castShadow !== false && i < shadowLightCount;

    light.shadow.radius = shadowRadius;
    light.shadow.bias = shadowBias;
    light.shadow.normalBias = shadowNormalBias;
    if (light.shadow.mapSize.x !== mapSize || light.shadow.mapSize.y !== mapSize){
      light.shadow.mapSize.set(mapSize, mapSize);
      if (light.shadow.map) {
        light.shadow.map.dispose();
        light.shadow.map = null;
      }
    }

    // Fit the shadow frustum to the instrument and the light's distance.
    const dist = light.position.length();
    light.shadow.camera.near = Math.max(0.1, dist * 0.1);
    light.shadow.camera.far = dist + ext * 2;
    light.shadow.camera.left = -ext;
    light.shadow.camera.right = ext;
    light.shadow.camera.top = ext;
    light.shadow.camera.bottom = -ext;
    light.shadow.camera.updateProjectionMatrix();
  });

  if (!cameraInitialized){
    radius = Math.max(2.5, Math.max(width, height) * 1.35 + depth);
    cameraInitialized = true;
  }
  updateCamera();
}

/* ---------- resize ---------- */
function resize(){
  const w = host.clientWidth, h = host.clientHeight;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  scheduleRender();
}
new ResizeObserver(resize).observe(host);

resize();
updateCamera();

export { build, setViewMode };
