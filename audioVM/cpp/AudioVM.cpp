#include "AudioVM.h"
#include "libs/picojson.h"
#include <assert.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "ops/CoreOps.h"

// basic operators
#include "ops/Envs.h"
#include "ops/Oscs.h"
#include "ops/Filters.h"

using namespace AudioVM;

const char *kIdAudioIn = "audioIn", *kIdAudioOut = "audioOut";

namespace OpCreatorHelper {

template <class Op>
AudioVM::Processor::OpCreator make() {
    return [] { return std::make_unique<Op>(); };
}

} // namespace

Processor::Processor()
{
    using namespace OpCreatorHelper;
    
    registerType(kIdAudioIn, make<AudioIn>(), "Provides the host audio input signal.");
    registerType(kIdAudioOut, make<AudioOut>(), "Sends a signal to the host audio output.");
    registerType("midiIn", make<MidiBuffer>(), "Provides the latest MIDI gate, note, velocity, and event triggers.");
    registerType("midiBuffer", make<MidiBuffer>(), "Buffers MIDI events for routing and inspection.");
    registerType("transportIn", make<TransportIn>(), "Provides transport tempo, beat position, play state, and loop length.");
    registerType("polyStart", make<PolyStart>(), "Begins a polyphonic voice-processing region.");
    registerType("polyEnd", make<PolyEnd>(), "Ends a polyphonic voice-processing region.");
    registerType("ifStart", make<IfStart>(), "Begins a conditionally processed region with an optional release timeout.");
    registerType("ifEnd", make<IfEnd>(), "Ends a conditionally processed region.");
    registerType("control", make<Control>(), "Exposes a block-rate value as a user-adjustable control.");
    registerType("const", make<ConstOp>(), "Outputs a constant block-rate value.");
    registerType("buffer", make<BufferOperator>(), "Creates a writable full-rate buffer for feedback and in-place operations.");
    registerType("delayRead", make<DelayRead>(), "Reads a delayed full-rate signal, optionally writing an incoming signal to the delay line.");
    registerType("delayWrite", make<DelayWrite>(), "Writes a full-rate signal to a delay line opened by delayRead.");
    registerType("iadd", make<InplaceBinaryOp<Add>>(), "Adds a value into a writable buffer in place.");
    registerType("isub", make<InplaceBinaryOp<Sub>>(), "Subtracts a value from a writable buffer in place.");
    registerType("imul", make<InplaceBinaryOp<Mul>>(), "Multiplies a writable buffer in place.");
    registerType("idiv", make<InplaceBinaryOp<Div>>(), "Divides a writable buffer in place.");
    registerType("toAudio", make<ControlToAudio>(), "Converts a block-rate value to a smoothly changing full-rate signal.");
    // math functions
    registerType("exp", make<MathExp>(), "Applies the natural exponential function.");
    registerType("floor", make<MathFloor>(), "Rounds each value down to the nearest integer.");
    registerType("round", make<MathRound>(), "Rounds each value to the nearest integer.");
    registerType("ceil", make<MathCeil>(), "Rounds each value up to the nearest integer.");
    
    // binary ops
    registerType("add", make<MathBinary<Add>>(), "Adds two values.");
    registerType("sub", make<MathBinary<Sub>>(), "Subtracts the second value from the first.");
    registerType("mul", make<MathBinary<Mul>>(), "Multiplies two values.");
    registerType("div", make<MathBinary<Div>>(), "Divides the first value by the second.");
    registerType("pow", make<MathBinary<Pow>>(), "Raises the first value to the power of the second.");
    registerType("mod", make<MathBinary<Mod>>(), "Returns the floating-point remainder of two values.");
    registerType("equal", make<MathBinary<Equal>>(), "Outputs 1 when two values are equal, otherwise 0.");
    registerType("notEqual", make<MathBinary<NotEqual>>(), "Outputs 1 when two values differ, otherwise 0.");
    registerType("less", make<MathBinary<Less>>(), "Outputs 1 when the first value is less than the second.");
    registerType("lessEqual", make<MathBinary<LessEqual>>(), "Outputs 1 when the first value is less than or equal to the second.");
    registerType("greater", make<MathBinary<Greater>>(), "Outputs 1 when the first value is greater than the second.");
    registerType("greaterEqual", make<MathBinary<GreaterEqual>>(), "Outputs 1 when the first value is greater than or equal to the second.");

    registerType("oscSin", make<OscSin>(), "Generates a sine wave from frequency or MIDI note input.");
    registerType("oscSaw", make<OscSaw>(), "Generates a sawtooth wave from frequency or MIDI note input.");
    registerType("oscNoise", make<OscNoise>(), "Generates white noise.");
    registerType("filterLP", make<FilterLP>(), "Applies a resonant low-pass filter to a full-rate signal.");
    registerType("filterBP", make<FilterBP>(), "Applies a resonant band-pass filter to a full-rate signal.");
    registerType("filterHP", make<FilterHP>(), "Applies a resonant high-pass filter to a full-rate signal.");
    registerType("envADSR", make<EnvADSR>(), "Generates an attack, decay, sustain, and release envelope.");
    registerType("envExp", make<EnvExp>(), "Generates a repeating exponential decay envelope.");
    registerType("envFollow", make<EnvFollower>(), "Tracks the amplitude envelope of an incoming signal.");

    mProgram.list.reserve(128);
}

