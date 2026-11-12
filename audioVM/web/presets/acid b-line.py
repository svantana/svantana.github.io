import audioVM

vm = audioVM.VM()
midi = vm.midi()

pWave = vm.control("wave", 0.5)
pCutoff = vm.control("cutoff", 0.5)
pRez = vm.control("resonance", 0.5)
pEnv = vm.control("filter envelope", 0.7)
pEnvT = vm.control("decay", 0.5)
pGlide = vm.control("glide", 0.1)

envA = vm.op("envExp", {"gate":midi.onset, "decay": 0.1 + 2.0 * midi.gate})
envF = vm.op("envExp", {"gate":midi.gate, "decay": pEnvT})

prevGate = vm.value(0) # use previous gate to determine glide
notOnset = midi.gate <= prevGate
prevGate.write(midi.gate)
glideNote = vm.op("smoother", {"time": pGlide * notOnset})(midi.note)
osc = vm.op("oscAnalog", {"note": glideNote, "pulse amount": pWave*2-1, "pulse width": 0.45})

filterHz = (0.2 + 0.8 * pCutoff + (pEnv - 0.5) * envF) ** 4 * 5e3

filter = vm.op("filterLP", {"frequency": filterHz, "resonance": pRez})(osc)

gain = envA * (0.5 - 0.3 * pRez)
out = filter * gain

vm.op("audioOut", out)
