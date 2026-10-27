import { run as faustRun, stop as faustStop, getAnalyser, getNode } from './faust.js';
import { buildGui } from './gui.js';
import { startScope } from './scope.js';
import { initKeyboard } from './keyboard.js';

const $ = s => document.querySelector(s);
const DEFAULT = `import("stdfaust.lib");

// Polyphony the standard Faust way: declare [nvoices:n] and use the standard
// freq / gain / gate parameters, which key-on / key-off events set per voice.
// https://faustdoc.grame.fr/manual/midi/#midi-polyphony-support
declare options "[midi:on][nvoices:8]";

freq   = hslider("freq", 440, 20, 5000, 0.01);
gain   = hslider("gain", 0.5, 0, 1, 0.01);
gate   = button("gate");

cutoff = hslider("[1]cutoff", 3000, 100, 10000, 1) : si.polySmooth(gate, 0.999, 1);
atk    = hslider("[2]attack", 0.01, 0.001, 1, 0.001);
rel    = hslider("[3]release", 0.3, 0.01, 2, 0.01);

env = en.adsr(atk, 0.2, 0.6, rel, gate);

// one voice (scaled down: active voices are summed without scaling)
process = os.sawtooth(freq) * gain * env : fi.lowpass(2, cutoff) : /(4);

// one effect instance shared by all voices
effect = dm.zita_light;
`;
$("#code").value = DEFAULT;

const status = (t, e) => {
  const s = $("#status");
  s.textContent = t;
  s.className = e ? "err" : "";
};

let mp = {};
let poly = false;

const kbApi = initKeyboard({
  kbEl: $("#kb"),
  octEl: $("#oct"),
  getNode: getNode,
  getPoly: () => poly,
  getMp: () => mp,
  setPoly: v => { poly = v; },
  setMp: v => { mp = v || {}; },
  vel: 100
});

function stopAll() {
  faustStop({
    clearHeld: () => {},
    clearPointers: () => {},
    clearStack: () => {},
    clearNoteEls: () => {}
  });
  kbApi.clearAll();
}

async function run() {
  $("#err").style.display = "none";
  try {
    const code = $("#code").value;
    await faustRun(code, status, {
      clearHeld: () => {},
      clearPointers: () => {},
      clearStack: () => {},
      clearNoteEls: () => {}
    }, (json, nv) => {
      mp = {};
      poly = nv > 0;
      if (nv > 0) {
        mp = {};
      } else {
        // will be filled by kbApi via setFromJson logic not needed; we handle
        mp = {};
      }
      buildGui(json, getNode(), $("#gui"));
      kbApi.setFromJson(json, nv);
    });
  } catch (e) {
    const msg = String(e?.message || e);
    $("#err").textContent = msg;
    $("#err").style.display = "block";
    status("Error", true);
  }
}

$("#run").onclick = run;
$("#stop").onclick = () => {
  stopAll();
  status("Stopped");
};

$("#code").addEventListener("keydown", e => {
  if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
    e.preventDefault();
    run();
  } else if (e.key === "Escape") {
    e.target.blur();
  } else if (e.key === "Tab") {
    e.preventDefault();
    document.execCommand("insertText", false, "  ");
  }
});

startScope($("#scope"), getAnalyser, getNode);
