#pragma once

#include <vector>
#include <string>
#include <map>
#include "Operator.h"

namespace AudioVM {

class Processor {
public:
    Processor();
    void setup(float samplerate, int numInputChannels, int numOutputChannels);
    
    void loadFromJSON(std::string jsonString);

    // manual program construction
    void clearProgram();
    int addOperator(std::string operatorId, int *inputs = nullptr);
    bool compileProgram();
    
    /* Accepts arbitrary buffer sizes */
    void process(float *const* inBufs, float **outBufs, int numSamples);

    // use this to register your operator types
    typedef std::function<std::unique_ptr<Operator>()> OpCreator;
    void registerType(const std::string& name, OpCreator creator);
private:
    std::unordered_map<std::string, OpCreator> mOpCreators;
    std::unique_ptr<Operator> create(const std::string& name) const;
    static const int OperatorIndexSpacing = 10;
    
    int mBlockSize, mNumInputChannels, mNumOutputChannels;
    float mSamplerate;
    
    void processBlock(float *const* inBufs, float **outBufs);

    struct Entry {
        std::unique_ptr<Operator> op;
        std::vector<int> mInputs;
        std::vector<Buffer> mOutputs;
        std::string mId;
        int mIndex;
    };
    struct Program {
        void clearProgram();
        std::vector<Entry> list;
    };
    Program mProgram;
    std::vector<Program> mVoicePrograms;
    
    struct Compiler {
        std::map<std::string, int> mIdToIndex;
    } mCompiler;
};

} // end namespace AudioVM
