import audioVM

vm = audioVM.VM()
midi = vm.midi()
noteOct = midi.note % 12

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")

# bass
gate = midi.gate * (noteOct==0)
envF = vm.op("envExp", {"gate": gate, "decay": 0.02})
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + unitLength)})
sin  = vm.op("oscSin", {"frequency": (50 + 100 * envF) * (.5 + unitPitch)})
mix += sin * envA

# snare
gate = midi.gate * (noteOct==2)
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + 0.5 * unitLength)})
sin  = vm.op("oscSin", {"frequency": 100 + 200 * unitPitch})
noi = vm.op("oscNoise")
noiBP = vm.op("filterBP", {"frequency": 3e3, "resonance": 0.5})(noi)
mix += (sin + noi) * (envA * 0.5)

# clap
gate = midi.gate * (noteOct==3)
envA = vm.op("envExp", {"gate": gate, "decay": (.01 + 0.3 * unitLength), "repeats": 3, "repeatTime": 0.025})
noi = vm.op("oscNoise")
noiBP = vm.op("filterBP", {"input":noi, "frequency": 1e3 + 1e3 * unitPitch, "resonance": 0.7})
mix += noiBP * envA

vm.op("audioOut", mix)
