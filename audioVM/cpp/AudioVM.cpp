#include "AudioVM.h"
#include "libs/picojson.h"
#include <assert.h>

using namespace AudioVM;

typedef const char* cStr;

// basic operators
class AudioIn : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? 1 : 0; }
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Full; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class AudioOut : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? 1 : 0; }
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Full; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class MidiIn : public Operator {
public:
    enum Type { Gate, Note, Velocity, NumTypes };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? NumTypes : 0; }
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Block; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class Control : public Operator {
public:
    Control(std::string name = "control") { mName = name; }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::In) ? 0 : 1;
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Block; }
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
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Block; }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value() = mValue; }
    float mValue;
    std::string mName;
};

// needed for feedback
class BufferOperator : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return 1; }
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Full; }
    void process(Buffer *i, Buffer *o, int N) override { /*o[0].copy(i[0]);*/ }
};

class ControlToAudio : public Operator {
public:
    int getNumPorts(Port::Dir dir) override { return 1; }
    Port::Rate getPortRate(Port::Dir dir, int index) override { return (dir==Port::In) ? Port::Block : Port::Full; }
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

template <class Operation>
class MathBinary : public BinaryOp {
protected:
    void process(Buffer *i, Buffer *o, int N) override {
        const int channels = std::max(i[0].numChannels, i[1].numChannels);
        assert(i[0].numSamples == i[1].numSamples);
        const int numSamples = std::min(N, i[0].numSamples);
        for (int ch = 0; ch < channels; ++ch) {
            const float *a = i[0].dataWrap(ch);
            const float *b = i[1].dataWrap(ch);
            float *dst = o[0].dataWrap(ch);
            for (int n = 0; n < numSamples; ++n) {
                dst[n] = Operation::apply(a[n], b[n]);
            }
        }
    }
};

struct Add { static float apply(float a, float b) { return a + b; } };
struct Sub { static float apply(float a, float b) { return a - b; } };
struct Mul { static float apply(float a, float b) { return a * b; } };
struct Div { static float apply(float a, float b) { return a / b; } };
struct Pow { static float apply(float a, float b) { return powf(a, b); } };
struct Mod { static float apply(float a, float b) { return fmodf(a, b); } };

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

// some basic ops to get started
static float convertNoteToHz(float note) {
    return (note > 0) * 440 * exp2f((note-69)/12);
}

class OscSin : public Operator {
public:
    enum In  { FreqHz, NoteNumber, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"frequency", "note"};
        cStr o[]={"output"};
        return (dir == Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float note = i[NoteNumber].value();
        float hz = convertNoteToHz(note) + i[FreqHz].value();
        float phaseStep = hz / mSamplerate;
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
    enum In  { FreqHz, NoteNumber, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"frequency", "note"};
        cStr o[]={"output"};
        return (dir == Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float note = i[NoteNumber].value();
        float hz = convertNoteToHz(note) + i[FreqHz].value();
        float phaseStep = hz / mSamplerate;
        float *dst = o[Output].data();
        for (int n = 0; n < N; n++) {
            mUnitPhase += phaseStep;
            mUnitPhase -= floorf(mUnitPhase);
            dst[n] = mUnitPhase*2-1;
        }
    }
    void init() override {
        
    }
    void reset() override {
        mUnitPhase = 0.f;
    }
private:
    float mUnitPhase = 0.f;
};

class FilterLP : public Operator {
public:
    enum In  { Input, FreqHz, Resonance, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getName() { return "FilterLP"; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","frequency","resonance"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float f = std::min(.5f, i[FreqHz].value() / mSamplerate);
        cK = 2 * f * (3.1415f - 2.15f * 2*f);
        float d = 1 - i[Resonance].value();
        cDamp = std::min(2 * d, .99f / cK);
        float *x = i[Input][0], *y = o[Output][0];
        for (int n = 0; n < N; n++) {
            float hp = x[n] - sL - cDamp * sB;
            sB += cK * hp;
            sL += cK * sB;
            y[n] = sL;
        }
    }
    void reset() override {
        sL = sB = 0.f;
    }
private:
    float sL, sB, cK = 0.f, cDamp = 0.f;
};
class FilterSatLP : public Operator {
public:
    enum In  { Input, FreqHz, Resonance, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","frequency","resonance"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float f = std::min(.5f, i[FreqHz].value() / mSamplerate);
        cK = 2 * f * (3.1415f - 2.15f * 2*f);
        float d = 1 - i[Resonance].value();
        cDamp = std::min(2 * d, .99f / cK);
        float *x = i[Input][0], *y = o[Output][0];
        for (int n = 0; n < N; n++) {
            float hp = x[n] - satL - cDamp * sB;
            sB += cK * hp;
            sL += cK * sB;
            envL += .2f * (fabsf(sL) - envL);
            satL = sL * (1 + envL) / (1 + envL * (1 + envL));
            y[n] = satL;
        }
    }
    void reset() override {
        sL = sB = envL = satL = 0.f;
    }
private:
    float sL, sB, envL, satL, cK = 0.f, cDamp = 0.f;
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
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    enum Phase {A, D, S, R};
    void process(Buffer *i, Buffer *o, int N) override {
        float dt = N / mSamplerate;
        bool on = i[Gate].value() > .5f;
        if (on && mPhase >= R) { // todo: add legato parameter
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
                float dur = std::max(1e-9f, i[Release].value());
                mPhase = mPhase + dt / dur;
                mVal = mReleaseVal * (R+1 - mPhase);
                break;
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
    // math functions
    registerType("exp", [] { return std::make_unique<MathExp>(); });
    
    // binary ops
    registerType("add", [] { return std::make_unique<MathBinary<Add>>(); });
    registerType("sub", [] { return std::make_unique<MathBinary<Sub>>(); });
    registerType("mul", [] { return std::make_unique<MathBinary<Mul>>(); });
    registerType("div", [] { return std::make_unique<MathBinary<Div>>(); });
    registerType("pow", [] { return std::make_unique<MathBinary<Pow>>(); });
    registerType("mod", [] { return std::make_unique<MathBinary<Mod>>(); });

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
    OpPtr op = makeOpWithId(operatorId);
    assert(mBlockSize > 0);

    // Output buffer sizes are resolved as each operator is added.  This makes
    // Port::Keep concrete before binary inputs are normalized, so an audio
    // rate value propagated through a Keep output is not mistaken for a
    // control-rate value.
    if (dynamic_cast<BinaryOp*>(op.get()) != nullptr) {
        std::vector<bool> inputIsFullRate(inputIds.size(), false);
        bool hasFullRateInput = false;
        for (size_t input = 0; input < inputIds.size(); ++input) {
            const int sourceId = inputIds[input];
            if (sourceId < 0) {
                continue;
            }
            const int sourceIndex = sourceId / OpSpacing;
            const int sourcePort = sourceId % OpSpacing;
            assert(sourceIndex >= 0 && sourceIndex < static_cast<int>(mProgram.list.size()));
            const Entry& source = mProgram.list[sourceIndex];
            assert(sourcePort >= 0 && sourcePort < static_cast<int>(source.mOutputs.size()));
            inputIsFullRate[input] = source.mOutputs[sourcePort].numSamples == mBlockSize;
            hasFullRateInput = hasFullRateInput || inputIsFullRate[input];
        }
        if (hasFullRateInput) {
            for (size_t input = 0; input < inputIds.size(); ++input) {
                if (inputIds[input] >= 0 && !inputIsFullRate[input]) {
                    inputIds[input] = addOperator("toAudio", {inputIds[input]});
                }
            }
        }
    }

    const int opCount = static_cast<int>(mProgram.list.size());
    Entry &entry = mProgram.list.emplace_back();
    entry.mIndex = opCount * OpSpacing;
    entry.mType = operatorId;
    entry.op = std::move(op);
    entry.op->mSamplerate = mSamplerate;
    entry.op->init();
    entry.mInputIds = inputIds;

    const int numIns = entry.op->getNumPorts(Port::In);
    entry.mInputs.resize(numIns);
    for (int input = 0; input < numIns; ++input) {
        const int sourceId = input < static_cast<int>(inputIds.size()) ? inputIds[input] : -1;
        if (sourceId < 0) {
            entry.mInputs[input] = Buffer(nullptr);
            continue;
        }
        const int sourceIndex = sourceId / OpSpacing;
        const int sourcePort = sourceId % OpSpacing;
        assert(sourceIndex >= 0 && sourceIndex < static_cast<int>(mProgram.list.size()));
        const Entry& source = mProgram.list[sourceIndex];
        assert(sourcePort >= 0 && sourcePort < static_cast<int>(source.mOutputs.size()));
        entry.mInputs[input] = source.mOutputs[sourcePort];
    }

    const int numOuts = entry.op->getNumPorts(Port::Out);
    entry.mOutputs.resize(numOuts);
    for (int output = 0; output < numOuts; ++output) {
        const Port::Rate rate = entry.op->getPortRate(Port::Out, output);
        const int numSamples = rate == Port::Full ? mBlockSize
            : rate == Port::Block ? 1
            : numIns > 0 ? entry.mInputs[0].numSamples : 1;
        entry.mOutputs[output] = Buffer(nullptr, numSamples);
    }
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
    mProgram.mAudioOutIds.clear();
    mProgram.mAudioInIds.clear();
    mProgram.mControlIds.clear();
    mProgram.mMidiInId = -1;

    int size = 1; // Keep a zero value for unconnected control-rate inputs.
    for (const Entry& entry : mProgram.list) {
        for (const Buffer& output : entry.mOutputs) {
            size += output.size();
        }
    }
    mProgram.mIoBuffer.assign(size, 0.f);

    int pos = 1;
    for (int n = 0; n < static_cast<int>(mProgram.list.size()); ++n) {
        Entry& entry = mProgram.list[n];
        for (int input = 0; input < static_cast<int>(entry.mInputs.size()); ++input) {
            const int sourceId = input < static_cast<int>(entry.mInputIds.size()) ? entry.mInputIds[input] : -1;
            if (sourceId < 0) {
                entry.mInputs[input].ptr = &mProgram.mIoBuffer[0];
                continue;
            }
            const Entry& source = mProgram.list[sourceId / OpSpacing];
            entry.mInputs[input].ptr = source.mOutputs[sourceId % OpSpacing].ptr;
        }
        for (Buffer& output : entry.mOutputs) {
            output.ptr = &mProgram.mIoBuffer[pos];
            pos += output.size();
        }
        if (dynamic_cast<AudioOut*>(entry.op.get())) {
            mProgram.mAudioOutIds.push_back(entry.mInputIds[0]);
        }
        if (dynamic_cast<Control*>(entry.op.get())) {
            mProgram.mControlIds.push_back(n);
        }
        if (dynamic_cast<MidiIn*>(entry.op.get())) {
            mProgram.mMidiInId = n;
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
                    static_cast<double>(op->getPortRate(direction, index)));
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

void Processor::midiEvent(char b0, char b1, char b2) {
    if (mProgram.mMidiInId < 0) {
        return; // no midi
    }
    Entry &E = mProgram.list[mProgram.mMidiInId];
    auto &O = E.mOutputs;
    //O[MidiIn::Channel].value() = b0 & 0x0f; // TODO: handle channel
    int msg = b0 & 0xf0;
    switch (msg) {
        case kMidiNoteOn: {
            O[MidiIn::Gate].set(1.f);
            O[MidiIn::Note].set(b1);
            O[MidiIn::Velocity].set(b2);
            mCurrMidiNote = b1; // TODO: polyphony
            break;
        }
        case kMidiNoteOff: {
            if (b1 == mCurrMidiNote) {
                O[MidiIn::Gate].set(0.f);
            }
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

#if 0 // test
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
    
    vm.midiEvent(kMidiNoteOn, 60, 100);
    vm.process(tmpPtrs, tmpPtrs, blockSize);
    
    return 0;
}
static int didTestAudioVM = testAudioVM();
#endif
