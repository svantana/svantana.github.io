import audioVM

vm = audioVM.VM()
midi = vm.midi()
vm.set("blocksize", 32)

pSustain  = vm.control("sustain", 0.9)
pDamp     = vm.control("damping", 0.2)
pPluck = vm.control("pluck", 0.5)
pPos = vm.control("position", 0.2)
pFriction = vm.control("friction", 0.5)

dampFreq = 20e3 * (1 - pDamp)
mix = vm.op("buffer")
with vm.poly(count=8, timeOut=None) as voice:
  fbGain = .8 + 0.1 * pSustain + 0.1 * midi.gate 
  lag = (2**(69/12)/440) * (2**(-1/12)) ** midi.note
  delay = vm.delay({"time": lag, "gain": fbGain, "cutoff": dampFreq, "max": 0.1})
  
  sum = vm.op("buffer")
  sum += delay
  with vm.when(midi.onset, timeOut=0.1):
    envA = vm.op("envExp", {"gate": midi.onset, "decay": 0.01 + 0.1 * pFriction}) / (2 + 4 * pFriction)
    excite = vm.op("oscNoise") * (envA * (1 - pPluck)) + pPluck * envA
    sum += excite
    sum -= vm.op("delay", {"time": lag * (.05 + .9 * pPos), "max": 0.1})(excite)
    
  delay.write(sum)
  mix += sum
  ms = vm.op("meanSquare")(sum)
  voice.kill(ms < 1e-8, timeOut=0.10)

vm.op("audioOut", mix)
