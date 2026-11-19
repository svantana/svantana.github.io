import audioVM

vm = audioVM.VM()
midi = vm.midi()

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.3)

mix = vm.op("buffer")
noise = vm.op("oscNoise")
onsets = vm.array(size=12)

# store note-ons in an array to enable simultaneous hits
with vm.poly(count=12):
  with vm.when(midi.onset):
    onsets.write(midi.note % 12, midi.onset)

# kick
onset = onsets.read(0)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + unitLength)})
with vm.when(envA > 1e-3):
  envF = vm.op("envExp", {"gate": onset, "decay": 0.05})
  sin  = vm.op("oscShape", {"frequency": (80 + 20 * envF) * (.5 + unitPitch), "shape":0.3*envA, "reset": onset})
  mix += vm.op("vca", {"gain": envA * 0.7, "reset": onset})(sin)

# snare
onset = onsets.read(2)
envA = vm.op("envExp", {"gate": onset, "decay": (.01 + unitLength)})
with vm.when(envA > 1e-3):
  osc = vm.op("oscShape", {"frequency": 180 * (.5 + unitPitch), "shape":0.5*envA, "reset": onset})
  noi = vm.op("oscNoise");
  mix += vm.op("vca", {"gain": envA * 0.7, "reset": onset})(osc + noi)

vm.op("audioOut", mix)