import sys
import json
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from audioVM import VM


class OperatorCallTests(unittest.TestCase):
    def test_first_audio_input_defaults_to_input(self):
        vm = VM()
        self.assertEqual(vm.op('audioIn').name, 'input')
        self.assertEqual(vm.op('audioIn').name, 'audioIn2')

    def test_included_effect_can_be_called_with_signal(self):
        vm = VM()
        source = vm.op('oscAnalog', {'note': 60})
        path = str(Path(__file__).resolve().parents[1] / 'web/presets/pass-thru.py')
        effect = vm.add(path)
        result = effect(source)
        self.assertEqual(result.name, source.name)
        self.assertEqual(effect.outputs['audioOut1'].name, source.name)
        with self.assertRaisesRegex(TypeError, 'already has an input'):
            effect(source)

    def test_include_defaults_to_filename_and_rejects_duplicate_instance(self):
        vm = VM()
        path = str(Path(__file__).resolve().parents[1] / "web/presets/acid bass.py")
        patch = vm.add(path)
        self.assertIn("audio", patch.outputs)
        self.assertEqual((patch * 0.5).input[0], patch.outputs['audio'].name)
        self.assertTrue(any(op.name == "acid bass.wave" for op in vm.program))
        with self.assertRaisesRegex(ValueError, "conflicts"):
            vm.add(path)

    def test_extrema_emit_dsp_nodes_for_controls_and_literals(self):
        vm = VM()
        value = vm.control("value", -0.5)

        lower_bounded = vm.max(value, 0.0)
        upper_bounded = vm.min(lower_bounded, 1.0)

        self.assertEqual(lower_bounded.type, "max")
        self.assertEqual(upper_bounded.type, "min")
        self.assertEqual([op.type for op in vm.program], [
            "control", "const", "max", "const", "min",
        ])
        self.assertEqual(lower_bounded.input, ["value", "const1"])
        self.assertEqual(upper_bounded.input, ["max1", "const2"])

    def test_control_emits_its_requested_range(self):
        vm = VM()
        control = vm.control("frequency", 440, range=[20, 20000])

        self.assertEqual(control.range, [20, 20000])
        program = json.loads(vm.get_json())
        self.assertEqual(program["program"][0], {
            "type": "control", "id": "frequency", "input": 440,
            "range": [20, 20000],
        })

    def test_control_defaults_to_normalized_range(self):
        vm = VM()
        control = vm.control("gain")

        self.assertEqual(control.range, [0, 1])

    def test_control_rejects_invalid_ranges(self):
        vm = VM()
        for invalid_range in ((0, 1), [0], [0, 1, 2], [0, "1"], [1, 1], [2, 1]):
            with self.subTest(range=invalid_range):
                with self.assertRaises((TypeError, ValueError)):
                    vm.control("gain", range=invalid_range)

    def test_unused_comparison_warns_about_builtin_extrema(self):
        vm = VM()
        value = vm.control("value", 0)

        self.assertEqual(max(value, 0.0), 0.0)
        with self.assertWarnsRegex(
            RuntimeWarning,
            r"unused operation 'less1'.*vm\.max\(\.\.\.\).*vm\.min\(\.\.\.\)",
        ):
            vm.get_json()

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
        osc = vm.op("oscAnalog", {"note": 60})

        filtered = vm.op("filterLP", {"frequency": 1e3, "resonance": 0.5})(osc)

        self.assertEqual(filtered.input, {
            "frequency": 1e3,
            "resonance": 0.5,
            "input": "oscAnalog1",
        })
        self.assertIn('"input": {"frequency": 1000.0, "resonance": 0.5, "input": "oscAnalog1"}', vm.get_json())

    def test_call_sets_positional_input_on_an_unconfigured_operator(self):
        vm = VM()
        phase = vm.op("oscAnalog", {"frequency": 1})

        lfo = vm.op("unitSin")(phase)

        self.assertEqual(lfo.input, "oscAnalog1")
        self.assertEqual([op.type for op in vm.program], ["oscAnalog", "unitSin"])
        self.assertIn('"type": "unitSin", "id": "unitSin1", "input": "oscAnalog1"', vm.get_json())

    def test_call_rejects_a_second_positional_input(self):
        vm = VM()
        node = vm.op("unitSin")(vm.op("oscAnalog", {"frequency": 1}))

        with self.assertRaisesRegex(TypeError, "already has an input"):
            node("anotherOsc")

    def test_call_rejects_an_existing_input(self):
        vm = VM()
        node = vm.op("filterLP", {"input": "osc", "frequency": 1e3})

        with self.assertRaisesRegex(TypeError, "already has an input"):
            node("anotherOsc")

    def test_call_places_a_computed_input_before_its_consumer(self):
        vm = VM()
        osc1 = vm.op("oscAnalog", {"note": 60})
        osc2 = vm.op("oscAnalog", {"note": 67})

        filtered = vm.op("filterLP", {"frequency": 1e3})(osc1 + osc2)

        self.assertEqual([op.type for op in vm.program], [
            "oscAnalog", "oscAnalog", "add", "filterLP",
        ])
        self.assertEqual(filtered.input["input"], "add1")

    def test_delay_emits_linked_read_and_write_endpoints(self):
        vm = VM()
        string = vm.delay({"time": 0.01, "max": 0.1}, id="string")
        string.write(string)

        self.assertEqual(string.type, "delay")
        self.assertEqual(vm.program[-1].type, "delayWrite")
        self.assertEqual(vm.program[-1].input["delay"], "string")
        self.assertIn('"type": "delay", "id": "string"', vm.get_json())

    def test_delay_accepts_mapping_inputs_like_other_operators(self):
        vm = VM()
        source = vm.op("oscAnalog", {"note": 60})
        delayed = vm.delay({"input": source, "time": 0.01, "max": 0.1})

        self.assertEqual(delayed.input, {
            "input": "oscAnalog1",
            "time": 0.01,
            "max": 0.1,
        })

    def test_delay_requires_a_dictionary_of_inputs(self):
        vm = VM()
        with self.assertRaisesRegex(TypeError, "dictionary"):
            vm.delay(0.1)

    def test_when_emits_conditional_range(self):
        vm = VM()
        midi = vm.midi()
        with vm.if_(midi.gate):
            vm.op("oscAnalog", {"note": midi.note})

        self.assertEqual([op.type for op in vm.program], ["ifStart", "oscAnalog", "ifEnd"])
        self.assertEqual(vm.program[0].input, {"condition": "midi.gate", "timeOut": 0})
        self.assertIn('"type": "ifStart"', vm.get_json())
        self.assertIn('"type": "ifEnd"', vm.get_json())

    def test_transport_exposes_implicit_sources(self):
        vm = VM()
        transport = vm.transport()
        vm.op("oscAnalog", {"note": transport.bpm + transport.beatPos})

        self.assertEqual(transport.play.name, "transport.play")
        self.assertEqual(transport.loopBeats.name, "transport.loopBeats")
        self.assertIn('"transport.bpm"', vm.get_json())
        self.assertIn('"transport.beatPos"', vm.get_json())

    def test_named_midi_buffer_is_a_program_source(self):
        vm = VM()
        arp = vm.midi("arp")
        vm.op("oscAnalog", {"note": arp.note})

        self.assertEqual(vm.program[0].type, "midiBuffer")
        self.assertEqual(vm.program[0].name, "arp")
        self.assertIn('"arp.note"', vm.get_json())

    def test_composed_midi_defaults_to_global_and_can_override_implicit_input(self):
        vm = VM()
        presets = Path(__file__).resolve().parents[1] / 'web/presets'
        arp = vm.add(str(presets / 'midi/arpeggiator.py'), name='arp')
        vm.add(str(presets / 'acid bass.py'), name='bass',
               midi={'midi': arp.midi_outputs['output']})
        program = json.loads(vm.get_json())['program']
        text = json.dumps(program)
        self.assertIn('midi.gate', text)
        self.assertIn('arp.output.gate', text)
        self.assertNotIn('arp.input.gate', text)
        bass_env = next(op for op in program if op['id'] == 'bass.envADSR1')
        self.assertEqual(bass_env['input']['gate'], 'arp.output.gate')

    def test_value_reads_implicitly_in_expressions_and_writes_in_order(self):
        vm = VM()
        previous_gate = vm.value(0, id="prevMidiGate")
        before = previous_gate == 0
        previous_gate.write(1)
        after = previous_gate == 1

        self.assertEqual([op.type for op in vm.program], [
            "value", "value", "const", "equal", "valueWrite",
            "value", "const", "equal",
        ])
        self.assertEqual(vm.program[4].input, ["prevMidiGate", 1])
        self.assertIn('"type": "value", "id": "prevMidiGate", "input": 0', vm.get_json())
        self.assertEqual(before.type, "equal")
        self.assertEqual(after.type, "equal")


if __name__ == "__main__":
    unittest.main()
