'use strict';

/* ------------------------------------------------------------------ utils */

const STEPS = 16;
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

const clamp = (v, min, max) => Math.min(max, Math.max(min, v));
const midiToFreq = (m) => 440 * Math.pow(2, (m - 69) / 12);
const noteName = (m) => NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
const pct = (v) => Math.round(v * 100) + '%';
const bipct = (v) => {
  const amount = Math.round((v - 0.5) * 200);
  return (amount > 0 ? '+' : '') + amount + '%';
};
const ms = (v) => Math.round(v) + 'ms';
const beatClass = (index) => (index % 4 === 0 ? ' beat-mark' : '');

function fmtHz(hz) {
  return hz >= 1000 ? (hz / 1000).toFixed(hz < 10000 ? 1 : 0) + 'kHz' : Math.round(hz) + 'Hz';
}

/* ------------------------------------------------------------- parameters */

const KNOBS_303 = [
  { key: 'cutoff', label: 'CUTOFF', format: (v) => fmtHz(90 * Math.pow(2, v * 6.4)) },
  { key: 'resonance', label: 'RES', format: (v) => String(Math.round(v * 100)) },
  { key: 'envmod', label: 'ENV MOD', format: bipct },
  { key: 'decay', label: 'DECAY', format: (v) => ms(45 + v * 500) },
  { key: 'accent', label: 'ACCENT', format: pct },
  { key: 'vol', label: 'VOL', format: pct },
];

const KNOBS_808 = {
  kick: [
    { key: 'tune', label: 'TUNE', format: (v) => Math.round(38 + v * 42) + 'Hz' },
    { key: 'decay', label: 'DECAY', format: (v) => ms(170 + v * 750) },
    { key: 'click', label: 'CLICK', format: pct },
  ],
  snare: [
    { key: 'tune', label: 'TUNE', format: pct },
    { key: 'tone', label: 'TONE', format: pct },
    { key: 'decay', label: 'DECAY', format: (v) => ms(80 + v * 380) },
  ],
  clap: [
    { key: 'spread', label: 'SPREAD', format: (v) => ms(4 + v * 22) },
    { key: 'tone', label: 'TONE', format: pct },
    { key: 'decay', label: 'DECAY', format: (v) => ms(120 + v * 380) },
  ],
  hat: [
    { key: 'tune', label: 'TUNE', format: pct },
    { key: 'tone', label: 'TONE', format: pct },
    { key: 'decay', label: 'DECAY', format: (v) => ms(18 + v * 340) },
  ],
};

const DEFAULT_303_NOTE = 36;
const MINOR_INTERVALS = [0, 2, 3, 5, 7, 8, 10, 12, 14, 15];

function minorScaleNotes(root) {
  return MINOR_INTERVALS.map((interval) => root + interval);
}

function steps303(entries) {
  return Array.from({ length: STEPS }, (_, i) => {
    const entry = entries[i];
    return entry
      ? { on: true, note: entry[0], accent: !!entry[1], slide: !!entry[2] }
      : { on: false, note: DEFAULT_303_NOTE, accent: false, slide: false };
  });
}

function steps808(hits) {
  return Array.from({ length: STEPS }, (_, i) => ({
    on: hits.includes(i),
    note: 36,
    slide: false,
  }));
}

/* --------------------------------------------------------------- patterns */

const tracks = [
  {
    id: 'acid-one',
    type: '303',
    name: 'ACID ONE',
    accent: '#ffffff',
    wave: 'sawtooth',
    muted: false,
    scaleRoot: 33,
    params: { cutoff: 0.34, resonance: 0.48, envmod: 0.55, decay: 0.4, accent: 0.68, vol: 0.82 },
    steps: steps303([
      [33, 1],
      null,
      [33],
      [36, 0, 1],
      [40, 1],
      null,
      [33],
      [43, 0, 1],
      [45, 1],
      null,
      [43],
      [40, 0, 1],
      [36],
      [33, 0, 1],
      null,
      [35, 1],
    ]),
  },
  {
    id: 'acid-two',
    type: '303',
    name: 'ACID TWO',
    accent: '#ffffff',
    wave: 'square',
    muted: false,
    scaleRoot: 45,
    params: { cutoff: 0.49, resonance: 0.32, envmod: 0.4, decay: 0.3, accent: 0.5, vol: 0.55 },
    steps: steps303([
      [45, 1],
      null,
      [48],
      [52, 0, 1],
      [55, 1],
      null,
      [53, 0, 1],
      [52],
      [50, 1],
      null,
      [48, 0, 1],
      [45],
      [55, 1],
      [52, 0, 1],
      null,
      [45],
    ]),
  },
  {
    id: 'drums',
    type: '808',
    name: 'DRUMS',
    accent: '#ffffff',
    muted: false,
    voices: [
      {
        id: 'kick',
        name: 'KICK',
        params: { tune: 0.42, decay: 0.5, click: 0.34, vol: 0.92 },
        steps: steps808([0, 3, 6, 10, 12]),
      },
      {
        id: 'snare',
        name: 'SNARE',
        params: { tune: 0.45, tone: 0.55, decay: 0.36, vol: 0.7 },
        steps: steps808([4, 12]),
      },
      {
        id: 'clap',
        name: 'CLAP',
        params: { spread: 0.38, tone: 0.45, decay: 0.3, vol: 0.6 },
        steps: steps808([4, 12]),
      },
      {
        id: 'hat',
        name: 'HI-HAT',
        params: { tune: 0.5, tone: 0.45, decay: 0.18, vol: 0.52 },
        steps: steps808([0, 2, 4, 6, 8, 10, 12, 14]),
      },
    ],
  },
];

