import pypatch

# Example patch
dsp = pypatch.Graph()
midi = dsp.midi()
osc = dsp.node("oscSaw", {"note": midi.note})
envAmp = dsp.node("envADSR", {"gate":midi.gate, "attack":0.01, "decay":0.2, "sustain":0.7, "release":0.4})

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

import json
json_obj = json.loads(json_str)