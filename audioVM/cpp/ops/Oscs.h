#pragma once

#include "../Operator.h"
#include "InterpTab.h"

using namespace AudioVM;

// some basic ops to get started
static inline float convertNoteToHz(float note) {
    return (note > 0) * 440 * exp2f((note-69)/12);
}
static inline float fastUnitSinUnsafe(float x) {
    float b = x * (1-x);
    return b * (.5f-x) * (12.63816744f + b * (35.870132f + 56.16403226f * b));
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
class FMOp : public Operator {
public:
    enum In  { FMSource, FreqHz, NoteNumber, Reset, NumInputs };
    enum Out { Output, NumOutputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : NumOutputs; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input", "frequency", "note", "reset"};
        cStr o[]={"output"};
        return (dir == Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return ((dir==Port::Out && index==Output) || (dir==Port::In && index==FMSource)) ? Port::Full : Port::Block;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        if (i[Reset].value() > 0.5f) {
            mUnitPhase = 0.f;
        }
        float note = i[NoteNumber].value();
        float hz = convertNoteToHz(note) + i[FreqHz].value();
        float phaseStep = mPhaseStep, stepTrg = hz / mSamplerate, stepStep = 1 / mBlockSize;
        float *fm = i[FMSource].data();
        for (int n = 0; n < N; n++) {
            phaseStep += stepStep * (stepTrg - phaseStep);
            mUnitPhase += phaseStep;
            mUnitPhase -= floorf(mUnitPhase);
            float phase = mUnitPhase + fm[n];
            o->data()[n] = fastUnitSinUnsafe(phase - floorf(phase));
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
};

namespace Blit {
class Voice {
public:
    static const int MaxBlockSize = 32, MaxNumChannels = 2, MaxPulseSize = InterpTab::KernelSize, MaxNumOscillators = 16, NumOscParts = 2;
    Voice() {
        memset(m_params, 0, sizeof(m_params));
        resetVoiceSoft(1.f);
    }
    ~Voice() {}
    
    void resetFull();
    void randomInit(unsigned int &seed);
    void resetVoiceSoft(float reset_amount = 0);
    void setOscPhase(int osc, float phase) { m_osc_states[osc].mOscPhase = phase; }
    void scaleOscPhase(int osc, float scale) { m_osc_states[osc].soft_reset(scale); }
    void render(float **bufs, int numSamples, int numChannels);
    
    int m_num_oscillators = 1;
    int mMainBlockSize = MaxBlockSize;
    
    struct Osc {
        enum ParamId {Frequency, Amp, NumParams};
        void soft_reset(float reset_amount) {
            mOscPhase *= 1-reset_amount;
            mSyncPhase *= 1-reset_amount;
        }

        void render_add(Voice &V, float **bufs, int numSamples, int numChannels, float *accBias, float *accBiasStep, bool doOnset);
        
        float* m_params = nullptr;
        
        // pulse wave by alternating parts
        typedef float PhaseType;
        PhaseType mSyncPhase=0, mOscPhase=0;
        float mValues[MaxNumChannels]={0}, mValSteps[MaxNumChannels]={0};
        int mCurrPart=0;
        float mCurrOscFreq=0, mBlockSizeSmooth1 = 32, mBlockSizeSmooth2 = 32; // for reading from outside
    };
    static const int NumColors = 3; // tri, saw, imp
    enum ParamId { Color , PulseAmount, PulseWidth, SyncAmount, SyncFreq,
        OscParam0,
        LastOscParam = OscParam0 + Osc::NumParams * MaxNumOscillators - 1,
        NumVoiceParams
    };
    float *getOscParams(int o) { return &m_params[OscParam0 + Osc::NumParams * o]; }
    Osc *getOscState(int o) { return &m_osc_states[o]; }
    float m_params[NumVoiceParams];
    bool channelIsActive(int ch);
private:
    
    static inline int osc_parameter_index(int osc, int index=0) { return OscParam0 + Osc::NumParams * osc + index; }
    
    Osc m_osc_states[MaxNumOscillators];
    struct ChannelState {
        float s1=0, s2=0, mDC=0;
        void reset() { s1 = s2 = mDC = 0; }
        bool isActive() { return s1!=0 || s2!=0; }
    } mChannels[MaxNumChannels];
    //float mPulseSpillover[MaxNumChannels][MaxPulseSize] = {{0.f}};
    static const int MaxRenderSize = 256, RenderBufSize = MaxRenderSize + MaxPulseSize;
    float mRenderBufs[MaxNumChannels][RenderBufSize];
    int mRenderBufPos = 0;
    struct FilterCoefs {
        float p1, p2;
        void reset() { p1=p2=0; }
    } mFilterCoefs;
    bool mNeedsInit = false;
    int mSamplesSinceInit = 0;
    float mSamplerate;
    
#ifndef NDEBUG
    struct DC_Detector {
        static const int BufSize = 1<<12;
        float buf[BufSize] = {0}, lp1=0, lp2=0;
        long long sampleCount = 0;
        void reset() { lp1=lp2=0; sampleCount=0; }
    } mDC_Detectors[MaxNumChannels];
#endif
};

typedef Voice::Osc::PhaseType PhaseType;
static inline PhaseType wrapAdd(PhaseType &x, PhaseType add) { // returns num wraps
    x += add;
    PhaseType f = floor(x);
    x -= f;
    return f;
}
float freq_to_amp(float f) {
    return powf(std::max(0.f, 1 - 20*f*f*f), 2.f);
}
float absClamp(float x, float a) {
    return std::clamp(x, -a, a);
}
/*static inline float wrapErr(float a, float b) {
 return fabsf(a-b - roundf(a-b));
 }*/

void Voice::Osc::render_add(Voice &V, float **outBufs, int numSamples, int numChannels, float* accBias, float *accBiasStep, bool doOnset) {
    
    const float *P = m_params, *P0 = V.m_params;
    const float baseFreq = P[Frequency];
    const float syncFreq = P0[Voice::SyncFreq];
    const float syncAmount = P0[Voice::SyncAmount];
    
    bool doSync = (syncAmount>0);
    
    mBlockSizeSmooth1 = std::max(.9f * mBlockSizeSmooth1, (float)numSamples);
    mBlockSizeSmooth2 = std::max(.9f * mBlockSizeSmooth2, mBlockSizeSmooth1);
    
    const float freqOut = baseFreq;
    if (doOnset) { mCurrOscFreq = freqOut; }
    const float freqIn = std::clamp(mCurrOscFreq, .5f * freqOut, 1.5f * freqOut);
    const float freqFM = (freqIn + freqOut) / 2;
    mCurrOscFreq = freqOut;
    
    const float pulseWidth = P0[Voice::PulseWidth], pulseAmount = P0[Voice::PulseAmount], uPulse = 2 * fabsf(pulseWidth - .5f);
    const float rampCoef = -2 * (1 - pulseAmount * (1 - (pulseAmount<0) * uPulse));
    
    float partAmps[NumOscParts];
    {
        float m = pulseAmount, g = 1, dg = -rampCoef * pulseWidth, *A = partAmps;
        if (m >= 0) { A[0] = g; A[1] = g - dg + m * (-2 * g - dg); }
        else        { A[0] = g * (1 + uPulse*(2-uPulse)*m); float a1 = A[0] - dg; A[1] = a1 - m * (g-a1); }
    }
    const int numParts = 1 + (fabsf(pulseAmount) > 1e-6f);
    float totFreq = syncAmount * syncFreq + freqFM;
    const float f0scaling = freq_to_amp(totFreq); // roll off as we approach nyquist
    const float ampTrgs[2] = {P[Amp] * f0scaling, P[Amp] * f0scaling};
    //const uint32_t syncProb = (uint32_t) (P[SyncProb] * ((1ULL<<32ULL)-1));
    
    PhaseType partBounds[NumOscParts+1] = {0.0, pulseWidth};
    partBounds[numParts] = 1;
    
    if (doOnset) {
        mCurrPart = (mOscPhase > partBounds[1]);
    }
    float valTrg = partAmps[mCurrPart] + rampCoef * ((float)mOscPhase - pulseWidth*(float)mCurrPart);
    if (doOnset) {
        for (int ch = 0; ch < numChannels; ch++) {
            mValues[ch] = valTrg * ampTrgs[ch];
            outBufs[ch][0] += mValues[ch];
        }
    }
    PhaseType oscPeriod = 1 / std::max(1e-20f, baseFreq), oscPerSync = baseFreq/syncFreq;
    
    float totGain = fabsf(ampTrgs[0]) + fabsf(ampTrgs[1]) * (numChannels > 1);
    bool isSilent = totGain < 1e-6f;
    //float remPhase = partBounds[mCurrPart+1] - mOscPhase;
    //float samplesTilPulse = remPhase * oscPeriod + 1e9f * isSilent;
    //float valStepMix = samplesTilPulse / (numSamples + samplesTilPulse);
    float valStepSteps[MaxNumChannels];
    for (int ch = 0; ch < numChannels; ch++) {
        float valStepTrg = rampCoef * freqFM * ampTrgs[ch];
        float valErr = ampTrgs[ch] * valTrg - mValues[ch];
        mValSteps[ch] = (doOnset) ? valStepTrg : absClamp(mValSteps[ch], fabsf(valStepTrg) + .1f / (float)numSamples);
        valStepSteps[ch] = (valStepTrg - mValSteps[ch] + .01f * valErr) / (float)numSamples;
        accBias[ch]     += mValSteps[ch];
        accBiasStep[ch] += valStepSteps[ch];
    }
    // if no volume or frequency, reset and return
    if (isSilent) {
        float step = (float)numSamples;
        for (int ch = 0; ch < numChannels; ch++) {
            mValues[ch] += step * (mValSteps[ch] + .5f * step * valStepSteps[ch]);
            mValSteps[ch] += step * valStepSteps[ch];
        }
        wrapAdd(mOscPhase , freqFM * step);
        wrapAdd(mSyncPhase, syncFreq * step);
        DEBUGCHECK(mOscPhase < 2);
        return;
    }
    
    PhaseType resyncPhase = 1 + (!doSync) * 1e9f;
    PhaseType bufPos = 0;
    DEBUGCHECK(mOscPhase < 2);
    for (int iter = 0; iter <= numSamples; iter++) {
        PhaseType oscRem = std::max((PhaseType)0, partBounds[mCurrPart+1] - mOscPhase);
        PhaseType syncStep = (resyncPhase - mSyncPhase) * oscPerSync;
        PhaseType phaseStep = std::max((PhaseType)0, std::min(oscRem, syncStep));
        PhaseType sampleRem = phaseStep * oscPeriod;
        PhaseType bufRem = (PhaseType)numSamples-bufPos;
        PhaseType sampleStep = std::min(bufRem, sampleRem);
        
        wrapAdd(mSyncPhase, sampleStep * syncFreq);
        mOscPhase += sampleStep * baseFreq;
        for (int ch = 0; ch < numChannels; ch++) {
            mValues  [ch] += sampleStep * (mValSteps[ch] + .5f * sampleStep * valStepSteps[ch]);
            mValSteps[ch] += sampleStep * valStepSteps[ch];
        }
        
        DEBUGCHECK(std::abs(mValues[0]) < 20 * (1 + std::abs(totGain)), "Blit::Voice::Osc::mValue diverged!");
        DEBUGCHECK(mOscPhase < 1.001f, "Blit::Voice::Osc::OscPhase out of range!");
        bufPos += sampleStep;
        if (sampleStep == bufRem || bufPos >= (PhaseType)numSamples) {
            break;
        }
        mOscPhase -= (mOscPhase > .999);
        mSyncPhase -= (mSyncPhase > .999); // account for rounding errors
        if (phaseStep == syncStep) {
            PhaseType oC = 2*mOscPhase-1;
            mOscPhase = (syncAmount>.9999f) ? 0.f : .5f+.5f*oC / (1 - syncAmount * (1-std::abs(oC)));
            DEBUGCHECK(mOscPhase > -.001f && mOscPhase < 1.001f);
            mCurrPart = (mOscPhase+1e-4f > partBounds[1]) * (numParts>1);
        } else {
            mCurrPart = (mCurrPart + 1) % numParts;
        }
        
        float trgVal = partAmps[mCurrPart] + rampCoef * (mOscPhase - partBounds[mCurrPart]);
        int subPos = (int)(bufPos * InterpTab::SubSteps);
        DEBUGCHECK(subPos >= 0 && subPos < InterpTab::SubSteps * numSamples);
        
        for (int ch = 0; ch < numChannels; ch++) {
            float impulseScale = ampTrgs[ch] * trgVal - mValues[ch];
            DEBUGCHECK(fabsf(impulseScale) < 3.f + 4 * fabsf(ampTrgs[ch]));
            InterpTab::writePulse(outBufs[ch], subPos, impulseScale);
            mValues[ch] += impulseScale;
        }
        
        DEBUGCHECK(mOscPhase > -0.01f && mOscPhase <= 1.0001f);
    }
}
void Voice::resetVoiceSoft(float reset_amount) {
    for (int o = 0; o < MaxNumOscillators; o++) {
        m_osc_states[o].m_params = m_params + osc_parameter_index(o);
        m_osc_states[o].soft_reset(reset_amount);
    }
    mNeedsInit = true;
    mSamplesSinceInit = 0;
    for (int ch = 0; ch < MaxNumChannels; ch++) {
        mChannels[ch].reset();
        for (int i = 0; i < InterpTab::KernelSize; i++) {
            mRenderBufs[ch][i] = 0;
        }
        mRenderBufPos = 0;
#ifndef NDEBUG
        mDC_Detectors[ch].reset();
#endif
    }
}
//static inline float square(float x) { return x*x; }

static inline float linLookup(const float *x, int N, float p) {
    int i0 = std::max(0, std::min(N-2, (int)p));
    return x[i0] + (p-(float)i0) * (x[i0+1] - x[i0]);
}

void Voice::render(float **outBufs, int numSamples, int numChannels) {
    DEBUGCHECK(numSamples <= MaxRenderSize);
    
    if (mRenderBufPos + numSamples + InterpTab::KernelSize > RenderBufSize) {
        for (int ch = 0; ch < numChannels; ch++) {
            memcpy(&mRenderBufs[ch][0], &mRenderBufs[ch][mRenderBufPos], InterpTab::KernelSize * sizeof(float));
        }
        mRenderBufPos = 0;
    }
    
    float *renderPtrs[MaxNumChannels];
    for (int ch = 0; ch < numChannels; ch++) {
        renderPtrs[ch] = &mRenderBufs[ch][mRenderBufPos];
        memset(renderPtrs[ch]+InterpTab::KernelSize, 0, sizeof(float) * numSamples);
    }
    
    // render all blits
    float bias0[MaxNumChannels] = {0}, biasStep[MaxNumChannels] = {0};
    for (int o = 0; o < m_num_oscillators; o++) {
        Osc& osc = m_osc_states[o];
        osc.render_add(*this, renderPtrs, numSamples, numChannels, bias0, biasStep, mNeedsInit);
    }
    
    float *P = m_params;
    const float oscF0 = std::max(1e-6f, m_osc_states[0].mCurrOscFreq);
    float color = std::clamp(P[Color], 0.f, 1.f);
    //float width = std::clamp(P[PulseWidth], 0.f, 1.f);
    //float uWidth = 2*fabsf(width - roundf(width)), uPulse = std::min(1.f, P[PulseAmount]);
    //float f0Scale = (1 + std::max(0.f, -uPulse)) / std::max(.01f, uWidth);
    FilterCoefs coefTrg;
    float pole = 4 / (4 + oscF0), p2 = .5f / (.5f + oscF0), pHi = -.5f;
    coefTrg.p1 =  pole * std::min(1.f, 2 - 2*color);
    coefTrg.p2 = pHi + (p2-pHi) * std::max(0.f, 1-2*color);
    
    if (mNeedsInit) {
        mFilterCoefs = coefTrg;
    }
#if 0
    float coefX = a1 * (1-coefQ) / coefK;
    for (int i = 0; i < buf_size; i++) {
        mStateB = coefQ * mStateB + coefX * (tmp_pulse_buf[i] - tot_bias) - coefK * mStateL;
        mStateL += coefK * mStateB;
        out_buf[i] = mStateL;
    }
#else // 2nd order filter
    ChannelState state[2];
    float bias[MaxNumChannels];
    for (int ch = 0; ch < MaxNumChannels; ch++) {
        state[ch] = mChannels[ch]; // local copy to enable optimizations
        float dc = state[ch].mDC, dc2 = dc * fabsf(dc) / (0.25f + dc * dc);
        bias[ch] = bias0[ch] - .02f * dc2;
    }
    
    //const float biasSmoothCoef = clamp(2 * oscF0, .02f, .2f); // faster interpolation for higher f0's
    float s = 1.f / (float)std::max(1, numSamples);
    FilterCoefs K = mFilterCoefs, &T=coefTrg, kStep = {.p1=s*(T.p1-K.p1), .p2=s*(T.p2-K.p2)};
    
    for (int i = 0; i < numSamples; i++) {
        K.p1 += kStep.p1;
        K.p2 += kStep.p2;
        for (int ch = 0; ch < numChannels; ch++) {
            ChannelState &C = state[ch];
            float input = renderPtrs[ch][i] + bias[ch];
            C.s1 = K.p1 * C.s1 + input;
            C.s2 = K.p2 * C.s2 + input;
            outBufs[ch][i] = C.s1 - C.s2;
            bias[ch] += biasStep[ch];
        }
    }
    mFilterCoefs = K;
    for (int ch = 0; ch < numChannels; ch++) {
        DEBUGCHECK(fabsf(state[ch].s1) < 1e4f && fabsf(state[ch].s2) < 1e4f);
        mChannels[ch] = state[ch]; // write local copy to state
    }
#endif
    mNeedsInit = false;
    mRenderBufPos += numSamples;
    mSamplesSinceInit += numSamples;
    
#ifdef DEBUG // detect DC
    for (int ch = 0; ch < numChannels; ch++) {
        DC_Detector &DC = mDC_Detectors[ch];
        const float kCoefDC = .1f * std::min(.1f, oscF0);
        for (int i = 0; i < numSamples; i++) {
            float v = (float)outBufs[ch][i];
            DC.lp1 += kCoefDC * (v   - DC.lp1);
            DC.lp2 += kCoefDC * (v*v - DC.lp2);
            DC.buf[DC.sampleCount % DC_Detector::BufSize] = v;
            DC.sampleCount++;
        }
        float rms = sqrtf(DC.lp2);
        if (fabsf(DC.lp1) > .75f * rms + .05f) {
            printf("Warning: blit dc = %.3f (%.0f%%), T=%lld (%d)\n", DC.lp1, 100.f * DC.lp1 / rms, DC.sampleCount, (int)(DC.sampleCount % DC_Detector::BufSize));
            DC.reset();
        }
    }
#endif
}
} // end Blit namespace

class OscAnalog : public Operator {
public:
    enum In { FreqHz, NoteNumber, Color, PulseAmount, PulseWidth, UnisonCount, UnisonCents, NumInputs };
    enum Out { Output, NumOutputs };

    OscAnalog() {}

    int getNumPorts(Port::Dir dir) override {
        return dir == Port::In ? NumInputs : NumOutputs;
    }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"frequency", "note", "color", "pulse amount", "pulse width", "unison count", "unison spread"};
        static const cStr outputs[] = {"output"};
        return dir == Port::In ? inputs[index] : outputs[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir == Port::Out) ? Port::Full : Port::Block;
    }

    void init(const std::vector<Buffer>& inputs) override {
        
    }

    void process(Buffer* ins, Buffer* outs, int N) override {
        using namespace Blit;
        float noteHz = (ins[NoteNumber].hasData()) ? convertNoteToHz(ins[NoteNumber].value()) : 0.f;
        float hz = noteHz + ins[FreqHz].value();
        float freqN = hz / mSamplerate;
        
        float *P = mVoice.m_params;
        P[Voice::Color] = ins[Color].valueOrDefault(0.5f);
        P[Voice::PulseAmount] = ins[PulseAmount].value();
        P[Voice::PulseWidth] = .01f + .98f * ins[PulseWidth].valueOrDefault(0.5f);
        
        float unisonCount = std::max(1.f, ins[UnisonCount].value());
        float unisonScale = exp2f(ins[UnisonCents].value() / 1200);
        float unisonScales[2] = {unisonScale, 1.f/unisonScale};
        float unisonAcc[2] = {1.f, 1.f};
        
        mVoice.m_num_oscillators = (int)(.99f + unisonCount);
        
        for (int o = 0; o < mVoice.m_num_oscillators; o++) {
            float *P = mVoice.getOscParams(o);
            float &uScale = unisonAcc[o & 1];
            uScale *= unisonScales[o & 1];
            P[Voice::Osc::Frequency] = freqN * uScale;
            P[Voice::Osc::Amp] = 0.5f * std::min(1.f, unisonCount - o);
        }
        
        float *outPtr = outs[Output].data();
        mVoice.render(&outPtr, N, 1);
    }
    void reset() override { mVoice.resetVoiceSoft(); }

private:
    Blit::Voice mVoice;
};

