#pragma once

#include "../Operator.h"

using namespace AudioVM;

// some basic ops to get started
static float convertNoteToHz(float note) {
    return (note > 0) * 440 * exp2f((note-69)/12);
}

class OscSin : public Operator {
public:
    enum In  { FreqHz, NoteNumber, Reset, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"frequency", "note", "reset"};
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
        if (i[Reset].value() > 0.5f) {
            mUnitPhase = 0.f;
        }
        float note = i[NoteNumber].value();
        float hz = convertNoteToHz(note) + i[FreqHz].value();
        float phaseStep = mPhaseStep, stepTrg = hz / mSamplerate, stepStep = 1 / mBlockSize;
        for (int n = 0; n < N; n++) {
            phaseStep += stepStep * (stepTrg - phaseStep);
            mUnitPhase += phaseStep;
            mUnitPhase -= floorf(mUnitPhase);
            o->data()[n] = fastUnitSinUnsafe(mUnitPhase);
        }
        mPhaseStep = phaseStep;
    }
private:
    float mUnitPhase = 0.f, mPhaseStep = 0.f;
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
