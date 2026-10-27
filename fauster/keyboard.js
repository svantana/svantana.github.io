const KEYS = ["a","w","s","e","d","f","t","g","y","h","u","j","k","o","l","p"];
const isBlack = n => [1,3,6,8,10].includes(n % 12);
const KW = 32, BW = 19;

export function initKeyboard(opts) {
  const {
    kbEl,
    octEl,
    getNode,
    getPoly,
    getMp,
    setPoly,
    setMp,
    vel = 100
  } = opts;

  let octave = 4;
  const base = () => (octave + 1) * 12;
  const held = new Map();
  const cnt = new Map();
  const noteEls = new Map();
  const stack = [];
  const ptr = new Map();

  const mtof = n => 440 * 2 ** ((n - 69) / 12);

  function findParams(json) {
    const out = {};
    (function walk(items) {
      for (const it of items || []) {
        if (it.items) walk(it.items);
        else if (it.address) {
          const b = it.address.split("/").pop();
          if (b === "freq" || b === "gate") out[b] = it.address;
        }
      }
    })(json.ui || []);
    return out;
  }

  function noteOn(n) {
    stack.push(n);
    const node = getNode();
    if (!node) return;
    const poly = getPoly();
    const mp = getMp();
    if (poly) return node.keyOn(0, n, vel);
    if (mp && mp.freq && mp.gate) {
      node.setParamValue(mp.freq, mtof(n));
      node.setParamValue(mp.gate, 1);
    } else {
      node.midiMessage([0x90, n, 100]);
    }
  }

  function noteOff(n) {
    const i = stack.lastIndexOf(n);
    if (i >= 0) stack.splice(i, 1);
    const node = getNode();
    if (!node) return;
    const poly = getPoly();
    const mp = getMp();
    if (poly) return node.keyOff(0, n, 0);
    if (mp && mp.freq && mp.gate) {
      if (stack.length) {
        node.setParamValue(mp.freq, mtof(stack[stack.length - 1]));
      } else {
        node.setParamValue(mp.gate, 0);
      }
    } else {
      node.midiMessage([0x80, n, 0]);
    }
  }

  function press(n) {
    const c = (cnt.get(n) || 0) + 1;
    cnt.set(n, c);
    if (c === 1) {
      const el = noteEls.get(n);
      if (el) el.classList.add("on");
      noteOn(n);
    }
  }

  function release(n) {
    const c = cnt.get(n) || 0;
    if (c <= 0) return;
    if (c === 1) {
      cnt.delete(n);
      const el = noteEls.get(n);
      if (el) el.classList.remove("on");
      noteOff(n);
    } else {
      cnt.set(n, c - 1);
    }
  }

  function down(k) {
    if (held.has(k)) return;
    const idx = KEYS.indexOf(k);
    if (idx < 0) return;
    const n = base() + idx;
    held.set(k, n);
    press(n);
  }

  function up(k) {
    if (!held.has(k)) return;
    release(held.get(k));
    held.delete(k);
  }

  function drawKb() {
    kbEl.innerHTML = "";
    noteEls.clear();
    const avail = kbEl.parentElement ? kbEl.parentElement.clientWidth - 24 : 800;
    const W = Math.max(10, Math.floor(avail / KW));
    const b = base();
    const MAPPED_WHITES = 9;
    let s = b;
    for (let i = 0, left = Math.floor((W - MAPPED_WHITES) / 2); i < left; i++) {
      s--;
      while (isBlack(s)) s--;
    }
    while (s < 0 || isBlack(s)) s++;
    const whites = [];
    for (let n = s; n <= 127 && whites.length < W; n++) {
      if (!isBlack(n)) whites.push(n);
    }
    const add = (n, cls, left, width) => {
      const el = document.createElement("div");
      el.className = "k " + cls;
      el.style.left = left + "px";
      el.style.width = width + "px";
      el.dataset.n = n;
      const m = n - b;
      if (m >= 0 && m < KEYS.length) {
        el.classList.add("m");
        el.textContent = KEYS[m].toUpperCase();
      }
      if (cnt.get(n)) el.classList.add("on");
      noteEls.set(n, el);
      kbEl.appendChild(el);
    };
    whites.forEach((n, i) => add(n, "", i * KW, KW));
    whites.forEach((n, i) => {
      if (i < whites.length - 1 && isBlack(n + 1)) {
        add(n + 1, "b", (i + 1) * KW - BW / 2, BW);
      }
    });
    kbEl.style.width = whites.length * KW + "px";
    if (octEl) octEl.textContent = "Octave " + octave;
  }

  drawKb();
  addEventListener("resize", drawKb);

  const noteAt = e => {
    const el = document.elementFromPoint(e.clientX, e.clientY);
    if (!el || !el.dataset) return null;
    if (el.dataset.n === undefined) return null;
    if (!kbEl.contains(el)) return null;
    return +el.dataset.n;
  };
  const moveTo = (id, n) => {
    const prev = ptr.get(id);
    if (prev === n) return;
    if (prev !== undefined) release(prev);
    if (n !== null) {
      press(n);
      ptr.set(id, n);
    } else {
      ptr.delete(id);
    }
  };

  kbEl.addEventListener("pointerdown", e => {
    e.preventDefault();
    try { kbEl.setPointerCapture(e.pointerId); } catch {}
    moveTo(e.pointerId, noteAt(e));
  });
  kbEl.addEventListener("pointermove", e => {
    if (ptr.has(e.pointerId) || (e.buttons & 1 && kbEl.hasPointerCapture && kbEl.hasPointerCapture(e.pointerId))) {
      moveTo(e.pointerId, noteAt(e));
    }
  });
  const endPtr = e => {
    const n = ptr.get(e.pointerId);
    if (n !== undefined) release(n);
    ptr.delete(e.pointerId);
  };
  kbEl.addEventListener("pointerup", endPtr);
  kbEl.addEventListener("pointercancel", endPtr);

  const typing = e => /^(TEXTAREA|INPUT|SELECT)$/.test(e.target.tagName) && e.target.type !== "range" && e.target.type !== "checkbox";

  addEventListener("keydown", e => {
    if (typing(e) || e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    const k = e.key.toLowerCase();
    if (KEYS.includes(k)) {
      down(k);
    } else if (k === "z" && octave > 1) {
      octave--;
      drawKb();
    } else if (k === "x" && octave < 7) {
      octave++;
      drawKb();
    }
  });
  addEventListener("keyup", e => {
    const k = e.key.toLowerCase();
    if (KEYS.includes(k)) up(k);
  });
  addEventListener("blur", () => {
    [...held.keys()].forEach(up);
  });

  return {
    setFromJson: (json, nv) => {
      if (nv > 0) {
        setPoly(true);
        setMp({});
      } else {
        setPoly(false);
        setMp(findParams(json));
      }
      stack.length = 0;
    },
    clearAll: () => {
      held.clear();
      cnt.clear();
      noteEls.forEach(e => e.classList.remove("on"));
      noteEls.clear();
      ptr.clear();
      stack.length = 0;
    }
  };
}
