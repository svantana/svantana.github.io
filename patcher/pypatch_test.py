import pypatch

# Example patch
dsp = pypatch.Graph()
midi = dsp.midi()
osc = dsp.node("oscSaw", {"note": midi.note})
envAmp = dsp.node("envADSR", {"gate":midi.gate, "a":0.01, "d":0.2, "s":0.7, "r":0.4})

bass = dsp.node("oscSin", {"note": 48})

# Operators record graph instructions.
sound = osc * envAmp + bass * 0.5

lfo = dsp.node("lfo", 1)
cutoffHz = 400 + lfo * 100

filtered = dsp.node("filterLP", {"input": sound, "frequency": cutoffHz})

dsp.node("audioOut", filtered)
json_str = dsp.get_json()

print("json:")
print(json_str)

# check that json parses
import json
json_obj = json.loads(json_str)