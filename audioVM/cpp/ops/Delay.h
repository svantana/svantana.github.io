#pragma once

#include "../Operator.h"

using namespace AudioVM;

namespace InterpTab {
static const int Bits = 10, KernelSize = 4;
static float data[1<<Bits][KernelSize];
static int make() {
    int nSub = 1<<Bits;
    const float kPIf = 3.1415926535f;
    for (int i = 0; i < nSub; i++) {
        float p = i / (nSub+0.f), *tab = data[i], shape = .16f, sum = 0;
        for (int n = 0; n < KernelSize; n++) {
            float w = kPIf * (2.f * (n+1-p) / KernelSize - 1);
            tab[n] = (1 + cosf(w) + shape * (cosf(2*w) - 1)) / (KernelSize * (1-shape));
            sum += tab[n];
        }
        DEBUGCHECK(fabsf(sum-1) < 1e-5f);
    }
    return 1;
}
static int isMade = make();

inline float lookup(const float *x, int p) {
    const float *t = data[p];
    return t[0]*x[0] + t[1]*x[1] + t[2]*x[2] + t[3]*x[3];
}
}

class FilterDelay { // canonical use: read > process > write
public:
    static const int MaxBlockSize = 64;
    void setup(int size, bool roundUp = true);
    void reset();
    int getLength() { return (int)mBuf.size() - Pad; }
    
    struct Reader {
        Reader() { reset(); }
        void reset() { pos=-1; speed=1; s1 = s2 = a1 = b0 = 0; }
        float pos, speed, s1, s2, a1, b0;
    } mReader; // default reader
    
    float getLag(Reader &R);
    void setLag(Reader &R, float lag);
    
    // canonical interface: read -> do stuff -> write
    void read(float *dst, int N, float nextLag, float logAmpDC=0, float logAmpHi=0, Reader *reader = nullptr, bool preWrite=false);
    void write(const float *data, int N);
    
    // block convenience functions
    void delayBlock(const float *inBuf, float *outBuf, int N, float lag, float logAmpDC=0, float logAmpHi=0);
    void delayBlockIP(float *ioBuf, int N, float lag, float logAmpDC=0, float logAmpHi=0);
    void allpassBlock(float *ioBuf, int N, float lag, float logAmpDC=0, float logAmpHi=0, float apCoef = -.618f);
    void diffuseBlock(float *io0, float *io1, int N, float lag, float logAmpDC=0, float logAmpHi=0, float mix = 1);
    void diffuseBlock(float **ioBufs, int N, float lag, float logAmpDC=0, float logAmpHi=0, float mix = 1) {
        diffuseBlock(ioBufs[0], ioBufs[1], N, lag, logAmpDC, logAmpHi, mix);
    }
    
private:
    static const int Pad = 4;
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
    return mWritePos - R.pos + mBuf.size() * (mWritePos < R.pos);
}
void FilterDelay::setLag(Reader &R, float lag) {
    R.pos = mWritePos - lag + mBuf.size() * (mWritePos <  lag);
}