/* ------------------------------------------------------------------ state */

const state = {
  playing: false,
  started: false,
  bpm: 124,
  swing: 0.12,
  master: 0.82,
  nextStep: 0,
  nextNoteTime: 0,
  displayStep: -1,
  timer: null,
  visualQueue: [],
  transitioning: false,
};

/* ------------------------------------------------------------------ audio */

const audio = {
  ctx: null,
  master: null,
  analyser: null,
  noise: null,
  meterData: null,
};

let distCurve = null;

function getDistCurve() {
  if (distCurve) return distCurve;
  const n = 1024;
  distCurve = new Float32Array(n);
  const k = 1.9;
  for (let i = 0; i < n; i++) {
    const x = (i * 2) / (n - 1) - 1;
    distCurve[i] = Math.tanh(x * k) / Math.tanh(k);
  }
  return distCurve;
}

function makeNoise(ctx) {
  const length = Math.floor(ctx.sampleRate * 1.5);
  const buffer = ctx.createBuffer(1, length, ctx.sampleRate);
  const data = buffer.getChannelData(0);
  for (let i = 0; i < length; i++) data[i] = Math.random() * 2 - 1;
  return buffer;
}

function initAudio() {
  if (audio.ctx) return audio.ctx;
  const AudioCtor = window.AudioContext || window.webkitAudioContext;
  if (!AudioCtor) return null;

  const ctx = new AudioCtor();
  const master = ctx.createGain();
  master.gain.value = state.master;

  const compressor = ctx.createDynamicsCompressor();
  compressor.threshold.value = -14;
  compressor.knee.value = 16;
  compressor.ratio.value = 5;
  compressor.attack.value = 0.003;
  compressor.release.value = 0.24;

  const analyser = ctx.createAnalyser();
  analyser.fftSize = 256;
  analyser.smoothingTimeConstant = 0.5;

  master.connect(compressor);
  compressor.connect(analyser);
  analyser.connect(ctx.destination);

  audio.ctx = ctx;
  audio.master = master;
  audio.analyser = analyser;
  audio.noise = makeNoise(ctx);
  audio.meterData = new Uint8Array(analyser.fftSize);

  return ctx;
}

/* --------------------------------------------------------------- synthesis */

function get303Voice(track) {
  if (track.voice) return track.voice;
  const ctx = audio.ctx;
  if (!ctx || !audio.master) return null;

  const osc = ctx.createOscillator();
  osc.type = track.wave;

  const filter = ctx.createBiquadFilter();
  filter.type = 'lowpass';

  const amp = ctx.createGain();
  amp.gain.value = 0.0001;

  const shaper = ctx.createWaveShaper();
  shaper.curve = getDistCurve();
  shaper.oversample = '2x';

  osc.connect(filter);
  filter.connect(amp);
  amp.connect(shaper);
  shaper.connect(audio.master);
  osc.start();

  track.voice = { osc, filter, amp, shaper, gateUntil: 0 };
  return track.voice;
}

function holdParam(param, time) {
  if (param.cancelAndHoldAtTime) param.cancelAndHoldAtTime(time);
  else param.cancelScheduledValues(time);
}

function release303(track) {
  const voice = track.voice;
  if (!voice || !audio.ctx) return;
  const now = audio.ctx.currentTime;
  holdParam(voice.amp.gain, now);
  voice.amp.gain.setTargetAtTime(0.0001, now, 0.01);
  voice.gateUntil = 0;
}

function silence303(track) {
  const voice = track.voice;
  if (!voice || !audio.ctx) return;
  const now = audio.ctx.currentTime;
  holdParam(voice.amp.gain, now);
  voice.amp.gain.setValueAtTime(0.0001, now);
  voice.gateUntil = 0;
}

