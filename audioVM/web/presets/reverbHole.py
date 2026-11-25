import audioVM

vm = audioVM.VM()

# A long feedback loop around a chain of different allpass delays gives the
# reverb a dense, slowly evolving tail. Times are in seconds.
K = 24
pWet = vm.control("mix", 0.9, range=[0, 1])
size = vm.control("size", 0.5, range=[0.1, 1])
pGrav = vm.control("gravity", 0.618, range=[.5,.99])
pRate = vm.control("mod rate", 0.2, range=[0.01, 2])
pDepth = vm.control("mod depth", 0, range=[0, 5]) * 1e-3

dry = vm.op("audioIn")
signal = dry
phase = vm.op("lfoRamp", {"rate": pRate})

for n in range(K):
    tMax = .025 * (1 + n*(n+1) / (K*(K+1)))
    tMod = vm.op("unitSin")(phase + n / K) * pDepth
    signal = vm.op("allpass", {"time": tMax * size + tMod, "max": tMax+0.01, "coefficient": pGrav})(signal)

vm.op("audioOut", dry * (1 - pWet) + signal * pWet)
