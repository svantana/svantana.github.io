import audioVM

vm = audioVM.VM()
midi = vm.midi()
#noteWrap = midi.note % 12
#gate0 = (noteWrap == 0)

unitCutoff = vm.control("cutoff", 0.1)
unitRez = vm.control("resonance", 0.7)

oscSum = vm.op("buffer")
for n in [0,3,7,10]:
  oscSum += vm.op("oscSaw", {"note": midi.note + n})

envAmp    = vm.op("envADSR", {"gate":midi.gate, "a":0.03, "d":0.3, "s":0.5, "r":0.3})
envFilter = vm.op("envADSR", {"gate":midi.gate, "a":0.08, "d":1.5, "s":0.0, "r":0.5})

filterHz = (unitCutoff + envFilter)**4 * 5e3
filterOut = vm.op("filterLP", {"input": oscSum, "frequency": filterHz, "resonance": unitRez})

envOut = filterOut * (envAmp * 0.2)
vm.op("audioOut", envOut)