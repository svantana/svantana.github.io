export function buildGui(json, node, container) {
  container.innerHTML = "";
  const meters = {};
  node?.setOutputParamHandler((path, v) => {
    const fn = meters[path];
    if (fn) fn(v);
  });
  const clean = s => (s || "").replace(/\[[^\]]*\]/g, "").trim();
  const make = (it) => {
    const t = it.type;
    const label = clean(it.label);
    if (t && t.endsWith("group")) {
      const g = document.createElement("div");
      g.className = "grp";
      if (label) g.innerHTML = `<div class="t">${label}</div>`;
      const c = document.createElement("div");
      c.className = t === "hgroup" ? "row" : (t === "tgroup" ? "row" : "col");
      (it.items || []).forEach(i => c.appendChild(make(i)));
      g.appendChild(c);
      return g;
    }
    const w = document.createElement("div");
    w.className = "w" + (t && t.startsWith("v") ? " v" : "");
    const addr = it.address;
    const step = +it.step || 0.01;
    const dec = Math.max(0, Math.min(4, (String(step).split(".")[1] || "").length));
    if (t === "button") {
      const b = document.createElement("button");
      b.textContent = label;
      const set = v => node?.setParamValue(addr, v);
      b.onpointerdown = () => set(1);
      b.onpointerup = b.onpointerleave = () => set(0);
      w.appendChild(b);
    } else if (t === "checkbox") {
      const l = document.createElement("label");
      const cbox = document.createElement("input");
      cbox.type = "checkbox";
      cbox.onchange = () => node?.setParamValue(addr, cbox.checked ? 1 : 0);
      l.append(cbox, " " + label);
      w.appendChild(l);
    } else if (t === "hslider" || t === "vslider" || t === "nentry") {
      const r = document.createElement("input");
      r.type = "range";
      r.min = it.min;
      r.max = it.max;
      r.step = step;
      r.value = it.init;
      const v = document.createElement("span");
      v.className = "val";
      v.textContent = (+it.init).toFixed(dec);
      r.oninput = () => {
        v.textContent = (+r.value).toFixed(dec);
        node?.setParamValue(addr, +r.value);
      };
      const l = document.createElement("span");
      l.textContent = label;
      w.append(l, r, v);
    } else if (t === "hbargraph" || t === "vbargraph") {
      const mt = document.createElement("div");
      mt.className = "meter";
      const bar = document.createElement("i");
      mt.appendChild(bar);
      const vert = t === "vbargraph";
      const lo = +it.min;
      const hi = +it.max;
      meters[addr] = val => {
        const p = Math.max(0, Math.min(1, (val - lo) / (hi - lo))) * 100;
        bar.style[vert ? "height" : "width"] = p + "%";
      };
      const l = document.createElement("span");
      l.textContent = label;
      w.append(l, mt);
    }
    return w;
  };
  (json.ui || []).forEach(i => container.appendChild(make(i)));
}
