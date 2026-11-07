import audioVM

vm = audioVM.VM()
midi = vm.midi()

def snapToDiatonic(note, scale): # 5 is cMaj/Amin
  return (vm.op("round", note * (7/12)) * 12 + scale) // 7

pScale = vm.control("scale", 0)
pCut = vm.control("cutoff", 0.1)
pRez = vm.control("resonance", 0.7)

oscSum = vm.op("buffer")
for n in range(4):
  note = snapToDiatonic(midi.note + 3.2*n, 12*pScale + 5)
  oscSum += vm.op("oscSaw", {"note": note})

envAmp    = vm.op("envADSR", {"gate":midi.gate, "a":0.03, "d":0.3, "s":0.5, "r":0.3})
envFilter = vm.op("envADSR", {"gate":midi.gate, "a":0.08, "d":1.5, "s":0.0, "r":0.5})

cutHz = (pCut + envFilter)**4 * 5e3
filterOut = vm.op("filterLP", {"input": oscSum, "frequency": cutHz, "resonance": pRez})

envOut = filterOut * (envAmp * 0.2)
vm.op("audioOut", envOut)
