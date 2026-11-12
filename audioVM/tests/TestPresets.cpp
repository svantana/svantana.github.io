#include "../cpp/AudioVM.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kChannels = 2;
constexpr int kBlockSize = 32;
constexpr int kMinSampleRate = 10000;
constexpr int kMaxSampleRate = 90000;
constexpr int kRenderSeconds = 30;
constexpr int kNotesPerSecond = 10;
constexpr float kInputFrequency = 440.f;
constexpr float kInputAmplitude = 0.25f;
constexpr float kPi = 3.14159265358979323846f;

bool processAndCheck(AudioVM::Processor& processor, int frames, int& inputSample, float sampleRate,
                     std::vector<std::vector<float>>& input, std::vector<std::vector<float>>& output,
                     float& peak,
                     std::chrono::duration<double>& processingTime) {
    float* inputPointers[kChannels];
    float* outputPointers[kChannels];
    for (int channel = 0; channel < kChannels; ++channel) {
        for (int sample = 0; sample < frames; ++sample) {
            const float phase = 2.f * kPi * kInputFrequency
                * static_cast<float>(inputSample + sample) / sampleRate;
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

uint32_t seedFor(const std::string& text) {
    uint32_t seed = 2166136261u;
    for (const unsigned char character : text) {
        seed ^= character;
        seed *= 16777619u;
    }
    return seed;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string presetName = argc >= 2 ? argv[1] : "<stdin>";
    const uint32_t runSeed = argc == 3 ? static_cast<uint32_t>(std::stoul(argv[2])) : 0u;
    const std::string json(std::istreambuf_iterator<char>(std::cin), {});
    if (json.empty()) {
        std::cerr << presetName << ": no program JSON received\n";
        return 1;
    }

    try {
        // The script provides a wall-clock seed. Mix in the preset JSON so
        // every preset has a distinct, reproducible fuzz stream for that run.
        std::mt19937 random(seedFor(json) ^ runSeed);
        const int sampleRate = std::uniform_int_distribution<int>(kMinSampleRate, kMaxSampleRate)(random);
        AudioVM::Processor processor;
        processor.setup(static_cast<float>(sampleRate), kChannels, kChannels, kBlockSize);
        processor.loadFromJSON(json);

        const int numControls = processor.getNumControls();
        for (int index = 0; index < numControls; ++index) {
            const float value = processor.getControlValue(index);
            if (!std::isfinite(value)) {
                std::cerr << presetName << ": control " << index << " is not finite\n";
                return 1;
            }
        }

        float peak = 0.f;
        int inputSample = 0;
        std::chrono::duration<double> processingTime{};
        std::vector<std::vector<float>> input(kChannels, std::vector<float>(kBlockSize));
        std::vector<std::vector<float>> output(kChannels, std::vector<float>(kBlockSize));
        std::uniform_real_distribution<float> controlValue(0.f, 1.f);
        std::uniform_int_distribution<int> note(0, 127);
        std::uniform_int_distribution<int> velocity(1, 127);
        std::uniform_int_distribution<int> durationSamples(sampleRate / 20, sampleRate / 4);
        std::vector<std::pair<int, int>> noteOffs;
        const int totalFrames = sampleRate * kRenderSeconds;
        const int noteInterval = sampleRate / kNotesPerSecond;
        int nextNoteFrame = 0;

        // Drive 10 random note-ons per second, vary every control before each
        // audio block, and include a partial final block when the selected
        // sample rate does not divide the render length by the block size.
        while (inputSample < totalFrames) {
            for (int control = 0; control < numControls; ++control) {
                processor.setControlValue(control, controlValue(random));
            }
            for (auto it = noteOffs.begin(); it != noteOffs.end();) {
                if (it->first > inputSample) {
                    ++it;
                    continue;
                }
                processor.midiEvent(static_cast<char>(0x80), static_cast<char>(it->second), 0);
                it = noteOffs.erase(it);
            }
            while (nextNoteFrame <= inputSample) {
                const int newNote = note(random);
                processor.midiEvent(static_cast<char>(0x90), static_cast<char>(newNote),
                                    static_cast<char>(velocity(random)));
                noteOffs.emplace_back(nextNoteFrame + durationSamples(random), newNote);
                nextNoteFrame += noteInterval;
            }
            const int frames = std::min(kBlockSize, totalFrames - inputSample);
            if (!processAndCheck(processor, frames, inputSample, static_cast<float>(sampleRate),
                                 input, output, peak, processingTime)) return 1;
        }

        if (peak < 1e-6f) {
            std::cerr << presetName << ": produced only silence during randomized MIDI sequence\n";
            return 1;
        }
        const double renderedSeconds = static_cast<double>(inputSample) / sampleRate;
        const double realtimeFactor = processingTime.count() / renderedSeconds;
        std::cout << numControls << '\t' << peak << '\t' << (100*realtimeFactor) << '\n';
    } catch (const std::exception& error) {
        std::cerr << presetName << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
