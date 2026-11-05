import audioVM

vm = audioVM.VM()

x = vm.op("audioIn")
vm.op("audioOut", x)
