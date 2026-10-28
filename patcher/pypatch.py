# pypatch.py - DSP graph builder and instruction recorder.

class Node:
    def __init__(self, graph, register):
        self.graph = graph
        self.register = register

    def __add__  (self, other): return self.graph.binary("ADD", self, other)
    def __radd__ (self, other): return self.graph.binary("ADD", other, self)
    def __sub__  (self, other): return self.graph.binary("SUB", self, other)
    def __rsub__ (self, other): return self.graph.binary("SUB", other, self)
    def __mul__  (self, other): return self.graph.binary("MUL", self, other)
    def __rmul__ (self, other): return self.graph.binary("MUL", other, self)
    def __neg__  (self): return self.graph.unary("NEG", self)
    #def __truediv__ (self, other): return self.graph.binary("DIV", self, other)
    #def __rtruediv__(self, other): return self.graph.binary("DIV", other, self)

class Graph:
    def __init__(self):
        self.instructions = []
        self.next_register = 0

    def emit(self, op, *args):
        dst = self.next_register
        self.next_register += 1

        self.instructions.append({
            "op": op,
            "dst": dst,
            "args": args,
        })

        return Node(self, dst)

    def constant(self, value):
        return self.emit("CONST", value)

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
        return self.emit(op, a.register, b.register)

    def unary(self, op, a):
        a = self.as_node(a)
        return self.emit(op, a.register)

    # DSP building blocks

    def osc_saw(self, frequency):
        return self.emit("OSC_SAW", frequency)

    def osc_sine(self, frequency):
        return self.emit("OSC_SINE", frequency)

    def adsr(self, attack, decay, sustain, release):
        return self.emit(
            "ADSR", attack, decay, sustain, release
        )

    def lpf(self, signal, cutoff, q=0.707):
        signal = self.as_node(signal)
        cutoff = self.as_node(cutoff)

        return self.emit(
            "FILTER_LPF",
            signal.register,
            cutoff.register,
            q,
        )

    def output(self, signal):
        signal = self.as_node(signal)

        self.instructions.append({
            "op": "OUTPUT",
            "args": (signal.register,),
        })

    def print_instructions(self):
        print("-- DSP instructions")

        for ins in self.instructions:
            op = ins["op"]
            args = ", ".join(map(str, ins["args"]))

            if "dst" in ins:
                print(f"r{ins['dst']} = {op} {args}")
            else:
                print(f"{op} {args}")

