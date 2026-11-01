#include "AudioVM.h"
#include "libs/picojson.h"
#include <assert.h>

using namespace AudioVM;

typedef const char* cStr;

// basic operators
class AudioIn : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? 1 : 0; }
    int isFullRatePort(Port::Dir dir, int index) override { return true; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class AudioOut : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? 1 : 0; }
    int isFullRatePort(Port::Dir dir, int index) override { return true; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class MidiIn : public Operator {
public:
    enum Type { Gate, Note, Velocity, NumTypes };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? NumTypes : 0; }
    int isFullRatePort(Port::Dir dir, int index) override { return false; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class Control : public Operator {
public:
    Control(std::string name = "control") { mName = name; }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::In) ? 0 : 1;
    }
    int isFullRatePort(Port::Dir dir, int index) override { return false; }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value() = mValue; }
    float mValue;
    std::string mName;
};
class ConstOp : public Operator {
public:
    ConstOp(std::string name = "const") { mName = name; }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::In) ? 0 : 1;
    }
    int isFullRatePort(Port::Dir dir, int index) override { return false; }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value() = mValue; }
    float mValue;
    std::string mName;
};

// needed for feedback
class BufferOperator : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return 1; }
    int isFullRatePort(Port::Dir dir, int index) override { return true; }
    void process(Buffer *i, Buffer *o, int N) override { /*o[0].copy(i[0]);*/ }
};

class ControlToAudio : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return 1; }
    int isFullRatePort(Port::Dir dir, int index) override { return dir; }
    void process(Buffer *i, Buffer *o, int N) override {
        float v = state, next = i[0].value(), step = (next-v) / N;
        float *dst = o[0].data();
        for (int n = 0; n < N; n++) {
            dst[n] = v + step * (n + 1);
        }
        state = next;
    }
private:
    float state = 0.f;
};

// math ops
class Constant : public Operator {
public:
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? 1 : 0;
    }
    int isFullRatePort(Port::Dir dir, int index) override { return false; }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value() = mValue; }
private:
    float mValue;
};
class UnaryOp : public Operator {
public:
    int getNumPorts(Port::Dir dir) override {
        return 1;
    }
};
class BinaryOp : public Operator {
    public: int getNumPorts(Port::Dir dir) override {
        return (dir == Port::In) ? 2 : 1;
    }
};

class MathBinary : public BinaryOp {
public:
    int isFullRatePort(Port::Dir dir, int index) override { return true; }
protected:
    virtual float apply(float a, float b) const = 0;
    void process(Buffer *i, Buffer *o, int N) override {
        const int channels = std::max(i[0].numChannels, i[1].numChannels);
        for (int ch = 0; ch < channels; ++ch) {
            const float *a = i[0].dataWrap(ch);
            const float *b = i[1].dataWrap(ch);
            float *dst = o[0].dataWrap(ch);
            for (int n = 0; n < N; ++n) {
                dst[n] = apply(a[i[0].numSamples == 1 ? 0 : n],
                               b[i[1].numSamples == 1 ? 0 : n]);
            }
        }
    }
};
class MathAdd : public MathBinary {
    float apply(float a, float b) const override { return a + b; }
};
class MathSub : public MathBinary {
    float apply(float a, float b) const override { return a - b; }
};
class MathMul : public MathBinary {
    float apply(float a, float b) const override { return a * b; }
};

class MultiplyOperator : public BinaryOp {
public:
    void process(Buffer *i, Buffer *o) {
        int numChannels = std::max(i[0].numChannels, i[1].numChannels);
        for (int ch = 0; ch < numChannels; ch++) {
            float *a = i[0].dataWrap(ch);
            float *b = i[1].dataWrap(ch);
            float *dst = o[0].dataWrap(ch);
            for (int n = 0; n < i->numSamples; n++) {
                dst[n] = a[n] * b[n];
            }
        }
    }
};

