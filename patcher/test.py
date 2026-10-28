import pypatch

# Example patch
dsp = pypatch.Graph()

osc = dsp.osc_saw(110)
env = dsp.adsr(0.01, 0.2, 0.7, 0.4)
bass = dsp.osc_saw(55)

# Operators record graph instructions.
sound = osc * env + bass * 0.5

lfo = dsp.osc_sine(1)
cutoff = 200 + lfo * 1000

filtered = dsp.lpf(sound, cutoff, 0.8)

dsp.output(filtered)
dsp.print_instructions()