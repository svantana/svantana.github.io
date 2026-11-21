import audioVM

vm = audioVM.VM()

numDelays = 4

pMix = vm.control("mix", 0.75)
pSize = vm.control("size", 0.8, range=[0.1, 1.0])
pDecay = vm.control("decay", 0.5)
pDamp = vm.control("damping", 0.25)

input = vm.op("audioIn")

damp = 4*(1.1-pDecay)**2
dampHi = damp + 4 * pDamp

delays = []
for n in range(numDelays):
  tBase, tExpo = 0.03, 1.54
  t = pSize * (tBase * tExpo ** n)
  delays += [vm.delay({"time": t, "cutoff": 2e3, "damp": damp, "damp hi": dampHi, "max": (1.01 * tBase * tExpo ** (numDelays-1) + .01)})]

# Mix taps
tap_sum = sum(delays[1:], delays[0]) # hack to avoid adding to zero
in_scaled = input * (0.25 - 0.2 * pDecay) # compensate for feedback buildup
sum_feedback = in_scaled + tap_sum * (2 / numDelays) # Householder factor

for n in range(numDelays):
    feedback = delays[(n+1) % numDelays] - sum_feedback
    delays[n].write(feedback)

wet = tap_sum * pMix
vm.op("audioOut", wet + input * (1 - pMix))
