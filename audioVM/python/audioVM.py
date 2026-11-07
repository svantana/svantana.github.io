# audioVM.py - DSP vm builder
# generates an audio processing program in python. inputs are either constants or references to earlier output buffers

from contextlib import contextmanager
import json

class Operator:
    def __init__(self, vm, nodeType, name=None, input=None):
        self.vm = vm
        self.type = nodeType # TODO: check that type and inputs are valid
        self.name = name if name is not None else nodeType
        if type(input) is Operator:
            input = input.name
        if type(input) is dict:
            for key in input:
                if type(input[key]) is Operator:
                    input[key] = input[key].name
        if type(input) is list:
            for i, item in enumerate(input):
                if type(item) is Operator:
                    input[i] = item.name
        self.input = input

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

    def __call__(self, input):
        """Set the signal input of a mapping-style operator.

        This permits a pipeline-like form, for example:
        ``vm.op("filterLP", {"frequency": 1e3})(osc)``.
        """
        if not isinstance(self.input, dict):
            raise TypeError("Only operators configured with a dictionary can accept an input")
        if "input" in self.input:
            raise TypeError("This operator already has an input")
        if isinstance(input, Operator):
            if input.vm is not self.vm:
                raise ValueError("Cannot mix different DSP vms")
            input = input.name
        self.input["input"] = input
        return self

class Delay:
    """A compiler-linked delay read/write pair.

    The delay line is intentionally not an ordinary signal graph edge: the
    read happens earlier in the program and the write commits the next block
    into the same state.  ``maxSeconds`` is fixed at compile time so no audio
    thread allocation is necessary.
    """
    def __init__(self, vm, maxSeconds, name):
        if not isinstance(maxSeconds, (int, float)) or maxSeconds <= 0:
            raise ValueError("Delay maxSeconds must be a positive number")
        self.vm = vm
        self.maxSeconds = maxSeconds
        self.name = name
        self._read = None
        self._written = False

    def read(self, time):
        if self._read is not None:
            raise TypeError("A delay may only have one read endpoint; reuse its output")
        self._read = self.vm.op("delayRead", {
            "time": time,
            "maxSeconds": self.maxSeconds,
        }, id=self.name + ".read")
        return self._read

    def write(self, input):
        if self._read is None:
            raise TypeError("Call delay.read() before delay.write()")
        if self._written:
            raise TypeError("A delay may only have one write endpoint")
        self._written = True
        return self.vm.op("delayWrite", {"delay": self._read, "input": input},
                          id=self.name + ".write")

class Midi: # convenience class for midi inputs
    def __init__(self, vm):
        outs = ["channel", "gate", "onset", "offset", "note", "velocity", "pitchbend", "modwheel"]
        for out in outs:
            setattr(self, out, Operator(vm, "const", "midi."+out))

class Transport:
    """Block-rate transport values supplied by the AudioVM host."""
    def __init__(self, vm):
        self.bpm = Operator(vm, "const", "transport.bpm")
        self.beatPos = Operator(vm, "const", "transport.beatPos")
        self.play = Operator(vm, "const", "transport.play")
        self.loopBeats = Operator(vm, "const", "transport.loopBeats")

class VM:
    def __init__(self, polyphony=1):
        self.program = []
        self.typeCounts = {}
        self.polyphony = polyphony
        self.midiObj = Midi(self)
        self.transportObj = Transport(self)
        self.settings = {}

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
        obj = Operator(self, nodeType, id if id is not None else nodeType + str(self.typeCounts[nodeType]), input)
        self.program += [obj]
        return obj

    def constant(self, value):
        return self.op("const", value)

    def control(self, name, value=0):
        return self.op("control", id=name, input=value)

    def delay(self, maxSeconds, id=None):
        """Create a feedback delay handle; call read() before write()."""
        if id is None:
            if "delay" not in self.typeCounts:
                self.typeCounts["delay"] = 0
            self.typeCounts["delay"] += 1
            id = "delay" + str(self.typeCounts["delay"])
        return Delay(self, maxSeconds, id)

    def as_op(self, value):
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

    def midi(self):
        #self.op("midi", input={}) # TODO: any midi settings?
        return self.midiObj

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
    
    def get_json(self):
        s = '{\n\t"version": 1,\n\t"polyphony":'+str(self.polyphony)
        for key, value in self.settings.items():
            s += ',\n\t' + json.dumps(key) + ': ' + json.dumps(value)
        s += ',\n\t"program": [\n'
        for p in self.program:
            if type(p) is not Operator:
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
            s += '},\n'
        if self.program:
            s = s[:-2] + "\n\t]\n}\n"
        else:
            s += "\t]\n}\n"
        return s
    def flatten_json(s):
        return s.replace('\n','').replace('\t','').replace('"','\\"')
