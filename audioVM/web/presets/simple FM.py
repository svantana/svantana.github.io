import audioVM

vm = audioVM.VM()
midi = vm.midi()

pFM = vm.control("fm", 0.3)
pRatio = vm.control("ratio", 0.5)
release = 0.5

mix = vm.op("buffer")
with vm.poly(count=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.5, "s": 0.4, "r": release})
  op1 = vm.op("oscSin", {"note": midi.note + 48 * (pRatio - 0.5)}) * pFM
  op2 = vm.op("oscFM", {"note": midi.note})(op1)
  mix += op1 * (env * 0.2)

vm.op("audioOut", mix)
