import audioVM

vm = audioVM.VM()
midi = vm.midi()

cutoff = vm.control("cutoff", 0.35)
resonance = vm.control("resonance", 0.2)
release = 0.5

mix = vm.op("buffer")
with vm.poly(numVoices=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.5, "s": 0.4, "r": release})
  osc = vm.op("oscSaw", {"note": midi.note})
  filterHz = (cutoff + env) ** 4 * 5000
  filtered = vm.op("filterLP", {"input": osc, "frequency": filterHz, "resonance": resonance})
  mix += filtered * (env * 0.2)

vm.op("audioOut", mix)
