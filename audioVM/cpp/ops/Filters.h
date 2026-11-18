#pragma once

#include "../Operator.h"

using namespace AudioVM;

class FilterLP : public Operator {
public:
    enum In  { Input, FreqHz, Resonance, Reset, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getName() { return "FilterLP"; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","frequency","resonance","reset"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        // The signal input and output are sample-rate; frequency, resonance,
        // and reset are control-rate parameters.
        return (dir == Port::Out || (dir == Port::In && index == Input))
            ? Port::Full : Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }

    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        if (i[Reset].value() > .5f) {
            mState.reset();
            C = T;
        }
        Coefs cStep = getCoefStep(C, T);
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0];
        for (int n = 0; n < N; n++) {
            stepCoefs(C, T, cStep);
            doSVF(x[n], S, C);
            y[n] = S.l;
        }
        mState = S;
        mCoefs = C;
    }
    void reset() override {
        mState.reset();
    }
    struct State { float l, b; void reset() {l=b=0;} } mState;
    struct Coefs { float k=1, d=1, g=1; } mCoefs;
    
    static inline float doSVF(float x, State &S, Coefs &C) {
        float hp = x - S.l - C.d * S.b;
        S.b += C.k * hp;
        S.l += C.k * S.b;
        return hp;
    }
    static inline void stepCoefs(Coefs &C, const Coefs &T, const Coefs &step) {
        C.k += step.k * (T.k - C.k);
        C.d += step.d * (T.d - C.d);
    }
    static inline float fastQuarterSin(float x) {
        return x * (3.141592653589793f + x * (-0.5663706143591725f + x * -3.4336293856408275f));
    }
    static inline Coefs getCoefs(float f, float r) {
        float s = fastQuarterSin(std::min(0.5f, f)), s2 = s*s, r2=r*r;
        float u = (1+r2)*s2 / (1+r2*s2);
        Coefs C;
        C.g = 1 / (1 + r*u);
        C.k = 2*s * C.g;
        C.d = r2 * (1-u) / (1+r2*u) + 2*r*u/(1+r);
        C.d = std::min(C.d, 1/C.k + .5f - .5f * C.k); // should not be needed
        if (!(C.k > 0.f && C.k <= 2.f && C.d >= 0.f && C.d < (2/C.k-C.k/2))) {
            printf("FilterLP WARNING: f=%f, r=%f -> k=%f, d=%f (unstable)\n", f, r, C.k, C.d);
        }
        return C;
    }
    Coefs getCoefs(Buffer *i) {
        float f = i[FreqHz].value() / mSamplerate;
        float rInv = std::max(0.f, 1 - i[Resonance].value());
        return getCoefs(f, rInv * (3*rInv + 1));
    }
    Coefs getCoefStep(Coefs &C, Coefs &T) const {
        float s = 1.f / mBlockSize;
        Coefs S;
        S.k = s;
        S.d = std::min(1.f, s * (1+C.d)/(1+T.d));
        return S;
    }
};

class FilterBP : public FilterLP {
public:
    std::string getName() { return "FilterBP"; }
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        float gainTrg = T.g;
        if (i[Reset].value() > .5f) {
            mState.reset();
            C = T;
            mGain = gainTrg;
        }
        Coefs cStep = getCoefStep(C, T);
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0];
        float gain = mGain, gainStep = (gainTrg - gain) / mBlockSize;
        for (int n = 0; n < N; n++) {
            stepCoefs(C, T, cStep);
            doSVF(x[n], S, C);
            gain += gainStep;
            y[n] = S.b * gain;
        }
        mState = S;
        mCoefs = C;
        mGain = gain;
    }
private:
    float mGain = 1.f;
};

class FilterHP : public FilterLP {
public:
    std::string getName() { return "FilterHP"; }
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        float gainTrg = T.g * T.g;
        if (i[Reset].value() > .5f) {
            mState.reset();
            C = T;
        }
        Coefs cStep = getCoefStep(C, T);
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0];
        float gain = mGain, gainStep = (gainTrg - gain) / mBlockSize;
        for (int n = 0; n < N; n++) {
            stepCoefs(C, T, cStep);
            float hp = doSVF(x[n], S, C);
            gain += gainStep;
            y[n] = hp * gain;
        }
        mState = S;
        mCoefs = C;
        mGain = gain;
    }
private:
    float mGain = 1.f;
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

struct Smoother : public BlockRateOp {
    enum In  { Input, SmoothTime, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","time"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }

    void process(Buffer *i, Buffer *o, int N) override {
        float x = i[Input].value(), &y = o[Output].value();
        float time = i[SmoothTime].value(), t = N / mSamplerate;
        float step = t / std::max(1e-30f, t + .25f * time);
        y += step * (x - y);
    }
    void reset() override {
        mState = 0.f;
    }
    float mState;
};
