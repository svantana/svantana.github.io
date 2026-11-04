#include "AudioVM.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kChannels = 2;
constexpr int kBlockSize = 128;
constexpr float kSampleRate = 44100.f;

bool processAndCheck(AudioVM::Processor& processor, int frames, float& peak) {
    std::vector<std::vector<float>> input(kChannels, std::vector<float>(frames, 0.f));
    std::vector<std::vector<float>> output(kChannels, std::vector<float>(frames, 0.f));
    float* inputPointers[kChannels];
    float* outputPointers[kChannels];
    for (int channel = 0; channel < kChannels; ++channel) {
        inputPointers[channel] = input[channel].data();
        outputPointers[channel] = output[channel].data();
    }

    processor.process(inputPointers, outputPointers, frames);
    for (const auto& channel : output) {
        for (const float sample : channel) {
            if (!std::isfinite(sample)) {
                std::cerr << "produced a non-finite output sample\n";
                return false;
            }
            peak = std::max(peak, std::abs(sample));
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string presetName = argc == 2 ? argv[1] : "<stdin>";
    const std::string json(std::istreambuf_iterator<char>(std::cin), {});
    if (json.empty()) {
        std::cerr << presetName << ": no program JSON received\n";
        return 1;
    }

    try {
        AudioVM::Processor processor;
        processor.setup(kSampleRate, kChannels, kChannels, kBlockSize);
        processor.loadFromJSON(json);

        for (int index = 0; index < processor.getNumControls(); ++index) {
            const float value = processor.getControlValue(index);
            if (!std::isfinite(value)) {
                std::cerr << presetName << ": control " << index << " is not finite\n";
                return 1;
            }
            processor.setControlValue(index, 0.5f);
        }

        float peak = 0.f;
        processor.midiEvent(static_cast<char>(0x90), 60, 100);
        // Includes a partial final block to cover Processor::process splitting.
        for (const int frames : {kBlockSize, kBlockSize * 3 + 17, kBlockSize * 4}) {
            if (!processAndCheck(processor, frames, peak)) return 1;
        }
        processor.midiEvent(static_cast<char>(0x80), 60, 0);
        if (!processAndCheck(processor, kBlockSize * 3 + 11, peak)) return 1;

        if (peak < 1e-6f) {
            std::cerr << presetName << ": produced only silence after MIDI note-on\n";
            return 1;
        }
        std::cout << presetName << ": ok (" << processor.getNumControls()
                  << " controls, peak " << peak << ")\n";
    } catch (const std::exception& error) {
        std::cerr << presetName << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
