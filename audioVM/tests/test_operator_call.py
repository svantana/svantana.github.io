import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from audioVM import VM


class OperatorCallTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