function trigger303(track, step, previous, next, time, stepDur, preview = false) {
  const ctx = audio.ctx;
  if (!ctx) return;
  const voice = get303Voice(track);
  if (!voice) return;

  const p = track.params;
  const osc = voice.osc;
  const filter = voice.filter;
  const amp = voice.amp;
  const freq = midiToFreq(step.note);
  const accent = step.accent;
  const peak = 0.3 * p.vol * (accent ? 1.5 : 0.92);
  const sustained = !preview && next && next.on && next.slide;
  const gate = Math.max(0.055, stepDur * (sustained ? 1.75 : 0.86));
  const decay = 0.045 + p.decay * 0.5;
  const baseCutoff = 90 * Math.pow(2, p.cutoff * 6.4);
  const accentAmt = accent ? 0.55 + p.accent * 1.15 : 0.72;
  const envAmount = (p.envmod - 0.5) * 2;
  const envStart = clamp(baseCutoff * Math.pow(2, envAmount * 4.5 * accentAmt), 30, 15000);
  const legato = !preview && step.slide && previous && previous.on && voice.gateUntil > time;
  const glide = legato;

  osc.type = track.wave;
  osc.frequency.cancelScheduledValues(time);
  if (glide) {
    osc.frequency.setValueAtTime(midiToFreq(previous.note), time);
    osc.frequency.exponentialRampToValueAtTime(freq, time + Math.min(0.09, stepDur * 0.55));
  } else {
    osc.frequency.setValueAtTime(freq, time);
  }

  if (!legato) {
    amp.gain.cancelScheduledValues(time);
    filter.frequency.cancelScheduledValues(time);
  }

  if (!legato) {
    filter.Q.setValueAtTime(0.6 + p.resonance * 19, time);
    filter.frequency.setValueAtTime(envStart, time);
    filter.frequency.exponentialRampToValueAtTime(baseCutoff, time + 0.035 + p.decay * 0.35);
  }

  const sustain = Math.max(peak * 0.34, 0.0002);
  const releaseAt = time + gate;
  const releaseEnd = releaseAt + 0.035;

  if (legato) {
    holdParam(amp.gain, time);
    amp.gain.exponentialRampToValueAtTime(sustain, time + 0.02);
  } else {
    amp.gain.setValueAtTime(0.0001, time);
    amp.gain.exponentialRampToValueAtTime(peak, time + 0.004);
    amp.gain.exponentialRampToValueAtTime(sustain, time + Math.min(decay, gate));
  }
  amp.gain.setValueAtTime(sustain, releaseAt);
  amp.gain.exponentialRampToValueAtTime(0.0001, releaseEnd);

  if (!preview) voice.gateUntil = releaseEnd;
}

function triggerKick(p, time) {
  const ctx = audio.ctx;
  const vol = p.vol * 0.64;
  const tune = 38 + p.tune * 42;
  const decay = 0.17 + p.decay * 0.75;

  const osc = ctx.createOscillator();
  osc.type = 'sine';
  osc.frequency.setValueAtTime(tune * 3.5, time);
  osc.frequency.exponentialRampToValueAtTime(tune, time + 0.055);

  const amp = ctx.createGain();
  amp.gain.setValueAtTime(0.0001, time);
  amp.gain.exponentialRampToValueAtTime(0.92 * vol, time + 0.005);
  amp.gain.exponentialRampToValueAtTime(0.0001, time + decay);

  osc.connect(amp);
  amp.connect(audio.master);
  osc.start(time);
  osc.stop(time + decay + 0.02);

  if (p.click > 0.01) {
    const src = ctx.createBufferSource();
    src.buffer = audio.noise;
    const hp = ctx.createBiquadFilter();
    hp.type = 'highpass';
    hp.frequency.value = 1800;
    const clickAmp = ctx.createGain();
    clickAmp.gain.setValueAtTime(0.0001, time);
    clickAmp.gain.exponentialRampToValueAtTime(0.3 * p.click * vol, time + 0.002);
    clickAmp.gain.exponentialRampToValueAtTime(0.0001, time + 0.035);

    src.connect(hp);
    hp.connect(clickAmp);
    clickAmp.connect(audio.master);
    src.start(time);
    src.stop(time + 0.05);
  }
}

function triggerSnare(p, time) {
  const ctx = audio.ctx;
  const vol = p.vol * 0.66;
  const tune = 0.78 + p.tune * 0.55;
  const noiseDecay = 0.08 + p.decay * 0.34;

  const src = ctx.createBufferSource();
  src.buffer = audio.noise;
  const bp = ctx.createBiquadFilter();
  bp.type = 'bandpass';
  bp.frequency.value = 1100 + p.tone * 2800;
  bp.Q.value = 0.75;
  const noiseAmp = ctx.createGain();
  noiseAmp.gain.setValueAtTime(0.0001, time);
  noiseAmp.gain.exponentialRampToValueAtTime(0.95 * vol, time + 0.003);
  noiseAmp.gain.exponentialRampToValueAtTime(0.0001, time + noiseDecay);

  src.connect(bp);
  bp.connect(noiseAmp);
  noiseAmp.connect(audio.master);
  src.start(time);
  src.stop(time + noiseDecay + 0.02);

  const bodyAmp = ctx.createGain();
  const bodyDecay = 0.08 + p.decay * 0.16;
  bodyAmp.gain.setValueAtTime(0.0001, time);
  bodyAmp.gain.exponentialRampToValueAtTime(0.32 * vol, time + 0.003);
  bodyAmp.gain.exponentialRampToValueAtTime(0.0001, time + bodyDecay);
  bodyAmp.connect(audio.master);

  [185 * tune, 330 * tune].forEach((f) => {
    const osc = ctx.createOscillator();
    osc.type = 'triangle';
    osc.frequency.setValueAtTime(f, time);
    osc.connect(bodyAmp);
    osc.start(time);
    osc.stop(time + bodyDecay + 0.02);
  });
}

