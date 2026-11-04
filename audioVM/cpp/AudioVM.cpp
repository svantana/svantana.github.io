#include "AudioVM.h"
#include "libs/picojson.h"
#include <assert.h>

using namespace AudioVM;

typedef const char* cStr;

// basic operators
struct FullRateOp : public Operator {
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Full; }
};
struct BlockRateOp : public Operator {
    Port::Rate getPortRate(Port::Dir dir, int index) override { return Port::Block; }
};

struct AudioIn : public FullRateOp {
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? 1 : 0; }
    void process(Buffer *i, Buffer *o, int N) override {}
};
struct AudioOut : public FullRateOp {
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? 1 : 0; }
};
struct MidiIn : public BlockRateOp {
    enum Type { Gate, Note, Velocity, NumTypes };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? NumTypes : 0; }
};
struct PolyStart : public BlockRateOp {
    enum In { NumVoices, TimeOut, NumInputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[] = {"numVoices", "timeOut"};
        return i[index];
    }
};
struct PolyEnd : public BlockRateOp {
    int getNumPorts(Port::Dir dir) override { return 0; }
};

struct Control : public BlockRateOp {
    Control(std::string name = "control") { mName = name; }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::In) ? 0 : 1;
    }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value() = mValue; }
    float mValue;
    std::string mName;
};
struct ConstOp : public BlockRateOp {
    ConstOp(std::string name = "const") { mName = name; }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::In) ? 0 : 1;
    }
    void process(Buffer *i, Buffer *o, int N) override { o[0].value() = mValue; }
    float mValue;
    std::string mName;
};

// needed for feedback and sums
struct BufferOperator : public FullRateOp {
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? 1 : 0; }
    void process(Buffer *i, Buffer *o, int N) override {
        for (int ch = 0; ch < o[0].numChannels; ++ch) {
            float* dst = o[0].data(ch);
            for (int n = 0; n < N; ++n) {
                dst[n] = 0.f;
            }
        }
    }
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
    void reset() override { state = 0.f; }
private:
    float state = 0.f;
};

// math ops
struct UnaryOp : public Operator {
    int getNumPorts(Port::Dir dir) override {
        return 1;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        assert(i[0].numSamples == o[0].numSamples);
        for (int ch = 0; ch < i->numChannels; ch++) {
            for (int n = 0; n < i->numSamples; n++) {
                o->data(ch)[n] = expf(i->data(ch)[n]);
            }
        }
    }
};
struct BinaryOp : public Operator {
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? 2 : 1; }
};

struct Add { static float apply(float a, float b) { return a + b; } };
struct Sub { static float apply(float a, float b) { return a - b; } };
struct Mul { static float apply(float a, float b) { return a * b; } };
struct Div { static float apply(float a, float b) { return a / b; } };
struct Pow { static float apply(float a, float b) { return powf(a, b); } };
struct Mod { static float apply(float a, float b) { return fmodf(a, b); } };
struct Equal { static float apply(float a, float b) { return a == b ? 1.f : 0.f; } };
struct NotEqual { static float apply(float a, float b) { return a != b ? 1.f : 0.f; } };
struct Less { static float apply(float a, float b) { return a < b ? 1.f : 0.f; } };
struct LessEqual { static float apply(float a, float b) { return a <= b ? 1.f : 0.f; } };
struct Greater { static float apply(float a, float b) { return a > b ? 1.f : 0.f; } };
struct GreaterEqual { static float apply(float a, float b) { return a >= b ? 1.f : 0.f; } };

// Mutates an existing BufferOperator output. It has no output of its own: the
// first input is the mutable target and the second is the incoming value.
class InplaceOp : public BinaryOp {
public:
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? 2 : 0; }
};

template <class Operation>
class InplaceBinaryOp : public InplaceOp {
public:
    void process(Buffer *i, Buffer *o, int N) override {
        assert(i[0].numSamples == i[1].numSamples);
        const int channels = std::max(i[0].numChannels, i[1].numChannels);
        for (int ch = 0; ch < channels; ++ch) {
            float* dst = i[0].dataWrap(ch);
            const float* src = i[1].dataWrap(ch);
            for (int n = 0; n < N; ++n) {
                dst[n] = Operation::apply(dst[n], src[n]);
            }
        }
    }
};

