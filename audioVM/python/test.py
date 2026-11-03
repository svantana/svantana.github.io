import audioVM

# Example patch
vm = audioVM.VM()
midi = vm.midi()
#noteWrap = midi.note % 12
#gate0 = (noteWrap == 0)

controlCutoff = vm.control("cutoff", 0.5)

subOsc = vm.op("oscSin", {"note": midi.note - 12})
osc = vm.op("oscSaw", {"note": midi.note})
oscMix = (osc + subOsc) * 0.2

envAmp    = vm.op("envADSR", {"gate":midi.gate, "a":0.01, "d":0.3, "s":0.5, "r":0.2})
envFilter = vm.op("envADSR", {"gate":midi.gate, "a":0.05, "d":1.5, "s":0.0, "r":0.3})

cutoffHz = (0.5 + envFilter) * (controlCutoff * 3e3 + 100)
filterOut = vm.op("filterLP", {"input": oscMix, "frequency": cutoffHz, "resonance": 0.5})
envOut = filterOut * envAmp

vm.op("audioOut", envOut)

json_str = vm.get_json()
print("program graph as json:")
print(json_str)
print(json_str.replace('\n','').replace('\t','').replace('"','\\"'))
# check that json parses
import json
json_obj = json.loads(json_str)