function triggerClap(p, time) {
  const ctx = audio.ctx;
  const vol = p.vol * 0.7;
  const spread = 0.004 + p.spread * 0.022;
  const tail = 0.1 + p.decay * 0.38;
  const peak = 1.25 * vol;

  const src = ctx.createBufferSource();
  src.buffer = audio.noise;

  const bp = ctx.createBiquadFilter();
  bp.type = 'bandpass';
  bp.frequency.value = 850 + p.tone * 2400;
  bp.Q.value = 1.15;

  const amp = ctx.createGain();
  amp.gain.setValueAtTime(0.0001, time);

  for (let i = 0; i < 4; i++) {
    const t0 = time + i * spread;
    amp.gain.setValueAtTime(0.0001, t0);
    amp.gain.linearRampToValueAtTime(peak * (i === 3 ? 0.92 : 0.66), t0 + 0.002);
    amp.gain.exponentialRampToValueAtTime(0.0001, t0 + spread * 0.82 + 0.006);
  }

  const tailStart = time + spread * 4 + 0.008;
  amp.gain.setValueAtTime(peak * 0.82, tailStart);
  amp.gain.exponentialRampToValueAtTime(0.0001, tailStart + tail);

  src.connect(bp);
  bp.connect(amp);
  amp.connect(audio.master);
  src.start(time);
  src.stop(tailStart + tail + 0.03);
}

function triggerHat(p, time) {
  const ctx = audio.ctx;
  const vol = p.vol * 0.64;
  const decay = 0.02 + p.decay * 0.34;
  const tune = 0.62 + p.tune * 0.85;

  const sum = ctx.createGain();
  sum.gain.value = 0.9;

  const stopAt = time + decay + 0.03;
  [205.3, 304.4, 369.6, 522.7, 540, 800].forEach((f) => {
    const osc = ctx.createOscillator();
    osc.type = 'square';
    osc.frequency.setValueAtTime(f * tune, time);
    const g = ctx.createGain();
    g.gain.value = 0.2;
    osc.connect(g);
    g.connect(sum);
    osc.start(time);
    osc.stop(stopAt);
  });

  const src = ctx.createBufferSource();
  src.buffer = audio.noise;
  const noiseGain = ctx.createGain();
  noiseGain.gain.value = 0.5;
  src.connect(noiseGain);
  noiseGain.connect(sum);
  src.start(time);
  src.stop(stopAt);

  const bp = ctx.createBiquadFilter();
  bp.type = 'bandpass';
  bp.frequency.value = 8000 + p.tone * 3000;
  bp.Q.value = 0.6;

  const hp = ctx.createBiquadFilter();
  hp.type = 'highpass';
  hp.frequency.value = 3500;

  const amp = ctx.createGain();
  amp.gain.setValueAtTime(0.0001, time);
  amp.gain.exponentialRampToValueAtTime(2.4 * vol, time + 0.002);
  amp.gain.exponentialRampToValueAtTime(0.0001, time + decay);

  sum.connect(bp);
  bp.connect(hp);
  hp.connect(amp);
  amp.connect(audio.master);
}

function trigger808(voice, time) {
  const p = voice.params;
  if (!audio.ctx) return;
  if (voice.id === 'kick') triggerKick(p, time);
  else if (voice.id === 'snare') triggerSnare(p, time);
  else if (voice.id === 'clap') triggerClap(p, time);
  else if (voice.id === 'hat') triggerHat(p, time);
}

/* --------------------------------------------------------------- scheduler */

function stepDuration() {
  return 60 / state.bpm / 4;
}

function scheduleStep(index, time) {
  const dur = stepDuration();
  tracks.forEach((track) => {
    if (track.muted) return;
    if (track.type === '303') {
      const step = track.steps[index];
      if (!step.on) return;
      const previous = track.steps[(index + STEPS - 1) % STEPS];
      const next = track.steps[(index + 1) % STEPS];
      trigger303(track, step, previous, next, time, dur);
    } else {
      track.voices.forEach((voice) => {
        const step = voice.steps[index];
        if (step.on) trigger808(voice, time);
      });
    }
  });
  state.visualQueue.push({ step: index, time });
}

