#include "../cpp/utils/FFT.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

void requireNear(float actual, float expected, float tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

std::vector<float> directDft(const std::vector<float>& input) {
    const std::size_t n = input.size();
    std::vector<float> result(2 * n);
    for (std::size_t bin = 0; bin < n; ++bin) {
        for (std::size_t sample = 0; sample < n; ++sample) {
            const float phase = -2.0f * kPi * static_cast<float>(bin * sample) /
                                static_cast<float>(n);
            result[2 * bin] += input[sample] * std::cos(phase);
            result[2 * bin + 1] += input[sample] * std::sin(phase);
        }
    }
    return result;
}

void testAgainstDft() {
    std::mt19937 random(42);
    std::uniform_real_distribution<float> value(-1.0f, 1.0f);
    for (const std::size_t size : {std::size_t{1}, std::size_t{2}, std::size_t{4},
                                   std::size_t{8}, std::size_t{32}}) {
        FFT fft(size);
        std::vector<float> input(size), spectrum(2 * (size / 2 + 1)), restored(size);
        for (float& sample : input) sample = value(random);

        fft.rfft(input.data(), spectrum.data());
        const std::vector<float> expected = directDft(input);
        for (std::size_t i = 0; i < spectrum.size(); ++i)
            requireNear(spectrum[i], expected[i], 1e-4f, "rfft differs from DFT");

        fft.irfft(spectrum.data(), restored.data());
        for (std::size_t i = 0; i < size; ++i)
            requireNear(restored[i], input[i], 2e-5f, "rfft/irfft round trip");

        fft.irfft(expected.data(), restored.data());
        for (std::size_t i = 0; i < size; ++i)
            requireNear(restored[i], input[i], 2e-5f, "inverse of reference DFT");
    }
}

void testLargeRoundTrip() {
    constexpr std::size_t size = 4096;
    FFT fft(size);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> value(-1.0f, 1.0f);
    std::vector<float> input(size), spectrum(size + 2), restored(size);
    for (float& sample : input) sample = value(random);
    fft.rfft(input.data(), spectrum.data());
    fft.irfft(spectrum.data(), restored.data());
    for (std::size_t i = 0; i < size; ++i)
        requireNear(restored[i], input[i], 4e-4f, "large rfft/irfft round trip");
}

void testComplex() {
    for (const std::size_t size : {std::size_t{1}, std::size_t{2}, std::size_t{4},
                                   std::size_t{8}, std::size_t{32}}) {
        FFT fft(size);
        std::vector<float> real(size), imag(size), data(2 * size);
        for (std::size_t i = 0; i < size; ++i) {
            real[i] = std::sin(static_cast<float>(i));
            imag[i] = std::cos(static_cast<float>(i) * 0.7f);
            data[2 * i] = real[i]; data[2 * i + 1] = imag[i];
        }
        const auto realDft = directDft(real), imagDft = directDft(imag);
        fft.fft(data.data());
        for (std::size_t i = 0; i < size; ++i) {
            requireNear(data[2 * i], realDft[2 * i] - imagDft[2 * i + 1], 2e-4f, "complex fft real");
            requireNear(data[2 * i + 1], realDft[2 * i + 1] + imagDft[2 * i], 2e-4f, "complex fft imag");
        }
        fft.ifft(data.data());
        for (std::size_t i = 0; i < size; ++i) {
            requireNear(data[2 * i], real[i], 2e-5f, "complex ifft real");
            requireNear(data[2 * i + 1], imag[i], 2e-5f, "complex ifft imag");
        }
    }
}

void benchmark() {
    using Clock = std::chrono::steady_clock;
    constexpr int setupIterations = 100;
    const auto seed = static_cast<std::uint32_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    std::cout << "Random seed: " << seed << '\n'
              << "FFT benchmark (setup includes destruction):\n"
              << std::setw(8) << "size" << std::setw(16) << "setup us"
              << std::setw(18) << "rfft"
              << std::setw(18) << "irfft" << std::setw(16) << "rel L2 error" << '\n';
    double setupTotal = 0, forwardTotal = 0, inverseTotal = 0, errorTotal = 0;
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> value(-1.0f, 1.0f);
    int sizeCount = 0;
    for (std::size_t size = 64; size <= 4096; size *= 2) {
        const int iterations = static_cast<int>((std::size_t{1} << 25) / size);
        const auto setupStart = Clock::now();
        for (int i = 0; i < setupIterations; ++i) {
            FFT setup(size);
        }
        const auto setupEnd = Clock::now();
        FFT fft(size);
        std::vector<float> input(size), spectrum(size + 2), restored(size);
        for (float& sample : input) sample = value(random);

        fft.rfft(input.data(), spectrum.data());
        fft.irfft(spectrum.data(), restored.data());
        const auto forwardStart = Clock::now();
        for (int i = 0; i < iterations; ++i) fft.rfft(input.data(), spectrum.data());
        const auto forwardEnd = Clock::now();
        for (int i = 0; i < iterations; ++i) fft.irfft(spectrum.data(), restored.data());
        const auto inverseEnd = Clock::now();
        // Relative L2 error: ||restored - input||_2 / ||input||_2.
        // Accumulate in double so the metric measures the float FFT's error.
        double errorSquared = 0, inputSquared = 0;
        for (std::size_t i = 0; i < size; ++i) {
            const double difference = static_cast<double>(restored[i]) - input[i];
            errorSquared += difference * difference;
            inputSquared += static_cast<double>(input[i]) * input[i];
        }
        const double relativeError = std::sqrt(errorSquared / inputSquared);
        volatile float sink = restored[0];
        (void)sink;
        const double setupSeconds = std::chrono::duration<double>(setupEnd - setupStart).count();
        const double forwardSeconds = std::chrono::duration<double>(forwardEnd - forwardStart).count();
        const double inverseSeconds = std::chrono::duration<double>(inverseEnd - forwardEnd).count();
        const double megaSamples = static_cast<double>(size) * iterations / 1e6;
        const double setupUs = setupSeconds * 1e6 / setupIterations;
        const double forwardRate = megaSamples / forwardSeconds;
        const double inverseRate = megaSamples / inverseSeconds;
        setupTotal += setupUs;
        forwardTotal += forwardRate;
        inverseTotal += inverseRate;
        errorTotal += relativeError;
        ++sizeCount;
        std::cout << std::fixed << std::setprecision(2)
                  << std::setw(8) << size << std::setw(16) << setupUs
                  << std::setw(18) << forwardRate
                  << std::setw(18) << inverseRate
                  << std::scientific << std::setw(16) << relativeError << '\n';
    }
    std::cout << std::fixed << std::setw(8) << "average" << std::setw(16) << setupTotal / sizeCount
              << std::setw(18) << forwardTotal / sizeCount
              << std::setw(18) << inverseTotal / sizeCount
              << std::scientific << std::setw(16) << errorTotal / sizeCount << '\n';
}

} // namespace

int main() {
    testAgainstDft();
    testLargeRoundTrip();
    testComplex();
    benchmark();
    std::cout << "FFT tests passed\n";
}
