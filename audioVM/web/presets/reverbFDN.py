import audioVM

vm = audioVM.VM()

numDelays = 4 # global fixed setting

# UI-exposed control parameters
pWet = vm.control("mix", 0.5)
pSize = vm.control("size", 0.8, range=[0.1, 1.0])
pDecay = vm.control("decay", 0.5)
pDamp = vm.control("damping", 0)
pRate = vm.control("lfo rate", 0.6, range=[0.01, 5])
pDepth = vm.control("lfo depth", 2, range=[0,10])

damp = 4*(1.1-pDecay)**2
dampHi = damp + 4 * pDamp
dry = vm.op("audioIn")
in_scaled = dry * (0.25 - 0.2 * pDecay) # compensate for feedback buildup

lfoPhase = vm.op("lfoRamp", {"rate": pRate})
lfoScale = 1e-3 * pDepth
delays = []
for n in range(numDelays):
  tBase, tExpo = 0.03, 1.57
  lfo = vm.op("unitSin")(lfoPhase + n / numDelays)
  t = pSize * (tBase * tExpo ** n) + lfoScale * lfo
  delays += [vm.delay({"time": t, "cutoff": 2e3, "damp": damp, "damp hi": dampHi, "max": (1.01 * tBase * tExpo ** (numDelays-1) + .01)})]
  
# mix taps
tap_sum = sum(delays[1:], delays[0]) # hack to avoid adding to zero
sum_feedback = in_scaled + tap_sum * (2 / numDelays) # Householder factor

# write output
for n in range(numDelays):
  feedback = delays[(n+1) % numDelays] - sum_feedback
  delays[n].write(feedback)

wet = tap_sum * pWet
vm.op("audioOut", wet + dry * (1 - pWet * pWet), id="wet")
