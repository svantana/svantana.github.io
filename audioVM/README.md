# AudioVM

AudioVM is a small DSP graph runtime. Write patches in Python; run them natively or in the browser via WebAssembly.

```python
import audioVM

vm = audioVM.VM()
osc = vm.op("oscAnalog", {"note": 60})
vm.op("audioOut", osc * 0.1)
```

The browser player lives in [`web/`](web/). Presets live in [`web/presets/`](web/presets/).

## Combining patches

A standalone patch can expose named `audioIn` and `audioOut` nodes. Include
it in another patch with `vm.add`, supplying each input by its node ID and
using its outputs by ID. Paths are relative to the including script (in the
browser, relative to the presets directory). For example, see
[`composite.py`](web/presets/composite.py):

```python
bass = vm.add("acid bass.py", name="bass")
reverb = vm.add("reverbFDN.py", name="reverb")(bass)
vm.op("audioOut", reverb)
```

Included nodes and controls are prefixed by the instance name; omit `name`
to use the filename without its extension. Give repeated instances distinct
names. Every audio input must be connected. MIDI and transport
sources remain shared with the parent graph. Top-level settings and polyphony
belong to the parent patch.

The first unnamed `audioIn` is called `input`. An included effect can also
receive its first input through the call interface, which returns its first
audio output: `processed = vm.add("reverbFDN.py")(signal)`. For multiple
inputs, supply the remaining named ports using `inputs`. Use `outputs` on
the included instance to access additional outputs after calling it.
An included patch used as a signal refers to its first audio output; use
`patch.outputs["name"]` to select another output explicitly.

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
./scripts/test-ops.sh
./scripts/build-wasm.sh
python3 -m http.server
```

Open [`web/test-presets.html`](web/test-presets.html) to compile and exercise
every preset through the browser's WebAssembly backend. The browser loads its
Python builder directly from [`python/audioVM.py`](python/audioVM.py).

`build-wasm.sh` requires Emscripten (`em++`).