class MathExp : public UnaryOp {
public:
    void process(Buffer *i, Buffer *o, int N) override {
        for (int ch = 0; ch < i->numChannels; ch++) {
            for (int n = 0; n < i->numSamples; n++) {
                o->data(ch)[n] = expf(i->data(ch)[n]);
            }
        }
    }
};
/*
class MathLog : public UnaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = logf(i[0]); }
};
class MathCos : public UnaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = cosf(i[0]); }
};
class MathSin : public UnaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = sinf(i[0]); }
};
class MathAdd : public BinaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] + i[1]; }
};
class MathSub : public BinaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] - i[1]; }
};
class MathMul : public BinaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] * i[1]; }
};
class MathDiv : public BinaryOp {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] / i[1]; }
};
 */

// some basic ops to get started
class OscSin : public Operator {
public:
    enum In  { FreqHz, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        return (dir == Port::In && index == FreqHz) ? "note" : "output";
    }
    void process(Buffer *i, Buffer *o, int N) override {
        o->numSamples = N;
        float phaseStep = i[FreqHz][0][0] / mSamplerate;
        for (int n = 0; n < N; n++) {
            mUnitPhase += phaseStep;
            mUnitPhase -= floorf(mUnitPhase);
            o->data()[n] = sinf(2 * M_PI * mUnitPhase);
        }
    }
private:
    float mUnitPhase = 0.f;
};
class OscSaw : public Operator {
public:
    enum In  { FreqHz, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        return (dir == Port::In && index == FreqHz) ? "note" : "output";
    }
    int isFullRatePort(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output);
    }
    void process(Buffer *i, Buffer *o, int N) override {
        o->numSamples = N;
        float phaseStep = i[FreqHz][0][0] / mSamplerate;
        for (int n = 0; n < N; n++) {
            mUnitPhase += phaseStep;
            mUnitPhase -= floorf(mUnitPhase);
            o->data()[n] = mUnitPhase*2-1;
        }
    }
private:
    float mUnitPhase = 0.f;
};
class FilterLP : public Operator {
public:
    enum In  { Input, FreqHz, Resonance, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","frequency","resonance"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    int isFullRatePort(Port::Dir dir, int index) override {
        return (dir==Port::In && index==Input) || (dir==Port::Out && index==Output);
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        o->numSamples = N;
        float f = std::min(.5f, i[FreqHz][0][0] / mSamplerate);
        cK = f * (3.1415f - 1.4f * f);
        float d = 1 - i[Resonance][0][0];
        cDamp = 4 * d * d;
        float *x = i[Input][0], *y = o[Output][0];
        for (int n = 0; n < N; n++) {
            float hp = x[n] - sL - cDamp * sB;
            sB += cK * hp;
            sL += cK * sB;
            y[n] = sL;
        }
    }
private:
    float sL = 0.f, sB = 0.f, cK = 0.f, cDamp = 0.f;
};
class EnvADSR : public Operator {
public:
    enum In      { Gate , Attack , Decay , Sustain , Release , NumInputs };
    enum Out     { Output , NumOutputs };
    std::string getPortName(Port::Dir t, int n) override {
        cStr i[]={"gate","a","d","s","r"};
        cStr o[]={"output"};
        return (t == Port::In) ? i[n] : o[n];
    }
    int isFullRatePort(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output);
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    enum Phase {A, D, S, R};
    void process(Buffer *i, Buffer *o, int N) override {
        float dt = N / mSamplerate;
        bool on = i[Gate].value() > .5f;
        if (on && mPhase >= R) {
            mPhase = 0.f;
        }
        if (!on && mPhase < R) {
            mPhase = R;
            mReleaseVal = mVal;
        }
        Phase P = (Phase)mPhase;
        float sustain = i[Sustain][0][0];
        switch (P) {
            case A: {
                float dur = std::max(1e-9f, i[Attack].value());
                mPhase = std::min((float)D, mPhase + dt / dur);
                mVal = mPhase;
                break;
            }
            case D: {
                float dur = std::max(1e-9f, i[Decay].value());
                mPhase = std::min((float)S, mPhase + dt / dur);
                float p = mPhase - D;
                mVal = (1-p) + p * sustain;
                break;
            }
            case S: {
                mVal = sustain;
                break;
            }
            case R: {
                float dur = std::max(1e-9f, i[Decay].value());
                mPhase = mPhase + dt / dur;
                mVal = mReleaseVal * (R+1 - mPhase);
            }
            default:
                mVal = 0.f;
        }
        o[Output].value() = mVal;
    }
private:
    float mPhase = R+1, mVal = 0.f, mReleaseVal = 0.f;
};

