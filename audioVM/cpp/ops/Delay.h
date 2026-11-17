#pragma once

#include "../Operator.h"
#include "InterpTab.h"

using namespace AudioVM;

class FilterDelay { // canonical use: read > process > write
public:
    static const int MaxBlockSize = 64;
    void setup(int size, bool roundUp = true);
    void reset();
    int getLength() { return (int)mBuf.size() - Pad; }
    
    struct Reader {
        Reader() { reset(); }
        void reset() { pos=-1; speed=1; s1 = a1 = b0 = b1 = 0; }
        float pos, speed, s1, a1, b0, b1;
    } mReader; // default reader
    
    float getLag(Reader &R);
    void setLag(Reader &R, float lag);
    
    struct FilterDesign {
        FilterDesign(float lo=1, float hi=0, float freq=.5f) { ampLo=lo; ampHi=hi; frequency=freq; }
        float ampLo, ampHi, frequency;
    };
    
    // canonical interface: read -> do stuff -> write
    void read(float *dst, int N, float nextLag, FilterDesign filter = FilterDesign(), Reader *reader = nullptr, bool preWrite=false);
    void write(const float *data, int N);
    
    // block convenience functions
    void delayBlock(const float *inBuf, float *outBuf, int N, float lag, FilterDesign filter = FilterDesign());
    void delayBlockIP(float *ioBuf, int N, float lag, FilterDesign filter = FilterDesign());
    void allpassBlock(float *ioBuf, int N, float lag, FilterDesign filter = FilterDesign(), float apCoef = -.618f);
    void diffuseBlock(float *io0, float *io1, int N, float lag, FilterDesign filter = FilterDesign(), float mix = 1);
    void diffuseBlock(float **ioBufs, int N, float lag, FilterDesign filter = FilterDesign(), float mix = 1) {
        diffuseBlock(ioBufs[0], ioBufs[1], N, lag, filter, mix);
    }

private:
    static const int Pad = 4, TabLag = 1;
    std::vector<float> mBuf;
    int mWritePos = 0, mNumWritten = 0;
    float mAllpassCoef=0, mDiffuseMix=0;
};

void FilterDelay::setup(int size, bool roundUp) {
    if (roundUp) {
        size = ((size+31)/32)*32;
    }
    mBuf.resize(size + Pad, 0.f);
    mWritePos = 0;
}
void FilterDelay::reset() {
    memset(mBuf.data(), 0, mBuf.size() * sizeof(mBuf[0]));
    mWritePos = mNumWritten = 0;
    mAllpassCoef = mDiffuseMix = 0;
    mReader.reset();
}

static inline float negLogAmpToA1(float x) {
    const float kFlatA1 = -.13f;
    return x * (1 + x * x)/(.24f + x * (1.36f + x * x)) * (1-kFlatA1) + kFlatA1;
}
static inline float fastNegExp(float x) {
    float t = x * (1 + x * (.5f + .1666f * x * x));
    return 1 / (1 + t);
}

float FilterDelay::getLag(Reader &R) {
    return mWritePos - R.pos + mBuf.size() * (mWritePos < R.pos) - TabLag;
}
void FilterDelay::setLag(Reader &R, float lag) {
    R.pos = mWritePos - (lag+TabLag) + mBuf.size() * (mWritePos < (lag+TabLag));
}
static inline float lerp(float a, float b, float p) {
    return a + (b-a)*p;
}

