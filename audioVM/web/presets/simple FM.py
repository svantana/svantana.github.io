import audioVM

vm = audioVM.VM()
midi = vm.midi()

pFM = vm.control("fm", 0)
pEnv = vm.control("fm env", 0.8)
pRatio = vm.control("ratio", 0.647)
release = 0.1

mix = vm.op("buffer")
with vm.poly(count=8, timeOut=release):
  env = vm.op("envADSR", {"gate": midi.gate, "a": 0.001, "d": 0.5, "s": 0.2, "r": release})
  op1 = vm.op("oscSin", {"note": midi.note + 48 * (pRatio - 0.5)})
  mod = op1 * (pFM + env * (pEnv - 0.5))
  op2 = vm.op("oscFM", {"note": midi.note})(mod)
  mix += op2 * (env * 0.2)

vm.op("audioOut", mix)
