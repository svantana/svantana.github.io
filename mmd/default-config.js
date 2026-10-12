export const DEFAULT_CONFIGS = [
  {
    name: "BarbieSynth",
    config: {
      constants: { "bodyColor": "#b6a" },
      body: {
        width: 4.2, height: 3, depth: 0.5,
        corner: 0.2, bevel: 0.2, cornerSegments: 20, bevelSegments: 20,
        material: { color: "bodyColor", roughness: 0.5, metalness: 0.3, opacity:1.0 }
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
        { label: "PLAY",  labelColor: "#b39", labelSize: 0.15, x: -1.2, y: -0.95, width: 0.34, length: 0.28, height: 0.1, material: { color: "#ecd", roughness: 0.4, metalness: 0.25 } },
        { label: "STOP",  x: -0.4 },
        { label: "REC" },
        { label: "FUN!" }
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
        x: 0, y: -0.05, width: 1.6, height: 0.8, bezel: 0.05,
        bezelColor: "#0d0d10", roundedness: 0.08,
        text: "you are\nbeautiful", textColor: "#fab",
        backgroundColor: "#024", material: { roughness: 0.1, metalness: 0.0 }
      },
      surface: {
        z: 0,
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
        dropHeight: 0.0,
        tilt: { x: 0.0, y: 0.0, z: 0.0 },
        restitution: 0.28,
        friction: 0.15,
        gravity: { x: 0, y: -9.82, z: 0 }
      }
    }
  },
  {
    name: "Darkwave",
    config: {
      constants: { "bodyColor": "#111" },
      body: {
        width: 4.0, height: 2, depth: 0.6, corner: 0.2, bevel: 0.2, cornerSegments: 1, bevelSegments: 1,
        material: { color: "bodyColor", roughness: 0.5, metalness: 0.3, opacity:1.0 }
      },
      knobs: [
        { label: "BLACK", x: -1.4, y: 0.1, radius: 0.2, height: 0.2, segments: 6, smoothShading: false,
            material: { color: "#ff0", roughness: 0.4, metalness: 0.3 }, value: 0.62, topRounding: 0.2,
            markColor: "#f00", markLength: 0.7, markWidth: 0.25,
            labelColor: "#ff0", labelSize: 0.16
        },
        { label: "PAIN",   x: -0.6, value: 0.28 },
        { label: "HARD", value: 0.1 },
        { label: "SICK", value: 0.5 }
      ],
      labels: [
        {
          text: "DARKWAVE", font: "Times New Roman", fontsize: 40, fontweight: "500", align: "left",
          x: -1.7, y: 0.6,
          material: { color: "#ff0", roughness: 0.4, metalness: 0.35 }
        }
      ],
      surface: {
        z: -0.001,
        material: { color: "#f00" }
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
        dropHeight: 0.0,
        tilt: { x: 0.0, y: 0.0, z: 0.0 },
        restitution: 0.28,
        friction: 0.15,
        gravity: { x: 0, y: -9.82, z: 0 }
      }
    }
  },
  {
    name: "Gainboy",
    config: {
      constants: { "bodyColor": "#fff" },
      body: {
        width: 2.5, height: 4.0, depth: 0.6, corner: 0.2, bevel: 0.3, cornerSegments: 10, bevelSegments: 10,
        material: {color: "bodyColor", roughness: 0.4, metalness: 0.0, opacity:1.0}
      },
      buttons: [
        { x: -0.85, y: -0.65, width: 0.22, length: 0.22, height: 0.05, material: { color: "#222", roughness: 0.4, metalness: 0.25 }},
        { x: -0.60, y: -0.90},
        { x: -0.60, y: -0.40},
        { x: -0.35, y: -0.65},
        { label: "A",  labelColor: "#11b", labelSize: 0.2, labelPosition: "below", x: 0.5, y: -0.75, width: 0.25, length: 0.25, height: 0.05, roundedness: 1.0, material: { color: "#f15", roughness: 0.4, metalness: 0.25 }},
        { label: "B",  x: 0.9, y: -0.55},
        { label: "SELECT", labelSize: 0.15, x: -0.25, y: -1.5, width: 0.3, length: 0.1,  material: { color: "#999", roughness: 0.4, metalness: 0.0 }},
        { label: "START",  x: 0.25, y: -1.5}
      ],
      labels: [
        {
          text: "Intendo GAINBOY", font: "Arial", fontsize: 16, fontweight: "600", align: "left",
          x: -0.9, y: -0.0,
          material: { color: "#11b", roughness: 0.4, metalness: 0.35 }
        }
      ],
      lcd: {
        x: 0.0, y: 0.9, width: 1.8, height: 1.6, bezel: 0.1,
        bezelColor: "#667", roundedness: 0.1,
        text: "\nlet's\nplay\n", textColor: "#252",
        backgroundColor: "#8d7", material: { roughness: 0.01, metalness: 0.0 }
      },

      surface: {z: -0.001, material: { color: "#5af" }},
      spotlight: {x: 0, y: -4, z: 4, color: "#fff", intensity: 4.5, shadowRadius: 2},
      fillLight: { color: "#ffd", intensity: 0.9},
      fillLight2: { color: "#fff", intensity: 0.8},
      ambientLight: {color: "#fff", intensity: 0.5},
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
        dropHeight: 0.0,
        tilt: { x: 0.0, y: 0.0, z: 0.0 },
        restitution: 0.28,
        friction: 0.15,
        gravity: { x: 0, y: -9.82, z: 0 }
      }
    }
  }
];