template <class Operation>
class MathBinary : public BinaryOp {
protected:
    void process(Buffer *i, Buffer *o, int N) override {
        const int channels = std::max(i[0].numChannels, i[1].numChannels);
        assert(i[0].numSamples == i[1].numSamples);
        assert(i[0].numSamples == o[0].numSamples);
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
class MathFloor : public UnaryOp {
public:
    void process(Buffer *i, Buffer *o, int N) override {
        for (int ch = 0; ch < i->numChannels; ++ch) {
            for (int n = 0; n < i->numSamples; ++n) {
                o->data(ch)[n] = floorf(i->data(ch)[n]);
            }
        }
    }
};
class MathRound : public UnaryOp {
public:
    void process(Buffer *i, Buffer *o, int N) override {
        for (int ch = 0; ch < i->numChannels; ++ch) {
            for (int n = 0; n < i->numSamples; ++n) {
                o->data(ch)[n] = roundf(i->data(ch)[n]);
            }
        }
    }
};
class MathCeil : public UnaryOp {
public:
    void process(Buffer *i, Buffer *o, int N) override {
        for (int ch = 0; ch < i->numChannels; ++ch) {
            for (int n = 0; n < i->numSamples; ++n) {
                o->data(ch)[n] = ceilf(i->data(ch)[n]);
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
    static inline float fastUnitSinUnsafe(float x) {
        float b = x * (1-x);
        return b * (.5f-x) * (12.63816744f + b * (35.870132f + 56.16403226f * b));
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float note = i[NoteNumber].value();
        float hz = convertNoteToHz(note) + i[FreqHz].value();
        float phaseStep = hz / mSamplerate;
        for (int n = 0; n < N; n++) {
            mUnitPhase += phaseStep;
            mUnitPhase -= floorf(mUnitPhase);
            o->data()[n] = fastUnitSinUnsafe(mUnitPhase);
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

class OscNoise : public Operator {
public:
    enum In  { Gain, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"gain"};
        cStr o[]={"output"};
        return (dir == Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    static float randSigned() {
        static int seed = 0;
        seed = 1664525 * seed + 1013904223;
        return 2.3283064365386963e-10f * (float)seed;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float *dst = o[Output].data(); //, scale = i[Gain].value();
        for (int n = 0; n < N; n++) {
            dst[n] = randSigned();
        }
    }
    void init() override {
    }
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
        Coefs T = getCoefs(i), C = mCoefs;
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0], step = 1.f / o[0].numSamples;
        for (int n = 0; n < N; n++) {
            C.k += step * (T.k - C.k);
            C.d += step * (T.d - C.d);
            float hp = x[n] - S.l - C.d * S.b;
            S.b += C.k * hp;
            S.l += C.k * S.b;
            y[n] = S.l;
        }
        mState = S;
        mCoefs = C;
    }
    void reset() override {
        mState.reset();
    }
    struct State { float l, b; void reset() {l=b=0;} } mState;
    struct Coefs { float k, d; } mCoefs;
    Coefs getCoefs(Buffer *i) {
        Coefs C;
        float f = std::min(.5f, i[FreqHz].value() / mSamplerate);
        C.k = 2 * f * (3.1415f - 2.15f * 2*f);
        float d = 1 - i[Resonance].value();
        C.d = std::min(2 * d, .99f / C.k);
        return C;
    }
};
class FilterBP : public FilterLP {
public:
    std::string getName() { return "FilterBP"; }
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0], step = 1.f / o[0].numSamples;
        for (int n = 0; n < N; n++) {
            C.k += step * (T.k - C.k);
            C.d += step * (T.d - C.d);
            float hp = x[n] - S.l - C.d * S.b;
            S.b += C.k * hp;
            S.l += C.k * S.b;
            y[n] = S.b;
        }
        mState = S;
        mCoefs = C;
    }
};
class FilterHP : public FilterLP {
public:
    std::string getName() { return "FilterHP"; }
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0], step = 1.f / o[0].numSamples;
        for (int n = 0; n < N; n++) {
            C.k += step * (T.k - C.k);
            C.d += step * (T.d - C.d);
            float hp = x[n] - S.l - C.d * S.b;
            S.b += C.k * hp;
            S.l += C.k * S.b;
            y[n] = hp;
        }
        mState = S;
        mCoefs = C;
    }
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

class EnvExp : public Operator {
public:
    enum In  { Gate, DecayTime, Repeats, RepeatTime, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"gate","decay","repeats","repeatTime"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float dt = N / mSamplerate;
        float gate = i[Gate].value(), tHalf = .31f * i[DecayTime].value(), repeats = i[Repeats].value();
        mPhase = (gate > mGate) ? -1e-20f : mPhase;
        mGate = gate;
        float phaseStep = dt / std::max(1e-20f, i[RepeatTime].value());
        float prevStep = ceilf(mPhase);
        mPhase += phaseStep * (mPhase <= repeats);
        mVal = (mPhase > prevStep) ? 1 : (mVal > 1e-30f) * mVal * tHalf / (tHalf + dt);
        o[Output].set(mVal);
    }
private:
    float mVal, mGate, mPhase;
};

class EnvFollower : public Operator {
public:
    enum In  { Input, Attack, Release, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","attack","release"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (index==0) ? Port::Keep : Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    void process(Buffer *i, Buffer *o, int numSamples) override {
        int N = std::min(numSamples, i[0].numSamples);
        float tA = i[Attack].value(), tR = i[Release].value();
        float cA = 1/(1+tA*mSamplerate), cR = 1/(1+tR*mSamplerate);
        float *x = i[Input].data(), *e = o[Output].data();
        for (int n = 0; n < N; n++) {
            float a = fabsf(x[n]);
            mEnv += ((a > mEnv) ? cA : cR) * (a - mEnv);
            e[n] = mEnv;
        }
    }
private:
    float mEnv;
};



const char *kIdAudioIn = "audioIn", *kIdAudioOut = "audioOut";

Processor::Processor()
{
    registerType(kIdAudioIn , [] { return std::make_unique<AudioIn>(); });
    registerType(kIdAudioOut, [] { return std::make_unique<AudioOut>(); });
    registerType("midiIn", [] { return std::make_unique<MidiIn>(); });
    registerType("polyStart", [] { return std::make_unique<PolyStart>(); });
    registerType("polyEnd", [] { return std::make_unique<PolyEnd>(); });
    registerType("control", [] { return std::make_unique<Control>(); });
    registerType("const", [] { return std::make_unique<ConstOp>(); });
    registerType("buffer", [] { return std::make_unique<BufferOperator>(); });
    registerType("iadd", [] { return std::make_unique<InplaceBinaryOp<Add>>(); });
    registerType("isub", [] { return std::make_unique<InplaceBinaryOp<Sub>>(); });
    registerType("imul", [] { return std::make_unique<InplaceBinaryOp<Mul>>(); });
    registerType("idiv", [] { return std::make_unique<InplaceBinaryOp<Div>>(); });
    registerType("toAudio", [] { return std::make_unique<ControlToAudio>(); });
    // math functions
    registerType("exp", [] { return std::make_unique<MathExp>(); });
    registerType("floor", [] { return std::make_unique<MathFloor>(); });
    registerType("round", [] { return std::make_unique<MathRound>(); });
    registerType("ceil", [] { return std::make_unique<MathCeil>(); });
    
    // binary ops
    registerType("add", [] { return std::make_unique<MathBinary<Add>>(); });
    registerType("sub", [] { return std::make_unique<MathBinary<Sub>>(); });
    registerType("mul", [] { return std::make_unique<MathBinary<Mul>>(); });
    registerType("div", [] { return std::make_unique<MathBinary<Div>>(); });
    registerType("pow", [] { return std::make_unique<MathBinary<Pow>>(); });
    registerType("mod", [] { return std::make_unique<MathBinary<Mod>>(); });
    registerType("equal", [] { return std::make_unique<MathBinary<Equal>>(); });
    registerType("notEqual", [] { return std::make_unique<MathBinary<NotEqual>>(); });
    registerType("less", [] { return std::make_unique<MathBinary<Less>>(); });
    registerType("lessEqual", [] { return std::make_unique<MathBinary<LessEqual>>(); });
    registerType("greater", [] { return std::make_unique<MathBinary<Greater>>(); });
    registerType("greaterEqual", [] { return std::make_unique<MathBinary<GreaterEqual>>(); });

    registerType("oscSin"  , [] { return std::make_unique<OscSin>(); });
    registerType("oscSaw"  , [] { return std::make_unique<OscSaw>(); });
    registerType("oscNoise", [] { return std::make_unique<OscNoise>(); });
    registerType("filterLP", [] { return std::make_unique<FilterLP>(); });
    registerType("filterBP", [] { return std::make_unique<FilterBP>(); });
    registerType("filterHP", [] { return std::make_unique<FilterHP>(); });
    registerType("envADSR" , [] { return std::make_unique<EnvADSR>(); });
    registerType("envExp"  , [] { return std::make_unique<EnvExp>(); });
    registerType("envFollow", [] { return std::make_unique<EnvFollower>(); });

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
    mPolyPrograms.clear();
    mVoiceAge = 0;
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

    mPolyPrograms.clear();
    mVoiceAge = 0;
    int polyStartCount = 0;
    for (const Entry& entry : mProgram.list) {
        polyStartCount += dynamic_cast<const PolyStart*>(entry.op.get()) != nullptr;
    }
    mPolyPrograms.reserve(polyStartCount);

    int openRange = -1;
    for (int index = 0; index < static_cast<int>(mProgram.list.size()); ++index) {
        const Entry& entry = mProgram.list[index];
        if (dynamic_cast<const PolyStart*>(entry.op.get()) != nullptr) {
            if (openRange >= 0) {
                throw std::runtime_error("Nested polyphonic ranges are not supported");
            }
            if (entry.mInputIds.empty() || entry.mInputIds[PolyStart::NumVoices] < 0) {
                throw std::runtime_error("polyStart requires a numVoices input");
            }
            const int sourceId = entry.mInputIds[PolyStart::NumVoices];
            const Entry& source = mProgram.list[sourceId / OpSpacing];
            float numVoicesValue = 0.f;
            if (const auto* constant = dynamic_cast<const ConstOp*>(source.op.get())) {
                numVoicesValue = constant->mValue;
            } else if (const auto* control = dynamic_cast<const Control*>(source.op.get())) {
                numVoicesValue = control->mValue;
            } else {
                throw std::runtime_error("polyStart numVoices must be a constant or control");
            }
            const int numVoices = static_cast<int>(roundf(numVoicesValue));
            if (numVoices < 1 || numVoices > 128 || numVoicesValue != static_cast<float>(numVoices)) {
                throw std::runtime_error("polyStart numVoices must be an integer between 1 and 128");
            }
            mPolyPrograms.emplace_back();
            PolyRange& range = mPolyPrograms.back();
            range.startIndex = index;
            range.voices.resize(numVoices);
            if (entry.mInputIds.size() > PolyStart::TimeOut && entry.mInputIds[PolyStart::TimeOut] >= 0) {
                const int timeoutSourceId = entry.mInputIds[PolyStart::TimeOut];
                const int timeoutSourceIndex = timeoutSourceId / OpSpacing;
                const Entry& timeoutSource = mProgram.list[timeoutSourceIndex];
                if (const auto* constant = dynamic_cast<const ConstOp*>(timeoutSource.op.get())) {
                    range.timeOutSeconds = constant->mValue;
                } else if (const auto* control = dynamic_cast<const Control*>(timeoutSource.op.get())) {
                    range.timeOutSeconds = control->mValue;
                    range.timeOutControlIndex = timeoutSourceIndex;
                } else {
                    throw std::runtime_error("polyStart timeOut must be a constant or control");
                }
                if (range.timeOutSeconds < 0.f) {
                    throw std::runtime_error("polyStart timeOut cannot be negative");
                }
            }
            openRange = static_cast<int>(mPolyPrograms.size()) - 1;
        } else if (dynamic_cast<const PolyEnd*>(entry.op.get()) != nullptr) {
            if (openRange < 0) {
                throw std::runtime_error("polyEnd has no matching polyStart");
            }
            mPolyPrograms[openRange].endIndex = index;
            openRange = -1;
        }
    }
    if (openRange >= 0) {
        throw std::runtime_error("polyStart has no matching polyEnd");
    }

    for (PolyRange& range : mPolyPrograms) {
        for (int index = range.startIndex + 1; index < range.endIndex; ++index) {
            const Entry& entry = mProgram.list[index];
            for (int input = 0; input < static_cast<int>(entry.mInputIds.size()); ++input) {
                const int sourceId = entry.mInputIds[input];
                if (sourceId < 0) continue;
                const int sourceIndex = sourceId / OpSpacing;
                if (dynamic_cast<const InplaceOp*>(entry.op.get()) != nullptr && input == 0) {
                    if (dynamic_cast<const BufferOperator*>(mProgram.list[sourceIndex].op.get()) == nullptr) {
                        throw std::runtime_error("in-place operations require a buffer target");
                    }
                    if (sourceIndex >= range.endIndex || sourceIndex == range.startIndex) {
                        throw std::runtime_error("polyphonic in-place targets must be local or precede polyStart");
                    }
                }
            }
        }
        for (int index = range.endIndex + 1; index < static_cast<int>(mProgram.list.size()); ++index) {
            const Entry& entry = mProgram.list[index];
            for (const int sourceId : entry.mInputIds) {
                if (sourceId >= 0 && sourceId / OpSpacing > range.startIndex &&
                    sourceId / OpSpacing < range.endIndex) {
                    throw std::runtime_error("polyphonic outputs must be accumulated into a global buffer with an in-place operation");
                }
            }
        }

        for (Voice& voice : range.voices) {
            const int cloneCount = range.endIndex - range.startIndex - 1;
            voice.list.resize(cloneCount);
            int bufferSize = 1;
            for (int offset = 0; offset < cloneCount; ++offset) {
                const Entry& source = mProgram.list[range.startIndex + 1 + offset];
                Entry& clone = voice.list[offset];
                clone.mIndex = source.mIndex;
                clone.mId = source.mId;
                clone.mType = source.mType;
                clone.mInputIds = source.mInputIds;
                clone.op = makeOpWithId(source.mType);
                clone.op->mSamplerate = mSamplerate;
                clone.op->init();
                if (const auto* constant = dynamic_cast<const ConstOp*>(source.op.get())) {
                    auto* cloneConstant = dynamic_cast<ConstOp*>(clone.op.get());
                    cloneConstant->mName = constant->mName;
                    cloneConstant->mValue = constant->mValue;
                } else if (const auto* control = dynamic_cast<const Control*>(source.op.get())) {
                    auto* cloneControl = dynamic_cast<Control*>(clone.op.get());
                    cloneControl->mName = control->mName;
                    cloneControl->mValue = control->mValue;
                }
                clone.mInputs.resize(source.mInputs.size());
                clone.mOutputs = source.mOutputs;
                for (const Buffer& output : clone.mOutputs) bufferSize += output.size();
            }
            voice.mIoBuffer.assign(bufferSize, 0.f);
            int position = 1;
            for (Entry& clone : voice.list) {
                for (Buffer& output : clone.mOutputs) {
                    output.ptr = &voice.mIoBuffer[position];
                    position += output.size();
                }
            }
        }

        // Link cloned inputs after the whole voice range has allocated its
        // buffers, so local wires resolve to the corresponding voice clone.
        for (Voice& voice : range.voices) {
            for (int offset = 0; offset < static_cast<int>(voice.list.size()); ++offset) {
                Entry& clone = voice.list[offset];
                for (int input = 0; input < static_cast<int>(clone.mInputs.size()); ++input) {
                    const int sourceId = clone.mInputIds[input];
                    if (sourceId < 0) {
                        clone.mInputs[input].ptr = &voice.mIoBuffer[0];
                        continue;
                    }
                    const int sourceIndex = sourceId / OpSpacing;
                    const int sourcePort = sourceId % OpSpacing;
                    if (sourceIndex > range.startIndex && sourceIndex < range.endIndex) {
                        clone.mInputs[input] = voice.list[sourceIndex - range.startIndex - 1].mOutputs[sourcePort];
                    } else if (sourceIndex == mProgram.mMidiInId) {
                        clone.mInputs[input] = Buffer(&voice.mMidiValues[sourcePort]);
                    } else {
                        clone.mInputs[input] = mProgram.list[sourceIndex].mOutputs[sourcePort];
                    }
                }
            }
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
    std::vector<PolyRange> previousPolyPrograms = std::move(mPolyPrograms);
    const unsigned long long previousVoiceAge = mVoiceAge;
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
        mPolyPrograms = std::move(previousPolyPrograms);
        mVoiceAge = previousVoiceAge;
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

void Processor::resetVoice(Voice& voice) {
    for (Entry& entry : voice.list) {
        entry.op->reset();
    }
    for (float& value : voice.mIoBuffer) {
        value = 0.f;
    }
}

void Processor::processPolyRange(PolyRange& range, int numSamples) {
    for (Voice& voice : range.voices) {
        if (!voice.active) continue;
        for (Entry& entry : voice.list) {
            entry.op->process(entry.mInputs.data(), entry.mOutputs.data(), numSamples);
        }
        if (voice.mMidiValues[MidiIn::Gate] == 0.f && voice.releaseSamples >= 0) {
            voice.releaseSamples -= numSamples;
            if (voice.releaseSamples <= 0) voice.active = false;
        }
    }
}

void Processor::midiEvent(char b0, char b1, char b2) {
    if (mProgram.mMidiInId < 0) {
        return; // no midi
    }
    Entry &E = mProgram.list[mProgram.mMidiInId];
    auto &O = E.mOutputs;
    //O[MidiIn::Channel].value() = b0 & 0x0f; // TODO: handle channel
    const int note = static_cast<unsigned char>(b1);
    const int velocity = static_cast<unsigned char>(b2);
    int msg = static_cast<unsigned char>(b0) & 0xf0;
    if (msg == kMidiNoteOn && velocity == 0) msg = kMidiNoteOff;
    switch (msg) {
        case kMidiNoteOn: {
            O[MidiIn::Gate].set(1.f);
            O[MidiIn::Note].set(note);
            O[MidiIn::Velocity].set(velocity);
            mCurrMidiNote = note;
            for (PolyRange& range : mPolyPrograms) {
                Voice* selected = nullptr;
                for (Voice& voice : range.voices) {
                    if (!voice.active) {
                        selected = &voice;
                        break;
                    }
                }
                if (selected == nullptr) {
                    selected = &range.voices.front();
                    for (Voice& voice : range.voices) {
                        if (voice.age < selected->age) selected = &voice;
                    }
                }
                resetVoice(*selected);
                selected->active = true;
                selected->note = note;
                selected->releaseSamples = -1;
                selected->age = ++mVoiceAge;
                selected->mMidiValues[MidiIn::Gate] = 1.f;
                selected->mMidiValues[MidiIn::Note] = static_cast<float>(note);
                selected->mMidiValues[MidiIn::Velocity] = static_cast<float>(velocity);
            }
            break;
        }
        case kMidiNoteOff: {
            if (b1 == mCurrMidiNote) {
                O[MidiIn::Gate].set(0.f);
            }
            for (PolyRange& range : mPolyPrograms) {
                for (Voice& voice : range.voices) {
                    if (voice.active && voice.note == note) {
                        voice.mMidiValues[MidiIn::Gate] = 0.f;
                        if (range.timeOutSeconds >= 0.f) {
                            voice.releaseSamples = static_cast<int>(ceilf(range.timeOutSeconds * mSamplerate));
                            if (voice.releaseSamples == 0) voice.active = false;
                        }
                    }
                }
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
            PolyRange* range = nullptr;
            for (PolyRange& candidate : mPolyPrograms) {
                if (candidate.startIndex == n) {
                    range = &candidate;
                    break;
                }
            }
            if (range != nullptr) {
                processPolyRange(*range, blockSize);
                n = range->endIndex;
                continue;
            }
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
    for (PolyRange& range : mPolyPrograms) {
        if (range.timeOutControlIndex == opIndex) range.timeOutSeconds = value;
        if (opIndex <= range.startIndex || opIndex >= range.endIndex) continue;
        const int voiceIndex = opIndex - range.startIndex - 1;
        for (Voice& voice : range.voices) {
            Control* clone = dynamic_cast<Control*>(voice.list[voiceIndex].op.get());
            assert(clone != nullptr);
            clone->mValue = value;
        }
    }
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
