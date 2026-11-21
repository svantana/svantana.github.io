import audioVM

vm = audioVM.VM()
midi = vm.midi()

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
noise = vm.op("oscNoise")
onsets = vm.array(size=12)

# store note-ons in an array to enable simultaneous hits
with vm.poly(count=12):
  with vm.when(midi.onset):
    onsets.write(midi.note % 12, midi.onset)

# bass
onset = onsets.read(0)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + unitLength)})
with vm.when(envA > 1e-3):
  envF = vm.op("envExp", {"gate": onset, "decay": 0.05})
  sin  = vm.op("oscSin", {"frequency": (60 + 100 * envF) * (.5 + unitPitch), "reset": onset})
  mix += sin * (envA * 0.7)

# rimshot
onset = onsets.read(1)
envA = vm.op("envExp", {"gate": onset, "decay": .015})
with vm.when(envA > 1e-3):
  osc1 = vm.op("oscSin", {"frequency": 460, "reset": onset})
  osc2 = vm.op("oscSin", {"frequency": 1.7e3, "reset": onset})
  rim = osc1 + osc2 * envA # double envelope
  rim = vm.op("saturation", {"pre-gain": 3.0 * envA, "post-gain": 0.7})(rim)
  rim = vm.op("filterHP", {"frequency": 500, "resonance": 0.2})(rim)
  mix += rim

# snare
onset = onsets.read(2)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.5 * unitLength)})
with vm.when(envA > 1e-3):
  freq = 100 + 200 * unitPitch
  sin  = vm.op("oscSin", {"frequency": freq, "reset": onset})
  sin2  = vm.op("oscSin", {"frequency": 1.97 * freq, "reset": onset})
  env2 = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.1 * unitLength)})
  noiBP = vm.op("filterBP", {"frequency": 3.5e3, "resonance": 0.6, "reset": onset})(noise)
  mix += (sin + noiBP) * (envA * 0.5) + sin2 * (env2 * 0.5)

# clap
onset = onsets.read(3)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.3 * unitLength), "repeats": 2, "repeatTime": 0.028})
with vm.when(envA > 1e-3):
  noiBP = vm.op("filterBP", {"frequency": 1e3 + 1e3 * unitPitch, "resonance": 0.7, "reset": onset})(noise)
  mix += noiBP * envA

# hi-hats: 6 == closed, 8 == open, with shared mute state
oscCB = vm.op("oscAnalog", {"frequency": 565, "pulse amount": 1.0, "pulse width": 0.45, "color":0.8, "unison count": 2, "unison spread": 670})
oscHH = vm.op("oscAnalog", {"frequency": 205, "pulse amount": 1.0, "pulse width": 0.45, "color":0.8, "unison count": 4, "unison spread": 520})
sumHH = oscCB * 0.5 + oscHH + noise

ch_on, oh_on = onsets.read(6), onsets.read(8)
hatOpen = vm.value(0)
with vm.when(oh_on):
  hatOpen.write(1)
with vm.when(ch_on):
  hatOpen.write(0)

envA = vm.op("envExp", {"gate": ch_on + oh_on, "decay": (.01 + hatOpen + 0.1 * unitLength)})
with vm.when(envA > 1e-3):
  # Both hats use exactly the same noise source and filter; only decay differs.
  hhBP = vm.op("filterHP", {"frequency": 8e3, "resonance": 0.9, "reset": onset})(sumHH) 
  mix += hhBP * (envA * 0.2)

# cowbell
onset = onsets.read(7)
envA = vm.op("envExp", {"gate": onset, "decay": (.07 + 0.5 * unitLength)})
with vm.when(envA > 1e-3):
  env2 = vm.op("envExp", {"gate": onset, "decay": .02})
  bp = vm.op("filterBP", {"frequency": 1e3, "resonance": 0.7, "reset": onset})(oscCB) 
  mix += bp * (envA * 0.1  + env2 * 0.2)

vm.op("audioOut", mix)

