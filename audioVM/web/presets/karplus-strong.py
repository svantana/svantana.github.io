import audioVM

vm = audioVM.VM()
midi = vm.midi()
vm.set("blocksize", 32)

pSustain  = vm.control("sustain", 0.8)
pDamp     = vm.control("damping", 0.2)
pPluck = vm.control("pluck", 0.5)
pPos = vm.control("position", 0.2)
pFriction = vm.control("friction", 0.5)

mix = vm.op("buffer")
with vm.poly(count=8, timeOut=1.0):
  # The block-sized lower bound is deliberate: this VM's feedback pairs are
  # block scheduled, rather than sample scheduled.
  string = vm.delay(maxSeconds=0.1, id="string")
  lag = (2**(69/12)/440) * (2**(-1/12)) ** midi.note
  fbGain = .9 + 0.02 * midi.gate + .08 * pSustain
  fb = string.read(lag) * fbGain
  fb = vm.op("filterLP", {"frequency": 1e3 + midi.gate * (9e3 * (1-pDamp)), "resonance": 0})(fb)
  sum = vm.op("buffer")  
  sum += fb
  with vm.when(midi.onset, timeOut=0.1):
    envA = vm.op("envExp", {"gate": midi.onset, "decay": 0.01 + 0.1 * pFriction}) / (5 + 10 * pFriction)
    excite = vm.op("oscNoise") * envA
    sum += excite
    sum -= vm.op("delay", {"time": lag * (.05 + .9 * pPos), "maxSeconds": 0.1})(excite)
    
  string.write(sum)
  mix += sum

vm.op("audioOut", mix)
