import audioVM

vm = audioVM.VM()

pThres = vm.control("threshold", 0.5, range=[0.001, 1])
pAttack = vm.control("attack ms", 5, range=[0,100]) * 1e-3
pRelease = vm.control("release ms", 30, range=[0,200]) * 1e-3
# pRatio = vm.control("ratio", 0.5) # work in progress!

x = vm.op("audioIn")

env = vm.op("envFollow", input={"attack": pAttack, "release": pRelease})(x)

envScale = 1 / (0.2 + pThres)
x = x / (pThres + env * envScale)

vm.op("audioOut", x)