void FilterDelay::read(float *outBuf, int numSamples, float lagTarget, FilterDesign filter, Reader *reader, bool preWrite) {
    int bufSize = getLength();
    if (lagTarget >= bufSize-numSamples) {
        DEBUGCHECK(0);
        memset(outBuf, 0, numSamples * sizeof(float));
        return;
    }
    Reader &R = (reader) ? *reader : mReader;
    
    float currLag = getLag(R) - preWrite * numSamples;
    if (currLag >= mNumWritten-2) {
        setLag(R, lagTarget);
    }
    if (R.pos<0) {
        R.pos = mWritePos - lagTarget + bufSize * (lagTarget > mWritePos);
    }
    float deltaLag = lagTarget - currLag; // dLag = N*(1-s0) + N*N/2*d = N*(1-s0-d*N/2) --> d = -dLag*2/N-s0+2
    float endSpeed = std::clamp(1 - 1e-3f * deltaLag, .5f, 2.f); //-deltaLag*2/numSamples - R->speed+2; //
    float speedStep = (endSpeed-R.speed) / (1.5f * numSamples);
    speedStep /= (1 + 1e3f * fabsf(speedStep));
    
    float s1 = R.s1, a1 = R.a1, b0 = R.b0, b1 = R.b1, step = 1.f/numSamples;
    const float f = std::clamp(2 * filter.frequency, 1e-5f, 1.f);
    const float pMax = -0.602f;
    Reader T;
    T.a1 = 1 - f*(2-f) * (1-pMax);
    T.b0 = lerp(filter.ampHi, -(1+pMax), std::max(0.f,1-1.1f*(1-f)));
    T.b1 = (1-T.a1) * (filter.ampLo - T.b0);
    assert(fabsf(T.a1)<1);
    
    const float stepA1 = step * (T.a1 - a1), stepB0 = step * (T.b0 - b0), stepB1 = step * (T.b1 - b1);
    
    R.pos -= bufSize * floorf(R.pos / bufSize);
    DEBUGCHECK(R.pos >= 0.f, "ModDelay reading before buffer!");
    typedef long long SubType;
    const SubType kNumSubBits = 16, nSub = ((SubType)1)<<kNumSubBits, kSubSize = bufSize*nSub, kTabMask = (1<<InterpTab::Bits)-1;
    SubType subPos  = (SubType)(R.pos   * nSub + .5f);
    SubType subStep = (SubType)(R.speed * nSub + .5f); //, subStepStep = (int)floorf(speedStep * nSub + .5f);
    for (int i=0; i<numSamples; i++) {
        subPos -= kSubSize * (subPos>=kSubSize);
        float read = InterpTab::lookup(&mBuf[subPos>>kNumSubBits], (subPos>>(kNumSubBits-InterpTab::Bits)) & kTabMask);
        
        s1 = a1 * s1 + read;
        outBuf[i] = b0 * read + b1 * s1;
        subPos  += subStep;
        a1 += stepA1;
        b0 += stepB0;
        b1 += stepB1;
    }
    R.pos = ((float)subPos) / nSub;
    R.speed += numSamples * speedStep;
    R.s1 = s1; R.a1 = a1; R.b0 = b0; R.b1 = b1;
}
static inline void copy(const float *src, float *dst, size_t N) {
    memcpy(dst, src, sizeof(float)*N);
}
void FilterDelay::write(const float *src, int N) {
    const int ringSize = getLength();
    for (int ioPos=0; ioPos<N;) {
        DEBUGCHECK(mWritePos <= ringSize);
        mWritePos *= (mWritePos < ringSize);
        int bufRem = ringSize - mWritePos;
        int ioRem = N-ioPos;
        int numProc = std::min(ioRem, bufRem);
        copy(src+ioPos, &mBuf[mWritePos], numProc);
        if (mWritePos < Pad) {
            copy(&mBuf[mWritePos], &mBuf[mWritePos+ringSize], std::min(numProc, Pad-mWritePos));
        }
        ioPos    += numProc;
        mWritePos += numProc;
    }
    mNumWritten = std::min(mNumWritten + N, ringSize);
}

void FilterDelay::delayBlock(const float *inBuf, float *outBuf, int N, float trgLag, FilterDesign filter) {
    const int L = getLength();
    if (trgLag+N >= L) {
        DEBUGCHECK(0);
        return;
    }
    write(inBuf, N);
    read(outBuf, N, std::max(2.f, trgLag)+N, filter);
}
void FilterDelay::delayBlockIP(float *ioBuf, int N, float trgLag, FilterDesign filter) {
    delayBlock(ioBuf, ioBuf, N, trgLag, filter);
}

void FilterDelay::allpassBlock(float *ioBuf, int N, float trgLag, FilterDesign filter, float nextApCoef) {
    const int L = getLength();
    if (L==0 || trgLag > L || trgLag < N+2) {
        DEBUGCHECK(0, "FilterDelay::allpassBlock lag out of range!\n");
        return;
    }
    DEBUGCHECK(N <= MaxBlockSize);
    DEBUGCHECK(mWritePos <= L);
    
    float readBuf[MaxBlockSize], writeBuf[MaxBlockSize];
    read(readBuf, N, trgLag, filter);
    float apCoef = mAllpassCoef;
    if (fabsf(apCoef-nextApCoef) > 1e-5f) {
        float coefStep = (nextApCoef - apCoef) / std::max(N, 1024);
        for (int i = 0; i < N; i++) {
            float fb = ioBuf[i] + apCoef * readBuf[i];
            ioBuf[i] = readBuf[i] - apCoef * fb;
            writeBuf[i] = fb;
            apCoef += coefStep;
        }
        mAllpassCoef = apCoef;
    } else { // optimization: slightly faster without stepping allpass coef
        for (int i = 0; i < N; i++) {
            float fb = ioBuf[i] + apCoef * readBuf[i];
            ioBuf[i] = readBuf[i] - apCoef * fb;
            writeBuf[i] = fb;
        }
    }
    
    write(writeBuf, N);
}
void FilterDelay::diffuseBlock(float *io0, float *io1, int N, float trgLag, FilterDesign filter, float mixTrg) {
    const int L = getLength();
    if (L==0 || trgLag > L-N || trgLag < 2) {
        DEBUGCHECK(0);
        return;
    }
    DEBUGCHECK(N <= MaxBlockSize);
    write(io0, N);
    
    float readBuf[MaxBlockSize];
    read(readBuf, N, trgLag, filter, nullptr, true);
    const float kRootOfHalf = 0.7071067812f;
    if (mixTrg == 1 && mDiffuseMix == 1) {
        for (int i = 0; i < N; i++) {
            float lag = readBuf[i], in1 = io1[i];
            io0[i] = kRootOfHalf * (in1 + lag);
            io1[i] = kRootOfHalf * (in1 - lag);
        }
    } else {
        float mix = mDiffuseMix, mixStep = (mixTrg - mix) / N;
        for (int i = 0; i < N; i++) {
            float lag = readBuf[i], in1 = io1[i];
            io0[i] += mix * (kRootOfHalf * (in1 + lag) - io0[i]);
            io1[i] += mix * (kRootOfHalf * (in1 - lag) - io1[i]);
            mix += mixStep;
        }
        mDiffuseMix = mix;
    }
}


