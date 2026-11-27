import audioVM

vm = audioVM.VM()
midi = vm.midi()

pUnison = vm.control("unison", 8, range=[1,8])
pWidth = vm.control("width", 0.15)
pColor = vm.control("color", 0.8)
pCut = vm.control("cutoff", 0.5)
pRez = vm.control("resonance", 0.25)
release = 0.1

mix = vm.op("buffer")
uniCents = pWidth ** 3 * 1200;

with vm.poly(count=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.5, "s": 0.5, "r": release})
  osc = vm.op("oscAnalog", {"note": midi.note, "color": pColor, "unison count": pUnison, "unison spread": uniCents})
  filterHz = (pCut + env) ** 4 * 5000
  filtered = vm.op("filterLP", {"frequency": filterHz, "Q": .3+4*pRez})(osc)
  mix += filtered * (env * 0.2)

vm.op("audioOut", mix)