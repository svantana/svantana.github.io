#pragma once

#include "../Operator.h"

using namespace AudioVM;

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
                mVal = sustain + (1-sustain) * powf(1-p, 2);
                break;
            }
            case S: {
                mVal = sustain;
                break;
            }
            case R: {
                float dur = std::max(1e-9f, i[Release].value());
                mPhase = mPhase + dt / dur;
                mVal = mReleaseVal * powf(R+1 - mPhase, 2);
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
    float timeToCoef(float t) {
        float n = t * mSamplerate;
        return 1 / (1 + n);
    }
    void process(Buffer *i, Buffer *o, int numSamples) override {
        int N = std::min(numSamples, i[0].numSamples);
        float tA = i[Attack].value(), tR = i[Release].value();
        float cA = timeToCoef(tA), cR = timeToCoef(tR);
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
