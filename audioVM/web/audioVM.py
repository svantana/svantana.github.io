# audioVM.py - DSP vm builder
# generates an audio processing program in python. inputs are either constants or references to earlier output buffers

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
    def __iadd__ (self, other): return self.vm.binary("add", self, other)
    def __sub__  (self, other): return self.vm.binary("sub", self, other)
    def __rsub__ (self, other): return self.vm.binary("sub", other, self)
    def __isub__ (self, other): return self.vm.binary("sub", self, other)
    def __mul__  (self, other): return self.vm.binary("mul", self, other)
    def __rmul__ (self, other): return self.vm.binary("mul", other, self)
    def __imul__ (self, other): return self.vm.binary("mul", self, other)
    def __truediv__ (self, other): return self.vm.binary("div", self, other)
    def __rtruediv__(self, other): return self.vm.binary("div", other, self)
    def __itruediv__(self, other): return self.vm.binary("div", self, other)
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

    def unary(self, op, a):
        a = self.as_op(a)
        return self.op(op, a.name)

    def write(self, input):
        self.op("write", input)

    def midi(self, numVoices=1):
        #self.op("midi", input={"polyphony": numVoices})
        return self.midiObj
    
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
