# audioVM.py - DSP vm builder
# generates an audio processing program in python. inputs are either constants or references to earlier output buffers

from contextlib import contextmanager
import inspect
import json
import math
import os
import warnings
import copy

class Operator:
    def __init__(self, vm, nodeType, name=None, input=None):
        self.vm = vm
        self.type = nodeType # TODO: check that type and inputs are valid
        self.name = name if name is not None else nodeType
        if isinstance(input, Operator):
            input = input.name
        if type(input) is dict:
            for key in input:
                if isinstance(input[key], Operator):
                    input[key] = input[key].name
        if type(input) is list:
            for i, item in enumerate(input):
                if isinstance(item, Operator):
                    input[i] = item.name
        self.input = input
        self.properties = {}

    def __add__  (self, other): return self.vm.binary("add", self, other)
    def __radd__ (self, other): return self.vm.binary("add", other, self)
    def __iadd__ (self, other): return self.vm.inplace("iadd", self, other)
    def __sub__  (self, other): return self.vm.binary("sub", self, other)
    def __rsub__ (self, other): return self.vm.binary("sub", other, self)
    def __isub__ (self, other): return self.vm.inplace("isub", self, other)
    def __mul__  (self, other): return self.vm.binary("mul", self, other)
    def __rmul__ (self, other): return self.vm.binary("mul", other, self)
    def __imul__ (self, other): return self.vm.inplace("imul", self, other)
    def __truediv__ (self, other): return self.vm.binary("div", self, other)
    def __rtruediv__(self, other): return self.vm.binary("div", other, self)
    def __itruediv__(self, other): return self.vm.inplace("idiv", self, other)
    def __floordiv__(self, other): return self.vm.unary("floor", self.vm.binary("div", self, other))
    def __rfloordiv__(self, other): return self.vm.unary("floor", self.vm.binary("div", other, self))
    def __ifloordiv__(self, other): return self.vm.unary("floor", self.vm.binary("div", self, other))
    def __mod__  (self, other): return self.vm.binary("mod", self, other)
    def __imod__ (self, other): return self.vm.binary("mod", self, other)
    def __pow__  (self, other): return self.vm.binary("pow", self, other)
    def __rpow__ (self, other): return self.vm.binary("pow", other, self)
    def __ipow__ (self, other): return self.vm.binary("pow", self, other)
    def __eq__   (self, other): return self.vm.binary("equal", self, other)
    def __ne__   (self, other): return self.vm.binary("notEqual", self, other)
    def __lt__   (self, other): return self.vm.binary("less", self, other)
    def __le__   (self, other): return self.vm.binary("lessEqual", self, other)
    def __gt__   (self, other): return self.vm.binary("greater", self, other)
    def __ge__   (self, other): return self.vm.binary("greaterEqual", self, other)
    def __neg__  (self): return self.vm.unary("neg", self)
    def floor(self): return self.vm.unary("floor", self)
    def round(self): return self.vm.unary("round", self)
    def ceil(self): return self.vm.unary("ceil", self)
    def __round__(self, ndigits=None):
        if ndigits not in (None, 0):
            raise TypeError("AudioVM Operator only supports rounding to an integer")
        return self.round()

    def __getitem__(self, index):
        """Select a channel using a constant, zero-based integer index."""
        if isinstance(index, bool) or not isinstance(index, int):
            raise TypeError("channel index must be an integer")
        if index < 0:
            raise ValueError("channel index must be non-negative")
        return self.vm.op("channel", {"input": self, "index": index})

    def __call__(self, input):
        """Set the signal input of an operator.

        This permits pipeline-like forms for both positional and named inputs,
        for example ``vm.op("unitSin")(phase)`` and
        ``vm.op("filterLP", {"frequency": 1e3})(osc)``.
        """
        if isinstance(input, Operator):
            if input.vm is not self.vm:
                raise ValueError("Cannot mix different DSP vms")
            input = input.name

        if self.input is None:
            self.input = input
        elif isinstance(self.input, dict):
            if "input" in self.input:
                raise TypeError("This operator already has an input")
            self.input["input"] = input
        else:
            raise TypeError("This operator already has an input")
        # Python evaluates ``vm.op(... )`` before evaluating the argument to
        # this second call.  An expression such as ``filter(osc1 + osc2)``
        # therefore appends its ``add`` node after the filter.  Move the
        # filter to the end so every input still precedes its consumer.
        if self.vm.program[-1] is not self:
            self.vm.program[:] = [op for op in self.vm.program if op is not self]
            self.vm.program.append(self)
        return self

