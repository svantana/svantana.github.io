import audioVM

# Example patch
vm = audioVM.VM()
midi = vm.midi()
#noteWrap = midi.note % 12
#gate0 = (noteWrap == 0)

subOsc = vm.op("oscSin", {"note": midi.note - 12})
osc = vm.op("oscSaw", {"note": midi.note})

envAmp    = vm.op("envADSR", {"gate":midi.gate, "a":0.01, "d":0.3, "s":0.5, "r":0.2})
envFilter = vm.op("envADSR", {"gate":midi.gate, "a":0.05, "d":1.5, "s":0.0, "r":0.3})

oscMix = (osc + subOsc * 0.5) * envAmp 

cutoffHz = 400 + envFilter * 2.5e3

filterOut = vm.op("filterLP", {"input": oscMix, "frequency": cutoffHz, "resonance": 0.5})

vm.op("audioOut", filterOut)

json_str = vm.get_json()
print("program graph as json:")
print(json_str)
print(json_str.replace('\n','').replace('\t','').replace('"','\\"'))
# check that json parses
import json
json_obj = json.loads(json_str)