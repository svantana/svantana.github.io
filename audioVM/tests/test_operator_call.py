import sys
import json
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from audioVM import VM


class OperatorCallTests(unittest.TestCase):
    def test_set_emits_top_level_json_properties(self):
        vm = VM()
        vm.set("blocksize", 16)
        vm.set({"name": "test", "enabled": True})

        program = json.loads(vm.get_json())
        self.assertEqual(program["blocksize"], 16)
        self.assertEqual(program["name"], "test")
        self.assertTrue(program["enabled"])

    def test_set_rejects_invalid_key_form(self):
        vm = VM()
        with self.assertRaisesRegex(TypeError, "string key or a dictionary"):
            vm.set([("blocksize", 16)])

    def test_call_sets_mapping_input_and_returns_operator(self):
        vm = VM()
        osc = vm.op("oscSaw", {"note": 60})

        filtered = vm.op("filterLP", {"frequency": 1e3, "resonance": 0.5})(osc)

        self.assertEqual(filtered.input, {
            "frequency": 1e3,
            "resonance": 0.5,
            "input": "oscSaw1",
        })
        self.assertIn('"input": {"frequency": 1000.0, "resonance": 0.5, "input": "oscSaw1"}', vm.get_json())

    def test_call_rejects_an_existing_input(self):
        vm = VM()
        node = vm.op("filterLP", {"input": "osc", "frequency": 1e3})

        with self.assertRaisesRegex(TypeError, "already has an input"):
            node("anotherOsc")

    def test_call_places_a_computed_input_before_its_consumer(self):
        vm = VM()
        osc1 = vm.op("oscSaw", {"note": 60})
        osc2 = vm.op("oscSaw", {"note": 67})

        filtered = vm.op("filterLP", {"frequency": 1e3})(osc1 + osc2)

        self.assertEqual([op.type for op in vm.program], [
            "oscSaw", "oscSaw", "add", "filterLP",
        ])
        self.assertEqual(filtered.input["input"], "add1")

    def test_delay_emits_linked_read_and_write_endpoints(self):
        vm = VM()
        string = vm.delay(0.1, id="string")
        tap = string.read(0.01)
        string.write(tap)

        self.assertEqual(tap.type, "delayRead")
        self.assertEqual(vm.program[-1].type, "delayWrite")
        self.assertEqual(vm.program[-1].input["delay"], "string.read")
        self.assertIn('"type": "delayRead", "id": "string.read"', vm.get_json())

    def test_delay_requires_read_before_write(self):
        vm = VM()
        string = vm.delay(0.1)
        with self.assertRaisesRegex(TypeError, "read"):
            string.write(0)

    def test_when_emits_conditional_range(self):
        vm = VM()
        midi = vm.midi()
        with vm.if_(midi.gate):
            vm.op("oscSaw", {"note": midi.note})

        self.assertEqual([op.type for op in vm.program], ["ifStart", "oscSaw", "ifEnd"])
        self.assertEqual(vm.program[0].input, {"condition": "midi.gate", "timeOut": 0})
        self.assertIn('"type": "ifStart"', vm.get_json())
        self.assertIn('"type": "ifEnd"', vm.get_json())

    def test_transport_exposes_implicit_sources(self):
        vm = VM()
        transport = vm.transport()
        vm.op("oscSaw", {"note": transport.bpm + transport.beatPos})

        self.assertEqual(transport.play.name, "transport.play")
        self.assertEqual(transport.loopBeats.name, "transport.loopBeats")
        self.assertIn('"transport.bpm"', vm.get_json())
        self.assertIn('"transport.beatPos"', vm.get_json())

    def test_named_midi_buffer_is_a_program_source(self):
        vm = VM()
        arp = vm.midi("arp")
        vm.op("oscSaw", {"note": arp.note})

        self.assertEqual(vm.program[0].type, "midiBuffer")
        self.assertEqual(vm.program[0].name, "arp")
        self.assertIn('"arp.note"', vm.get_json())


if __name__ == "__main__":
    unittest.main()
