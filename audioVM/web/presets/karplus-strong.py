import audioVM

vm = audioVM.VM()
midi = vm.midi()

pSustain = vm.control("sustain", 0.8)
pDamp    = vm.control("damping", 0.2)

mix = vm.op("buffer")
with vm.poly(count=8, timeOut=1.0):
    # The block-sized lower bound is deliberate: this VM's feedback pairs are
    # block scheduled, rather than sample scheduled.
    string = vm.delay(maxSeconds=0.1, id="string")
    lag = (2**(69/12)/440) * (2**(-1/12)) ** midi.note
    fbGain = .9 + 0.02 * midi.gate + .08 * pSustain
    fb = string.read(lag) * fbGain
    fb = vm.op("filterLP", {"frequency": 1e3 + midi.gate * (5e3 * (1-pDamp)), "resonance": 0})(fb)
    excite = vm.op("oscNoise") * vm.op("envExp", {"gate": midi.gate, "decay": 0.02})
    sum = fb + excite
    string.write(sum)
    mix += sum * 0.2

vm.op("audioOut", mix)