const char *kIdAudioIn = "audioIn", *kIdAudioOut = "audioOut";

Processor::Processor()
{
    registerType(kIdAudioIn , [] { return std::make_unique<AudioIn>(); });
    registerType(kIdAudioOut, [] { return std::make_unique<AudioOut>(); });
    registerType("midiIn", [] { return std::make_unique<MidiIn>(); });
    registerType("control", [] { return std::make_unique<Control>(); });
    registerType("const", [] { return std::make_unique<ConstOp>(); });
    registerType("buffer", [] { return std::make_unique<BufferOperator>(); });
    registerType("toAudio", [] { return std::make_unique<ControlToAudio>(); });
    // basic math
    registerType("exp", [] { return std::make_unique<MathExp>(); });
    registerType("add", [] { return std::make_unique<MathAdd>(); });
    registerType("sub", [] { return std::make_unique<MathSub>(); });
    registerType("mul", [] { return std::make_unique<MathMul>(); });
    /*
    registerType("log", [] { return std::make_unique<MathLog>(); });
    registerType("cos", [] { return std::make_unique<MathCos>(); });
    registerType("sin", [] { return std::make_unique<MathSin>(); });
    registerType("add", [] { return std::make_unique<MathAdd>(); });
    registerType("sub", [] { return std::make_unique<MathSub>(); });
    registerType("mul", [] { return std::make_unique<MathMul>(); });
    registerType("div", [] { return std::make_unique<MathDiv>(); });
    */
    
    registerType("oscSin"  , [] { return std::make_unique<OscSin>(); });
    registerType("oscSaw"  , [] { return std::make_unique<OscSaw>(); });
    registerType("filterLP", [] { return std::make_unique<FilterLP>(); });
    registerType("envADSR" , [] { return std::make_unique<EnvADSR>(); });

    mProgram.list.reserve(128);
}

void Processor::setup(float samplerate, int numInputChannels, int numOutputChannels, int blockSize) {
    mSamplerate = samplerate;
    mNumInputChannels = numInputChannels;
    mNumOutputChannels = numOutputChannels;
    mBlockSize = blockSize;
}

void Processor::clearProgram() {
    mProgram.list.clear();
}
int Processor::addOperator(std::string operatorId, std::vector<int> inputIds) {
    int opCount = (int)mProgram.list.size();
    OpPtr op = makeOpWithId(operatorId);
    Entry &entry = mProgram.list.emplace_back();
    entry.mIndex = opCount * OpSpacing;
    entry.mType = operatorId;
    entry.op = std::move(op);
    entry.op->mSamplerate = mSamplerate;
    entry.op->init();
    
    // setup inputs and outputs
    entry.mInputIds = inputIds;
    return entry.mIndex;
}
int Processor::addControl(std::string name, float value) {
    const int controlId = addOperator("control");
    auto* control = dynamic_cast<Control*>(mProgram.list.back().op.get());
    assert(control != nullptr);
    control->mName = std::move(name);
    control->mValue = value;
    return controlId;
}
int Processor::addConstant(float value) {
    const int constantId = addOperator("const");
    auto* constant = dynamic_cast<ConstOp*>(mProgram.list.back().op.get());
    assert(constant != nullptr);
    constant->mValue = value;
    return constantId;
}

