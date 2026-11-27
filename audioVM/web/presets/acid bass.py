import audioVM

vm = audioVM.VM()
midi = vm.midi()

pPulse = vm.control("pulse", 0, range=[-1,1])
pColor = vm.control("color", 0.5)
pCutoff = vm.control("cutoff", 0.5)
pRez = vm.control("resonance", 0.5)
pEnv = vm.control("filter envelope", 0.5, range=[-1,1])
pEnvT = vm.control("decay", 0.5, range=[0.01, 4])
pGlide = vm.control("glide", 0.1)
pDrive = vm.control("drive", 0.1, range=[0.1, 10])

envA = vm.op("envADSR", {"gate":midi.gate, "a": 0.01, "d": 4, "s":0.1, "r": 0.05})
envF = vm.op("envExp", {"gate":midi.gate, "decay": pEnvT})

prevGate = vm.value(0) # use previous gate to determine glide
notOnset = midi.gate <= prevGate
prevGate.write(midi.gate)
glideNote = vm.op("smoother", {"time": pGlide * notOnset})(midi.note)
osc = vm.op("oscAnalog", {"note": glideNote, "color":pColor, "pulse amount": pPulse, "pulse width": 0.45})

filterHz = (0.2 + 0.8 * pCutoff + pEnv * (envF - 0.3)) ** 4 * 12e3

filter = vm.op("filterLP", {"frequency": filterHz, "Q": 0.3 + pRez * 5})(osc)
filter = vm.op("filterLP", {"frequency": filterHz*3, "Q": 0.5})(filter)

gain = envA * (0.6 - 0.4 * pRez)
out = filter * gain
sat = vm.op("saturation", {"pre-gain": pDrive, "post-gain": .1 + 1 / pDrive})(out)
vm.op("audioOut", sat, id="audio")