function advanceClock() {
  const dur = stepDuration();
  const swing = state.swing;
  if (state.nextStep % 2 === 0) {
    state.nextNoteTime += dur * (1 + swing);
  } else {
    state.nextNoteTime += dur * (1 - swing);
  }
  state.nextStep = (state.nextStep + 1) % STEPS;
}

function scheduler() {
  if (!state.playing || !audio.ctx) return;
  const ctx = audio.ctx;
  const now = ctx.currentTime;

  if (state.nextNoteTime < now - 0.25) {
    state.nextNoteTime = now + 0.03;
    state.visualQueue = [];
  }

  while (state.nextNoteTime < now + 0.12) {
    scheduleStep(state.nextStep, state.nextNoteTime);
    advanceClock();
  }
}

function setDisplayStep(index) {
  document.querySelectorAll('.is-current').forEach((el) => el.classList.remove('is-current'));
  if (index >= 0) {
    document.querySelectorAll('[data-step="' + index + '"]').forEach((el) => {
      el.classList.add('is-current');
    });
  }
}

function updateVisuals() {
  if (!audio.ctx || !state.visualQueue.length) return;
  const now = audio.ctx.currentTime;
  let changed = false;
  while (state.visualQueue.length && state.visualQueue[0].time <= now + 0.004) {
    const event = state.visualQueue.shift();
    if (event.step !== state.displayStep) {
      state.displayStep = event.step;
      changed = true;
    }
  }
  if (changed) setDisplayStep(state.displayStep);
}

async function togglePlay() {
  if (state.transitioning) return;
  const ctx = initAudio();
  if (!ctx) return;

  state.transitioning = true;
  try {
    if (!state.playing) {
      try {
        await ctx.resume();
      } catch (err) {
        /* no-op */
      }

      if (!state.started) {
        state.started = true;
        state.nextStep = 0;
        state.nextNoteTime = ctx.currentTime + 0.07;
        state.visualQueue = [];
        state.displayStep = -1;
        setDisplayStep(-1);
      } else if (state.nextNoteTime < ctx.currentTime) {
        state.nextNoteTime = ctx.currentTime + 0.07;
      }

      state.playing = true;
      updatePlayButton();
      scheduler();
      state.timer = window.setInterval(scheduler, 25);
    } else {
      state.playing = false;
      if (state.timer) {
        window.clearInterval(state.timer);
        state.timer = null;
      }
      updatePlayButton();
      tracks.forEach((track) => {
        if (track.type === '303') silence303(track);
      });
      try {
        await ctx.suspend();
      } catch (err) {
        /* no-op */
      }
    }
  } finally {
    state.transitioning = false;
  }
}

/* -------------------------------------------------------------------- view */

const tracksEl = document.getElementById('tracks');
const playBtn = document.getElementById('playBtn');
const bpmInput = document.getElementById('bpmInput');
const bpmUp = document.getElementById('bpmUp');
const bpmDown = document.getElementById('bpmDown');

function createKnob(options) {
  const { label, value, defaultValue, accent = '#ffffff', format, onInput, size = '' } = options;
  const initial = clamp(Number.isFinite(value) ? value : 0, 0, 1);
  const resetValue = clamp(Number.isFinite(defaultValue) ? defaultValue : initial, 0, 1);

  const wrap = document.createElement('div');
  wrap.className = 'knob-control';
  wrap.innerHTML =
    '<div class="knob ' + size + '" tabindex="0" role="slider" aria-label="' + label +
    '" aria-valuemin="0" aria-valuemax="100">' +
    '<div class="knob-cap"></div>' +
    '</div>' +
    '<span class="knob-label">' + label + '</span>' +
    '<span class="knob-value"></span>';

  const knob = wrap.querySelector('.knob');
  const cap = wrap.querySelector('.knob-cap');
  const valueEl = wrap.querySelector('.knob-value');
  knob.style.setProperty('--accent', accent);

  let current = initial;
  let dragging = false;
  let startY = 0;
  let startValue = initial;
  let pointerId = null;

  function set(v, fire) {
    current = clamp(v, 0, 1);
    cap.style.transform = 'rotate(' + (-135 + current * 270) + 'deg)';
    knob.style.setProperty('--fill', current * 270 + 'deg');
    knob.setAttribute('aria-valuenow', String(Math.round(current * 100)));
    valueEl.textContent = format ? format(current) : String(Math.round(current * 100));
    if (fire && onInput) onInput(current);
  }

  knob.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    event.preventDefault();
    dragging = true;
    pointerId = event.pointerId;
    startY = event.clientY;
    startValue = current;
    knob.setPointerCapture(pointerId);
    knob.classList.add('is-dragging');
  });

  knob.addEventListener('pointermove', (event) => {
    if (!dragging || event.pointerId !== pointerId) return;
    const precision = event.shiftKey ? 0.22 : 1;
    const delta = ((startY - event.clientY) / 145) * precision;
    set(startValue + delta, true);
  });

  function endDrag(event) {
    if (!dragging) return;
    dragging = false;
    knob.classList.remove('is-dragging');
    if (pointerId !== null && knob.hasPointerCapture(pointerId)) {
      knob.releasePointerCapture(pointerId);
    }
    pointerId = null;
  }

  knob.addEventListener('pointerup', endDrag);
  knob.addEventListener('pointercancel', endDrag);

  knob.addEventListener('wheel', (event) => {
    event.preventDefault();
    const amount = event.shiftKey ? 0.01 : 0.025;
    set(current + (event.deltaY < 0 ? amount : -amount), true);
  }, { passive: false });

  knob.addEventListener('dblclick', () => set(resetValue, true));

  knob.addEventListener('keydown', (event) => {
    const amount = event.shiftKey ? 0.01 : 0.025;
    if (event.key === 'ArrowUp' || event.key === 'ArrowRight') {
      event.preventDefault();
      set(current + amount, true);
    } else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') {
      event.preventDefault();
      set(current - amount, true);
    } else if (event.key === 'Home') {
      event.preventDefault();
      set(0, true);
    } else if (event.key === 'End') {
      event.preventDefault();
      set(1, true);
    }
  });

  set(initial, false);
  return wrap;
}

