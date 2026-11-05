import audioVM

vm = audioVM.VM()
midi = vm.midi()

pCutoff = vm.control("cutoff", 0.5)
pRez = vm.control("resonance", 0.5)
pEnv = vm.control("filter envelope", 0.7)

osc = vm.op("oscSaw", {"note": midi.note})

envA = vm.op("envADSR", {"gate":midi.gate, "a":0.01, "d":2, "s":0, "r":0.1})
envF = vm.op("envADSR", {"gate":midi.gate, "a":0, "d":0.5, "s":0.0, "r":0.1})

filterHz = (0.5 + 0.5 * pCutoff + (pEnv - 0.5) * envF) ** 4

filter = vm.op("filterLP", {"frequency": filterHz, "resonance": pRez})(osc)
out = filter * envA

vm.op("audioOut", out)
