#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include "../Operator.h"

using namespace AudioVM;

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
struct MidiBuffer : public BlockRateOp {
    // Onset and Offset are one-processing-block triggers set by note-on and
    // note-off events.  Keep the established output indices intact so manual
    // programs that address the original MIDI outputs remain compatible.
    enum Type { Gate, Note, Velocity, Onset, Offset, NumTypes };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? NumTypes : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr o[] = {"gate", "note", "velocity", "onset", "offset"};
        return dir == Port::Out ? o[index] : "";
    }
    bool write(MidiEvent event) { return mIncomingEvents.push(event); }
    bool read(MidiEvent& event) { return mProcessedEvents.pop(event); }
    bool consume(MidiEvent& event) { return mIncomingEvents.pop(event); }
    void publish(MidiEvent event) { mProcessedEvents.push(event); }
    void clearEvents() { mIncomingEvents.clear(); mProcessedEvents.clear(); }

private:
    MidiEventFifo mIncomingEvents;
    MidiEventFifo mProcessedEvents;
};
using MidiIn = MidiBuffer;
struct TransportIn : public BlockRateOp {
    enum Type { BPM, BeatPos, Play, LoopBeats, NumTypes };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::Out) ? NumTypes : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr o[] = {"bpm", "beatPos", "play", "loopBeats"};
        return dir == Port::Out ? o[index] : "";
    }
};
struct PolyStart : public BlockRateOp {
    enum In { NumVoices, TimeOut, NumInputs };
    int getNumPorts(Port::Dir dir) override { return (dir == Port::In) ? NumInputs : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[] = {"count", "timeOut"};
        return i[index];
    }
};
struct PolyEnd : public BlockRateOp {
    int getNumPorts(Port::Dir dir) override { return 0; }
};

// Structural operators that delimit a conditionally processed range.  The
// processor consumes these markers directly; they have no signal outputs.
struct IfStart : public BlockRateOp {
    enum In { Condition, TimeOut, NumInputs };
    int getNumPorts(Port::Dir dir) override { return dir == Port::In ? NumInputs : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"condition", "timeOut"};
        return dir == Port::In ? inputs[index] : "";
    }
    void process(Buffer* in, Buffer*, int N) override {
        if (in[Condition].value() != 0.f) {
            mReleasePhase = 0.f;
            mActive = true;
        } else if (mReleasePhase >= 0.f) {
            const float duration = std::max(1e-9f, in[TimeOut].value());
            mReleasePhase = std::min(1.f, mReleasePhase + N / (mSamplerate * duration));
            mActive = mReleasePhase < 1.f;
        } else {
            mActive = false;
        }
    }
    bool active() const { return mActive; }
    void reset() override { mActive = false; mReleasePhase = -1.f; }
private:
    bool mActive = false;
    float mReleasePhase = -1.f;
};
struct IfEnd : public BlockRateOp {
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
    int getOutputNumSamples(int, int, const std::vector<Buffer>&) override {
        return mValues.empty() ? 1 : static_cast<int>(mValues.size());
    }
    void setValues(std::vector<float> values) { mValues = std::move(values); }
    bool isVector() const { return !mValues.empty(); }
    const std::vector<float>& values() const { return mValues; }
    void copyTo(Buffer& output) const {
        assert(isVector() && output.numSamples == static_cast<int>(mValues.size()));
        std::copy(mValues.begin(), mValues.end(), output.data());
    }
    void process(Buffer *i, Buffer *o, int N) override {
        if (!isVector()) o[0].value() = mValue;
    }
    float mValue = 0.f;
    std::string mName;
private:
    std::vector<float> mValues;
};

// A scalar register. With no input its graph-owned output buffer is
// initialized once and otherwise retains its value between blocks. With an
// input it copies that input into a new scalar snapshot.
class Value : public BlockRateOp {
public:
    int getNumPorts(Port::Dir) override { return 1; }
    std::string getPortName(Port::Dir dir, int) override {
        return dir == Port::In ? "value" : "output";
    }
    void setInitial(float initial) { mInitial = initial; }
    float initial() const { return mInitial; }
    void initialize(Buffer& output) const { output.value() = mInitial; }
    void process(Buffer* in, Buffer* out, int) override {
        if (in[0].hasData()) out[0].value() = in[0].value();
    }
private:
    float mInitial = 0.f;
};

struct ValueWrite : public BlockRateOp {
public:
    enum In { Target, Input, NumInputs };
    int getNumPorts(Port::Dir dir) override { return dir == Port::In ? NumInputs : 0; }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"target", "input"};
        return dir == Port::In ? inputs[index] : "";
    }
    void process(Buffer* in, Buffer*, int) override {
        in[Target].value() = in[Input].value();
    }
};

// Linearly interpolates a table at zero-based floating-point indices. The
// first and last table segments extend beyond the table endpoints.
class InterpOp : public Operator {
public:
    enum In { Array, Index, NumInputs };
    int getNumPorts(Port::Dir dir) override { return dir == Port::In ? NumInputs : 1; }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"array", "index"};
        return dir == Port::In ? inputs[index] : "output";
    }
    int getOutputNumSamples(int, int, const std::vector<Buffer>& inputs) override {
        return inputs.size() > Index ? inputs[Index].numSamples : 1;
    }
    void process(Buffer* in, Buffer* out, int N) override {
        const int tableSize = in[Array].numSamples;
        const float* table = in[Array].data();
        const float* indices = in[Index].data();
        float* dst = out[0].data();
        const int count = std::min(N, out[0].numSamples);
        if (tableSize < 2) {
            for (int n = 0; n < count; ++n) dst[n] = table[0];
            return;
        }
        for (int n = 0; n < count; ++n) {
            const float index = indices[in[Index].numSamples == 1 ? 0 : n];
            if (std::isnan(index)) {
                dst[n] = index;
                continue;
            }
            int lower = 0;
            if (index >= static_cast<float>(tableSize - 1)) lower = tableSize - 2;
            else if (index >= 0.f) lower = static_cast<int>(std::floor(index));
            const float fraction = index - static_cast<float>(lower);
            dst[n] = table[lower] + fraction * (table[lower + 1] - table[lower]);
        }
    }
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

// DelayRead is a normal delay when its Input port is connected.  With Input
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

class DelayRead : public Operator {
public:
    enum In { Input, TimeSeconds, MaxSeconds, NumInputs };
    enum Out { Output, NumOutputs };

    DelayRead() : mState(std::make_shared<DelayLineState>()) {}

    int getNumPorts(Port::Dir dir) override {
        return dir == Port::In ? NumInputs : NumOutputs;
    }
    std::string getPortName(Port::Dir dir, int index) override {
        static const cStr inputs[] = {"input", "time", "maxSeconds"};
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
            throw std::runtime_error("delayRead maxSeconds must be positive and finite");
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
            throw std::runtime_error("delayRead was not configured");
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
    // Delay is a compile-time link to DelayRead.  Its buffer is unused at
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
            throw std::runtime_error("delayWrite is not linked to a delayRead");
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