function updatePlayButton() {
  playBtn.classList.toggle('is-playing', state.playing);
  playBtn.setAttribute('aria-label', state.playing ? 'Pause' : 'Play');
}

function updateStepView(track, index) {
  const view = track.view;

  if (track.type === '303') {
    const step = track.steps[index];
    view.accentCells[index].classList.toggle('on', !!step.accent);
    view.accentCells[index].setAttribute('aria-pressed', String(!!step.accent));

    const activeNote = step.on ? step.note : null;
    view.pitchCells[index].forEach((cell) => {
      const note = Number(cell.dataset.note);
      const active = activeNote !== null && note === activeNote;
      cell.classList.toggle('on', active);
      cell.setAttribute(
        'aria-label',
        track.name + ', step ' + (index + 1) + ', ' + noteName(note) + (active ? ', active' : '')
      );
    });

    view.slideCells[index].classList.toggle('on', !!step.slide);
    view.slideCells[index].setAttribute('aria-pressed', String(!!step.slide));
  } else {
    track.voices.forEach((voice, voiceIndex) => {
      const step = voice.steps[index];
      const rowView = view.drumRows[voiceIndex];
      const cell = rowView.noteCells[index];

      cell.classList.toggle('on', step.on);
      cell.setAttribute(
        'aria-label',
        voice.name + ', step ' + (index + 1) + (step.on ? ', on' : ', off')
      );
    });
  }
}

function preview303(track, step) {
  const ctx = initAudio();
  if (!ctx || ctx.state !== 'running' || state.playing) return;
  trigger303(track, step, null, null, ctx.currentTime + 0.01, 0.24, true);
}

function previewDrum(voice) {
  const ctx = initAudio();
  if (!ctx || ctx.state !== 'running') return;
  trigger808(voice, ctx.currentTime + 0.01);
}

function setupDrumCell(track, voice, index, cell) {
  const step = voice.steps[index];
  cell.addEventListener('click', () => {
    step.on = !step.on;
    updateStepView(track, index);
    if (step.on) previewDrum(voice);
  });
}

