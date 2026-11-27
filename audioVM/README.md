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
names. Every audio input must be connected. The default MIDI and transport
sources remain shared with the parent graph. Top-level settings and polyphony
belong to the parent patch.

## Composing MIDI generators and synths

Named MIDI streams are patch ports. A generator creates `vm.midi("output")`
and sends events to it with `vm.midi_out(output, gate, note, velocity)`. A
synth reads the default stream with `vm.midi()` or a named stream with
`vm.midi("input")`. Included MIDI inputs default to the global MIDI stream;
map an input through `midi` to override it. Generated streams are returned in
`instance.midi_outputs` and can feed another patch:

```python
arp = vm.add("midi/arpeggiator.py", name="arp")
synth = vm.add("acid bass.py", name="synth",
               midi={"midi": arp.midi_outputs["output"]})
vm.op("audioOut", synth)
```

MIDI is delivered in graph order, so put the generator before its receiving
synth. See `web/presets/midi composition.py`.

The first unnamed `audioIn` is called `input`. An included effect can also
receive its first input through the call interface, which returns its first
audio output: `processed = vm.add("reverbFDN.py")(signal)`. For multiple
inputs, supply the remaining named ports using `inputs`. Use `outputs` on
the included instance to access additional outputs after calling it.
An included patch used as a signal refers to its first audio output; use
`patch.outputs["name"]` to select another output explicitly.

## C++ operator export

Polyphonic patches can retire individual voices when a block-rate condition
stays true for a duration. `meanSquare` measures the mean squared amplitude
across the current block and all input channels:

```python
with vm.poly(count=8, timeOut=None) as voice:
    # ... produce and mix a voice-local signal ...
    ms = vm.op("meanSquare")(signal)
    voice.kill((ms < 1e-8) * (midi.gate == 0), timeOut=0.10)
```

The kill timer resets whenever the condition is false. Retirement takes effect
after the current block. `timeOut=None` disables the separate polyphonic
note-off timeout; when specified, either timeout can retire the voice.

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

## Channel routing

`channels` joins one to eight full-rate mono signals in order. `channel`
selects a single channel using a zero-based constant index, resolved when the
graph is initialized:

```python
pair = vm.op("channels", [left, right])
left_again = pair[0]
right_again = pair[1]
```

Indexing creates a `channel` operator; the equivalent explicit form is
`vm.op("channel", {"input": pair, "index": 0})`.
The inputs to `channels` must be consecutive and mono. A channel index must
be within the input's resolved channel count. These operators provide routing;
The `filterLP`, `filterBP`, and `filterHP` operators preserve one or two input
channels with independent filter state.
For these filters, `Q` sets approximately the linear gain at the
specified frequency when it is low relative to the sample rate: 1 means
unity gain, 0.5 approximately -6 dB, and 2 approximately +6 dB. Values at
or below 0 are treated as 0.0001; the approximation becomes less accurate
at higher frequencies.
For `filterBP`, set `compensate` above zero to divide the input by `Q`
when `Q` is below 1, giving approximately unity gain at the specified
frequency at low frequencies. At `Q` of 1 or above, or
when `compensate` is zero or negative, the normal response is used.
Other operators and the browser input do not yet automatically preserve
multichannel signals.

Writable buffers are mono by default; `vm.op("buffer", {"channels": 2})`
creates a stereo accumulator. Its channel count is fixed at graph initialization.
Binary arithmetic broadcasts mono inputs across the other input's channels.

Connecting `oscAnalog`'s `"stereo"` input enables two-channel output, even
when the connected value is zero. The value can be modulated at block rate:
0 centers the unison oscillators and 1 spreads them fully across the two
channels (values outside this range are clamped). A single oscillator, or
unison oscillators with no frequency spread, can sound centered. Leave the
input disconnected for mono output.

`build-wasm.sh` requires Emscripten (`em++`).
