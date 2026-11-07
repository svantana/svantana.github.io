#include "../cpp/AudioVM.h"

#include <cmath>
#include <iostream>

int main() {
    AudioVM::Processor processor;
    processor.setup(44100.f, 1, 1, 16);
    processor.loadFromJSON(R"({"version":1,"polyphony":1,"program":[
        {"type":"control","id":"enabled","input":0},
        {"type":"control","id":"release","input":0.001},
        {"type":"ifStart","id":"ifStart1","input":{"condition":"enabled","timeOut":"release"}},
        {"type":"oscNoise","id":"signal"},
        {"type":"ifEnd","id":"ifEnd1"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");

    float output[16]{};
    float* outputs[] = {output};
    processor.process(nullptr, outputs, 16);
    for (float sample : output) {
        if (sample != 0.f) {
            std::cerr << "disabled conditional range produced " << sample << "\n";
            return 1;
        }
    }

    processor.setControlValue(0, 1.f);
    processor.process(nullptr, outputs, 16);
    bool producedAudio = false;
    for (float sample : output) {
        producedAudio = producedAudio || std::abs(sample) > 1e-6f;
    }
    if (!producedAudio) {
        std::cerr << "enabled conditional range did not run\n";
        return 1;
    }

    processor.setControlValue(0, 0.f);
    processor.process(nullptr, outputs, 16);
    producedAudio = false;
    for (float sample : output) producedAudio = producedAudio || std::abs(sample) > 1e-6f;
    if (!producedAudio) {
        std::cerr << "conditional timeout did not keep the range running\n";
        return 1;
    }
    for (int block = 0; block < 3; ++block) processor.process(nullptr, outputs, 16);
    for (float sample : output) {
        if (sample != 0.f) {
            std::cerr << "conditional timeout did not end\n";
            return 1;
        }
    }

    // blocksize is a program-level setting, so it changes how an incoming
    // host buffer is split into processing blocks.
    AudioVM::Processor blockSizeProcessor;
    blockSizeProcessor.setup(44100.f, 1, 1, 16);
    blockSizeProcessor.loadFromJSON(R"({"version":1,"blocksize":8,"program":[
        {"type":"control","id":"enabled","input":0},
        {"type":"control","id":"release","input":0.0003},
        {"type":"ifStart","id":"ifStart1","input":{"condition":"enabled","timeOut":"release"}},
        {"type":"oscNoise","id":"signal"},
        {"type":"ifEnd","id":"ifEnd1"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    float blockSizeOutput[16]{};
    float* blockSizeOutputs[] = {blockSizeOutput};
    blockSizeProcessor.setControlValue(0, 1.f);
    blockSizeProcessor.process(nullptr, blockSizeOutputs, 8);
    blockSizeProcessor.setControlValue(0, 0.f);
    blockSizeProcessor.process(nullptr, blockSizeOutputs, 16);
    bool processedFirstBlock = false;
    for (int sample = 0; sample < 8; ++sample) processedFirstBlock = processedFirstBlock || blockSizeOutput[sample] != 0.f;
    if (!processedFirstBlock) {
        std::cerr << "JSON blocksize did not preserve the first release block\n";
        return 1;
    }
    for (int sample = 8; sample < 16; ++sample) {
        if (blockSizeOutput[sample] != 0.f) {
            std::cerr << "JSON blocksize did not split the host buffer\n";
            return 1;
        }
    }
    return 0;
}
