import audioVM

# Example patch
vm = audioVM.VM()
midi = vm.midi()
noteOct = midi.note % 12

unitPitch = vm.control("pitch", 0.5)
unitLength = vm.control("length", 0.5)

mix = vm.op("buffer")
for n in range(4):
  sin = vm.op("oscSin", {"note": (12 + n * 10) + 24 * unitPitch})
  env = vm.op("envExp", {"gate": midi.gate * (noteOct==n), "decay": (.1 + unitLength) / (1 + n)})
  mix += sin * env

vm.op("audioOut", mix)

json_str = vm.get_json()
print("program graph as json:")
print(json_str)
print(json_str.replace('\n','').replace('\t','').replace('"','\\"'))
# check that json parses
import json
json_obj = json.loads(json_str)