bool Processor::compileProgram() {
    assert(mBlockSize > 0);
    mProgram.mAudioOutIds.resize(0);
    mProgram.mAudioInIds.resize(0);
    mProgram.mControlIds.resize(0);
    // two pass: first determine amount of memory needed, then allocate, then set pointers
    for (int pass = 0; pass < 2; pass++) {
        int pos = 1; // put a zero at the beginning of the buffer as a convenience
        for (int n = 0; n < (int)mProgram.list.size(); n++) {
            Entry &E = mProgram.list[n];
            int numOuts = E.op->getNumPorts(Port::Out);
            E.mOutputs.resize(numOuts);
            for (int o = 0; o < numOuts; o++) {
                int rateType = E.op->isFullRatePort(Port::Out, o);
                E.mOutputs[o].numSamples = (rateType==1) ? mBlockSize : 1;
                if (pass == 1) {
                    E.mOutputs[o].ptr = &mProgram.mIoBuffer[pos];
                }
                pos += E.mOutputs[o].size();
            }
            if (pass == 1) {
                int numIns = E.op->getNumPorts(Port::In);
                E.mInputs.resize(numIns);
                for (int i = 0; i < numIns; i++) {
                    int id = (i < E.mInputIds.size()) ? E.mInputIds[i] : -1;
                    if (id < 0) {
                        E.mInputs[i] = Buffer(&mProgram.mIoBuffer[0]);
                        continue;
                    }
                    Entry &S = mProgram.list[E.mInputIds[i]/OpSpacing];
                    E.mInputs[i] = S.mOutputs[E.mInputIds[i]%OpSpacing];
                }
                if (auto *op = dynamic_cast<AudioOut*>(E.op.get())) {
                    mProgram.mAudioOutIds.push_back(E.mInputIds[0]);
                }
                if (auto *op = dynamic_cast<Control*>(E.op.get())) {
                    mProgram.mControlIds.push_back(n);
                }
                if (auto *op = dynamic_cast<MidiIn*>(E.op.get())) {
                    mProgram.mMidiInId = n;
                }
            }
            
        }
        if (pass == 0) { // allocate buffer memory
            mProgram.mIoBuffer.resize(pos);
            mProgram.mIoBuffer[0] = 0.f;
        }
    }
    
    return true;
}

