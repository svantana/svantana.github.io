import audioVM

vm = audioVM.VM()
midi = vm.midi()

pUnison = vm.control("unison", 1.0)
pWidth = vm.control("width", 0.2)
pColor = vm.control("color", 0.5)
pCut = vm.control("cutoff", 0.5)
pRez = vm.control("resonance", 0.25) * 0.98
release = 0.1

mix = vm.op("buffer")
uniCents = pWidth ** 3 * 1200;
uniVoices = pUnison * 8;
with vm.poly(count=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.5, "s": 0.5, "r": release})
  osc = vm.op("oscAnalog", {"note": midi.note, "color": pColor, "unison count": uniVoices, "unison spread": uniCents})
  filterHz = (pCut + env) ** 4 * 5000
  filtered = vm.op("filterLP", {"frequency": filterHz, "resonance": pRez})(osc)
  mix += filtered * (env * 0.2)

vm.op("audioOut", mix)
