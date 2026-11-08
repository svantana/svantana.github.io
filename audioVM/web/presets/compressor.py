import audioVM

vm = audioVM.VM()

pThres = vm.control("threshold", 0.5)
pAttack = vm.control("attack", 0.5)
pRelease = vm.control("release", 0.5)
pRatio = vm.control("ratio", 0.5)

x = vm.op("audioIn")

env = vm.op("envFollow", input={"attack": pAttack, "release": pRelease})(x)

envScale = 1 / (0.1 + pThres)
x = x / (pThres + env * envScale)

vm.op("audioOut", x)
