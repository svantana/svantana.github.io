# pypatch.py - DSP graph builder

class Node:
    def __init__(self, graph, nodeType, index, input):
        self.graph = graph
        self.type = nodeType # TODO: check that type and inputs are valid
        self.index = index
        if type(input) is Node:
            input = input.index
        if type(input) is dict:
            for key in input:
                if type(input[key]) is Node:
                    input[key] = input[key].index
        if type(input) is list:
            for i, item in enumerate(input):
                if type(item) is Node:
                    input[i] = item.index
        self.input = input

    def __add__  (self, other): return self.graph.binary("add", self, other)
    def __radd__ (self, other): return self.graph.binary("add", other, self)
    def __sub__  (self, other): return self.graph.binary("sub", self, other)
    def __rsub__ (self, other): return self.graph.binary("sub", other, self)
    def __mul__  (self, other): return self.graph.binary("mul", self, other)
    def __rmul__ (self, other): return self.graph.binary("mul", other, self)
    def __neg__  (self): return self.graph.unary("neg", self)
    def __truediv__ (self, other): return self.graph.binary("div", self, other)
    def __rtruediv__(self, other): return self.graph.binary("div", other, self)

# some special convenience nodes
class Midi(Node):
    def __init__(self, graph, index, input):
        super(Midi, self).__init__("midiIn", graph, index, input)
        self.gate = "midi.gate"
        self.note = "midi.note"
        self.velocity = "midi.velocity"

class Graph:
    def __init__(self):
        self.program = []

    def node(self, nodeType, input):
        obj = Node(self, nodeType, len(self.program), input)
        self.program += [obj]
        return obj

    def constant(self, value):
        return self.node("const", value)

    def as_node(self, value):
        if isinstance(value, Node):
            if value.graph is not self:
                raise ValueError("Cannot mix different DSP graphs")
            return value

        if isinstance(value, (int, float)):
            return self.constant(value)

        raise TypeError(
            f"Expected a DSP Node or number, got {type(value).__name__}"
        )

    def binary(self, op, a, b):
        a = self.as_node(a)
        b = self.as_node(b)
        return self.node(op, [a.index, b.index])

    def unary(self, op, a):
        a = self.as_node(a)
        return self.node(op, a.index)

    def midi(self, numVoices=1):
        obj = Midi(self, len(self.program), input={"polyphony": numVoices})
        self.program += [obj]
        return obj
    
    def get_json(self):
        s = '{\n\t"version": 1,\n\t"program": [\n'
        for p in self.program:
            if type(p) is not Node:
                print("warning!")
                print(p)
                continue
            s += '\t\t{"type": "'+p.type+'", "input": ' + str(p.input).replace("'",'"') + '},\n'
        s += "\t]\n}\n"
        return s
