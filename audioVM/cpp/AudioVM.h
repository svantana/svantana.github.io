#pragma once

#include <vector>
#include <string>
#include <map>
#include "Operator.h"

namespace AudioVM {

typedef std::unique_ptr<Operator> OpPtr;

class Processor {
public:
    Processor();
    void setup(float samplerate, int numInputChannels, int numOutputChannels, int blockSize);
    
    void loadFromJSON(std::string jsonString);
    std::string getOpListAsJSON();
    std::string getOpDefinitionsJSON() const;

    // manual program construction
    void clearProgram();
    int addOperator(std::string operatorId, std::vector<int> inputs = {});
    int addControl(std::string name, float value = 0.f);
    int addConstant(float value);
    bool compileProgram();
    
    // realtime processing interface
    void midiEvent(char b0, char b1, char b2);
    bool midiEvent(const std::string& bufferId, char b0, char b1, char b2);
    bool setMidiOutput(const std::string& bufferId);
    bool readMidiOutput(MidiEvent& event);
    void setBPM(float bpm);
    void setPlay(bool doPlay);
    void setBeatPos(float beatPos);
    float getBeatPos() const;
    void setLoop(float numBeats);
    /* Accepts arbitrary buffer sizes */
    void process(float *const* inBufs, float **outBufs, int numSamples);
    // Planar buffers used by the WebAssembly boundary.  Pointer arrays are
    // retained by the processor so the audio callback does not allocate.
    void processPlanar(float* input, float* output, int numSamples);
    
    int getNumControls();
    float getControlValue(int index);
    void setControlValue(int index, float value);

    // use this to register your operator types
    typedef std::function<OpPtr()> OpCreator;
    void registerType(const std::string& name, OpCreator creator);
private:
    std::unordered_map<std::string, OpCreator> mOpCreators;
    OpPtr makeOpWithId(const std::string& name) const;
    static const int OpSpacing = 10;
    
    int mBlockSize, mNumInputChannels, mNumOutputChannels;
    float mSamplerate;
    struct Transport {
        float bpm = 120.f;
        float beatPos = 0.f;
        float loopBeats = 4.f;
        bool playing = false;
    } mTransport;
    std::vector<float*> mInputPointers, mOutputPointers;
    
    void processBlock(float *const* inBufs, float **outBufs);

    struct Entry {
        std::unique_ptr<Operator> op;
        std::vector<int> mInputIds;
        std::vector<Buffer> mInputs, mOutputs;
        std::string mId;
        std::string mType;
        int mIndex;
        bool mIsIfStart = false;
    };
    struct Program {
        void clear();
        bool compile();
        void print();
        std::vector<Entry> list;
        std::vector<float> mIoBuffer;
        std::vector<int> mAudioInIds, mAudioOutIds, mControlIds, mMidiBufferIds;
        int mMidiInId = -1;
        int mMidiOutputId = -1;
        int mTransportInId = -1;
    };
    Program mProgram;

    // A polyphonic range shares the graph's global buffers but owns cloned
    // operator state and output buffers for every voice.
    struct Voice {
        std::vector<Entry> list;
        std::vector<float> mIoBuffer;
        // Keep in sync with MidiIn::NumTypes; MidiIn is implementation-only.
        std::array<float, 5> mMidiValues{};
        bool active = false;
        int note = -1;
        // -1 means the voice is gated; otherwise this advances from 0 to 1
        // over the polyphonic timeout, like an ADSR release phase.
        float releasePhase = -1.f;
        unsigned long long age = 0;
    };
    struct PolyRange {
        int startIndex = -1;
        int endIndex = -1;
        int midiBufferId = -1;
        float timeOutSeconds = -1.f;
        int timeOutControlIndex = -1;
        std::vector<Voice> voices;
    };
    std::vector<PolyRange> mPolyPrograms;
    // Indexed by the program index of an IfStart; -1 for non-start entries.
    std::vector<int> mIfEndByStart;
    // Maps a program index to its polyphonic range, or -1.  Only start
    // indices are populated, making the hot loop an O(1) lookup.
    std::vector<int> mPolyRangeByStart;
    unsigned long long mVoiceAge = 0;

    void resetVoice(Voice& voice);
    void processPolyRange(PolyRange& range, int numSamples);
    void applyMidiEvent(int midiBufferId, MidiEvent event);
    void consumeMidiEvents();
    void clearMidiEvents();
    void updateTransportOutputs();
    void advanceTransport(int numSamples);
    
    struct Compiler {
        std::map<std::string, int> mIdToIndex;
    } mCompiler;
};

} // end namespace AudioVM
