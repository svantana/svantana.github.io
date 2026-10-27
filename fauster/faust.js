const CDN = "https://cdn.jsdelivr.net/npm/@grame/faustwasm";

let faust = null;
let ctx = null;
let node = null;
let analyser = null;

export function getContext() { return ctx; }
export function getNode() { return node; }
export function getAnalyser() { return analyser; }

export async function loadFaust(statusFn) {
  if (faust) return faust;
  if (statusFn) statusFn("Loading Faust compiler…");
  const m = await import(`${CDN}/dist/esm-bundle/index.js`);
  const base = `${CDN}/libfaust-wasm/`;
  const mod = await m.instantiateFaustModuleFromFile(base + "libfaust-wasm.js", base + "libfaust-wasm.data", base + "libfaust-wasm.wasm");
  const lib = new m.LibFaust(mod);
  faust = { m, compiler: new m.FaustCompiler(lib) };
  return faust;
}

export function stop(stopHooks = {}) {
  if (node) {
    try { node.disconnect(); node.destroy?.(); } catch {}
    node = null;
  }
  if (stopHooks.clearHeld) stopHooks.clearHeld();
  if (stopHooks.clearPointers) stopHooks.clearPointers();
  if (stopHooks.clearStack) stopHooks.clearStack();
  if (stopHooks.clearNoteEls) stopHooks.clearNoteEls();
}

export async function run(code, statusFn, stopHooks, onBuilt) {
  try {
    const { m, compiler } = await loadFaust(statusFn);
    if (!ctx) {
      ctx = new AudioContext({ latencyHint: "interactive" });
      analyser = ctx.createAnalyser();
      analyser.fftSize = 2048;
      analyser.connect(ctx.destination);
    }
    await ctx.resume();
    if (statusFn) statusFn("Compiling…");
    const args = "-ftz 2";
    const mono = new m.FaustMonoDspGenerator();
    await mono.compile(compiler, "dsp", code, args);
    const opts = (mono.getMeta().meta || []).map(x => x.options).find(Boolean) || "";
    const nv = +(opts.match(/\[nvoices:(\d+)\]/) || [])[1] || 0;
    stop(stopHooks);
    if (nv > 0) {
      const polyGen = new m.FaustPolyDspGenerator();
      await polyGen.compile(compiler, "dsp", code, args);
      node = await polyGen.createNode(ctx, nv);
    } else {
      node = await mono.createNode(ctx);
    }
    if (!node) throw new Error("Could not create audio node");
    node.connect(analyser);
    const json = JSON.parse(node.getJSON());
    if (onBuilt) onBuilt(json, nv);
    if (statusFn) statusFn("Running");
    return { json, nv };
  } catch (e) {
    const msg = String(e?.message || e);
    throw { message: msg };
  }
}
