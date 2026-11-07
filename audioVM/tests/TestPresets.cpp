#include "../cpp/AudioVM.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kChannels = 2;
constexpr int kBlockSize = 32;
constexpr float kSampleRate = 44100.f;
constexpr float kInputFrequency = 440.f;
constexpr float kInputAmplitude = 0.25f;
constexpr float kPi = 3.14159265358979323846f;

bool processAndCheck(AudioVM::Processor& processor, int frames, int& inputSample, float& peak,
                     std::chrono::duration<double>& processingTime) {
    std::vector<std::vector<float>> input(kChannels, std::vector<float>(frames, 0.f));
    std::vector<std::vector<float>> output(kChannels, std::vector<float>(frames, 0.f));
    float* inputPointers[kChannels];
    float* outputPointers[kChannels];
    for (int channel = 0; channel < kChannels; ++channel) {
        for (int sample = 0; sample < frames; ++sample) {
            const float phase = 2.f * kPi * kInputFrequency
                * static_cast<float>(inputSample + sample) / kSampleRate;
            input[channel][sample] = kInputAmplitude * std::sin(phase);
        }
        inputPointers[channel] = input[channel].data();
        outputPointers[channel] = output[channel].data();
    }

    const auto start = std::chrono::steady_clock::now();
    processor.process(inputPointers, outputPointers, frames);
    processingTime += std::chrono::steady_clock::now() - start;
    inputSample += frames;
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
        int inputSample = 0;
        std::chrono::duration<double> processingTime{};
        processor.midiEvent(static_cast<char>(0x90), 60, 100);
        // Includes a partial final block to cover Processor::process splitting.
        for (const int frames : {kBlockSize, (int)kSampleRate + 17, kBlockSize * 4}) {
            if (!processAndCheck(processor, frames, inputSample, peak, processingTime)) return 1;
        }
        processor.midiEvent(static_cast<char>(0x80), 60, 0);
        if (!processAndCheck(processor, kBlockSize * 8 + 11, inputSample, peak, processingTime)) return 1;

        if (peak < 1e-6f) {
            std::cerr << presetName << ": produced only silence after MIDI note-on\n";
            return 1;
        }
        const double renderedSeconds = static_cast<double>(inputSample) / kSampleRate;
        const double realtimeFactor = processingTime.count() / renderedSeconds;
        std::cout << processor.getNumControls() << '\t' << peak << '\t' << (100*realtimeFactor) << '\n';
    } catch (const std::exception& error) {
        std::cerr << presetName << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
