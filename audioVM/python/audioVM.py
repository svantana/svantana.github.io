# audioVM.py - DSP vm builder
# generates an audio processing program in python. inputs are either constants or references to earlier output buffers

from contextlib import contextmanager

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

class Midi: # convenience class for midi inputs
    def __init__(self, vm):
        outs = ["channel", "gate", "note", "velocity", "pitchbend", "modwheel"]
        for out in outs:
            setattr(self, out, Operator(vm, "const", "midi."+out))

class VM:
    def __init__(self, polyphony=1):
        self.program = []
        self.typeCounts = {}
        self.polyphony = polyphony
        self.midiObj = Midi(self)

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

    @contextmanager
    def poly(self, count, timeOut=0):
        self.op("polyStart", {"count": count, "timeOut": timeOut})
        try:
            yield
        finally:
            self.op("polyEnd")
    
    def get_json(self):
        s = '{\n\t"version": 1,\n\t"polyphony":'+str(self.polyphony)+',\n\t"program": [\n'
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
        s = s[:-2] + "\n\t]\n}\n"
        return s
    def flatten_json(s):
        return s.replace('\n','').replace('\t','').replace('"','\\"')
