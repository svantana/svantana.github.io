import audioVM

vm = audioVM.VM()
midi = vm.midi()

color = vm.control("color", 0.5)
width = vm.control("width", 0.35)
stereo = vm.control("stereo", 0.8)
cutoff = vm.control("cutoff", 0.45)
resonance = vm.control("resonance", 0.2) * 0.9
release = 0.2

left_mix = vm.op("buffer")
right_mix = vm.op("buffer")
with vm.poly(count=8, timeOut=release):
    env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.4, "s": 0.5, "r": release})
    osc = vm.op("oscAnalog", {"note": midi.note, "color": color, "unison count": 4, "unison spread": width ** 3 * 120, "stereo": stereo})
    frequency = (cutoff + env * 0.4) ** 4 * 5000 + 100
    left = vm.op("filterLP", {"input": osc[0], "frequency": frequency, "resonance": resonance})
    right = vm.op("filterLP", {"input": osc[1], "frequency": frequency, "resonance": resonance})
    left_mix += left * (env * 0.15)
    right_mix += right * (env * 0.15)

vm.op("audioOut", vm.op("channels", [left_mix, right_mix]))