void Processor::setup(float samplerate, int numInputChannels, int numOutputChannels, int blockSize) {
    mSamplerate = samplerate;
    mNumInputChannels = numInputChannels;
    mNumOutputChannels = numOutputChannels;
    mBlockSize = blockSize;
    mInputPointers.resize(numInputChannels);
    mOutputPointers.resize(numOutputChannels);
}

void Processor::setBPM(float bpm) {
    if (std::isfinite(bpm) && bpm > 0.f) mTransport.bpm = bpm;
}

void Processor::setPlay(bool doPlay) {
    mTransport.playing = doPlay;
}

void Processor::setBeatPos(float beatPos) {
    if (!std::isfinite(beatPos)) return;
    mTransport.beatPos = beatPos;
    if (mTransport.loopBeats > 0.f) {
        mTransport.beatPos = std::fmod(mTransport.beatPos, mTransport.loopBeats);
        if (mTransport.beatPos < 0.f) mTransport.beatPos += mTransport.loopBeats;
    }
}

float Processor::getBeatPos() const {
    return mTransport.beatPos;
}

void Processor::setLoop(float numBeats) {
    if (!std::isfinite(numBeats)) return;
    mTransport.loopBeats = std::max(0.f, numBeats);
    setBeatPos(mTransport.beatPos);
}

