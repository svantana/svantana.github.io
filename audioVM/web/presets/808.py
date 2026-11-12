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
  rim = vm.op("filterHP", {"frequency": 1e3, "resonance": 0.3})(rim)
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

# hihat
onset = onsets.read(6)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.1 * unitLength)})
with vm.when(envA > 1e-3):
  noiBP = vm.op("filterHP", {"frequency": 5e3 + 9e3 * unitPitch, "resonance": 0.3, "reset": onset})(noise)
  mix += noiBP * (envA * 0.3)

vm.op("audioOut", mix)

