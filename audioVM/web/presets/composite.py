import audioVM
vm = audioVM.VM()

bass = vm.add("acid bass.py", name="synth")
reverb = vm.add("reverbFDN.py")(bass)

vm.op("audioOut", reverb)