class Delay(Operator):
    """A delay operator with an optional compiler-linked write endpoint.

    ``input`` follows the normal operator convention: provide a dictionary
    with ``time`` and ``max``, and optionally ``input``.  Without an
    ``input``, call ``write()`` later to explicitly close a feedback loop.
    """
    def __init__(self, vm, input, name):
        if not isinstance(input, dict):
            raise TypeError("delay expects a dictionary of inputs")
        if "time" not in input:
            raise TypeError("delay requires a time input")
        if "max" not in input:
            raise TypeError("delay requires a 'max' input")
        maxSeconds = input["max"]
        if not isinstance(maxSeconds, (int, float)) or maxSeconds <= 0:
            raise ValueError("Delay max must be a positive number")
        super().__init__(vm, "delay", name, input)
        self._written = False

    def write(self, input):
        if self._written:
            raise TypeError("A delay may only have one write endpoint")
        self._written = True
        return self.vm.op("delayWrite", {"delay": self, "input": input},
                          id=self.name + ".write")

class Value(Operator):
    """A persistent, block-rate scalar with ordered writes."""
    def __init__(self, vm, initial, name):
        super().__init__(vm, "value", name, initial)

    def _snapshot(self):
        """Capture this value at the current point in the generated program."""
        return self.vm.op("value", self)

    def write(self, input):
        """Replace this value immediately; the replacement persists to the next block."""
        return self.vm.op("valueWrite", [self, input])

class Array:
    """Fixed-size mutable block array reset to its initial values each block."""
    def __init__(self, vm, size, initial):
        self.vm = vm
        self.size = size
        self.storage = vm.op("arrayState", [initial] * size)

    def read(self, index):
        """Read an indexed block-rate scalar, clamping the index to this array."""
        return self.vm.op("arrayRead", [self.storage, self.vm.as_op(index)])

    def write(self, index, value):
        """Replace one element immediately; ordered writes are visible to later nodes."""
        return self.vm.op("arrayWrite", [self.storage, self.vm.as_op(index), self.vm.as_op(value)])

class Midi: # convenience class for midi inputs
    def __init__(self, vm, name="midi"):
        self.vm = vm
        self.name = name
        outs = ["channel", "gate", "onset", "offset", "note", "velocity", "pitchbend", "modwheel"]
        for out in outs:
            setattr(self, out, Operator(vm, "const", name+"."+out))

    def kit(self, pads):
        """Create named drum-pad triggers sourced from this MIDI stream.

        Pad names are mapped to MIDI notes by the runtime, keeping note maps
        out of patches. Each pad exposes ``onset`` and ``velocity``.
        """
        if not isinstance(pads, (list, tuple)) or not pads:
            raise TypeError("kit pads must be a non-empty list or tuple of names")
        if any(not isinstance(pad, str) or not pad.isidentifier() for pad in pads):
            raise ValueError("kit pad names must be valid Python identifiers")
        if len(set(pads)) != len(pads):
            raise ValueError("kit pad names must be unique")
        return Kit(self, pads)

class KitPad:
    def __init__(self, vm, source):
        self.onset = Operator(vm, "const", source.name + ".onset")
        self.velocity = Operator(vm, "const", source.name + ".velocity")

class Kit:
    def __init__(self, midi, pads):
        self.midi = midi
        midi.vm.kitCount += 1
        prefix = "kit" + str(midi.vm.kitCount)
        for pad in pads:
            source = midi.vm.op("kitPad", id=prefix + "." + pad)
            source.properties = {"midi": midi.name, "pad": pad}
            setattr(self, pad, KitPad(midi.vm, source))

