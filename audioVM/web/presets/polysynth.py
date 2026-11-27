import audioVM

vm = audioVM.VM()
midi = vm.midi()

pColor = vm.control("color", 0.5)
pCut = vm.control("cutoff", 0.35)
pRez = vm.control("resonance", 0.2)
release = 0.2

mix = vm.op("buffer")
with vm.poly(count=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.5, "s": 0.4, "r": release})
  osc = vm.op("oscAnalog", {"note": midi.note, "color": pColor})
  filterHz = (pCut + env) ** 4 * 5000
  filtered = vm.op("filterLP", {"frequency": filterHz, "Q": 0.2 + 4 * pRez})(osc)
  mix += filtered * (env * 0.2)

vm.op("audioOut", mix)
