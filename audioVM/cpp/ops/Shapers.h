#pragma once

#include "../Operator.h"

using namespace AudioVM;

class Saturation : public Operator {
public:
    enum In  { Input, PreGain, PostGain, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","pre-gain","post-gain"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (index==0) ? Port::Full : Port::Block;
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
        float gPre = i[PreGain].value(), gPost = i[PostGain].value();
        float *x = i[Input].data(), *y = o[Output].data(), env = mEnv;
        for (int n = 0; n < N; n++) {
            float v = gPre * x[n];
            float a = fabsf(v);
            env += .2f * (a - env);
            y[n] = gPost * v / (1 + env);
        }
        mEnv = env;
    }
private:
    float mEnv;
};