class Transport:
    """Block-rate transport values supplied by the AudioVM host."""
    def __init__(self, vm):
        self.bpm = Operator(vm, "const", "transport.bpm")
        self.beatPos = Operator(vm, "const", "transport.beatPos")
        self.play = Operator(vm, "const", "transport.play")
        self.loopBeats = Operator(vm, "const", "transport.loopBeats")

class PatchInstance(Operator):
    """Included patch whose default signal is its first audio output."""
    def __init__(self, vm, outputs, midi_outputs=None):
        self.outputs = outputs
        self.midi_outputs = {} if midi_outputs is None else midi_outputs
        if outputs:
            super().__init__(vm, 'port', next(iter(outputs.values())).name)
        else:
            self.vm = vm
            self.name = None
            self.type = 'port'
            self.input = None
            self.properties = {}

class PendingPatch:
    """An included patch awaiting its first audio input."""
    def __init__(self, vm, path, inputs, name, child, port, midi):
        self.vm, self.path, self.inputs = vm, path, inputs
        self.name, self.child, self.port = name, child, port
        self.midi = midi
        self.connected = False

    def __call__(self, signal):
        if self.connected:
            raise TypeError('This patch already has an input')
        instance = self.vm.add(self.path, {**self.inputs, self.port: signal},
                               self.name, midi=self.midi, _child=self.child)
        self.connected = True
        self.outputs = instance.outputs
        if not self.outputs:
            raise ValueError('A callable patch needs an audio output')
        return next(iter(self.outputs.values()))

