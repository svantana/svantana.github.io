import audioVM

vm = audioVM.VM()
midi = vm.midi()
noteOct = midi.note % 12

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
noise = vm.op("oscNoise")

# bass
gate = midi.gate * (noteOct==0)
onset = gate * midi.onset
envF = vm.op("envExp", {"gate": gate, "decay": 0.05})
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + unitLength)})
sin  = vm.op("oscSin", {"frequency": (60 + 100 * envF) * (.5 + unitPitch), "reset": onset})
mix += sin * (envA * 0.7)

# rimshot
gate = midi.gate * (noteOct==1)
onset = gate * midi.onset
envA = vm.op("envExp", {"gate": gate, "decay": .01})
osc1 = vm.op("oscSin", {"frequency": 460, "reset": onset})
osc2 = vm.op("oscSin", {"frequency": 1.7e3, "reset": onset})
rim = osc1 + osc2 * envA # double envelope
rim = vm.op("filterHP", {"frequency": 1e3, "resonance": 0.3})(rim)
mix += rim * (envA * 0.7)

# snare
gate = midi.gate * (noteOct==2)
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + 0.5 * unitLength)})
sin  = vm.op("oscSin", {"frequency": 100 + 200 * unitPitch})
noiBP = vm.op("filterBP", {"frequency": 3e3, "resonance": 0.5})(noise)
mix += (sin + noiBP) * (envA * 0.5)

# clap
gate = midi.gate * (noteOct==3)
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + 0.3 * unitLength), "repeats": 3, "repeatTime": 0.025})
noiBP = vm.op("filterBP", {"frequency": 1e3 + 1e3 * unitPitch, "resonance": 0.7})(noise)
mix += noiBP * envA

# hihat
gate = midi.gate * (noteOct==6)
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + 0.1 * unitLength)})
noiBP = vm.op("filterHP", {"frequency": 5e3 + 9e3 * unitPitch, "resonance": 0.3})(noise)
mix += noiBP * (envA * 0.3)

vm.op("audioOut", mix)
