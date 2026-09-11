export const DEFAULT_CONFIG = {
  constants: { "bodyColor": "#b6a" },
  body: { 
    width: 4.2, height: 3, depth: 0.5,
    roundedness: 0.2, segments: 20,
    material: { color: "bodyColor", roughness: 0.5, metalness: 0.3 }
  },
  knobs: [
    { label: "BOUNCE", x: -1.6, y: 0.8, radius: 0.15, height: 0.2, 
        material: { color: "#e9a", roughness: 0.4, metalness: 0.3 }, value: 0.62, topRounding: 0.2,
        markColor: "#645", markLength: 0.7, markWidth: 0.25,
        labelColor: "#fff", labelSize: 0.15
    },
    { label: "SHAKE",   x: -0.8, value: 0.28 },
    { label: "HAPPY", value: 0.1 },
    { label: "SAD",  value: 0.55 },
    { label: "BIG",  y:0.97, value: 0.5, radius: 0.3, 
      "topRounding": 0.3,
      "skirtFactor": 0.5,
      "skirtRounding": 0.98,
      "markLength": 0.36,
      "markWidth": 0.14,
      material: { roughness: 0.4, metalness: 0.1 }
     }
  ],
  buttons: [
    { label: "PLAY",  labelColor: "#b39", labelSize: 0.15, x: -1.2, y: -0.95, width: 0.34, length: 0.28, height: 0.1, material: { color: "#ecd", roughness: 0.4, metalness: 0.25 }, pressed: false },
    { label: "STOP",  x: -0.4 },
    { label: "REC",   pressed: false },
    { label: "FUN!", pressed: false }
  ],
  sliders: [
    { label: "EXCITE", labelColor: "#fff", labelSize: 0.15, x: -1.7, y: -0.05, length: 0.7, orientation: 0, value: 0.75, material: { color: "#e9a" }, capWidth: 0.14, capLength: 0.2, capHeight: 0.12, capRoundedness: 0.8 },
    { label: "ENHANCE", x: -1.2, value: 0.35 },
    { label: "ENGAGE", x: 1.2, value: 0.6 },
    { label: "ENTRANCE",  x: 1.7, value: 0.9 }
  ],
  labels: [
    {
      text: "BarbieSynth", font: "Brush Script MT", fontsize: 55, fontweight: "500", align: "left",
      x: -2.1, y: 1.25,
      material: { color: "#ffc", roughness: 0.4, metalness: 0.35 }
    }
  ],
  lcd: {
    width: 1.6,
    height: 0.8,
    x: 0,
    y: -0.05,
    bezel: 0.05,
     text: "you are\nbeautiful",
    textColor: "#fab",
    backgroundColor: "#024",
    material: { roughness: 0.1, metalness: 0.0 }
  },
  surface: {
    material: { color: "#3cb" },
    grid: { size: 10, color: "#fff", linewidth: 1.5 }
  },
  spotlight: {
    x: -3,
    y: -3,
    z: 4,
    color: "#fff",
    intensity: 3.5,
    shadowRadius: 1
  },
  fillLight: {
    color: "#ffd",
    intensity: 0.8
  },
  fillLight2: {
    color: "#fff",
    intensity: 0.8
  },
  ambientLight: {
    color: "#fff",
    intensity: 0.1
  },
  render: {
    pixelRatio: 2.0,
    shadows: true,
    shadowMapSize: 2048,
    softboxShadowCount: 2,
    detailShadows: true,
    continuous: false,
    showFps: true,
    wireframe: false
  },
  physics: {
    dropHeight: 5.0,
    tilt: { x: 0.3, y: 0.25, z: 0.02 },
    restitution: 0.28,
    friction: 0.15
  }
};
