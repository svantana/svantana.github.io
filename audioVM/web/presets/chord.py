import audioVM

vm = audioVM.VM()
midi = vm.midi()
#noteWrap = midi.note % 12
#gate0 = (noteWrap == 0)

unitCutoff = vm.control("cutoff", 0.5)
unitRez = vm.control("resonance", 0.5)

oscMix = vm.op("oscSaw", {"note": midi.note})
for n in [3,7,10]:
  oscMix += vm.op("oscSaw", {"note": midi.note + n})
oscMix *= 0.2

envAmp    = vm.op("envADSR", {"gate":midi.gate, "a":0.01, "d":0.3, "s":0.5, "r":0.3})
envFilter = vm.op("envADSR", {"gate":midi.gate, "a":0.1, "d":1.5, "s":0.0, "r":0.5})

filterHz = (0.5 + envFilter) * (unitCutoff * 0.3 + 0.7)**5 * 12e3
filterOut = vm.op("filterLP", {"input": oscMix, "frequency": filterHz, "resonance": unitRez})

envOut = filterOut * envAmp
vm.op("audioOut", envOut)