import audioVM

vm = audioVM.VM()
midi = vm.midi()

pCutoff = vm.control("cutoff", 0.1)
pRez = vm.control("resonance", 0.7)

oscSum = vm.op("buffer")
for n in [0,3,7,10]:
  oscSum += vm.op("oscAnalog", {"note": midi.note + n})

envA = vm.op("envADSR", {"gate":midi.gate, "a":0.03, "d":0.3, "s":0.5, "r":0.3})
envF = vm.op("envADSR", {"gate":midi.gate, "a":0.08, "d":1.5, "s":0.0, "r":0.5})

cutHz = vm.max(0.1, pCutoff + envF - 0.5)**4 * 6e3
out = vm.op("filterLP", {"frequency": cutHz, "resonance": pRez * 0.95})(oscSum)

out = out * (envA * (0.3 - 0.25 * pRez))
vm.op("audioOut", out)