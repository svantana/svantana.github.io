#include "GeneratedGain.h"

#include <cmath>
#include <iostream>

int main() {
    GeneratedGain patch;
    float source[] = {1.f, -2.f, 3.5f, 0.f};
    float gain = 0.25f;
    float result[4]{};
    AudioVM::Buffer inputs[] = {
        AudioVM::Buffer(source, 4),
        AudioVM::Buffer(&gain, 1),
    };
    AudioVM::Buffer outputs[] = {AudioVM::Buffer(result, 4)};
    patch.process(inputs, outputs, 4);

    // The graph's block-rate gain is promoted through toAudio, so the first
    // block ramps from the initial zero state to the supplied gain.
    constexpr float expected[] = {0.0625f, -0.25f, 0.65625f, 0.f};
    for (int index = 0; index < 4; ++index) {
        if (std::fabs(result[index] - expected[index]) > 1e-6f) {
            std::cerr << "generated operator result mismatch at " << index << '\n';
            return 1;
        }
    }
    return 0;
}
