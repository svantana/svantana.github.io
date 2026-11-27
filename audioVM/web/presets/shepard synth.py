import audioVM

vm = audioVM.VM()
midi = vm.midi()

pCenter = vm.control("center", 0.5) * 48 + (2*12+9)
pColor = vm.control("color", 0.5)
pCut = vm.control("cutoff", 0.35)
pRez = vm.control("resonance", 0.2)
release = 0.2

mix = vm.op("buffer")
with vm.poly(count=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.5, "s": 0.4, "r": release})
  osc = vm.op("buffer")
  noteC = (midi.note - pCenter) % 12
  for n in range(3):
    d = noteC + (12*(n-1))
    gain = vm.max(0.0, 1 - (d * (2/(12*3)))**2)**2
    osc += vm.op("oscAnalog", {"note": pCenter+d, "color": pColor}) * gain
  filterHz = (pCut + env) ** 4 * 5000
  filtered = vm.op("filterLP", {"frequency": filterHz, "Q": .3+4*pRez})(osc)
  mix += filtered * (env * 0.1)

vm.op("audioOut", mix)
