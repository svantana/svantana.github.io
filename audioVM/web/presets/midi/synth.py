import audioVM

vm = audioVM.VM()
midi = vm.midi("input")

env = vm.op("envADSR", {"gate": midi.gate, "a": 0.005, "d": 0.15, "s": 0.4, "r": 0.08})
osc = vm.op("oscAnalog", {"note": midi.note, "color": 0.45})
vm.op("audioOut", osc * (env * 0.2))