function buildTrack(track, index) {
  const section = document.createElement('section');
  section.className = 'track';
  section.dataset.track = track.id;
  section.style.setProperty('--accent', track.accent);

  const panel = document.createElement('div');
  panel.className = 'track-panel';

  const head = document.createElement('div');
  head.className = 'track-head';
  head.innerHTML =
    '<span class="track-index">' + String(index + 1).padStart(2, '0') + '</span>' +
    '<h2>' + track.name + '</h2>';

  const tools = document.createElement('div');
  tools.className = 'track-tools';

  if (track.type === '303') {
    const waves = document.createElement('div');
    waves.className = 'wave-switch';
    [['sawtooth', 'SAW'], ['square', 'SQR']].forEach(([kind, label]) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.className = track.wave === kind ? 'active' : '';
      button.setAttribute('aria-label', label + ' wave');
      button.addEventListener('click', () => {
        track.wave = kind;
        if (track.voice) track.voice.osc.type = kind;
        waves.querySelectorAll('button').forEach((b) => b.classList.toggle('active', b === button));
      });
      waves.appendChild(button);
    });
    tools.appendChild(waves);
  }

  const mute = document.createElement('button');
  mute.type = 'button';
  mute.className = 'mute-btn';
  mute.textContent = 'M';
  mute.title = 'Mute track';
  mute.setAttribute('aria-pressed', 'false');
  mute.addEventListener('click', () => {
    track.muted = !track.muted;
    section.classList.toggle('muted', track.muted);
    mute.classList.toggle('active', track.muted);
    mute.setAttribute('aria-pressed', String(track.muted));
    if (track.muted) release303(track);
  });
  tools.appendChild(mute);
  head.appendChild(tools);

  const defs = track.type === '303' ? KNOBS_303 : null;

  function appendKnobRow(group) {
    const knobRow = document.createElement('div');
    knobRow.className = 'knob-row';
    group.forEach((def) => {
      knobRow.appendChild(createKnob({
        label: def.label,
        value: track.params[def.key],
        defaultValue: track.params[def.key],
        accent: track.accent,
        format: def.format,
        onInput: (v) => { track.params[def.key] = v; },
      }));
    });
    panel.appendChild(knobRow);
  }

  panel.appendChild(head);

  if (track.type === '303') {
    appendKnobRow(defs.slice(0, 3));
    appendKnobRow(defs.slice(3));
  } else {
    const controls = document.createElement('div');
    controls.className = 'drum-controls';

    track.voices.forEach((voice) => {
      const controlRow = document.createElement('div');
      controlRow.className = 'drum-control-row';

      const name = document.createElement('span');
      name.className = 'drum-control-name';
      name.textContent = voice.name;

      const knobRow = document.createElement('div');
      knobRow.className = 'knob-row';
      KNOBS_808[voice.id].forEach((def) => {
        knobRow.appendChild(createKnob({
          label: def.label,
          value: voice.params[def.key],
          defaultValue: voice.params[def.key],
          accent: track.accent,
          format: def.format,
          onInput: (v) => { voice.params[def.key] = v; },
        }));
      });

      controlRow.appendChild(name);
      controlRow.appendChild(knobRow);
      controls.appendChild(controlRow);
    });

    panel.appendChild(controls);
  }

  const seq = document.createElement('div');
  seq.className = 'track-seq';
  const scroll = document.createElement('div');
  scroll.className = 'seq-scroll';
  const grid = document.createElement('div');
  grid.className = 'seq-grid' + (track.type === '303' ? ' piano' : ' drums');

  const view = {
    section,
    pitchCells: Array.from({ length: STEPS }, () => []),
    accentCells: [],
    slideCells: [],
    drumRows: [],
    muteButton: mute,
  };

  if (track.type === '303') {
    const scaleNotes = minorScaleNotes(track.scaleRoot);

    for (let row = scaleNotes.length - 1; row >= 0; row--) {
      const note = scaleNotes[row];
      const pitchRow = document.createElement('div');
      pitchRow.className = 'seq-row pitch-row' + (note % 12 === track.scaleRoot % 12 ? ' root' : '');

      const label = document.createElement('span');
      label.className = 'seq-label';
      label.textContent = noteName(note);
      pitchRow.appendChild(label);

      for (let i = 0; i < STEPS; i++) {
        const cell = document.createElement('button');
        cell.type = 'button';
        cell.className = 'step-cell pitch-cell' + beatClass(i);
        cell.dataset.step = String(i);
        cell.dataset.note = String(note);
        cell.title = 'Step ' + (i + 1) + ' · ' + noteName(note);
        cell.addEventListener('click', () => {
          const current = track.steps[i];
          if (current.on && current.note === note) {
            current.on = false;
          } else {
            current.on = true;
            current.note = note;
          }
          updateStepView(track, i);
          if (current.on) preview303(track, current);
        });
        pitchRow.appendChild(cell);
        view.pitchCells[i].push(cell);
      }
      grid.appendChild(pitchRow);
    }

    const accentRow = document.createElement('div');
    accentRow.className = 'seq-row flags acc';
    accentRow.innerHTML = '<span class="seq-label">ACC</span>';

    for (let i = 0; i < STEPS; i++) {
      const step = track.steps[i];
      const accent = document.createElement('button');
      accent.type = 'button';
      accent.className = 'flag-cell' + beatClass(i);
      accent.dataset.step = String(i);
      accent.setAttribute('aria-label', track.name + ' accent step ' + (i + 1));
      accent.setAttribute('aria-pressed', String(!!step.accent));
      accent.addEventListener('click', () => {
        step.accent = !step.accent;
        updateStepView(track, i);
      });
      accentRow.appendChild(accent);
      view.accentCells.push(accent);
    }
    grid.appendChild(accentRow);

    const slideRow = document.createElement('div');
    slideRow.className = 'seq-row flags sld';
    slideRow.innerHTML = '<span class="seq-label">SLD</span>';

    for (let i = 0; i < STEPS; i++) {
      const step = track.steps[i];
      const slide = document.createElement('button');
      slide.type = 'button';
      slide.className = 'flag-cell slide' + beatClass(i);
      slide.dataset.step = String(i);
      slide.setAttribute('aria-label', track.name + ' slide step ' + (i + 1));
      slide.setAttribute('aria-pressed', String(!!step.slide));
      slide.addEventListener('click', () => {
        step.slide = !step.slide;
        updateStepView(track, i);
      });
      slideRow.appendChild(slide);
      view.slideCells.push(slide);
    }
    grid.appendChild(slideRow);
  } else {
    track.voices.forEach((voice) => {
      const rowView = { voice, noteCells: [] };

      const hitRow = document.createElement('div');
      hitRow.className = 'seq-row hits drum-row';
      hitRow.dataset.voice = voice.id;

      const hitLabel = document.createElement('span');
      hitLabel.className = 'seq-label';
      hitLabel.textContent = voice.name;
      hitRow.appendChild(hitLabel);

      for (let i = 0; i < STEPS; i++) {
        const cell = document.createElement('button');
        cell.type = 'button';
        cell.className = 'step-cell drum-cell' + beatClass(i);
        cell.dataset.step = String(i);
        cell.dataset.voice = voice.id;
        cell.title = voice.name + ' step ' + (i + 1);
        setupDrumCell(track, voice, i, cell);
        hitRow.appendChild(cell);
        rowView.noteCells.push(cell);
      }
      grid.appendChild(hitRow);

      view.drumRows.push(rowView);
    });
  }

  scroll.appendChild(grid);
  seq.appendChild(scroll);
  section.appendChild(panel);
  section.appendChild(seq);
  track.view = view;

  for (let i = 0; i < STEPS; i++) updateStepView(track, i);

  if (state.displayStep >= 0) {
    section.querySelectorAll('[data-step="' + state.displayStep + '"]').forEach((el) => {
      el.classList.add('is-current');
    });
  }

  return section;
}