void Processor::clearProgram() {
    clearMidiEvents();
    mProgram.list.clear();
    mPolyPrograms.clear();
    mIfEndByStart.clear();
    mPolyRangeByStart.clear();
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
    entry.op->mBlockSize = mBlockSize;
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
        // Port metadata is also used by the editor. Warn when a full buffer
        // is wired into a control-rate input: the operator may only read its
        // first sample, and the exported definition would otherwise hide a
        // rate mismatch like this one.
        if (mBlockSize > 1 && entry.op->getPortRate(Port::In, input) == Port::Block &&
            source.mOutputs[sourcePort].numSamples == mBlockSize) {
            std::fprintf(stderr, "AudioVM warning: full-rate %s.%s connected to block-rate %s.%s\n",
                         source.mType.c_str(), source.op->getPortName(Port::Out, sourcePort).c_str(),
                         operatorId.c_str(), entry.op->getPortName(Port::In, input).c_str());
        }
        entry.mInputs[input] = source.mOutputs[sourcePort];
        entry.mInputs[input].connected = true;
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
    mProgram.mMidiOutputId = -1;
    mProgram.mMidiBufferIds.clear();
    mProgram.mTransportInId = -1;

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
                entry.mInputs[input].connected = false;
                continue;
            }
            const Entry& source = mProgram.list[sourceId / OpSpacing];
            entry.mInputs[input].ptr = source.mOutputs[sourceId % OpSpacing].ptr;
            entry.mInputs[input].connected = true;
        }
        for (Buffer& output : entry.mOutputs) {
            output.ptr = &mProgram.mIoBuffer[pos];
            pos += output.size();
        }
        if (dynamic_cast<AudioIn*>(entry.op.get())) {
            mProgram.mAudioInIds.push_back(entry.mIndex);
        }
        if (dynamic_cast<AudioOut*>(entry.op.get())) {
            mProgram.mAudioOutIds.push_back(entry.mInputIds[0]);
        }
        if (dynamic_cast<Control*>(entry.op.get())) {
            mProgram.mControlIds.push_back(n);
        }
        if (dynamic_cast<MidiBuffer*>(entry.op.get())) {
            mProgram.mMidiBufferIds.push_back(n);
            if (mProgram.mMidiInId < 0) mProgram.mMidiInId = n;
        }
        if (dynamic_cast<TransportIn*>(entry.op.get())) {
            mProgram.mTransportInId = n;
        }
    }

    // Delay capacity is structural rather than a DSP input: it must be known
    // before processing starts so delay lines never allocate on the audio
    // thread.  Keep it as a regular constant input in JSON so the builder
    // does not need a second property syntax.
    for (Entry& entry : mProgram.list) {
        auto* delay = dynamic_cast<DelayRead*>(entry.op.get());
        if (delay == nullptr) continue;
        if (entry.mInputIds.size() <= DelayRead::MaxSeconds ||
            entry.mInputIds[DelayRead::MaxSeconds] < 0) {
            throw std::runtime_error("delayRead requires a constant maxSeconds input");
        }
        const int maxSourceId = entry.mInputIds[DelayRead::MaxSeconds];
        const Entry& maxSource = mProgram.list[maxSourceId / OpSpacing];
        const auto* maxSeconds = dynamic_cast<const ConstOp*>(maxSource.op.get());
        if (maxSeconds == nullptr) {
            throw std::runtime_error("delayRead maxSeconds must be a constant");
        }
        delay->configure(maxSeconds->mValue);
        const bool hasInput = entry.mInputIds.size() > DelayRead::Input &&
            entry.mInputIds[DelayRead::Input] >= 0;
        if (!hasInput && delay->maxSeconds() * mSamplerate < static_cast<float>(mBlockSize)) {
            throw std::runtime_error("delayRead maxSeconds must be at least one processing block");
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
                throw std::runtime_error("polyStart requires a count input");
            }
            const int sourceId = entry.mInputIds[PolyStart::NumVoices];
            const Entry& source = mProgram.list[sourceId / OpSpacing];
            float polyCountValue = 0.f;
            if (const auto* constant = dynamic_cast<const ConstOp*>(source.op.get())) {
                polyCountValue = constant->mValue;
            } else if (const auto* control = dynamic_cast<const Control*>(source.op.get())) {
                polyCountValue = control->mValue;
            } else {
                throw std::runtime_error("polyStart count must be a constant or control");
            }
            const int polyCount = static_cast<int>(roundf(polyCountValue));
            if (polyCount < 1 || polyCount > 128 || polyCountValue != static_cast<float>(polyCount)) {
                throw std::runtime_error("polyStart count must be an integer between 1 and 128");
            }
            mPolyPrograms.emplace_back();
            PolyRange& range = mPolyPrograms.back();
            range.startIndex = index;
            range.voices.resize(polyCount);
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
            PolyRange& range = mPolyPrograms[openRange];
            range.midiBufferId = -1;
            for (int rangeIndex = range.startIndex + 1; rangeIndex < range.endIndex; ++rangeIndex) {
                for (const int sourceId : mProgram.list[rangeIndex].mInputIds) {
                    if (sourceId < 0) continue;
                    const int sourceIndex = sourceId / OpSpacing;
                    if (dynamic_cast<MidiBuffer*>(mProgram.list[sourceIndex].op.get()) == nullptr) continue;
                    if (range.midiBufferId >= 0 && range.midiBufferId != sourceIndex) {
                        throw std::runtime_error("polyphonic range cannot use more than one MIDI buffer");
                    }
                    range.midiBufferId = sourceIndex;
                }
            }
            if (range.midiBufferId < 0) range.midiBufferId = mProgram.mMidiInId;
            openRange = -1;
        }
    }
    if (openRange >= 0) {
        throw std::runtime_error("polyStart has no matching polyEnd");
    }
    mPolyRangeByStart.assign(mProgram.list.size(), -1);
    for (int rangeIndex = 0; rangeIndex < static_cast<int>(mPolyPrograms.size()); ++rangeIndex) {
        mPolyRangeByStart[mPolyPrograms[rangeIndex].startIndex] = rangeIndex;
    }

    // Resolve conditional ranges once at compile time.  This keeps the audio
    // loop allocation-free and lets a false condition jump over an entire
    // nested range without calling any of its operators.
    mIfEndByStart.assign(mProgram.list.size(), -1);
    std::vector<int> openIfs;
    const auto polyScopeForIndex = [&](int index) {
        for (int rangeIndex = 0; rangeIndex < static_cast<int>(mPolyPrograms.size()); ++rangeIndex) {
            const PolyRange& range = mPolyPrograms[rangeIndex];
            if (index > range.startIndex && index < range.endIndex) return rangeIndex;
        }
        return -1;
    };
    for (int index = 0; index < static_cast<int>(mProgram.list.size()); ++index) {
        const Entry& entry = mProgram.list[index];
        if (dynamic_cast<const IfStart*>(entry.op.get()) != nullptr) {
            if (entry.mInputIds.empty() || entry.mInputIds[IfStart::Condition] < 0) {
                throw std::runtime_error("ifStart requires a condition input");
            }
            openIfs.push_back(index);
        } else if (dynamic_cast<const IfEnd*>(entry.op.get()) != nullptr) {
            if (openIfs.empty()) {
                throw std::runtime_error("ifEnd has no matching ifStart");
            }
            const int startIndex = openIfs.back();
            openIfs.pop_back();
            if (polyScopeForIndex(startIndex) != polyScopeForIndex(index)) {
                throw std::runtime_error("ifStart and ifEnd must be in the same polyphonic scope");
            }
            mIfEndByStart[startIndex] = index;
        }
    }
    if (!openIfs.empty()) {
        throw std::runtime_error("ifStart has no matching ifEnd");
    }
    for (int index = 0; index < static_cast<int>(mProgram.list.size()); ++index) {
        mProgram.list[index].mIsIfStart = mIfEndByStart[index] >= 0;
    }

    // Bind write endpoints to the state owned by their read endpoint.  A
    // feedback pair must live in one scope: allowing a polyphonic writer to
    // mutate a global reader would make voices share delay memory.
    const auto rangeForIndex = [&](int index) {
        for (int rangeIndex = 0; rangeIndex < static_cast<int>(mPolyPrograms.size()); ++rangeIndex) {
            const PolyRange& range = mPolyPrograms[rangeIndex];
            if (index > range.startIndex && index < range.endIndex) return rangeIndex;
        }
        return -1;
    };
    std::map<int, int> delayWriterCounts;
    for (int index = 0; index < static_cast<int>(mProgram.list.size()); ++index) {
        Entry& entry = mProgram.list[index];
        auto* writer = dynamic_cast<DelayWrite*>(entry.op.get());
        if (writer == nullptr) continue;
        if (entry.mInputIds.size() <= DelayWrite::Delay || entry.mInputIds[DelayWrite::Delay] < 0 ||
            entry.mInputIds.size() <= DelayWrite::Input || entry.mInputIds[DelayWrite::Input] < 0) {
            throw std::runtime_error("delayWrite requires delay and input arguments");
        }
        const int readIndex = entry.mInputIds[DelayWrite::Delay] / OpSpacing;
        if (readIndex >= index || readIndex < 0 || readIndex >= static_cast<int>(mProgram.list.size())) {
            throw std::runtime_error("delayWrite must refer to an earlier delayRead");
        }
        auto* reader = dynamic_cast<DelayRead*>(mProgram.list[readIndex].op.get());
        if (reader == nullptr) {
            throw std::runtime_error("delayWrite delay argument must refer to a delayRead");
        }
        if (rangeForIndex(readIndex) != rangeForIndex(index)) {
            throw std::runtime_error("delayRead and delayWrite must be in the same polyphonic scope");
        }
        if (++delayWriterCounts[readIndex] != 1) {
            throw std::runtime_error("a delayRead may have only one delayWrite");
        }
        if (rangeForIndex(index) < 0) writer->link(reader->state());
    }

    // A DelayRead either writes from its regular input or is paired with one
    // DelayWrite.  Allowing both would advance the same delay line twice per
    // block and produce ambiguous feedback timing.
    for (int index = 0; index < static_cast<int>(mProgram.list.size()); ++index) {
        const Entry& entry = mProgram.list[index];
        if (dynamic_cast<const DelayRead*>(entry.op.get()) == nullptr) continue;
        const bool hasInput = entry.mInputIds.size() > DelayRead::Input &&
            entry.mInputIds[DelayRead::Input] >= 0;
        if (hasInput && delayWriterCounts.count(index) != 0) {
            throw std::runtime_error("delayRead with an input cannot also have a delayWrite");
        }
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
                clone.mIsIfStart = source.mIsIfStart;
                clone.mInputIds = source.mInputIds;
                clone.op = makeOpWithId(source.mType);
                clone.op->mSamplerate = mSamplerate;
                clone.op->init();
                if (const auto* sourceDelay = dynamic_cast<const DelayRead*>(source.op.get())) {
                    auto* cloneDelay = dynamic_cast<DelayRead*>(clone.op.get());
                    cloneDelay->configure(sourceDelay->maxSeconds());
                }
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
                    const int sourceId = input < static_cast<int>(clone.mInputIds.size())
                        ? clone.mInputIds[input] : -1;
                    if (sourceId < 0) {
                        clone.mInputs[input].ptr = &voice.mIoBuffer[0];
                        clone.mInputs[input].connected = false;
                        continue;
                    }
                    const int sourceIndex = sourceId / OpSpacing;
                    const int sourcePort = sourceId % OpSpacing;
                    if (sourceIndex > range.startIndex && sourceIndex < range.endIndex) {
                        clone.mInputs[input] = voice.list[sourceIndex - range.startIndex - 1].mOutputs[sourcePort];
                    } else if (sourceIndex == range.midiBufferId) {
                        clone.mInputs[input] = Buffer(&voice.mMidiValues[sourcePort]);
                    } else {
                        clone.mInputs[input] = mProgram.list[sourceIndex].mOutputs[sourcePort];
                    }
                    clone.mInputs[input].connected = true;
                }
            }

            // The master graph validated the pair; bind this writer to the
            // corresponding voice-local reader state.
            for (int offset = 0; offset < static_cast<int>(voice.list.size()); ++offset) {
                Entry& clone = voice.list[offset];
                auto* writer = dynamic_cast<DelayWrite*>(clone.op.get());
                if (writer == nullptr) continue;
                const int readIndex = clone.mInputIds[DelayWrite::Delay] / OpSpacing;
                if (readIndex <= range.startIndex || readIndex >= range.endIndex) {
                    throw std::runtime_error("internal error linking polyphonic delay");
                }
                auto* reader = dynamic_cast<DelayRead*>(
                    voice.list[readIndex - range.startIndex - 1].op.get());
                if (reader == nullptr) {
                    throw std::runtime_error("internal error linking polyphonic delay reader");
                }
                writer->link(reader->state());
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

    // Events belong to the graph that received them.  Do not let a held or
    // queued event from an old patch affect the newly loaded program.
    clearMidiEvents();

    int blockSize = mBlockSize;
    const auto blockSizeIt = root.find("blocksize");
    if (blockSizeIt != root.end()) {
        if (!blockSizeIt->second.is<double>()) {
            throw std::runtime_error("Invalid program JSON: property 'blocksize' must be a positive integer");
        }
        const double requestedBlockSize = blockSizeIt->second.get<double>();
        if (!std::isfinite(requestedBlockSize) || requestedBlockSize < 1 ||
            requestedBlockSize > std::numeric_limits<int>::max() ||
            std::floor(requestedBlockSize) != requestedBlockSize) {
            throw std::runtime_error("Invalid program JSON: property 'blocksize' must be a positive integer");
        }
        blockSize = static_cast<int>(requestedBlockSize);
    }

    const picojson::array& program = programIt->second.get<picojson::array>();

    // Construct the new graph separately from the active one.  A failed load
    // must not leave the processor with a half-built program.
    Program previousProgram = std::move(mProgram);
    Compiler previousCompiler = std::move(mCompiler);
    std::vector<PolyRange> previousPolyPrograms = std::move(mPolyPrograms);
    std::vector<int> previousIfEndByStart = std::move(mIfEndByStart);
    const unsigned long long previousVoiceAge = mVoiceAge;
    const int previousBlockSize = mBlockSize;
    mBlockSize = blockSize;
    mProgram = Program();
    mProgram.list.reserve(program.size());
    mCompiler = Compiler();

    try {
        // audioVM.py exposes these as implicit sources rather than program
        // entries.  Store their individual output-port IDs in the same lookup
        // table used by regular operator IDs.
        const int midiInId = addOperator("midiIn");
        mProgram.list.back().mId = "midi";
        mCompiler.mIdToIndex.emplace("midi", midiInId);
        mCompiler.mIdToIndex.emplace("midi.gate", midiInId + MidiIn::Gate);
        mCompiler.mIdToIndex.emplace("midi.note", midiInId + MidiIn::Note);
        mCompiler.mIdToIndex.emplace("midi.velocity", midiInId + MidiIn::Velocity);
        mCompiler.mIdToIndex.emplace("midi.onset", midiInId + MidiIn::Onset);
        mCompiler.mIdToIndex.emplace("midi.offset", midiInId + MidiIn::Offset);
        const int transportInId = addOperator("transportIn");
        mCompiler.mIdToIndex.emplace("transport.bpm", transportInId + TransportIn::BPM);
        mCompiler.mIdToIndex.emplace("transport.beatPos", transportInId + TransportIn::BeatPos);
        mCompiler.mIdToIndex.emplace("transport.play", transportInId + TransportIn::Play);
        mCompiler.mIdToIndex.emplace("transport.loopBeats", transportInId + TransportIn::LoopBeats);

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
            if (dynamic_cast<MidiBuffer*>(entry.op.get()) != nullptr) {
                mCompiler.mIdToIndex.emplace(id + ".gate", opId + MidiBuffer::Gate);
                mCompiler.mIdToIndex.emplace(id + ".note", opId + MidiBuffer::Note);
                mCompiler.mIdToIndex.emplace(id + ".velocity", opId + MidiBuffer::Velocity);
                mCompiler.mIdToIndex.emplace(id + ".onset", opId + MidiBuffer::Onset);
                mCompiler.mIdToIndex.emplace(id + ".offset", opId + MidiBuffer::Offset);
            }
        }
        compileProgram();
    } catch (...) {
        mProgram = std::move(previousProgram);
        mCompiler = std::move(previousCompiler);
        mPolyPrograms = std::move(previousPolyPrograms);
        mIfEndByStart = std::move(previousIfEndByStart);
        mVoiceAge = previousVoiceAge;
        mBlockSize = previousBlockSize;
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
        if (entry.mType != "midiIn" &&
            dynamic_cast<const TransportIn*>(entry.op.get()) == nullptr && !entry.mId.empty()) {
            usedIds.emplace(entry.mId, true);
        }
    }
    int generatedId = 0;
    for (const Entry& entry : mProgram.list) {
        if (entry.mType == "midiIn") {
            sourceIds.emplace(entry.mIndex + MidiIn::Gate, "midi.gate");
            sourceIds.emplace(entry.mIndex + MidiIn::Note, "midi.note");
            sourceIds.emplace(entry.mIndex + MidiIn::Velocity, "midi.velocity");
            sourceIds.emplace(entry.mIndex + MidiIn::Onset, "midi.onset");
            sourceIds.emplace(entry.mIndex + MidiIn::Offset, "midi.offset");
            continue;
        }
        if (dynamic_cast<const MidiBuffer*>(entry.op.get()) != nullptr) {
            sourceIds.emplace(entry.mIndex + MidiBuffer::Gate, entry.mId + ".gate");
            sourceIds.emplace(entry.mIndex + MidiBuffer::Note, entry.mId + ".note");
            sourceIds.emplace(entry.mIndex + MidiBuffer::Velocity, entry.mId + ".velocity");
            sourceIds.emplace(entry.mIndex + MidiBuffer::Onset, entry.mId + ".onset");
            sourceIds.emplace(entry.mIndex + MidiBuffer::Offset, entry.mId + ".offset");
        }
        if (dynamic_cast<const TransportIn*>(entry.op.get()) != nullptr) {
            sourceIds.emplace(entry.mIndex + TransportIn::BPM, "transport.bpm");
            sourceIds.emplace(entry.mIndex + TransportIn::BeatPos, "transport.beatPos");
            sourceIds.emplace(entry.mIndex + TransportIn::Play, "transport.play");
            sourceIds.emplace(entry.mIndex + TransportIn::LoopBeats, "transport.loopBeats");
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
        if (entry.mType == "midiIn" ||
            dynamic_cast<const TransportIn*>(entry.op.get()) != nullptr) {
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
        definition["description"] = picojson::value(mOpDescriptions.at(operatorId));

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
        for (int offset = 0; offset < static_cast<int>(voice.list.size()); ++offset) {
            Entry& entry = voice.list[offset];
            const int programIndex = entry.mIndex / OpSpacing;
            if (entry.mIsIfStart) {
                auto* ifStart = static_cast<IfStart*>(entry.op.get());
                ifStart->process(entry.mInputs.data(), entry.mOutputs.data(), numSamples);
                if (ifStart->active()) continue;
                const int endIndex = mIfEndByStart[programIndex];
                assert(endIndex > programIndex);
                const int endOffset = endIndex - range.startIndex - 1;
                // A skipped producer must not expose audio from its previous
                // invocation to a later operator outside this range.
                for (int skipped = offset + 1; skipped < endOffset; ++skipped) {
                    for (Buffer& output : voice.list[skipped].mOutputs) {
                        std::fill(output.ptr, output.ptr + output.size(), 0.f);
                    }
                }
                offset = endOffset;
                continue;
            }
            entry.op->process(entry.mInputs.data(), entry.mOutputs.data(), numSamples);
        }
        if (voice.mMidiValues[MidiIn::Gate] == 0.f && voice.releasePhase >= 0.f) {
            const float duration = std::max(1e-9f, range.timeOutSeconds);
            voice.releasePhase = std::min(1.f,
                voice.releasePhase + numSamples / (mSamplerate * duration));
            if (voice.releasePhase >= 1.f) voice.active = false;
        }
    }
}

bool MidiEventFifo::push(MidiEvent event) {
    const size_t write = mWrite.load(std::memory_order_relaxed);
    const size_t next = (write + 1) % Capacity;
    if (next == mRead.load(std::memory_order_acquire)) return false;
    mEvents[write] = event;
    mWrite.store(next, std::memory_order_release);
    return true;
}

bool MidiEventFifo::pop(MidiEvent& event) {
    const size_t read = mRead.load(std::memory_order_relaxed);
    if (read == mWrite.load(std::memory_order_acquire)) return false;
    event = mEvents[read];
    mRead.store((read + 1) % Capacity, std::memory_order_release);
    return true;
}

void MidiEventFifo::clear() {
    const size_t write = mWrite.load(std::memory_order_acquire);
    mRead.store(write, std::memory_order_release);
}

void Processor::clearMidiEvents() {
    for (const int bufferId : mProgram.mMidiBufferIds) {
        static_cast<MidiBuffer*>(mProgram.list[bufferId].op.get())->clearEvents();
    }
}

void Processor::midiEvent(char b0, char b1, char b2) {
    midiEvent("midi", b0, b1, b2);
}

bool Processor::midiEvent(const std::string& bufferId, char b0, char b1, char b2) {
    for (const int index : mProgram.mMidiBufferIds) {
        Entry& entry = mProgram.list[index];
        if (entry.mId != bufferId) continue;
        return static_cast<MidiBuffer*>(entry.op.get())->write({
            static_cast<unsigned char>(b0),
            static_cast<unsigned char>(b1),
            static_cast<unsigned char>(b2),
        });
    }
    return false;
}

bool Processor::setMidiOutput(const std::string& bufferId) {
    for (const int index : mProgram.mMidiBufferIds) {
        if (mProgram.list[index].mId == bufferId) {
            mProgram.mMidiOutputId = index;
            return true;
        }
    }
    return false;
}

bool Processor::readMidiOutput(MidiEvent& event) {
    if (mProgram.mMidiOutputId < 0) return false;
    return static_cast<MidiBuffer*>(mProgram.list[mProgram.mMidiOutputId].op.get())->read(event);
}

void Processor::applyMidiEvent(int midiBufferId, MidiEvent event) {
    Entry &E = mProgram.list[midiBufferId];
    auto &O = E.mOutputs;
    //O[MidiIn::Channel].value() = b0 & 0x0f; // TODO: handle channel
    const int note = event.data1;
    const int velocity = event.data2;
    int msg = event.status & 0xf0;
    if (msg == kMidiNoteOn && velocity == 0) msg = kMidiNoteOff;
    switch (msg) {
        case kMidiNoteOn: {
            O[MidiIn::Gate].set(1.f);
            O[MidiIn::Note].set(note);
            O[MidiIn::Velocity].set(velocity);
            O[MidiIn::Onset].set(1.f);
            for (PolyRange& range : mPolyPrograms) {
                if (range.midiBufferId != midiBufferId) continue;
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
                selected->releasePhase = -1.f;
                selected->age = ++mVoiceAge;
                selected->mMidiValues[MidiIn::Gate] = 1.f;
                selected->mMidiValues[MidiIn::Note] = static_cast<float>(note);
                selected->mMidiValues[MidiIn::Velocity] = static_cast<float>(velocity);
                selected->mMidiValues[MidiIn::Onset] = 1.f;
            }
            break;
        }
        case kMidiNoteOff: {
            if (static_cast<int>(O[MidiIn::Note].value()) == note) {
                O[MidiIn::Gate].set(0.f);
            }
            O[MidiIn::Offset].set(1.f);
            for (PolyRange& range : mPolyPrograms) {
                if (range.midiBufferId != midiBufferId) continue;
                for (Voice& voice : range.voices) {
                    if (voice.active && voice.note == note) {
                        voice.mMidiValues[MidiIn::Gate] = 0.f;
                        voice.mMidiValues[MidiIn::Offset] = 1.f;
                        if (range.timeOutSeconds >= 0.f) {
                            voice.releasePhase = 0.f;
                        }
                    }
                }
            }
            break;
        }
    };
}

void Processor::consumeMidiEvents() {
    for (const int bufferId : mProgram.mMidiBufferIds) {
        auto* buffer = static_cast<MidiBuffer*>(mProgram.list[bufferId].op.get());
        MidiEvent event;
        while (buffer->consume(event)) {
            applyMidiEvent(bufferId, event);
            buffer->publish(event);
        }
    }
}

void Processor::updateTransportOutputs() {
    if (mProgram.mTransportInId < 0) return;
    auto& outputs = mProgram.list[mProgram.mTransportInId].mOutputs;
    outputs[TransportIn::BPM].set(mTransport.bpm);
    outputs[TransportIn::BeatPos].set(mTransport.beatPos);
    outputs[TransportIn::Play].set(mTransport.playing ? 1.f : 0.f);
    outputs[TransportIn::LoopBeats].set(mTransport.loopBeats);
}

void Processor::advanceTransport(int numSamples) {
    if (!mTransport.playing || numSamples <= 0) return;
    mTransport.beatPos += numSamples * mTransport.bpm / (60.f * mSamplerate);
    if (mTransport.loopBeats > 0.f && mTransport.beatPos >= mTransport.loopBeats) {
        mTransport.beatPos = std::fmod(mTransport.beatPos, mTransport.loopBeats);
    }
}
    
void Processor::process(float *const* inBufs, float **outBufs, int numSamples) {
    int maxBlockSize = mBlockSize;
    for (int ioPos = 0; ioPos < numSamples;) {
        int blockSize = std::min(maxBlockSize, numSamples - ioPos);

        // Apply all host MIDI received since the preceding audio block on the
        // audio thread.  Multiple note-ons are processed in FIFO order, so a
        // polyphonic range can allocate one voice per event.
        consumeMidiEvents();

        // Transport values are block-rate controls.  Expose the position at
        // the start of this block, then advance after every operator has
        // observed it.
        updateTransportOutputs();

        for (int n = 0; n < (int)mProgram.mAudioInIds.size(); n++) {
            int id = mProgram.mAudioInIds[n];
            for (int ch = 0; ch < mNumInputChannels && inBufs; ch++) {
                float *dst = mProgram.list[id/OpSpacing].mOutputs[id%OpSpacing].dataWrap(ch);
                const float *src = &inBufs[ch][ioPos];
                std::memcpy(dst, src, static_cast<size_t>(blockSize) * sizeof(float));
            }
        }
        
        for (int n = 0; n < (int)mProgram.list.size(); n++) {
            const int rangeIndex = mPolyRangeByStart[n];
            if (rangeIndex >= 0) {
                PolyRange& range = mPolyPrograms[rangeIndex];
                processPolyRange(range, blockSize);
                n = range.endIndex;
                continue;
            }
            Entry &E = mProgram.list[n];
            if (E.mIsIfStart) {
                auto* ifStart = static_cast<IfStart*>(E.op.get());
                ifStart->process(E.mInputs.data(), E.mOutputs.data(), blockSize);
                if (ifStart->active()) continue;
                const int endIndex = mIfEndByStart[n];
                assert(endIndex > n);
                // Keep skipped outputs deterministic rather than leaking the
                // previous block's samples to consumers after the range.
                for (int skipped = n + 1; skipped < endIndex; ++skipped) {
                    for (Buffer& output : mProgram.list[skipped].mOutputs) {
                        std::fill(output.ptr, output.ptr + output.size(), 0.f);
                    }
                }
                n = endIndex;
                continue;
            }
            E.op->process(E.mInputs.data(), E.mOutputs.data(), blockSize);
        }
        for (int o = 0; o < (int)mProgram.mAudioOutIds.size(); o++) {
            int id = mProgram.mAudioOutIds[o];
            for (int ch = 0; ch < mNumOutputChannels; ch++) {
                const float *src = mProgram.list[id/OpSpacing].mOutputs[id%OpSpacing].dataWrap(ch);
                float *dst = &outBufs[ch][ioPos];
                std::memcpy(dst, src, static_cast<size_t>(blockSize) * sizeof(float));
            }
        }
        // MIDI inputs are control-rate, so an event trigger is observable for
        // this processing block only.  Clear them after all global and
        // polyphonic operators have consumed the values.
        if (mProgram.mMidiInId >= 0) {
            auto& midiOutputs = mProgram.list[mProgram.mMidiInId].mOutputs;
            midiOutputs[MidiIn::Onset].set(0.f);
            midiOutputs[MidiIn::Offset].set(0.f);
        }
        for (PolyRange& range : mPolyPrograms) {
            for (Voice& voice : range.voices) {
                voice.mMidiValues[MidiIn::Onset] = 0.f;
                voice.mMidiValues[MidiIn::Offset] = 0.f;
            }
        }
        advanceTransport(blockSize);
        ioPos += blockSize;
    }
}

void Processor::processPlanar(float* input, float* output, int numSamples) {
    if (input == nullptr) {
        for (int channel = 0; channel < mNumOutputChannels; ++channel) {
            mOutputPointers[channel] = output + static_cast<size_t>(channel) * numSamples;
        }
        process(nullptr, mOutputPointers.data(), numSamples);
        return;
    }
    for (int channel = 0; channel < mNumInputChannels; ++channel) {
        mInputPointers[channel] = input + static_cast<size_t>(channel) * numSamples;
    }
    for (int channel = 0; channel < mNumOutputChannels; ++channel) {
        mOutputPointers[channel] = output + static_cast<size_t>(channel) * numSamples;
    }
    process(mInputPointers.data(), mOutputPointers.data(), numSamples);
}

void Processor::registerType(const std::string& name, OpCreator creator, std::string description) {
    auto [it, inserted] = mOpCreators.emplace(name, std::move(creator));
    if (!inserted) {
        throw std::runtime_error("Duplicate processor type: " + name);
    }
    mOpDescriptions.emplace(name, std::move(description));
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
