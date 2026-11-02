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
    /* Accepts arbitrary buffer sizes */
    void process(float *const* inBufs, float **outBufs, int numSamples);
    
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
    
    void processBlock(float *const* inBufs, float **outBufs);

    struct Entry {
        std::unique_ptr<Operator> op;
        std::vector<int> mInputIds;
        std::vector<Buffer> mInputs, mOutputs;
        std::string mId;
        std::string mType;
        int mIndex;
    };
    struct Program {
        void clear();
        bool compile();
        void print();
        std::vector<Entry> list;
        std::vector<float> mIoBuffer;
        std::vector<int> mAudioInIds, mAudioOutIds, mControlIds;
        int mMidiInId = -1;
    };
    Program mProgram;
    std::vector<Program> mVoicePrograms;
    
    struct Compiler {
        std::map<std::string, int> mIdToIndex;
    } mCompiler;
};

} // end namespace AudioVM