function renderTracks() {
  tracksEl.innerHTML = '';
  tracks.forEach((track, index) => tracksEl.appendChild(buildTrack(track, index)));
}

/* ------------------------------------------------------------ top controls */

function buildTopControls() {
  const swingMount = document.getElementById('swingKnob');
  swingMount.appendChild(createKnob({
    label: 'SWING',
    value: state.swing / 0.6,
    defaultValue: 0.2,
    accent: '#ffffff',
    size: 'knob-sm',
    format: (v) => Math.round(v * 60) + '%',
    onInput: (v) => { state.swing = v * 0.6; },
  }));

  const masterMount = document.getElementById('masterKnob');
  masterMount.appendChild(createKnob({
    label: 'MASTER',
    value: state.master,
    defaultValue: 0.82,
    accent: '#ffffff',
    size: 'knob-sm',
    format: pct,
    onInput: (v) => {
      state.master = v;
      if (audio.master && audio.ctx) {
        audio.master.gain.setTargetAtTime(v, audio.ctx.currentTime, 0.03);
      }
    },
  }));
}

function applyBpm(value) {
  const v = clamp(Math.round(value), 60, 200);
  state.bpm = v;
  bpmInput.value = String(v);
}

bpmUp.addEventListener('click', () => applyBpm(state.bpm + 1));
bpmDown.addEventListener('click', () => applyBpm(state.bpm - 1));
bpmInput.addEventListener('input', () => {
  const v = Number(bpmInput.value);
  if (Number.isFinite(v) && v >= 60 && v <= 200) state.bpm = Math.round(v);
});
bpmInput.addEventListener('change', () => applyBpm(Number(bpmInput.value) || state.bpm));
bpmInput.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    event.preventDefault();
    bpmInput.blur();
  }
});

playBtn.addEventListener('click', togglePlay);

document.addEventListener('keydown', (event) => {
  if (event.code !== 'Space') return;
  const tag = document.activeElement ? document.activeElement.tagName : '';
  if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'BUTTON' || tag === 'SELECT') return;
  event.preventDefault();
  togglePlay();
});

/* ------------------------------------------------------------------- meter */

const meterSegments = Array.from(document.querySelectorAll('#meter i'));
let meterLevel = 0;

function updateMeter() {
  if (!audio.analyser || !audio.meterData) return;
  audio.analyser.getByteTimeDomainData(audio.meterData);
  let peak = 0;
  for (let i = 0; i < audio.meterData.length; i++) {
    const v = Math.abs(audio.meterData[i] - 128) / 128;
    if (v > peak) peak = v;
  }
  const target = clamp(peak * 2.6, 0, 1);
  meterLevel += (target - meterLevel) * (target > meterLevel ? 0.55 : 0.1);
  const lit = Math.round(meterLevel * meterSegments.length);
  meterSegments.forEach((segment, i) => segment.classList.toggle('on', i < lit));
}

function animationLoop() {
  updateVisuals();
  updateMeter();
  window.requestAnimationFrame(animationLoop);
}

/* -------------------------------------------------------------------- init */

renderTracks();
buildTopControls();
updatePlayButton();
applyBpm(state.bpm);
window.requestAnimationFrame(animationLoop);
