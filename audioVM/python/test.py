import audioVM
vm = audioVM.VM()
midi = vm.midi()
osc = vm.op("oscAnalog", {"note": midi.note})
vm.op("audioOut", osc)

json_str = vm.get_json()
print("program graph as json:")
print(json_str)
print(json_str.replace('\n','').replace('\t','').replace('"','\\"'))
# check that json parses
import json
json_obj = json.loads(json_str)