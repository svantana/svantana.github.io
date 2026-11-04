import audioVM

vm = audioVM.VM()
midi = vm.midi()

unitCutoff = vm.control("cutoff", 0.5)
unitRez = vm.control("resonance", 0.5)

subOsc = vm.op("oscSin", {"note": midi.note - 7})
osc = vm.op("oscSaw", {"note": midi.note})

envAmp    = vm.op("envADSR", {"gate":midi.gate, "a":0.01, "d":0.3, "s":0.5, "r":0.3})
envFilter = vm.op("envADSR", {"gate":midi.gate, "a":0.1, "d":1.5, "s":0.0, "r":0.5})

oscMix = (osc + subOsc) * 0.2

filterHz = (0.5 + envFilter) * (unitCutoff * 3e3 + 100)

filter = vm.op("filterLP", {"frequency": filterHz, "resonance": unitRez})(oscMix)
envOut = filter * envAmp

vm.op("audioOut", envOut)