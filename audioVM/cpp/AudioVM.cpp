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
    enum In { FreqHz, NumInputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? 1 : NumInputs; }
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
        cStr i[]={"In","Frequency","Resonance"};
        cStr o[]={"Out"};
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
        float *x = i[Input][0], *y = o[Port::Out][0];
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
        cStr i[]={"gate","attack","decay","sustain","release"};
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
        bool on = i[Gate][0][0] > .5f;
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
                float dur = std::max(1e-9f, i[Attack][0][0]);
                mPhase = std::min((float)D, mPhase + dt / dur);
                mVal = mPhase;
                break;
            }
            case D: {
                float dur = std::max(1e-9f, i[Decay][0][0]);
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
                float dur = std::max(1e-9f, i[Decay][0][0]);
                mPhase = mPhase + dt / dur;
                mVal = mReleaseVal * (R+1 - mPhase);
            }
            default:
                mVal = 0.f;
        }
        o[Output][0][0] = mVal;
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
    registerType("buffer", [] { return std::make_unique<BufferOperator>(); });
    registerType("toAudio", [] { return std::make_unique<ControlToAudio>(); });
    // basic math
    registerType("exp", [] { return std::make_unique<MathExp>(); });
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
int Processor::addOperator(std::string operatorId, std::vector<int> inputs) {
    int opCount = (int)mProgram.list.size();
    OpPtr op = makeOpWithId(operatorId);
    if (auto* bin = dynamic_cast<BinaryOp*>(op.get())) {
        // check if we need to convert an input
        int sizes[2], maxSize = 0;
        for (int i = 0; i < 2; i++) {
            sizes[i] = mProgram.list[inputs[i]/OpSpacing].mOutputs[inputs[i]%OpSpacing].numSamples;
            maxSize = std::max(maxSize, sizes[i]);
        }
        for (int i = 0; i < 2; i++) {
            if (sizes[i] < maxSize) { // TODO: check if conversion already exists
                inputs[i] = addOperator("toAudio", {inputs[i]});
            }
        }
    }
    Entry &entry = mProgram.list.emplace_back();
    entry.mIndex = opCount * OpSpacing;
    entry.op = std::move(op);
    entry.op->mSamplerate = mSamplerate;
    entry.op->init();
    
    // setup inputs and outputs
    entry.mInputIds = inputs;
    return entry.mIndex;
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
            }
            
        }
        if (pass == 0) { // allocate buffer memory
            mProgram.mIoBuffer.resize(pos);
            mProgram.mIoBuffer[0] = 0.f;
        }
    }
    
}

void Processor::loadFromJSON(std::string jsonString) {
    picojson::value value;
    std::string err;
    picojson::parse(value, jsonString);

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
    const int numChannels = 2, vmBlockSize = 32;
    vm.setup(44100.f, numChannels, numChannels, vmBlockSize);
    
    //int audioIn = proc.addOperator("audioIn");
    int pitchControl = vm.addOperator("control");
    int osc1 = vm.addOperator("oscSaw", {pitchControl});
    
    int audioOut = vm.addOperator("audioOut", {osc1});
    
    vm.compileProgram();
    
    vm.getNumControls();
    vm.setControlValue(0, 55.f);
    
    const int blockSize = 32;
    float tmpBuf[numChannels * blockSize] = {0.f}, *tmpPtrs[numChannels];
    for (int ch = 0; ch < numChannels; ch++) { tmpPtrs[ch] = &tmpBuf[ch * blockSize]; }
    
    vm.process(tmpPtrs, tmpPtrs, blockSize);
    
    return 0;
}
static int didTestAudioVM = testAudioVM();