void Processor::loadFromJSON(std::string jsonString) {
    picojson::value value;
    const std::string parseError = picojson::parse(value, jsonString);
    if (!parseError.empty()) {
        throw std::runtime_error("Invalid program JSON: " + parseError);
    }
    if (!value.is<picojson::object>()) {
        throw std::runtime_error("Invalid program JSON: root must be an object");
    }

    const picojson::object& root = value.get<picojson::object>();
    const auto programIt = root.find("program");
    if (programIt == root.end() || !programIt->second.is<picojson::array>()) {
        throw std::runtime_error("Invalid program JSON: missing array property 'program'");
    }

    const picojson::array& program = programIt->second.get<picojson::array>();

    // Construct the new graph separately from the active one.  A failed load
    // must not leave the processor with a half-built program.
    Program previousProgram = std::move(mProgram);
    Compiler previousCompiler = std::move(mCompiler);
    mProgram = Program();
    mProgram.list.reserve(program.size());
    mCompiler = Compiler();

    try {
        // audioVM.py exposes these as implicit sources rather than program
        // entries.  Store their individual output-port IDs in the same lookup
        // table used by regular operator IDs.
        const int midiInId = addOperator("midiIn");
        mCompiler.mIdToIndex.emplace("midi.gate", midiInId + MidiIn::Gate);
        mCompiler.mIdToIndex.emplace("midi.note", midiInId + MidiIn::Note);
        mCompiler.mIdToIndex.emplace("midi.velocity", midiInId + MidiIn::Velocity);

        for (size_t index = 0; index < program.size(); ++index) {
            const picojson::value& node = program[index];
            const std::string context = "Invalid program JSON at program[" + std::to_string(index) + "]: ";
            if (!node.is<picojson::object>()) {
                throw std::runtime_error(context + "operator must be an object");
            }

            const picojson::object& object = node.get<picojson::object>();
            const auto typeIt = object.find("type");
            const auto idIt = object.find("id");
            if (typeIt == object.end() || !typeIt->second.is<std::string>()) {
                throw std::runtime_error(context + "missing string property 'type'");
            }
            if (idIt == object.end() || !idIt->second.is<std::string>()) {
                throw std::runtime_error(context + "missing string property 'id'");
            }

            const std::string& type = typeIt->second.get<std::string>();
            const std::string& id = idIt->second.get<std::string>();
            if (id.empty()) {
                throw std::runtime_error(context + "property 'id' cannot be empty");
            }
            if (mCompiler.mIdToIndex.count(id) != 0) {
                throw std::runtime_error(context + "duplicate operator id '" + id + "'");
            }

            // The Python builder permits positional inputs (a string or array)
            // and named inputs (a dictionary).  Use a prototype to translate
            // dictionary keys into the destination operator's input indices.
            OpPtr prototype = makeOpWithId(type);
            const int numInputPorts = prototype->getNumPorts(Port::In);
            std::vector<int> inputs;
            const auto makeConstant = [&](double number) {
                return addConstant(static_cast<float>(number));
            };
            const auto resolveInputId = [&](const picojson::value& input) {
                if (input.is<picojson::null>()) {
                    return -1;
                }
                if (input.is<double>()) {
                    return makeConstant(input.get<double>());
                }
                if (input.is<std::string>()) {
                    const std::string& inputName = input.get<std::string>();
                    const auto source = mCompiler.mIdToIndex.find(inputName);
                    if (source == mCompiler.mIdToIndex.end()) {
                        throw std::runtime_error(context + "unknown or forward input id '" + inputName + "'");
                    }
                    return source->second;
                }
                throw std::runtime_error(context + "input values must be operator ids or numbers");
            };

            const auto inputIt = object.find("input");
            if (inputIt != object.end()) {
                if (type == "const" && inputIt->second.is<double>()) {
                    prototype.reset();
                    const int opId = addConstant(static_cast<float>(inputIt->second.get<double>()));
                    mProgram.list.back().mId = id;
                    mCompiler.mIdToIndex.emplace(id, opId);
                    continue;
                } else if (type == "control" && inputIt->second.is<double>()) {
                    prototype.reset();
                    const int opId = addControl(id, static_cast<float>(inputIt->second.get<double>()));
                    mProgram.list.back().mId = id;
                    mCompiler.mIdToIndex.emplace(id, opId);
                    continue;
                } else if (inputIt->second.is<std::string>() || inputIt->second.is<double>()) {
                    inputs.push_back(resolveInputId(inputIt->second));
                } else if (inputIt->second.is<picojson::array>()) {
                    for (const picojson::value& input : inputIt->second.get<picojson::array>()) {
                        inputs.push_back(resolveInputId(input));
                    }
                } else if (inputIt->second.is<picojson::object>()) {
                    inputs.assign(numInputPorts, -1);
                    for (const auto& namedInput : inputIt->second.get<picojson::object>()) {
                        int inputIndex = -1;
                        for (int port = 0; port < numInputPorts; ++port) {
                            if (prototype->getPortName(Port::In, port) == namedInput.first) {
                                inputIndex = port;
                                break;
                            }
                        }
                        if (inputIndex < 0) {
                            throw std::runtime_error(context + "operator '" + type +
                                "' has no input named '" + namedInput.first + "'");
                        }
                        inputs[inputIndex] = resolveInputId(namedInput.second);
                    }
                } else {
                    throw std::runtime_error(context +
                        "property 'input' must be an operator id, array of ids, or object keyed by input name");
                }
            }
            if (inputs.size() > static_cast<size_t>(numInputPorts)) {
                throw std::runtime_error(context + "has more inputs than operator '" + type + "' accepts");
            }

            const int opId = addOperator(type, std::move(inputs));
            Entry& entry = mProgram.list.back();
            entry.mId = id;
            mCompiler.mIdToIndex.emplace(id, opId);
        }
        compileProgram();
    } catch (...) {
        mProgram = std::move(previousProgram);
        mCompiler = std::move(previousCompiler);
        throw;
    }
}
std::string Processor::getOpListAsJSON() {
    picojson::object root;
    root["version"] = picojson::value(1.0);
    picojson::array operators;

    // IDs are normally supplied by JSON.  Operators introduced internally
    // for numeric literals do not have one, so assign stable export-only IDs.
    std::map<int, std::string> sourceIds;
    std::map<std::string, bool> usedIds;
    for (const Entry& entry : mProgram.list) {
        if (dynamic_cast<const MidiIn*>(entry.op.get()) == nullptr && !entry.mId.empty()) {
            usedIds.emplace(entry.mId, true);
        }
    }
    int generatedId = 0;
    for (const Entry& entry : mProgram.list) {
        if (dynamic_cast<const MidiIn*>(entry.op.get()) != nullptr) {
            sourceIds.emplace(entry.mIndex + MidiIn::Gate, "midi.gate");
            sourceIds.emplace(entry.mIndex + MidiIn::Note, "midi.note");
            sourceIds.emplace(entry.mIndex + MidiIn::Velocity, "midi.velocity");
            continue;
        }
        std::string id = entry.mId;
        if (id.empty()) {
            do {
                id = entry.mType + std::to_string(++generatedId);
            } while (usedIds.count(id) != 0);
            usedIds.emplace(id, true);
        }
        sourceIds.emplace(entry.mIndex, id);
    }

    for (const Entry& entry : mProgram.list) {
        // MIDI is represented by audioVM.py as the implicit midi.* sources.
        if (dynamic_cast<const MidiIn*>(entry.op.get()) != nullptr) {
            continue;
        }

        picojson::object node;
        const auto id = sourceIds.find(entry.mIndex);
        assert(id != sourceIds.end());
        node["type"] = picojson::value(entry.mType);
        node["id"] = picojson::value(id->second);

        if (const auto* constant = dynamic_cast<const ConstOp*>(entry.op.get())) {
            node["input"] = picojson::value(static_cast<double>(constant->mValue));
        } else if (const auto* control = dynamic_cast<const Control*>(entry.op.get())) {
            node["input"] = picojson::value(static_cast<double>(control->mValue));
        } else if (entry.op->getNumPorts(Port::In) > 0) {
            picojson::array inputs;
            const int numInputs = entry.op->getNumPorts(Port::In);
            for (int input = 0; input < numInputs; ++input) {
                const int sourceId = input < static_cast<int>(entry.mInputIds.size())
                    ? entry.mInputIds[input] : -1;
                const auto source = sourceIds.find(sourceId);
                inputs.emplace_back(source == sourceIds.end()
                    ? picojson::value()
                    : picojson::value(source->second));
            }
            node["input"] = picojson::value(inputs);
        }
        operators.emplace_back(node);
    }

    root["program"] = picojson::value(operators);
    return picojson::value(root).serialize(true);
}

