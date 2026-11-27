#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "../cpp/ops/Filters.h"

template <typename Filter>
bool checkGain(const char* name) {
    constexpr int blockSize = 32;
    constexpr float sampleRate = 44100.f;
    constexpr float frequency = 100.f;
    constexpr double pi = 3.14159265358979323846;
    for (float gain : {0.1f, 0.5f, 1.f, 2.f, 5.f}) {
        Filter filter;
        filter.mSamplerate = sampleRate;
        filter.mBlockSize = blockSize;
        float input[blockSize], output[blockSize], reset = 0, compensate = 0;
        float hz = frequency;
        AudioVM::Buffer in[] = {input, &hz, &gain, &reset, &compensate};
        in[FilterLP::NumInputs].connected = true;
        AudioVM::Buffer out[] = {output};
        double inputEnergy = 0, outputEnergy = 0;
        for (int block = 0; block < 6000; ++block) {
            for (int n = 0; n < blockSize; ++n)
                input[n] = std::sin(2 * pi * frequency * (block * blockSize + n) / sampleRate);
            filter.process(in, out, blockSize);
            if (block >= 4500) {
                for (int n = 0; n < blockSize; ++n) {
                    inputEnergy += input[n] * input[n];
                    outputEnergy += output[n] * output[n];
                }
            }
        }
        double measured = std::sqrt(outputEnergy / inputEnergy);
        if (std::abs(measured / gain - 1) > 0.03) {
            std::fprintf(stderr, "%s gain %.3f: measured %.6f\n", name, gain, measured);
            return false;
        }
    }
    return true;
}

bool checkCompensation() {
    constexpr int blockSize = 32;
    constexpr float sampleRate = 44100.f;
    constexpr double pi = 3.14159265358979323846;
    for (float frequency : {100.f, 5000.f, 10000.f}) {
        for (float q : {0.1f, 0.5f, 1.f, 2.f}) {
            FilterBP filter;
            filter.mSamplerate = sampleRate;
            filter.mBlockSize = blockSize;
            float input[blockSize], output[blockSize], reset = 0, compensate = 0;
            AudioVM::Buffer in[] = {input, &frequency, &q, &reset, &compensate};
            AudioVM::Buffer out[] = {output};
            double measured[4]{};
            for (int phase = 0; phase < 4; ++phase) {
                // Exercise connected values and the disconnected default.
                compensate = phase == 1 ? 0.1f : (phase == 2 ? -1.f : 0.f);
                in[FilterBP::Compensate].connected = phase != 3;
                double inputEnergy = 0, outputEnergy = 0;
                for (int block = 0; block < 2000; ++block) {
                    for (int n = 0; n < blockSize; ++n)
                        input[n] = std::sin(2 * pi * frequency *
                                            (block * blockSize + n) / sampleRate);
                    filter.process(in, out, blockSize);
                    if (block >= 1500) {
                        for (int n = 0; n < blockSize; ++n) {
                            inputEnergy += input[n] * input[n];
                            outputEnergy += output[n] * output[n];
                        }
                    }
                }
                measured[phase] = std::sqrt(outputEnergy / inputEnergy);
            }
            double expected = q < 1.f ? measured[0] / q : measured[0];
            if (std::abs(measured[1] - expected) > 0.005 ||
                std::abs(measured[2] - measured[0]) > 0.005 ||
                std::abs(measured[3] - measured[1]) > 0.005) {
                std::fprintf(stderr, "BP compensation at %.0f Hz, Q %.2f: %.6f %.6f %.6f %.6f\n",
                             frequency, q, measured[0], measured[1], measured[2], measured[3]);
                return false;
            }
        }
    }
    return true;
}

bool checkSweepTail() {
    constexpr int blockSize = 32;
    FilterBP compensated, reference;
    compensated.mSamplerate = reference.mSamplerate = 44100.f;
    compensated.mBlockSize = reference.mBlockSize = blockSize;
    float input[blockSize]{}, outputA[blockSize], outputB[blockSize];
    float frequency = 1000.f, q = 100.f, reset = 0.f, enabled = 1.f, disabled = 0.f;
    AudioVM::Buffer inA[] = {input, &frequency, &q, &reset, &enabled};
    AudioVM::Buffer inB[] = {input, &frequency, &q, &reset, &disabled};
    inA[FilterBP::Compensate].connected = true;
    inB[FilterBP::Compensate].connected = true;
    AudioVM::Buffer outA[] = {outputA}, outB[] = {outputB};
    for (int block = 0; block < 500; ++block) {
        for (int n = 0; n < blockSize; ++n)
            input[n] = std::sin(2 * 3.14159265358979323846 * frequency *
                                (block * blockSize + n) / 44100.0);
        compensated.process(inA, outA, blockSize);
        reference.process(inB, outB, blockSize);
    }
    std::fill_n(input, blockSize, 0.f);
    q = 0.001f;
    for (int block = 0; block < 32; ++block) {
        compensated.process(inA, outA, blockSize);
        reference.process(inB, outB, blockSize);
        for (int n = 0; n < blockSize; ++n) {
            if (std::abs(outputA[n] - outputB[n]) > 1e-6f) {
                std::fprintf(stderr, "BP compensation altered the existing tail during a Q sweep\n");
                return false;
            }
        }
    }
    return true;
}

int main() {
    return checkGain<FilterLP>("filterLP") && checkGain<FilterBP>("filterBP") &&
           checkGain<FilterHP>("filterHP") && checkCompensation() && checkSweepTail() ? 0 : 1;
}
