#include "AudioVM.h"
#include "libs/picojson.h"

using namespace AudioVM;

typedef const char* cStr;

// basic operators
class AudioIn : public Operator {
public:
    int getNumPorts(PortDir dir) override { return (dir == Output) ? 1 : 0; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class AudioOut : public Operator {
public:
    int getNumPorts(PortDir dir) override { return (dir == Input) ? 1 : 0; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class MidiIn : public Operator {
public:
    enum Type { Gate, Note, Velocity, NumTypes };
    int getNumPorts(PortDir dir) override { return (dir == Output) ? NumTypes : 0; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
class Control : public Operator {
public:
    Control(std::string name = "control") { mName = name; }
    int getNumPorts(PortDir dir) override {
        return (dir == Input) ? 0 : 1;
    }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value = mValue; }
    float mValue;
    std::string mName;
};

// needed for feedback
class BufferOperator : public Operator {
public:
    int getNumPorts(PortDir dir) override { return 1; }
    void process(Buffer *i, Buffer *o, int N) override { /*o[0].copy(i[0]);*/ }
};

class CvToBuffer : public Operator {
public:
    void process(Buffer *i, Buffer *o, int N) override {
        float v = state, next = i[0].value, step = (next-v) / N;
        float *dst = o[0].data();
        for (int n = 0; n < N; n++) {
            dst[n] = v + step * (n + 1);
        }
        state = next;
    }
private:
    float state = 0.f;
};

// math nodes
class Constant : public Operator {
public:
    int getNumPorts(PortDir dir) {
        return (dir == Output) ? 1 : 0;
    }
    void process(Buffer *i, Buffer *o, int N) { o[0].value = mValue; }
private:
    float mValue;
};
class ControlOp1 : public Operator {
public:
    int getNumPorts(PortDir dir) override {
        return 1;
    }
};
class ControlOp2 : public Operator {
    public: int getNumPorts(PortDir dir) override {
        return (dir == Input) ? 2 : 1;
    }
};

class MultiplyOperator : public ControlOp2 {
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

class MathExp : public ControlOp1 {
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
class MathLog : public ControlOp1 {
public: void process(Buffer *i, Buffer *o) { o[0] = logf(i[0]); }
};
class MathCos : public ControlOp1 {
public: void process(Buffer *i, Buffer *o) { o[0] = cosf(i[0]); }
};
class MathSin : public ControlOp1 {
public: void process(Buffer *i, Buffer *o) { o[0] = sinf(i[0]); }
};
class MathAdd : public ControlOp2 {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] + i[1]; }
};
class MathSub : public ControlOp2 {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] - i[1]; }
};
class MathMul : public ControlOp2 {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] * i[1]; }
};
class MathDiv : public ControlOp2 {
public: void process(Buffer *i, Buffer *o) { o[0] = i[0] / i[1]; }
};
 */

// some basic nodes to get started
class OscSin : public Operator {
public:
    enum In { FreqHz, NumInputs };
    int getNumPorts(PortDir dir) override { return (dir == Output) ? 1 : NumInputs; }
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
    enum In { FreqHz, NumInputs };
    int getNumPorts(PortDir dir) override { return (dir == Output) ? 1 : NumInputs; }
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
    enum In  { In, FreqHz, Resonance, NumInputs };
    enum Out { Out, NumOutputs };
    std::string getPortName(PortDir dir, int index) override {
        cStr i[]={"In","Frequency","Resonance"};
        cStr o[]={"Out"};
        return (dir==Operator::Input) ? i[index] : o[index];
    }
    int getNumPorts(PortDir dir) override {
        return (dir == Operator::Output) ? NumOutputs : NumInputs;
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
    enum In { Gate, Attack, Decay, Sustain, Release, NumInputs };
    enum Out { Out, NumOutputs };
    int getNumPorts(PortDir dir) override {
        return (dir == Output) ? NumOutputs : NumInputs;
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


}

void Processor::setup(float samplerate, int numInputChannels, int numOutputChannels) {
    mSamplerate = samplerate;
    mNumInputChannels = numInputChannels;
    mNumOutputChannels = numOutputChannels;
}

void Processor::clearProgram() {
    mProgram.list.clear();
}
int Processor::addOperator(std::string operatorId, int *inputs) {
    int opCount = (int)mProgram.list.size();
    Entry &entry = mProgram.list.emplace_back();
    entry.mIndex = opCount * OperatorIndexSpacing;
    entry.op = create(operatorId);
    entry.op->mSamplerate = mSamplerate;
    entry.op->init();
    return entry.mIndex;
}
bool Processor::compileProgram() {
    
}

void Processor::loadFromJSON(std::string jsonString) {
    picojson::value value;
    std::string err;
    picojson::parse(value, jsonString);

}
    
void Processor::process(float *const* inBufs, float **outBufs, int numSamples) {
    if (numSamples) {
        
    }
}


void Processor::processBlock(float *const* inBufs, float **outBufs) {
    // iterate through program

}

void Processor::registerType(const std::string& name, OpCreator creator) {
    auto [it, inserted] = mOpCreators.emplace(name, std::move(creator));
    if (!inserted) {
        throw std::runtime_error("Duplicate processor type: " + name);
    }
}

std::unique_ptr<Operator> Processor::create(const std::string& name) const {
    auto it = mOpCreators.find(name);
    if (it == mOpCreators.end()) {
        throw std::runtime_error("Unknown processor type: " + name);
    }
    return it->second();
}

// test
static inline int testAudioVM() {
    Processor vm;
    const int numChannels = 2;
    vm.setup(44100.f, numChannels, numChannels);
    
    //int audioIn = proc.addOperator("audioIn");
    int pitchControl = vm.addOperator("control");
    int osc1 = vm.addOperator("oscSaw", &pitchControl);
    
    int audioOut = vm.addOperator("audioOut");
    
    //((Control*)vm.getOperator(pitchControl))->mValue = 200.f;
    vm.compileProgram();
    const int blockSize = 32;
    float tmpBuf[numChannels * blockSize] = {0.f}, *tmpPtrs[numChannels];
    for (int ch = 0; ch < numChannels; ch++) { tmpPtrs[ch] = &tmpBuf[ch * blockSize]; }
    
    vm.process(tmpPtrs, tmpPtrs, blockSize);
    
    return 0;
}
static int didTestAudioGraph = testAudioVM();