std::string Processor::getOpDefinitionsJSON() const {
    // mOpCreators is unordered; sort IDs so the exported definitions are
    // stable, making them suitable for UI caching and file comparisons.
    std::vector<std::string> operatorIds;
    operatorIds.reserve(mOpCreators.size());
    for (const auto& creator : mOpCreators) {
        operatorIds.push_back(creator.first);
    }
    std::sort(operatorIds.begin(), operatorIds.end());

    picojson::array operators;
    for (const std::string& operatorId : operatorIds) {
        OpPtr op = makeOpWithId(operatorId);
        picojson::object definition;
        definition["id"] = picojson::value(operatorId);

        const auto makePorts = [&](Port::Dir direction) {
            picojson::array ports;
            const int count = op->getNumPorts(direction);
            for (int index = 0; index < count; ++index) {
                picojson::object port;
                //port["index"] = picojson::value(static_cast<double>(index));
                port["name"] = picojson::value(op->getPortName(direction, index));
                // Preserve -1 (automatic/unknown), 0 (control-rate), and 1
                // (audio-rate) instead of reducing the registered metadata to
                // a boolean.
                port["rate"] = picojson::value(
                    static_cast<double>(op->isFullRatePort(direction, index)));
                ports.emplace_back(port);
            }
            return ports;
        };

        picojson::array inputs = makePorts(Port::In);
        picojson::array outputs = makePorts(Port::Out);
        definition["inputs"] = picojson::value(inputs);
        definition["outputs"] = picojson::value(outputs);
        operators.emplace_back(definition);
    }

    picojson::object root;
    root["operators"] = picojson::value(operators);
    return picojson::value(root).serialize(true);
}