void FilterDelay::read(float *outBuf, int numSamples, float lagTarget, float logAmpDC, float logAmpHi, Reader *reader, bool preWrite) {
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
    
    float s1 = R.s1, s2 = R.s2, a1 = R.a1, b0 = R.b0, step = 1.f/numSamples;
    float nextA1 = negLogAmpToA1(-logAmpHi), nextB0 = fastNegExp(-logAmpDC) * (1 - nextA1);
    float stepA1 = step * (nextA1 - a1), stepB0 = step * (nextB0 - b0);
    
    R.pos -= bufSize * floorf(R.pos / bufSize);
    DEBUGCHECK(R.pos >= 0.f, "ModDelay reading before buffer!");
    typedef long long SubType;
    const SubType kNumSubBits = 16, nSub = ((SubType)1)<<kNumSubBits, kSubSize = bufSize*nSub, kTabMask = (1<<InterpTab::Bits)-1;
    SubType subPos  = (SubType)(R.pos   * nSub + .5f);
    SubType subStep = (SubType)(R.speed * nSub + .5f); //, subStepStep = (int)floorf(speedStep * nSub + .5f);
    for (int i=0; i<numSamples; i++) {
        subPos -= kSubSize * (subPos>=kSubSize);
        float read = InterpTab::lookup(&mBuf[subPos>>kNumSubBits], (subPos>>(kNumSubBits-InterpTab::Bits)) & kTabMask);
        
        outBuf[i] = a1 * s2 + b0 * read;
        s2 = s1;
        s1 = outBuf[i];
        
        subPos  += subStep;
        a1 += stepA1;
        b0 += stepB0;
    }
    R.pos = ((float)subPos) / nSub;
    R.speed += numSamples * speedStep;
    R.s1 = s1; R.s2 = s2; R.a1 = a1; R.b0 = b0;
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

void FilterDelay::delayBlock(const float *inBuf, float *outBuf, int N, float trgLag, float logAmpDC, float logAmpHi) {
    const int L = getLength();
    if (trgLag+N >= L) {
        DEBUGCHECK(0);
        return;
    }
    write(inBuf, N);
    read(outBuf, N, std::max(2.f, trgLag)+N, logAmpDC, logAmpHi);
}
void FilterDelay::delayBlockIP(float *ioBuf, int N, float trgLag, float logAmpDC, float logAmpHi) {
    delayBlock(ioBuf, ioBuf, N, trgLag, logAmpDC, logAmpHi);
}

void FilterDelay::allpassBlock(float *ioBuf, int N, float trgLag, float logAmpDC, float logAmpHi, float nextApCoef) {
    const int L = getLength();
    if (L==0 || trgLag > L || trgLag < N+2) {
        DEBUGCHECK(0, "FilterDelay::allpassBlock lag out of range!\n");
        return;
    }
    DEBUGCHECK(N <= MaxBlockSize);
    DEBUGCHECK(mWritePos <= L);
    
    float readBuf[MaxBlockSize], writeBuf[MaxBlockSize];
    read(readBuf, N, trgLag, logAmpDC, logAmpHi);
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
void FilterDelay::diffuseBlock(float *io0, float *io1, int N, float trgLag, float logAmpDC, float logAmpHi, float mixTrg) {
    const int L = getLength();
    if (L==0 || trgLag > L-N || trgLag < 2) {
        DEBUGCHECK(0);
        return;
    }
    DEBUGCHECK(N <= MaxBlockSize);
    write(io0, N);
    
    float readBuf[MaxBlockSize];
    read(readBuf, N, trgLag, logAmpDC, logAmpHi, nullptr, true);
    const float kSquareMix = 0.7071067812f;
    if (mixTrg == 1 && mDiffuseMix == 1) {
        for (int i = 0; i < N; i++) {
            float lag = readBuf[i], in1 = io1[i];
            io0[i] = kSquareMix * (in1 + lag);
            io1[i] = kSquareMix * (in1 - lag);
        }
    } else {
        float mix = mDiffuseMix, mixStep = (mixTrg - mix) / N;
        for (int i = 0; i < N; i++) {
            float lag = readBuf[i], in1 = io1[i];
            io0[i] += mix * (kSquareMix * (in1 + lag) - io0[i]);
            io1[i] += mix * (kSquareMix * (in1 - lag) - io1[i]);
            mix += mixStep;
        }
        mDiffuseMix = mix;
    }
}


// Delay is a normal delay when its Input port is connected.  With Input
// disconnected, it exposes the line for an explicit DelayWrite later in the
// program, making feedback explicit without introducing a graph cycle.
struct DelayLineState {
    std::vector<float> samples;
    size_t writeHead = 0;

    void reset() {
        std::fill(samples.begin(), samples.end(), 0.f);
        writeHead = 0;
    }
};

class Delay : public Operator {
public:
    enum In { Input, TimeSeconds, LowpassFreq, MaxSeconds, NumInputs };
    enum Out { Output, NumOutputs };

    Delay() : mState(std::make_shared<DelayLineState>()) {}

    int getNumPorts(Port::Dir dir) override {
        return dir == Port::In ? NumInputs : NumOutputs;
    }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"input", "time", "cutoff", "maxSeconds"};
        static const cStr outputs[] = {"output"};
        return dir == Port::In ? inputs[index] : outputs[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        if (dir == Port::Out) return Port::Full;
        return index == Input ? Port::Full : Port::Block;
    }

    // Called only by the compiler, before audio processing starts.
    void configure(float maxSeconds) {
        if (!std::isfinite(maxSeconds) || maxSeconds <= 0.f) {
            throw std::runtime_error("delay maxSeconds must be positive and finite");
        }
        mMaxSamples = std::max(1, static_cast<int>(std::ceil(maxSeconds * mSamplerate)));
        // One extra location makes a delay equal to maxSeconds addressable.
        mState->samples.assign(static_cast<size_t>(mMaxSamples) + 1, 0.f);
        mState->writeHead = 0;
    }
    float maxSeconds() const { return static_cast<float>(mMaxSamples) / mSamplerate; }
    std::shared_ptr<DelayLineState> state() const { return mState; }

    void process(Buffer* in, Buffer* out, int N) override {
        if (mState->samples.empty()) {
            throw std::runtime_error("delay was not configured");
        }
        const int requestedSamples = static_cast<int>(std::lround(in[TimeSeconds].value() * mSamplerate));
        const size_t size = mState->samples.size();
        float* dst = out[Output].data();
        if (in[Input].hasData()) {
            // Standard delay mode is sample scheduled: commit the input
            // before reading the tap, so delays shorter than one block work.
            const float* src = in[Input].data();
            const int sourceSamples = in[Input].numSamples;
            const int delaySamples = std::clamp(requestedSamples, 1, mMaxSamples);
            for (int n = 0; n < N; ++n) {
                mState->samples[mState->writeHead] = src[sourceSamples == 1 ? 0 : n];
                const size_t readHead = (mState->writeHead + size - static_cast<size_t>(delaySamples)) % size;
                dst[n] = mState->samples[readHead];
                mState->writeHead = (mState->writeHead + 1) % size;
            }
            return;
        }

        // Reader-only mode is paired with a later DelayWrite, so the tap
        // cannot observe samples that the writer has not produced yet.
        const int delaySamples = std::clamp(requestedSamples, N, mMaxSamples);
        const size_t start = (mState->writeHead + size - static_cast<size_t>(delaySamples)) % size;
        for (int n = 0; n < N; ++n) {
            dst[n] = mState->samples[(start + static_cast<size_t>(n)) % size];
        }
    }
    void reset() override { mState->reset(); }

private:
    std::shared_ptr<DelayLineState> mState;
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

    void link(std::shared_ptr<DelayLineState> state) { mState = std::move(state); }
    void process(Buffer* in, Buffer*, int N) override {
        if (!mState || mState->samples.empty()) {
            throw std::runtime_error("delayWrite is not linked to a delay");
        }
        const size_t size = mState->samples.size();
        const float* src = in[Input].data();
        for (int n = 0; n < N; ++n) {
            mState->samples[mState->writeHead] = src[n];
            mState->writeHead = (mState->writeHead + 1) % size;
        }
    }

private:
    std::shared_ptr<DelayLineState> mState;
};
