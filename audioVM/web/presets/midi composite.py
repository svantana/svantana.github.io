from audioVM import VM

vm = VM()
arp = vm.add("midi/arpeggiator.py", name="arp")
synth = vm.add("acid bass.py", name="synth", midi={"midi": arp.midi_outputs["output"]})
vm.op("audioOut", synth)
