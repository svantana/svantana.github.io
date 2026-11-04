import audioVM

vm = audioVM.VM()
midi = vm.midi()

cutoff = vm.control("cutoff", 0.35)
resonance = vm.control("resonance", 0.2)
release = 0.5

mix = vm.op("buffer")
with vm.poly(numVoices=8, timeOut=release):
    env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.25, "s": 0.5, "r": release})
    osc = vm.op("oscSaw", {"note": midi.note})
    mix += osc * (env * 0.2)

# This filter sees the sum of all active voices.
filterHz = cutoff ** 3 * 7000 + 120
filtered = vm.op("filterLP", {"input": mix, "frequency": filterHz, "resonance": resonance})

vm.op("audioOut", filtered)
