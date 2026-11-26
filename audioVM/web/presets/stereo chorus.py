import audioVM

vm = audioVM.VM()

# A mono source feeds independent, oppositely phased delay lines.  Keeping
# each line mono makes the modulation visibly and audibly distinct per side.
mix = vm.control("mix", 0.5, range=[0, 1])
rate = vm.control("rate", 0.35, range=[0.05, 3])
depth = vm.control("depth", 0.5, range=[0, 1])

dry = vm.op("audioIn")
phase = vm.op("lfoRamp", {"rate": rate})

base_time = 0.018
modulation = depth * 0.008
channels = []
for channel in range(2):
    delay_time = base_time + vm.op("unitSin")(phase + channel * 0.375) * modulation
    delayed = vm.op("delay", {"input": dry, "time": delay_time, "max": 0.03})
    channels.append(dry * (1 - mix) + delayed * mix)

vm.op("audioOut", vm.op("channels", channels))
