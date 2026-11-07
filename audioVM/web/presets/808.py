import audioVM

vm = audioVM.VM()
midi = vm.midi()
noteOct = midi.note % 12

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
noise = vm.op("oscNoise")

# bass
onset = midi.onset * (noteOct==0)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + unitLength)})
with vm.when(envA > 1e-3):
  envF = vm.op("envExp", {"gate": onset, "decay": 0.05})
  sin  = vm.op("oscSin", {"frequency": (60 + 100 * envF) * (.5 + unitPitch), "reset": onset})
  mix += sin * (envA * 0.7)

# rimshot
onset = midi.onset * (noteOct==1)
envA = vm.op("envExp", {"gate": onset, "decay": .01})
with vm.when(envA > 1e-3):
  osc1 = vm.op("oscSin", {"frequency": 460, "reset": onset})
  osc2 = vm.op("oscSin", {"frequency": 1.7e3, "reset": onset})
  rim = osc1 + osc2 * envA # double envelope
  rim = vm.op("filterHP", {"frequency": 1e3, "resonance": 0.3})(rim)
  mix += rim * (envA * 0.7)

# snare
onset = midi.onset * (noteOct==2)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.5 * unitLength)})
with vm.when(envA > 1e-3):
  sin  = vm.op("oscSin", {"frequency": 100 + 200 * unitPitch})
  noiBP = vm.op("filterBP", {"frequency": 3e3, "resonance": 0.5, "reset": onset})(noise)
  mix += (sin + noiBP) * (envA * 0.5)

# clap
onset = midi.onset * (noteOct==3)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.3 * unitLength), "repeats": 3, "repeatTime": 0.025})
with vm.when(envA > 1e-3):
  noiBP = vm.op("filterBP", {"frequency": 1e3 + 1e3 * unitPitch, "resonance": 0.7, "reset": onset})(noise)
  mix += noiBP * envA

# hihat
onset = midi.onset * (noteOct==6)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + 0.1 * unitLength)})
with vm.when(envA > 1e-3):
  noiBP = vm.op("filterHP", {"frequency": 5e3 + 9e3 * unitPitch, "resonance": 0.3, "reset": onset})(noise)
  mix += noiBP * (envA * 0.3)

vm.op("audioOut", mix)

