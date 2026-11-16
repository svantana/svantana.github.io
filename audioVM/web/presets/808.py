import audioVM

vm = audioVM.VM()
midi = vm.midi()

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
noise = vm.op("oscNoise")
onsets = vm.array(size=12)

# Preserve each MIDI event that reaches this processing block.  The later
# instrument voices are deliberately monophonic: multiple hits for one note
# class become one onset slot and retrigger that instrument rather than stack.
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
envA = vm.op("envExp", {"gate": onset, "decay": .01})
with vm.when(envA > 1e-3):
  osc1 = vm.op("oscSin", {"frequency": 460, "reset": onset})
  osc2 = vm.op("oscSin", {"frequency": 1.7e3, "reset": onset})
  rim = osc1 + osc2 * envA # double envelope
  #rim = vm.op("filterHP", {"frequency": 1e3, "resonance": 0.3})(rim)
  mix += rim * (envA * 0.7)

# snare
onset = onsets.read(2)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.5 * unitLength)})
with vm.when(envA > 1e-3):
  sin  = vm.op("oscSin", {"frequency": 100 + 200 * unitPitch})
  noiBP = vm.op("filterBP", {"frequency": 3e3, "resonance": 0.5, "reset": onset})(noise)
  mix += (sin + noiBP) * (envA * 0.5)

# clap
onset = onsets.read(3)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.3 * unitLength), "repeats": 3, "repeatTime": 0.025})
with vm.when(envA > 1e-3):
  noiBP = vm.op("filterBP", {"frequency": 1e3 + 1e3 * unitPitch, "resonance": 0.7, "reset": onset})(noise)
  mix += noiBP * envA

# hi-hats: 6 == closed, 8 == open, with shared mute state
oscCB = vm.op("oscAnalog", {"frequency": 540, "pulse amount": 1, "color": 0.9, "unison count": 2, "unison spread": 700})
oscHH = vm.op("oscAnalog", {"frequency": 205, "pulse amount": 1, "color": 0.9, "unison count": 4, "unison spread": 520})
sumHH = oscCB + oscHH

ch_on, oh_on = onsets.read(6), onsets.read(8)
hh_on = ch_on + oh_on
hatOpen = vm.value(0)
with vm.when(oh_on):
  hatOpen.write(1)
with vm.when(ch_on):
  hatOpen.write(0)

envA = vm.op("envExp", {"gate": ch_on + oh_on, "decay": (.01 + hatOpen + 0.1 * unitLength)})
with vm.when(envA > 1e-3):
  # Both hats use exactly the same noise source and filter; only decay differs.
  hhBP = vm.op("filterHP", {"frequency": 10e3, "resonance": 0.6, "reset": onset})(sumHH) 
  mix += hhBP * (envA * 0.2)

# cowbell
onset = onsets.read(7)
envA = vm.op("envExp", {"gate": onset, "decay": (.02 + 0.3 * unitLength)})
with vm.when(envA > 1e-3):
  mix += oscCB * (envA * 0.2)

vm.op("audioOut", mix)

