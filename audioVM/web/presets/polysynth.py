import audioVM


vm = audioVM.VM()
midi = vm.midi()

# Global operators run once per block. The buffer is deliberately declared
# before polyStart so every voice can add into it.
mix = vm.op("buffer")
cutoff = vm.control("cutoff", 0.35)
resonance = vm.control("resonance", 0.2)
release = 0.5

vm.op("polyStart", {"numVoices": 8, "timeOut": release})

# Everything in this range has separate state for every active MIDI note.
env = vm.op("envADSR", {"gate": midi.gate, "a": 0.015, "d": 0.25, "s": 0.65, "r": release})
osc = vm.op("oscSaw", {"note": midi.note})
mix += osc * (env * 0.2)

vm.op("polyEnd")

# This filter sees the sum of all active voices.
filterHz = cutoff ** 3 * 7000 + 120
filtered = vm.op("filterLP", {"input": mix, "frequency": filterHz, "resonance": resonance})

vm.op("audioOut", filtered)
