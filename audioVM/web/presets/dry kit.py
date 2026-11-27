import audioVM

vm = audioVM.VM()
midi = vm.midi()
kit = midi.kit(["kick", "snare"])

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
noise = vm.op("oscNoise")
fScale = 0.5 + unitPitch

# kick
onset = kit.kick.onset
envA = vm.op("envADSR", {"gate": onset, "a": 0, "d": 0.3, "s":0, "r": 0.3})
with vm.when(envA > 1e-3):
  envF = vm.op("envExp", {"gate": onset, "decay": 0.03})
  sin  = vm.op("oscShape", {"frequency": (70 + 30 * envF) * fScale, "shape":0.3*envA*envA, "reset": onset})
  mix += vm.op("vca", {"gain": envA * 0.7, "reset": onset})(sin)

# snare
onset = kit.snare.onset
envA = vm.op("envADSR", {"gate": onset, "a": 0, "d": 0.2, "s":0, "r": 0.2})
with vm.when(envA > 1e-3):
  mod = vm.op("oscSin", {"frequency": 145 * fScale, "reset": onset})
  osc = vm.op("oscFM", {"frequency": 173 * fScale, "reset": onset})(mod * (envA * .2))
  noi = vm.op("oscNoise");
  noiF = vm.op("filterBP", {"frequency": 4e3, "Q": 2})(noi)
  envN = vm.op("envADSR", {"gate": envA>0.5, "a": 0.016, "d": 0.18, "s":0, "r": 0.18})
  mix += vm.op("vca", {"gain": envA * envA * 0.7, "reset": onset})(osc)
  mix += vm.op("vca", {"gain": envN * envN, "reset": onset})(noiF)

vm.op("audioOut", mix)
