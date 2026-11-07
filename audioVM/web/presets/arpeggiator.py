from audioVM import VM

vm = VM()
midi = vm.midi()
transport = vm.transport()

# Four 16th-note steps per beat. Hold a key on the piano keyboard, then start
# the transport in the panel to hear the pattern.
step = (transport.beatPos * 4).floor() % 4
interval = (step == 0) * 0 + (step == 1) * 4 + (step == 2) * 7 + (step == 3) * 12
note = midi.note + interval

# A short gate on each step retriggers the amplitude envelope.
phase = (transport.beatPos * 4) % 1
gate = midi.gate * (phase < 0.55)
env = vm.op("envADSR", {"gate": gate, "a": 0.005, "d": 0.09, "s": 0.35, "r": 0.04})
osc = vm.op("oscSaw", {"note": note})

vm.op("audioOut", osc * env * 0.22)
