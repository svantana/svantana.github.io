from audioVM import VM

vm = VM()
midi = vm.midi("input")
output = vm.midi("output")
transport = vm.transport()

# Follow the host position while playing. When stopped, the ramp continues
# at the host tempo and restarts when a note arrives after an idle period.
previous_gate = vm.value(0)
restart = (transport.play == 0) * (previous_gate == 0) * midi.onset
previous_gate.write(midi.gate)
beat = vm.op("lfoRamp", {"rate": transport.bpm / 60,
                         "reset": vm.max(transport.play, restart),
                         "reset phase": (transport.beatPos % 1) * transport.play})
step = (beat * 4).floor() % 4
interval = (step == 0) * 0 + (step == 1) * 4 + (step == 2) * 7 + (step == 3) * 12
phase = (beat * 4) % 1
gate = midi.gate * (phase < 0.55)

vm.midi_out(output, gate, midi.note + interval, midi.velocity)
