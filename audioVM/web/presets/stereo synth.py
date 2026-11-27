import audioVM

vm = audioVM.VM()
midi = vm.midi()

color = vm.control("color", 0.5)
width = vm.control("width", 0.35)
stereo = vm.control("stereo", 0.8)
cutoff = vm.control("cutoff", 0.45)
pRez = vm.control("resonance", 0.2)
release = 0.2

mix = vm.op("buffer", {"channels": 2})
with vm.poly(count=8, timeOut=release):
    env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.4, "s": 0.5, "r": release})
    osc = vm.op("oscAnalog", {"note": midi.note, "color": color, "unison count": 4, "unison spread": width ** 3 * 120, "stereo": stereo})
    frequency = (cutoff + env * 0.4) ** 4 * 5000 + 100
    filtered = vm.op("filterLP", {"input": osc, "frequency": frequency, "Q": .3+4*pRez})
    mix += filtered * (env * 0.15)

vm.op("audioOut", mix)