class VM:
    _PURE_OPERATOR_TYPES = frozenset((
        "add", "sub", "mul", "div", "pow", "mod", "min", "max",
        "equal", "notEqual", "less", "lessEqual", "greater", "greaterEqual",
        "exp", "floor", "round", "ceil", "interp", "arrayRead", "toAudio",
    ))

    def __init__(self, polyphony=1):
        self.program = []
        self.typeCounts = {}
        self.kitCount = 0
        self.polyphony = polyphony
        self.midiObj = Midi(self)
        self.midiObjects = {"midi": self.midiObj}
        self.transportObj = Transport(self)
        self.settings = {}

    def add(self, path, inputs=None, name=None, *, midi=None, _child=None):
        """Include a standalone patch, connecting its named audio and MIDI ports.

        Outputs are available by name in the returned object's ``outputs``.
        Paths are relative to the script containing the call.
        """
        inputs = {} if inputs is None else dict(inputs)
        midi = {} if midi is None else dict(midi)
        if any(not isinstance(port, str) or not port for port in midi):
            raise TypeError('MIDI port names must be non-empty strings')
        if any(not isinstance(source, Midi) or source.vm is not self for source in midi.values()):
            raise TypeError('MIDI inputs must be Midi objects from this VM')
        caller = inspect.currentframe().f_back
        filename = caller.f_globals.get('__file__', 'patch.py')
        path = os.path.abspath(os.path.join(os.path.dirname(filename), path))
        active = getattr(VM, '_active_includes', [])
        if path in active:
            raise ValueError('Circular patch inclusion: ' + path)
        child = _child
        if child is None:
            VM._active_includes = active + [path]
            try:
                with open(path, encoding='utf-8') as source:
                    namespace = {'__file__': path, '__name__': '__audiovm_patch__'}
                    exec(compile(source.read(), path, 'exec'), namespace)
                child = namespace.get('vm')
                if not isinstance(child, VM):
                    raise ValueError(f'{path} must define a VM named vm')
            finally:
                VM._active_includes = active

        ports = [op for op in child.program if op.type == 'audioIn']
        midi_ports = [op for op in child.program if op.type == 'midiBuffer']
        midi_output_names = {op.properties.get('target') for op in child.program if op.type == 'midiOut'}
        midi_input_names = {'midi'} | {op.name for op in midi_ports if op.name not in midi_output_names}
        unknown_midi = set(midi) - midi_input_names
        if unknown_midi:
            raise ValueError(f'{path} MIDI ports: unknown {sorted(unknown_midi)}')
        # Unconnected input ports read the parent graph's global MIDI source.
        # Generated streams remain distinct and are exposed as outputs.
        for port in midi_input_names:
            midi.setdefault(port, self.midiObj)
        expected = {op.name for op in ports}
        if ports and set(inputs) == expected - {ports[0].name}:
            return PendingPatch(self, path, inputs, name, child, ports[0].name, midi)
        if set(inputs) != expected:
            raise ValueError(f'{path} audio inputs: expected {sorted(expected)}, got {sorted(inputs)}')
        prefix = name if name is not None else os.path.splitext(os.path.basename(path))[0]
        if not isinstance(prefix, str) or not prefix:
            raise ValueError('Patch name must be a non-empty string')
        ids = {op.name: prefix + '.' + op.name for op in child.program if op.type not in ('audioIn', 'audioOut')}
        existing = {op.name for op in self.program}
        if existing.intersection(ids.values()):
            raise ValueError(f'Patch name {prefix!r} conflicts with existing nodes')
        for op in ports:
            ids[op.name] = self.as_op(inputs[op.name]).name
        for port, source in midi.items():
            ids[port] = source.name

        def translate(value):
            if isinstance(value, str):
                for old in sorted(ids, key=len, reverse=True):
                    if value == old or value.startswith(old + '.'):
                        return ids[old] + value[len(old):]
                return value
            if isinstance(value, list):
                return [translate(v) for v in value]
            if isinstance(value, dict):
                return {k: translate(v) for k, v in value.items()}
            return value

        outputs = {}
        midi_outputs = {}
        for op in child.program:
            if op.type == 'audioIn':
                continue
            if op.type == 'midiBuffer' and op.name in midi:
                continue
            if op.type == 'audioOut':
                if op.name in outputs:
                    raise ValueError(f'Duplicate output {op.name!r}')
                outputs[op.name] = Operator(self, 'port', translate(op.input))
                continue
            clone = copy.copy(op)
            clone.vm = self
            clone.name = ids[op.name]
            clone.input = translate(op.input)
            clone.properties = translate(op.properties)
            self.program.append(clone)
        for port in midi_ports:
            if port.name in midi_output_names:
                midi_outputs[port.name] = Midi(self, ids[port.name])
        return PatchInstance(self, outputs, midi_outputs)

    def set(self, key, value=None):
        """Set one or more top-level properties in the generated program JSON."""
        if isinstance(key, dict):
            if value is not None:
                raise TypeError("set() does not accept a value when its first argument is a dictionary")
            self.settings.update(key)
        elif isinstance(key, str):
            self.settings[key] = value
        else:
            raise TypeError("set() expects a string key or a dictionary")
        return self

    def op(self, nodeType, input=None, id=None):
        if nodeType not in self.typeCounts:
            self.typeCounts[nodeType] = 0
        self.typeCounts[nodeType] += 1
        default_id = 'input' if nodeType == 'audioIn' and self.typeCounts[nodeType] == 1 else nodeType + str(self.typeCounts[nodeType])
        obj = Operator(self, nodeType, id if id is not None else default_id, input)
        obj.source_location = self._source_location()
        self.program += [obj]
        return obj

    @staticmethod
    def _source_location():
        """Return the first call site outside this builder module."""
        module_filename = globals().get("__file__")
        if module_filename is None:
            return None
        frame = inspect.currentframe()
        module_path = os.path.abspath(module_filename)
        while frame is not None:
            if os.path.abspath(frame.f_code.co_filename) != module_path:
                return frame.f_code.co_filename, frame.f_lineno
            frame = frame.f_back
        return None

    def constant(self, value):
        return self.op("const", value)

    def control(self, name, value=0, range=[0, 1]):
        """Create a user-adjustable control with an inclusive UI range.

        ``range`` must be a two-item list containing the minimum and maximum
        finite values.  It defaults to the existing normalized ``[0, 1]``
        range.
        """
        if (not isinstance(range, list) or len(range) != 2 or
                not all(isinstance(bound, (int, float)) and
                        not isinstance(bound, bool) and math.isfinite(bound)
                        for bound in range)):
            raise TypeError("control range must be a list of two finite numbers")
        if range[0] >= range[1]:
            raise ValueError("control range minimum must be less than its maximum")
        control = self.op("control", id=name, input=value)
        control.range = list(range)
        return control

    def value(self, initial=0, id=None):
        """Create a persistent scalar value.

        It can be used in expressions like a normal scalar; each expression
        snapshots its current value in program order. ``write()`` updates it
        immediately and it retains its most recently written value between
        processing blocks.
        """
        if not isinstance(initial, (int, float)) or not math.isfinite(initial):
            raise TypeError("value initial must be a finite number")
        if id is not None and (not isinstance(id, str) or not id):
            raise TypeError("value id must be a non-empty string")
        if "value" not in self.typeCounts:
            self.typeCounts["value"] = 0
        self.typeCounts["value"] += 1
        name = id if id is not None else "value" + str(self.typeCounts["value"])
        obj = Value(self, initial, name)
        self.program.append(obj)
        return obj

    def array(self, values=None, *, size=None, initial=0):
        """Create an immutable lookup table or a block-reset mutable array.

        Pass numeric ``values`` for an immutable interpolation table.  Pass
        ``size`` and optional ``initial`` for indexed storage with ``read()``
        and ``write()`` methods.
        """
        if size is not None:
            if values is not None:
                raise TypeError("array() accepts values or size, not both")
            if not isinstance(size, int) or isinstance(size, bool) or size < 1:
                raise ValueError("array size must be a positive integer")
            if not isinstance(initial, (int, float)) or not math.isfinite(initial):
                raise TypeError("array initial must be a finite number")
            return Array(self, size, initial)
        if not isinstance(values, (list, tuple)):
            raise TypeError("array() expects a list or tuple of numbers")
        if len(values) < 2:
            raise ValueError("array() requires at least two values for linear extrapolation")
        if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in values):
            raise TypeError("array() values must all be finite numbers")
        return self.op("array", list(values))

    def interp(self, array, index):
        """Look up zero-based floating-point indices with linear extrapolation."""
        return self.op("interp", [self.as_op(array).name, self.as_op(index).name])

    def delay(self, input, id=None):
        """Create a delay from an input dictionary.

        Omit ``input`` and call ``write()`` later to make an explicit feedback
        loop.  Include it for a conventional delay operator.
        """
        if "delay" not in self.typeCounts:
            self.typeCounts["delay"] = 0
        self.typeCounts["delay"] += 1
        if id is None:
            id = "delay" + str(self.typeCounts["delay"])
        obj = Delay(self, input, id)
        self.program.append(obj)
        return obj

    def as_op(self, value):
        if isinstance(value, Value):
            if value.vm is not self:
                raise ValueError("Cannot mix different DSP vms")
            return value._snapshot()

        if isinstance(value, Operator):
            if value.vm is not self:
                raise ValueError("Cannot mix different DSP vms")
            return value

        if isinstance(value, (int, float)):
            return self.constant(value)

        raise TypeError(
            f"Expected a DSP Operator or number, got {type(value).__name__}"
        )

    def binary(self, op, a, b):
        a = self.as_op(a)
        b = self.as_op(b)
        return self.op(op, [a.name, b.name])

    def max(self, a, b):
        """Return the sample-wise larger of two DSP values."""
        return self.binary("max", a, b)

    def min(self, a, b):
        """Return the sample-wise smaller of two DSP values."""
        return self.binary("min", a, b)

    def inplace(self, op, target, value):
        target = self.as_op(target)
        value = self.as_op(value)
        if target.type != "buffer":
            raise TypeError("In-place operations are only supported for buffer operators")
        self.op(op, [target.name, value.name])
        return target

    def unary(self, op, a):
        a = self.as_op(a)
        return self.op(op, a.name)

    def write(self, input):
        self.op("write", input)

    def midi(self, name=None):
        """Return the default MIDI source or create a named virtual source."""
        if name is None:
            return self.midiObj
        if not isinstance(name, str) or not name:
            raise TypeError("MIDI buffer name must be a non-empty string")
        if name in self.midiObjects:
            return self.midiObjects[name]
        self.op("midiBuffer", id=name)
        midi = Midi(self, name)
        self.midiObjects[name] = midi
        return midi

    def midi_out(self, target, gate, note, velocity=100):
        """Send gate/note/velocity signals as events to a named MIDI stream.

        A rising gate sends note-on and a falling gate sends note-off. The
        target may be a named :meth:`midi` stream or an output from
        :meth:`add`, enabling an arpeggiator patch to drive a synth patch.
        """
        if not isinstance(target, Midi) or target.vm is not self:
            raise TypeError('midi_out target must be a Midi object from this VM')
        output = self.op('midiOut', {
            'gate': gate,
            'note': note,
            'velocity': velocity,
        })
        output.properties = {'target': target.name}
        return output

    def transport(self):
        """Return the engine-global, block-rate transport source."""
        return self.transportObj

    @contextmanager
    def poly(self, count, timeOut=0):
        self.op("polyStart", {"count": count, "timeOut": timeOut})
        try:
            yield
        finally:
            self.op("polyEnd")

    @contextmanager
    def when(self, condition, timeOut=0):
        """Run the enclosed operators only while ``condition`` is non-zero.

        The condition is sampled once per audio block.  When it becomes zero,
        an internal release phase advances from 0 to 1 at ``1 / timeOut`` per
        second; the block is skipped once it reaches 1.  This is deliberately
        control flow rather than a signal selector: skipped operators do not
        advance their internal state.  ``if`` cannot be used as this method's
        name because it is a Python keyword.
        """
        self.op("ifStart", {"condition": condition, "timeOut": timeOut})
        try:
            yield
        finally:
            self.op("ifEnd")

    def if_(self, condition, timeOut=0):
        """Alias for :meth:`when`; the trailing underscore avoids ``if``."""
        return self.when(condition, timeOut)

    def _warn_unused_operations(self):
        referenced = set()

        def collect_references(value):
            if isinstance(value, str):
                referenced.add(value)
            elif isinstance(value, dict):
                for item in value.values():
                    collect_references(item)
            elif isinstance(value, (list, tuple)):
                for item in value:
                    collect_references(item)

        for operator in self.program:
            collect_references(operator.input)

        for operator in self.program:
            if operator.type not in self._PURE_OPERATOR_TYPES or operator.name in referenced:
                continue
            hint = " Use vm.max(...) or vm.min(...), not Python's built-in max()/min()." \
                if operator.type in {"less", "lessEqual", "greater", "greaterEqual"} else ""
            message = (
                f"AudioVM compile warning: unused operation '{operator.name}' "
                f"({operator.type}); its output is not connected.{hint}"
            )
            if operator.source_location is not None:
                filename, lineno = operator.source_location
                warnings.warn_explicit(message, RuntimeWarning, filename, lineno)
            else:
                warnings.warn(message, RuntimeWarning, stacklevel=3)
    
    def get_json(self):
        self._warn_unused_operations()
        s = '{\n\t"version": 1,\n\t"polyphony":'+str(self.polyphony)
        for key, value in self.settings.items():
            s += ',\n\t' + json.dumps(key) + ': ' + json.dumps(value)
        s += ',\n\t"program": [\n'
        for p in self.program:
            if not isinstance(p, Operator):
                print("warning!")
                print(p)
                continue
            s += '\t\t{"type": "' + p.type + '", "id": "' + p.name + '"'
            if p.input is not None:
                i = p.input
                if type(i) is str:
                    i = '"' + i + '"'
                else:
                    i = str(i).replace("'",'"')
                s += ', "input": ' + i
            if hasattr(p, "range"):
                s += ', "range": ' + json.dumps(p.range)
            for key, value in p.properties.items():
                s += ', ' + json.dumps(key) + ': ' + json.dumps(value)
            s += '},\n'
        if self.program:
            s = s[:-2] + "\n\t]\n}\n"
        else:
            s += "\t]\n}\n"
        return s
    def flatten_json(s):
        return s.replace('\n','').replace('\t','').replace('"','\\"')
