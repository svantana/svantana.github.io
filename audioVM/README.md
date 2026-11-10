# AudioVM

AudioVM is a small DSP graph runtime. Write patches in Python; run them natively or in the browser via WebAssembly.

```python
import audioVM

vm = audioVM.VM()
osc = vm.op("oscSaw", {"note": 60})
vm.op("audioOut", osc * 0.1)
```

The browser player lives in [`web/`](web/). Presets live in [`web/presets/`](web/presets/).

## C++ operator export

`Processor::getGeneratedOperatorCode("MyPatch")` exports a loaded program as
a single `AudioVM::Operator` subclass. The generated `process()` has one
operator boundary and lowers supported arithmetic nodes to direct `float`
expressions in a sample loop. Its inputs are the original `audioIn` nodes,
followed by `control` nodes; its outputs are the `audioOut` nodes.

The initial exporter supports audio I/O, controls, scalar constants, rate
conversion, arithmetic/comparisons, and `exp`/`floor`/`round`/`ceil`. It throws
with the operator name for stateful or structural nodes not yet lowered (such
as oscillators, filters, delays, envelopes, MIDI, and polyphony), rather than
emitting code with different DSP semantics.

## Development

```sh
./scripts/test-presets.sh
./scripts/build-wasm.sh
cd web && python3 -m http.server
```

`build-wasm.sh` requires Emscripten (`em++`).