const int kMidiNoteOn = 0x90, kMidiNoteOff = 0x80;

void Processor::midi(char b0, char b1, char b2) {
    if (mProgram.mMidiInId < 0) {
        return; // no midi
    }
    Entry &E = mProgram.list[mProgram.mMidiInId];
    auto &O = E.mOutputs;
    //O[MidiIn::Channel].value() = b0 & 0x0f; // TODO: handle channel
    int msg = b0 & 0xf0;
    switch (msg) {
        case kMidiNoteOn: {
            O[MidiIn::Gate].value() = 1.f;
            O[MidiIn::Note].value() = b1;
            O[MidiIn::Velocity].value() = b2;
            break;
        }
        case kMidiNoteOff: {
            O[MidiIn::Gate].value() = 0.f;
            O[MidiIn::Note].value() = b1;
            break;
        }
    };
}
    
void Processor::process(float *const* inBufs, float **outBufs, int numSamples) {
    
    int maxBlockSize = mBlockSize;
    for (int ioPos = 0; ioPos < numSamples;) {
        int blockSize = std::min(maxBlockSize, numSamples - ioPos);
        
        for (int n = 0; n < (int)mProgram.list.size(); n++) {
            Entry &E = mProgram.list[n];
            E.op->process(E.mInputs.data(), E.mOutputs.data(), blockSize);
        }
        for (int o = 0; o < (int)mProgram.mAudioOutIds.size(); o++) {
            int id = mProgram.mAudioOutIds[o];
            for (int ch = 0; ch < mNumOutputChannels; ch++) {
                const float *src = mProgram.list[id/OpSpacing].mOutputs[id%OpSpacing].dataWrap(ch);
                float *dst = &outBufs[ch][ioPos];
                for (int i = 0; i < blockSize; i++) {
                    dst[i] = src[i];
                }
            }
        }
        ioPos += blockSize;
    }
}

void Processor::registerType(const std::string& name, OpCreator creator) {
    auto [it, inserted] = mOpCreators.emplace(name, std::move(creator));
    if (!inserted) {
        throw std::runtime_error("Duplicate processor type: " + name);
    }
}

OpPtr Processor::makeOpWithId(const std::string& name) const {
    auto it = mOpCreators.find(name);
    if (it == mOpCreators.end()) {
        throw std::runtime_error("Unknown processor type: " + name);
    }
    return it->second();
}

