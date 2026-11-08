import audioVM

vm = audioVM.VM()
midi = vm.midi()

pCutoff = vm.control("cutoff", 0.5)
pRez = vm.control("resonance", 0.5)
pEnv = vm.control("filter envelope", 0.7)
pGlide = vm.control("glide", 0.1)

envA = vm.op("envExp", {"gate":midi.onset, "decay": 0.1 + 2.0 * midi.gate})
envF = vm.op("envExp", {"gate":midi.gate, "decay": 0.5})

glideNote = vm.op("smoother", {"time": pGlide})(midi.note)
osc = vm.op("oscSaw", {"note": glideNote})

filterHz = (0.2 + 0.8 * pCutoff + (pEnv - 0.5) * envF) ** 4 * 5e3

filter = vm.op("filterLP", {"frequency": filterHz, "resonance": pRez})(osc)

gain = envA * (0.5 - 0.3 * pRez)
out = filter * gain

vm.op("audioOut", out)
