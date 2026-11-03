import audioVM

vm = audioVM.VM()
midi = vm.midi()
noteOct = midi.note % 12

unitPitch = vm.control("pitch", 0.3)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
for n in range(4):
  gate = midi.gate * (noteOct==n)
  envF = vm.op("envExp", {"gate": gate, "decay": 0.02})
  envA = vm.op("envExp", {"gate": gate, "decay": (.01 + unitLength) * (2**-n)})
  sin  = vm.op("oscSin", {"note": (24 + n * 15) + 36 * unitPitch + 7 * envF})
  mix += sin * envA

# clap
envA = vm.op("envExp", {"gate": midi.gate * (noteOct==4), "decay": (.01 + 0.3 * unitLength), "repeats": 3, "repeatTime": 0.033})
noi = vm.op("oscNoise")
noiBP = vm.op("filterBP", {"input":noi, "frequency": 2.5e3, "resonance": 0.68})
mix += noiBP * envA

vm.op("audioOut", mix)