int Processor::getNumControls() {
    return (int)mProgram.mControlIds.size();
}
float Processor::getControlValue(int controlIndex) {
    int opIndex = mProgram.mControlIds[controlIndex];
    Control *C = dynamic_cast<Control*>(mProgram.list[opIndex].op.get());
    return C->mValue;
}
void Processor::setControlValue(int controlIndex, float value) {
    int opIndex = mProgram.mControlIds[controlIndex];
    Control *C = dynamic_cast<Control*>(mProgram.list[opIndex].op.get());
    C->mValue = value;
}

// test
static inline int testAudioVM() {
    Processor vm;
    std::string opDefJson = vm.getOpDefinitionsJSON();
    printf("%s", opDefJson.c_str());
    const int numChannels = 2, vmBlockSize = 32;
    vm.setup(44100.f, numChannels, numChannels, vmBlockSize);
#if 1
    cStr json = "{\"version\": 1,\"polyphony\":1,\"program\": [{\"type\": \"const\", \"id\": \"const1\", \"input\": 12},{\"type\": \"sub\", \"id\": \"sub1\", \"input\": [\"midi.note\", \"const1\"]},{\"type\": \"oscSin\", \"id\": \"oscSin1\", \"input\": {\"note\": \"sub1\"}},{\"type\": \"oscSaw\", \"id\": \"oscSaw1\", \"input\": {\"note\": \"midi.note\"}},{\"type\": \"envADSR\", \"id\": \"envADSR1\", \"input\": {\"gate\": \"midi.gate\", \"a\": 0.01, \"d\": 0.3, \"s\": 0.5, \"r\": 0.2}},{\"type\": \"envADSR\", \"id\": \"envADSR2\", \"input\": {\"gate\": \"midi.gate\", \"a\": 0.05, \"d\": 1.5, \"s\": 0.0, \"r\": 0.3}},{\"type\": \"const\", \"id\": \"const2\", \"input\": 0.5},{\"type\": \"mul\", \"id\": \"mul1\", \"input\": [\"oscSin1\", \"const2\"]},{\"type\": \"add\", \"id\": \"add1\", \"input\": [\"oscSaw1\", \"mul1\"]},{\"type\": \"mul\", \"id\": \"mul2\", \"input\": [\"add1\", \"envADSR1\"]},{\"type\": \"const\", \"id\": \"const3\", \"input\": 2500.0},{\"type\": \"mul\", \"id\": \"mul3\", \"input\": [\"envADSR2\", \"const3\"]},{\"type\": \"const\", \"id\": \"const4\", \"input\": 400},{\"type\": \"add\", \"id\": \"add2\", \"input\": [\"const4\", \"mul3\"]},{\"type\": \"filterLP\", \"id\": \"filterLP1\", \"input\": {\"input\": \"mul2\", \"frequency\": \"add2\", \"resonance\": 0.5}},{\"type\": \"audioOut\", \"id\": \"audioOut1\", \"input\": \"filterLP1\"}]}";
    vm.loadFromJSON(json);
#else
    //int audioIn = proc.addOperator("audioIn");
    const float kFreq0 = 200.f;
    int freqControl = vm.addControl("freq", kFreq0);
    int osc1 = vm.addOperator("oscSaw", {freqControl});
    
    int audioOut = vm.addOperator("audioOut", {osc1});
    
    vm.compileProgram();
    
    assert(vm.getNumControls() == 1);
    assert(vm.getControlValue(0) == kFreq0);
    //vm.setControlValue(0, 55.f);
#endif
    std::string ops_json = vm.getOpListAsJSON();
    printf("%s", ops_json.c_str());
    
    const int blockSize = 32;
    float tmpBuf[numChannels * blockSize] = {0.f}, *tmpPtrs[numChannels];
    for (int ch = 0; ch < numChannels; ch++) { tmpPtrs[ch] = &tmpBuf[ch * blockSize]; }
    
    vm.midi(kMidiNoteOn, 60, 100);
    vm.process(tmpPtrs, tmpPtrs, blockSize);
    
    return 0;
}
static int didTestAudioVM = testAudioVM();
