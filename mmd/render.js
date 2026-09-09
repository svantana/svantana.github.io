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
let physSettled = true; // true when body is sleeping / at rest
let physHasDropped = false; // only auto-drop from height on first (page-load) build
let physLastTime = 0;

function physDropPose(depth, phys){
  const dropHeight = Math.max(0.0, num(phys?.dropHeight, 1.0));
  const tilt = phys?.tilt || {};
  const tx = num(tilt.x, 0.10);
  const ty = num(tilt.y, 0.06);
  const tz = num(tilt.z, 0.12);
  const restY = 0.01; // floor sits at -depth/2 - 0.02, box half-thickness is depth/2
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

// lights
const ambient = new THREE.AmbientLight(0xffffff, 0.55);
scene.add(ambient);

const NUM_SOFTBOX_LIGHTS = 1;
const softboxLights = [];
const softboxGroup = new THREE.Group();
scene.add(softboxGroup);

for (let i = 0; i < NUM_SOFTBOX_LIGHTS; i++){
  const light = new THREE.DirectionalLight(0xfff2e0, 0.0);
  light.castShadow = i === 0;
  light.shadow.mapSize.set(2048, 2048);
  light.shadow.camera.near = 1.0;
  light.shadow.camera.far = 10.0;
  light.shadow.bias = 0.001;
  light.shadow.normalBias = 0.01;
  light.shadow.radius = 6;
  //light.shadow.blurSamples = 16; // VSMShadowMap only
  softboxGroup.add(light);
  softboxGroup.add(light.target);
  softboxLights.push(light);
}

const rim = new THREE.DirectionalLight(0x8fb8ff, 0.4); // settable via config.fillLight
rim.position.set(-5, 2, -4);
scene.add(rim);
const fill = new THREE.DirectionalLight(0xffffff, 0.25);
fill.position.set(-2, -3, 4);
scene.add(fill);

// floor surface (color settable via config.surface.color)
const floorGeo = new THREE.PlaneGeometry(60, 60);
const floorMat = new THREE.MeshStandardMaterial({ color: '#1c1e24', roughness: 0.9, metalness: 0.05 });
const floor = new THREE.Mesh(floorGeo, floorMat);
floor.receiveShadow = true;
floor.rotation.x = -Math.PI / 2;
scene.add(floor);

let gridHelper = null;

const rig = new THREE.Group();
// lay the instrument flat: the panel face (where knobs/LCD sit) now points up (+Y)
rig.rotation.x = -Math.PI / 2;
scene.add(rig);

/* ---------- manual orbit controls ---------- */
let azimuth = 0, elevation = 1.0 / 3, radius = 8; // straight front, 60° elevation
let cameraInitialized = false;
let dragging = false, lastX = 0, lastY = 0;

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
dom.addEventListener('mousedown', e => { dragging = true; lastX = e.clientX; lastY = e.clientY; dom.style.cursor = 'grabbing'; });
window.addEventListener('mouseup', () => { dragging = false; dom.style.cursor = 'grab'; });
window.addEventListener('mousemove', e => {
  if (!dragging) return;
  const dx = e.clientX - lastX, dy = e.clientY - lastY;
  lastX = e.clientX; lastY = e.clientY;
  azimuth -= dx * 0.002;
  elevation = Math.max(-0.4999, Math.min(0.4999, elevation + dy * 0.002));
  updateCamera();
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
  r = Math.min(r, w/2, h/2);
  const shape = new THREE.Shape();
  const x = -w/2, y = -h/2;
  shape.moveTo(x, y + r);
  shape.lineTo(x, y + h - r);
  shape.quadraticCurveTo(x, y + h, x + r, y + h);
  shape.lineTo(x + w - r, y + h);
  shape.quadraticCurveTo(x + w, y + h, x + w, y + h - r);
  shape.lineTo(x + w, y + r);
  shape.quadraticCurveTo(x + w, y, x + w - r, y);
  shape.lineTo(x + r, y);
  shape.quadraticCurveTo(x, y, x, y + r);
  return shape;
}

function makeBody(width, height, depth, roundedness, color, roughness, metalness, segments, bevelSegments, curveSegments){
  const bevel = Math.max(0.001, Math.min(roundedness, Math.min(width, height) * 0.45, depth * 0.45));
  const cornerR = Math.max(0.001, Math.min(bevel, Math.min(width, height) * 0.4));
  const shape2 = roundedRectShape(width, height, cornerR);
  const baseSegs = num(segments, 8);
  const bSegs = Math.max(1, Math.min(64, num(bevelSegments, baseSegs)));
  const cSegs = Math.max(1, Math.min(64, num(curveSegments, Math.max(bSegs * 2, 8))));

  const extrudeSettings = {
    steps: 1,
    depth: Math.max(0.02, depth - bevel * 2),
    bevelEnabled: true,
    bevelThickness: bevel,
    bevelSize: bevel * 0.85,
    bevelSegments: bSegs,
    curveSegments: cSegs
  };
  let geo = new THREE.ExtrudeGeometry(shape2, extrudeSettings);
  // Add interior support vertices to every flat-face triangle so those vertices
  // get purely perpendicular normals from computeVertexNormals. The returned
  // geometry has only a position attribute, so mergeVertices welds by position
  // only — no UV or pre-baked normal conflicts.
  geo = mergeVertices(geo, 1e-4);
  geo.computeVertexNormals();
  geo.center();
  const mat = new THREE.MeshStandardMaterial({ color, roughness, metalness });
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
  const color = k.color || '#cfd3d8';
  const roughness = Math.max(0, Math.min(1, num(k.roughness, 0.4)));
  const metalness = Math.max(0, Math.min(1, num(k.metalness, 0.25)));
  const value = Math.max(0, Math.min(1, num(k.value, 0.5))); // knob setting, 0–1
  const segments = Math.max(3, Math.min(64, num(k.segments, 32)));
  const topRounding = Math.max(0, Math.min(1, num(k.topRounding, 0)));
  const skirtFactor = Math.max(0, Math.min(1, num(k.skirtFactor, 0)));
  const skirtRounding = Math.max(0, Math.min(1, num(k.skirtRounding, 0)));

  // cap + indicator turn together to reflect the knob's value
  const capGroup = new THREE.Group();

  const bodyMat = new THREE.MeshStandardMaterial({ color, roughness, metalness });

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
  capGroup.add(knobMesh);

  // indicator ridge — markLength/markWidth are proportional to the knob radius
  const markColor = k.markColor || '#15161a';
  const markLength = Math.max(0.01, num(k.markLength, 0.55));
  const markWidth = Math.max(0.01, num(k.markWidth, 0.14));
  const markDepth = kHeight * 1.1;
  const ridgeGeo = new THREE.BoxGeometry(radius * markWidth, radius * markLength, markDepth);
  const ridgeMat = new THREE.MeshStandardMaterial({ color: markColor, metalness: 0.2, roughness: 0.6 });
  const ridge = new THREE.Mesh(ridgeGeo, ridgeMat);
  ridge.position.set(0, radius * markLength, depth/2 + kHeight * 0.02 + markDepth * 0.5);
  ridge.castShadow = true;
  ridge.receiveShadow = true;
  capGroup.add(ridge);

  const sweepDeg = 270; // full-sweep pot travel, value 0 = full ccw, 1 = full cw
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
    const labelMat = new THREE.MeshStandardMaterial({map: tex, transparent: true,
    roughness: 0.5, metalness: 0.0,
    polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4});
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
  const bez = num(lcd.bezel, 0.1*w);
  const bg = lcd.backgroundColor || '#08130b';
  const fg = lcd.textColor || '#7CFC98';
  const roughness = Math.max(0, Math.min(1, num(lcd.roughness, 0.6)));
  const metalness = Math.max(0, Math.min(1, num(lcd.metalness, 0.1)));

  const bezelGeo = new THREE.PlaneGeometry(w, h);
  const bezelMat = new THREE.MeshStandardMaterial({ color: '#0d0d10', roughness, metalness });
  const bezel = new THREE.Mesh(bezelGeo, bezelMat);
  bezel.position.z = depth/2 + 0.003;
  bezel.castShadow = true;
  bezel.receiveShadow = true;
  g.add(bezel);

  const tex = textTexture(lcd.text || '', {
    width: 512, height: 256, bg: bg, fg: fg,
    font: '600 46px ui-monospace, monospace'
  });
  const screenGeo = new THREE.PlaneGeometry(w-2*bez, h-2*bez);
  const screenMat = new THREE.MeshStandardMaterial({ color: '#fff', map: tex, roughness, metalness, emissiveMap: tex, emissive: "#fff", emissiveIntensity: 0.5 });
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
  const travel = Math.max(0.01, num(btn.travel, 0.12));
  const pressed = !!btn.pressed;
  const btnHeight = Math.max(0.01, num(btn.height, travel));
  const actualHeight = pressed ? Math.max(0.01, btnHeight * 0.35) : btnHeight;
  const color = btn.color || '#3a3d44';
  const roughness = Math.max(0, Math.min(1, num(btn.roughness, 0.4)));
  const metalness = Math.max(0, Math.min(1, num(btn.metalness, 0.25)));
  const roundedness = Math.max(0, Math.min(1, num(btn.roundedness, 0.4)));

  // recessed bezel (the "hole" the cap sits in) — rounded to match the cap
  const bezelR = Math.max(0.001, Math.min(w * 1.2, len * 1.2) * 0.5 * roundedness);
  const bezelShape = roundedRectShape(w * 1.2, len * 1.2, bezelR);
  const bezelGeo = new THREE.ExtrudeGeometry(bezelShape, { depth: 0.02, bevelEnabled: false, curveSegments: 10 });
  const bezelMat = new THREE.MeshStandardMaterial({ color: '#111216', roughness: 0.75, metalness: 0.1 });
  const bezel = new THREE.Mesh(bezelGeo, bezelMat);
  bezel.position.z = depth/2 - 0.016;
  bezel.receiveShadow = true;
  g.add(bezel);

  // rounded rectangular cap — corner radius driven by the roundedness parameter
  const capRadius = Math.max(0.001, Math.min(w, len) * 0.5 * roundedness);
  // Cap the bevel so a high roundedness can't balloon the base into a rectangular
  // lip that dips below the recess and pokes out at the cap's base.
  const bevelThickness = Math.min(capRadius * 0.35, 0.022);
  const bevelSize = Math.min(capRadius * 0.3, Math.min(w, len) * 0.06);
  const shape = roundedRectShape(w, len, capRadius);
  const capGeo = new THREE.ExtrudeGeometry(shape, {
    depth: actualHeight, bevelEnabled: true,
    bevelThickness, bevelSize,
    bevelSegments: 3, curveSegments: 8
  });
  capGeo.center();
  const capMat = new THREE.MeshStandardMaterial({ color, roughness, metalness });
  const capMesh = new THREE.Mesh(capGeo, capMat);
  capMesh.position.z = depth/2 + actualHeight/2 + 0.008;
  capMesh.castShadow = true;
  capMesh.receiveShadow = true;
  g.add(capMesh);

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
      const plateMat = new THREE.MeshStandardMaterial({
        map: tex, transparent: true,
        polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4
      });
      const plate = new THREE.Mesh(plateGeo, plateMat);
      const topZ = depth/2 + actualHeight + 0.008 + capRadius * 0.35;
      plate.position.set(0, 0, topZ + 0.015);
      g.add(plate);
    } else {
      // printed name plate above or below the button
      const tex = textTexture(upperLabel, {
        width: 220, height: 72, fg: labelColor,
        font: `600 ${Math.round(30 * labelSize)}px ui-monospace, monospace`
      });
      const labelGeo = new THREE.PlaneGeometry(Math.max(w, 0.5) * 1.4 * labelSize, Math.max(w, 0.5) * 0.45 * labelSize);
      const labelMat = new THREE.MeshBasicMaterial({
        map: tex, transparent: true,
        polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4
      });
      const labelMesh = new THREE.Mesh(labelGeo, labelMat);
      labelMesh.castShadow = false;
      const sign = labelPosition === 'above' ? 1 : -1;
      const yOff = sign * (len/2 + Math.max(w, 0.5) * 0.32);
      labelMesh.position.set(0, yOff, depth/2 + 0.02);
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
  const capColor = s.color || '#202228';
  const indicatorColor = s.indicatorColor || '#ffffff';
  const roughness = Math.max(0, Math.min(1, num(s.roughness, 0.4)));
  const metalness = Math.max(0, Math.min(1, num(s.metalness, 0.2)));

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
  const trackMat = new THREE.MeshStandardMaterial({
    map: trackTex,
    transparent: true,
    depthWrite: false,
    roughness: 0.85,
    metalness: 0.1,
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
  const capMat = new THREE.MeshStandardMaterial({
    color: capColor,
    roughness,
    metalness
  });
  const capMesh = new THREE.Mesh(capGeo, capMat);
  capMesh.castShadow = true;
  capMesh.receiveShadow = true;
  capGroup.add(capMesh);

  // Position cap along track seated flush against the chassis
  const capTravel = trackLen - capLength * 0.3;
  const capY = (value - 0.5) * capTravel;
  capGroup.position.set(0, capY, depth / 2 + capHeight / 2 - 0.01);
  g.add(capGroup);

  // Label plate (optional)
  if (s.label){
    const labelSize = num(s.labelSize, 1);
    const labelColor = s.labelColor || '#c9ccd2';
    const labelPosition = String(s.labelPosition || 'below').toLowerCase();
    const tex = textTexture(String(s.label).toUpperCase(), {
      width: 220,
      height: 72,
      fg: labelColor,
      font: `600 ${Math.round(30 * labelSize)}px ui-monospace, monospace`
    });
    const labelGeo = new THREE.PlaneGeometry(capWidth * 3.2 * labelSize, capWidth * 1.05 * labelSize);
    const labelMat = new THREE.MeshStandardMaterial({
      map: tex,
      transparent: true,
      polygonOffset: true,
      polygonOffsetFactor: -4,
      polygonOffsetUnits: -4
    });
    const labelMesh = new THREE.Mesh(labelGeo, labelMat);
    labelMesh.castShadow = false;
    const sign = labelPosition === 'above' ? 1 : -1;
    const yOff = sign * (plateLength / 2 + 0.11 * labelSize);
    labelMesh.position.set(0, yOff, depth / 2 + 0.006);
    g.add(labelMesh);
  }

  // Rotation according to orientation in degrees (0 = vertical, 90 = horizontal)
  g.rotation.z = -THREE.MathUtils.degToRad(orientationDeg);
  g.position.set(num(s.x, 0), num(s.y, 0), 0);
  return g;
}

function makeLabel(lbl, depth){
  const g = new THREE.Group();
  if (!lbl || lbl.text === undefined || lbl.text === null) return g;

  const textStr = String(lbl.text);
  const lines = textStr.split('\n');

  // Extract font, fontsize, fontweight, color, material properties
  const fontFam = lbl.font || 'ui-monospace, monospace';
  const fontWeight = lbl.fontweight ?? lbl.fontWeight ?? '600';
  const color = lbl.color || lbl.textColor || '#ffffff';
  const roughness = Math.max(0, Math.min(1, num(lbl.roughness, 0.4)));
  const metalness = Math.max(0, Math.min(1, num(lbl.metalness, 0.25)));

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
  ctx.fillStyle = color;
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
  const labelMat = new THREE.MeshStandardMaterial({
    map: tex,
    transparent: true,
    roughness,
    metalness,
    polygonOffset: true,
    polygonOffsetFactor: -4,
    polygonOffsetUnits: -4
  });
  const labelMesh = new THREE.Mesh(labelGeo, labelMat);
  labelMesh.castShadow = false;
  labelMesh.position.set(alignX, 0, depth / 2 + 0.008 + posZ);
  g.add(labelMesh);

  g.position.set(posX, posY, 0);
  return g;
}

function makeGridPlane(size, color, lineWidth, baseColor){
  size = Math.max(0.1, size);
  const divisions = Math.max(2, Math.round(size));
  const res = 1024;
  const canvas = document.createElement('canvas');
  canvas.width = res;
  canvas.height = res;
  const ctx = canvas.getContext('2d');

  const lw = Math.max(1, lineWidth * (size / 10));
  ctx.fillStyle = baseColor || '#1c1e24';
  ctx.fillRect(0, 0, res, res);

  ctx.strokeStyle = color;
  ctx.lineWidth = lw;

  for (let i = 0; i <= divisions; i++){
    const p = (i / divisions) * res;
    ctx.beginPath();
    ctx.moveTo(p, 0);
    ctx.lineTo(p, res);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(0, p);
    ctx.lineTo(res, p);
    ctx.stroke();
  }

  const tex = new THREE.CanvasTexture(canvas);
  tex.needsUpdate = true;
  const geo = new THREE.PlaneGeometry(size, size);
  const mat = new THREE.MeshStandardMaterial({
    map: tex,
    roughness: 0.9,
    metalness: 0.05,
    side: THREE.DoubleSide
  });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.rotation.x = -Math.PI / 2;
  mesh.receiveShadow = true;
  return mesh;
}

function num(v, fallback){
  return (typeof v === 'number' && isFinite(v)) ? v : fallback;
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

function build(config){
  while (rig.children.length) rig.remove(rig.children[0]);

  const render = config.render || {};
  const pixelRatio = Math.max(0.5, Math.min(2, num(render.pixelRatio, 1.5)));
  const shadowsEnabled = render.shadows !== false;
  const shadowLightCount = Math.max(0, Math.min(NUM_SOFTBOX_LIGHTS, Math.floor(num(render.softboxShadowCount, 1))));
  const detailShadows = render.detailShadows === true;
  const continuousRendering = render.continuous === true;
  const wireframe = render.wireframe === true;
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, pixelRatio));
  renderer.shadowMap.enabled = shadowsEnabled && !wireframe;
  fps.style.display = render.showFps === false ? 'none' : '';
  setRenderMode(continuousRendering);

  const b = config.body || {};
  const width = Math.max(0.3, num(b.width, 6));
  const height = Math.max(0.3, num(b.height, 2));
  const depth = Math.max(0.1, num(b.depth, 1.2));
  const roundedness = Math.max(0, Math.min(0.5, num(b.roundedness, 0.15)));
  const segments = num(b.segments ?? b.roundingSegments ?? b.bevelSegments, 16);
  const bevelSegments = b.bevelSegments !== undefined ? num(b.bevelSegments, segments) : undefined;
  const curveSegments = b.curveSegments !== undefined ? num(b.curveSegments, segments * 2) : undefined;
  const color = b.color || '#2a2c31';
  const bodyRoughness = Math.max(0, Math.min(1, num(b.roughness, 0.55)));
  const bodyMetalness = Math.max(0, Math.min(1, num(b.metalness, 0.18)));

  const bodyMesh = makeBody(width, height, depth, roundedness, color, bodyRoughness, bodyMetalness, segments, bevelSegments, curveSegments);
  rig.add(bodyMesh);

  const knobs = resolveArray(config.knobs);
  knobs.forEach(k => rig.add(makeKnob(k, depth)));

  const buttons = resolveArray(config.buttons);
  buttons.forEach(btn => rig.add(makeButton(btn, depth)));

  const sliders = resolveArray(config.sliders);
  sliders.forEach(s => rig.add(makeSlider(s, depth)));

  const labels = resolveArray(config.labels);
  labels.forEach(lbl => rig.add(makeLabel(lbl, depth)));

  if (config.lcd && typeof config.lcd === 'object'){
    rig.add(makeLCD(config.lcd, depth));
  }

  if (wireframe){
    rig.traverse(object => {
      if (object.isMesh && object.material){
        if (Array.isArray(object.material)){
          object.material.forEach(m => { m.wireframe = true; });
        } else {
          object.material.wireframe = true;
        }
      }
    });
  }

  // The chassis casts the important broad shadow. Small controls can opt in
  // when their individual shadows are worth the additional shadow-map work.
  rig.traverse(object => {
    if (object.isMesh && object.castShadow !== false) object.castShadow = detailShadows || object === bodyMesh;
  });

  const surface = config.surface || {};
  const surfaceColor = surface.color || '#1c1e24';
  floorMat.color.set(surfaceColor);
  floor.receiveShadow = shadowsEnabled;
  floor.position.y = -depth / 2 - 0.02;

  /* ---------- physics: whole instrument is one rigid box ---------- */
  const phys = config.physics || {};
  const physEnabled = phys.enabled !== false;
  const floorY = -depth / 2 - 0.02;
  physGround.position.set(0, floorY, 0);
  if (phys.friction !== undefined) physContactMat.friction = Math.max(0, num(phys.friction, 0.45));
  if (phys.restitution !== undefined) physContactMat.restitution = Math.max(0, Math.min(1, num(phys.restitution, 0.28)));
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
      syncRigFromBody();
      physHasDropped = true;
      physSettled = false;
      physLastTime = performance.now();
      ensurePhysLoop();
    } else {
      // Later edits: keep the current pose, just resize the collision box.
      while (physBox.shapes.length) physBox.removeShape(physBox.shapes[0]);
      physBox.addShape(new CANNON.Box(half));
      physBox.updateBoundingRadius();
      physBox.aabbNeedsUpdate = true;
      // World-space half-height of the (possibly tilted) box.
      const q = physBox.quaternion;
      const tq = new THREE.Quaternion(q.x, q.y, q.z, q.w);
      const ax = new THREE.Vector3(1, 0, 0).applyQuaternion(tq);
      const ay = new THREE.Vector3(0, 1, 0).applyQuaternion(tq);
      const az = new THREE.Vector3(0, 0, 1).applyQuaternion(tq);
      const worldHalfY = Math.abs(ax.y) * half.x + Math.abs(ay.y) * half.y + Math.abs(az.y) * half.z;
      const restingY = floorY + worldHalfY + 0.001;
      if (physBox.position.y < restingY){
        physBox.position.y = restingY + 0.02;
        physBox.velocity.setZero();
        physBox.angularVelocity.setZero();
        physBox.wakeUp();
        physSettled = false;
        physLastTime = performance.now();
        ensurePhysLoop();
      } else {
        syncRigFromBody();
        scheduleRender();
      }
    }
  } else {
    if (physBox){
      physWorld.removeBody(physBox);
      physBox = null;
    }
    physSettled = true;
    rig.position.set(0, -0.02, 0);
    rig.quaternion.setFromEuler(new THREE.Euler(-Math.PI / 2, 0, 0));
  }

  if (gridHelper){
    scene.remove(gridHelper);
    gridHelper = null;
  }
  const gridCfg = surface.grid;
  if (gridCfg && typeof gridCfg === 'object'){
    const gSize = Math.max(0.1, num(gridCfg.size, 10));
    const gColor = gridCfg.color || '#ffffff';
    const gLinewidth = Math.max(0.5, num(gridCfg.linewidth, 1.5));
    gridHelper = makeGridPlane(gSize, gColor, gLinewidth, surfaceColor);
    gridHelper.position.set(0, -depth / 2 - 0.019, 0);
    scene.add(gridHelper);
  }

  const spotlight = config.spotlight || {};
  const lx = num(spotlight.x, -3);
  const ly = num(spotlight.y, -3);
  const lz = num(spotlight.z, 4);
  const lightColor = spotlight.color || '#bdf';
  const totalIntensity = num(spotlight.intensity, 0.6);
  const shadowRadius = Math.max(0, num(spotlight.shadowRadius ?? spotlight.shadowSoftness ?? spotlight.radius, 8));
  const shadowBias = num(spotlight.shadowBias, -0.00006);
  const shadowNormalBias = num(spotlight.shadowNormalBias, 0.0015);
  const mapSize = Math.max(128, Math.floor(num(render.shadowMapSize ?? spotlight.shadowMapSize ?? spotlight.mapSize, 1024)));

  // Softbox spread: larger shadowRadius -> wider physical aperture -> softer penumbra & diffuse shadows
  const spread = shadowRadius * 0.07;

  const dir = new THREE.Vector3(lx, lz, ly).normalize();
  const up = Math.abs(dir.y) < 0.9 ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(1, 0, 0);
  const right = new THREE.Vector3().crossVectors(dir, up).normalize();
  const orthoUp = new THREE.Vector3().crossVectors(right, dir).normalize();

  const offsets = [
    { x: 0, y: 0, weight: 0.44 },
    { x: spread, y: 0, weight: 0.14 },
    { x: -spread, y: 0, weight: 0.14 },
    { x: 0, y: spread, weight: 0.14 },
    { x: 0, y: -spread, weight: 0.14 }
  ];

  const ext = Math.max(width, height) * 0.9 + 2.5;

  softboxLights.forEach((sl, i) => {
    const off = offsets[i];
    const pos = new THREE.Vector3(lx, lz, ly)
      .addScaledVector(right, off.x)
      .addScaledVector(orthoUp, off.y);

    sl.position.copy(pos);
    sl.target.position.set(0, 0, 0);
    sl.color.set(lightColor);
    sl.intensity = totalIntensity * off.weight;
    sl.castShadow = shadowsEnabled && i < shadowLightCount;

    sl.shadow.bias = shadowBias;
    sl.shadow.normalBias = shadowNormalBias;

    if (sl.shadow.mapSize.x !== mapSize || sl.shadow.mapSize.y !== mapSize){
      sl.shadow.mapSize.set(mapSize, mapSize);
      if (sl.shadow.map) {
        sl.shadow.map.dispose();
        sl.shadow.map = null;
      }
    }

    sl.shadow.camera.left = -ext;
    sl.shadow.camera.right = ext;
    sl.shadow.camera.top = ext;
    sl.shadow.camera.bottom = -ext;
    sl.shadow.camera.updateProjectionMatrix();
  });

  const fillLight = config.fillLight || {};
  rim.color.set(fillLight.color || '#8bf');
  rim.intensity = Math.max(0, num(fillLight.intensity, 0.4));

  const ambConfig = config.ambientLight || {};
  ambient.color.set(ambConfig.color || spotlight.ambientColor || '#ffffff');
  ambient.intensity = Math.max(0, num(ambConfig.intensity ?? spotlight.ambient ?? spotlight.ambientIntensity, 0.55));

  const fill2 = config.fillLight2 || {};
  fill.color.set(fill2.color || '#ffffff');
  fill.intensity = Math.max(0, num(fill2.intensity, 0.25));

  const noAuxLight = !num(fillLight.intensity, -1) && !num(fill2.intensity, -1) && !num(ambConfig.intensity, -1);
  if (noAuxLight) {
    softboxLights.forEach((sl, i) => {
      if (i === 0) {
        sl.intensity = totalIntensity;
      } else {
        sl.intensity = 0;
      }
      sl.castShadow = shadowsEnabled && i === 0;
    });
  }

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

export { build };
