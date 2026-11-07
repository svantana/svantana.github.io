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
    return 0;
}