// Delay is a normal delay when its Input port is connected.  With Input
// disconnected, it exposes the line for an explicit DelayWrite later in the
// program, making feedback explicit without introducing a graph cycle.

class Delay : public Operator {
public:
    enum In { Input, TimeSeconds, Gain, LowpassFreq, HighGain, MaxSeconds, NumInputs };
    enum Out { Output, NumOutputs };

    Delay() : mState(std::make_shared<FilterDelay>()) {}

    int getNumPorts(Port::Dir dir) override {
        return dir == Port::In ? NumInputs : NumOutputs;
    }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"input", "time", "gain", "cutoff", "highgain", "maxSeconds"};
        static const cStr outputs[] = {"output"};
        return dir == Port::In ? inputs[index] : outputs[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        if (dir == Port::Out) return Port::Full;
        return index == Input ? Port::Full : Port::Block;
    }

    void init(const std::vector<Buffer>& inputs) override {
        if (inputs.size() <= MaxSeconds || inputs[MaxSeconds].ptr == nullptr) {
            throw std::runtime_error("delay requires a maxSeconds input");
        }
        const float maxSeconds = inputs[MaxSeconds].valueOrDefault(0.1f);
        mMaxSamples = std::max(1, static_cast<int>(std::ceil(maxSeconds * mSamplerate)));
        // FilterDelay keeps a block-sized safety margin at the end of its
        // ring, so maxSeconds remains addressable for a full processing block.
        mState->setup(mMaxSamples + 2*mBlockSize, false);
        mState->reset();
        if (!inputs[Input].hasData() &&
            static_cast<float>(mMaxSamples) < mBlockSize) {
            throw std::runtime_error("delay maxSeconds must be at least one processing block");
        }
    }
    std::shared_ptr<FilterDelay> state() const { return mState; }

    void process(Buffer* ins, Buffer* outs, int N) override {
        if (mState->getLength() == 0) {
            throw std::runtime_error("delay was not configured");
        }
        const float lagSamples = std::clamp(ins[TimeSeconds].value() * mSamplerate, 1.f, mMaxSamples-1.f);
        float* dst = outs[Output].data();
        FilterDelay::FilterDesign filter(ins[Gain].valueOrDefault(1.f), ins[HighGain].valueOrDefault(0.f), ins[LowpassFreq].valueOrDefault(1e6f)/mSamplerate);
        if (ins[Input].hasData()) {
            // write() followed by read() is FilterDelay's normal delay mode.
            const float* src = ins[Input].data();
            mState->delayBlock(src, dst, N, lagSamples, filter);
            return;
        }

        // Reader-only mode is paired with a later DelayWrite, so the tap
        // cannot observe samples that the writer has not produced yet.
        mState->read(dst, N, std::max(lagSamples, mBlockSize+2), filter);
    }
    void reset() override { mState->reset(); }

private:
    std::shared_ptr<FilterDelay> mState;
    int mMaxSamples = 0;
};

class DelayWrite : public Operator {
public:
    // Delay is a compile-time link to Delay. Its buffer is unused at
    // runtime; Input contains the audio that will be committed to the line.
    enum In { Delay, Input, NumInputs };

    int getNumPorts(Port::Dir dir) override { return dir == Port::In ? NumInputs : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"delay", "input"};
        return dir == Port::In ? inputs[index] : "";
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return index == Input ? Port::Full : Port::Keep;
    }

    void link(std::shared_ptr<FilterDelay> state) { mState = std::move(state); }
    void process(Buffer* in, Buffer*, int N) override {
        if (!mState || mState->getLength() == 0) {
            throw std::runtime_error("delayWrite is not linked to a delay");
        }
        const float* src = in[Input].data();
        mState->write(src, N);
    }

private:
    std::shared_ptr<FilterDelay> mState;
};

