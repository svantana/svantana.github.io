#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__) && !defined(NO_VDSP) && __has_include(<Accelerate/Accelerate.h>)
#include <Accelerate/Accelerate.h>
#include <memory>
#include <type_traits>
#endif

// Radix-2 FFT for power-of-two sizes.  Complex values are always stored as
// consecutive real/imaginary floats: { re0, im0, re1, im1, ... }.
//
// rfft()/irfft() use bins 0..N/2: N/2+1 interleaved complex values,
// requiring N+2 floats for N >= 2 (two floats for N == 1).
// The imaginary DC and Nyquist lanes are zero. fft()/ifft() use 2*N floats.
#if defined(__APPLE__) && !defined(NO_VDSP) && __has_include(<Accelerate/Accelerate.h>)
// Apple builds default to vDSP and must link Accelerate. Define NO_VDSP to
// select the portable implementation without the framework dependency.
class FFT {
public:
    explicit FFT(std::size_t size) : size_(size), real_(size), imag_(size) {
        if (size == 0 || (size & (size - 1)) != 0)
            throw std::invalid_argument("FFT size must be a non-zero power of two");
        for (std::size_t n = size; n > 1; n >>= 1) ++logSize_;
        if (size >= 4) {
            setup_.reset(vDSP_create_fftsetup(logSize_, kFFTRadix2), vDSP_destroy_fftsetup);
            if (!setup_) throw std::runtime_error("vDSP FFT setup failed");
        }
    }

    std::size_t size() const noexcept { return size_; }

    // Input and output do not overlap. Real transforms use instance workspace;
    // concurrent transforms require separate FFT instances.
    void rfft(const float* real, float* spectrum) const {
        if (size_ == 1) { spectrum[0] = real[0]; spectrum[1] = 0; return; }
        if (size_ == 2) {
            spectrum[0] = real[0] + real[1]; spectrum[1] = 0;
            spectrum[2] = real[0] - real[1]; spectrum[3] = 0;
            return;
        }
        const std::size_t half = size_ / 2;
        for (std::size_t i = 0; i < half; ++i) {
            real_[i] = real[2 * i]; imag_[i] = real[2 * i + 1];
        }
        DSPSplitComplex split{real_.data(), imag_.data()};
        vDSP_fft_zrip(setup_.get(), &split, 1, logSize_, FFT_FORWARD);
        spectrum[0] = real_[0] * 0.5f; spectrum[1] = 0;
        spectrum[2 * half] = imag_[0] * 0.5f; spectrum[2 * half + 1] = 0;
        for (std::size_t k = 1; k < half; ++k) {
            const float re = real_[k] * 0.5f, im = imag_[k] * 0.5f;
            spectrum[2 * k] = re; spectrum[2 * k + 1] = im;
        }
    }

    // Reads the nonnegative half of a Hermitian spectrum; output is normalized.
    void irfft(const float* spectrum, float* real) const {
        if (size_ == 1) { real[0] = spectrum[0]; return; }
        if (size_ == 2) {
            const float dc = spectrum[0], nyquist = spectrum[2];
            real[0] = (dc + nyquist) * 0.5f;
            real[1] = (dc - nyquist) * 0.5f;
            return;
        }
        const std::size_t half = size_ / 2;
        real_[0] = spectrum[0]; imag_[0] = spectrum[2 * half];
        for (std::size_t k = 1; k < half; ++k) {
            real_[k] = spectrum[2 * k]; imag_[k] = spectrum[2 * k + 1];
        }
        DSPSplitComplex split{real_.data(), imag_.data()};
        vDSP_fft_zrip(setup_.get(), &split, 1, logSize_, FFT_INVERSE);
        const float scale = 1.0f / static_cast<float>(size_);
        for (std::size_t i = 0; i < half; ++i) {
            real[2 * i] = real_[i] * scale; real[2 * i + 1] = imag_[i] * scale;
        }
    }

    void fft(float* interleaved) const { complexTransform(interleaved, false); }
    void ifft(float* interleaved) const { complexTransform(interleaved, true); }

private:
    void complexTransform(float* data, bool inverse) const {
        if (size_ == 1) return;
        if (size_ == 2) {
            const float ar = data[0], ai = data[1], br = data[2], bi = data[3];
            const float scale = inverse ? 0.5f : 1.0f;
            data[0] = (ar + br) * scale; data[1] = (ai + bi) * scale;
            data[2] = (ar - br) * scale; data[3] = (ai - bi) * scale;
            return;
        }
        for (std::size_t i = 0; i < size_; ++i) {
            real_[i] = data[2 * i]; imag_[i] = data[2 * i + 1];
        }
        DSPSplitComplex split{real_.data(), imag_.data()};
        vDSP_fft_zip(setup_.get(), &split, 1, logSize_, inverse ? FFT_INVERSE : FFT_FORWARD);
        const float scale = inverse ? 1.0f / static_cast<float>(size_) : 1.0f;
        for (std::size_t i = 0; i < size_; ++i) {
            data[2 * i] = real_[i] * scale; data[2 * i + 1] = imag_[i] * scale;
        }
    }

    std::size_t size_;
    vDSP_Length logSize_ = 0;
    std::shared_ptr<typename std::remove_pointer<FFTSetup>::type> setup_;
    mutable std::vector<float> real_, imag_;
};
#else
class FFT {
public:
    explicit FFT(std::size_t size) : size_(size), bitReverse_(size), work_(size) {
        if (size_ == 0 || (size_ & (size_ - 1)) != 0)
            throw std::invalid_argument("FFT size must be a non-zero power of two");

        makeBitReverse();
        makeTwiddles();
    }

    std::size_t size() const noexcept { return size_; }

    // Forward real transform: real has N floats; spectrum has N/2+1 complex bins.
    // Input and output must not overlap. Uses the internal workspace, so calls
    // on the same instance must not run concurrently.
    void rfft(const float* real, float* spectrum) const {
        if (size_ == 1) {
            spectrum[0] = real[0];
            spectrum[1] = 0.0f;
            return;
        }
        const std::size_t half = size_ / 2;
        for (std::size_t i = 0; i < half; ++i) {
            const std::size_t source = bitReverse_[i] >> 1;
            work_[2 * i] = real[2 * source];
            work_[2 * i + 1] = real[2 * source + 1];
        }
        transform<false>(work_.data(), half, true);
        const float* twiddle = twiddles_.data() + 2 * (half - 1);
        spectrum[0] = work_[0] + work_[1];
        spectrum[1] = 0.0f;
        for (std::size_t k = 1; k < half; ++k) {
            const std::size_t mirror = half - k;
            const float ar = work_[2 * k], ai = work_[2 * k + 1];
            const float br = work_[2 * mirror], bi = -work_[2 * mirror + 1];
            const float dr = ar - br, di = ai - bi;
            const float wr = twiddle[2 * k], wi = twiddle[2 * k + 1];
            const float re = 0.5f * (ar + br + wr * di + wi * dr);
            const float im = 0.5f * (ai + bi - wr * dr + wi * di);
            spectrum[2 * k] = re;
            spectrum[2 * k + 1] = im;
        }
        spectrum[2 * half] = work_[0] - work_[1];
        spectrum[2 * half + 1] = 0.0f;
    }

    // Inverse real transform: spectrum has N/2+1 complex bins; real has N floats.
    // The forward/inverse pair is normalized, so irfft(rfft(x)) == x.  A
    // preallocated internal work buffer avoids allocations on the audio path;
    // use separate FFT instances for concurrent real transforms.
    void irfft(const float* spectrum, float* real) const {
        if (size_ == 1) { real[0] = spectrum[0]; return; }
        const std::size_t half = size_ / 2;
        const float* twiddle = twiddles_.data() + 2 * (half - 1);
        for (std::size_t k = 0; k < half; ++k) {
            const float ar = spectrum[2 * k], ai = spectrum[2 * k + 1];
            const float br = spectrum[2 * (half - k)], bi = -spectrum[2 * (half - k) + 1];
            const float dr = ar - br, di = ai - bi;
            const float wr = twiddle[2 * k], wi = twiddle[2 * k + 1];
            work_[2 * k] = 0.5f * (ar + br - wr * di + wi * dr);
            work_[2 * k + 1] = 0.5f * (ai + bi + wr * dr + wi * di);
        }
        transform<true>(work_.data(), half);
        for (std::size_t i = 0; i < size_; ++i) real[i] = work_[i];
    }

    // In-place complex transforms for callers that already use the interleaved
    // format.  ifft() leaves zero imaginary lanes for an inverse real spectrum.
    void fft(float* interleaved) const { transform<false>(interleaved, size_); }
    void ifft(float* interleaved) const { transform<true>(interleaved, size_); }

private:
    void makeBitReverse() {
        std::size_t reversed = 0;
        bitReverse_[0] = 0;
        for (std::size_t i = 1; i < size_; ++i) {
            std::size_t bit = size_ >> 1;
            while (reversed & bit) {
                reversed ^= bit;
                bit >>= 1;
            }
            reversed ^= bit;
            bitReverse_[i] = reversed;
        }
    }

    void makeTwiddles() {
        // A separate, contiguous table per stage keeps butterfly loads simple
        // and gives optimizers a conventional inner loop to vectorize.
        constexpr float twoPi = 6.28318530717958647692f;
        if (size_ == 1) return;
        for (std::size_t width = 2;; width <<= 1) {
            const std::size_t half = width >> 1;
            for (std::size_t j = 0; j < half; ++j) {
                const float phase = -twoPi * static_cast<float>(j) / static_cast<float>(width);
                twiddles_.push_back(std::cos(phase));
                twiddles_.push_back(std::sin(phase));
            }
            if (width == size_) break;
        }
        for (std::size_t width = 4; width <= size_; width *= 4) {
            for (std::size_t j = 0; j < width / 4; ++j) {
                const float phase = -3 * twoPi * static_cast<float>(j) / static_cast<float>(width);
                thirdTwiddles_.push_back(std::cos(phase));
                thirdTwiddles_.push_back(std::sin(phase));
            }
            if (width == size_) break;
        }
    }

    template<bool Inverse>
    void transform(float* data, std::size_t n, bool reordered = false) const {
        constexpr float imaginarySign = Inverse ? -1.0f : 1.0f;
        for (std::size_t i = 0; !reordered && i < n; ++i) {
            const std::size_t j = bitReverse_[i] >> (n != size_);
            if (i < j) {
                const float real = data[2 * i];
                const float imag = data[2 * i + 1];
                data[2 * i] = data[2 * j];
                data[2 * i + 1] = data[2 * j + 1];
                data[2 * j] = real;
                data[2 * j + 1] = imag;
            }
        }

        if (n == 1) return;

        // Fuse two radix-2 stages: intermediates stay in registers and each
        // complex value is loaded/stored only once per pair of stages.
        std::size_t completed = 1;
        if (n >= 4) {
            // Unit twiddles: vectorize across independent four-point groups.
            for (std::size_t i = 0; i < 2 * n; i += 8) {
                const float ar = data[i], ai = data[i + 1];
                const float br = data[i + 2], bi = data[i + 3];
                const float cr = data[i + 4], ci = data[i + 5];
                const float dr = data[i + 6], di = data[i + 7];
                const float er = cr + dr, ei = ci + di;
                const float fr = imaginarySign * (ci - di), fi = -imaginarySign * (cr - dr);
                data[i] = ar + br + er; data[i + 1] = ai + bi + ei;
                data[i + 4] = ar + br - er; data[i + 5] = ai + bi - ei;
                data[i + 2] = ar - br + fr; data[i + 3] = ai - bi + fi;
                data[i + 6] = ar - br - fr; data[i + 7] = ai - bi - fi;
            }
            completed = 4;
        }
        for (std::size_t width = 16; width <= n; width *= 4) {
            const std::size_t quarter = width / 4;
            if (quarter == 4) radix4<4, Inverse>(data, n, quarter);
            else if (quarter == 16) radix4<16, Inverse>(data, n, quarter);
            else radix4<0, Inverse>(data, n, quarter);
            completed = width;
            if (width == n) break;
        }

        // An odd number of binary stages leaves one radix-2 stage.
        for (std::size_t width = completed * 2; width <= n; width <<= 1) {
            const std::size_t half = width >> 1;
            const float* const twiddle = twiddles_.data() + 2 * (half - 1);
            for (std::size_t start = 0; start < n; start += width) {
                float* const lower = data + 2 * start;
                float* const upper = lower + 2 * half;
                for (std::size_t j = 0; j < half; ++j) {
                    const float wr = twiddle[2 * j];
                    const float wi = imaginarySign * twiddle[2 * j + 1];
                    const float ur = lower[2 * j];
                    const float ui = lower[2 * j + 1];
                    const float vr = upper[2 * j];
                    const float vi = upper[2 * j + 1];
                    const float tr = wr * vr - wi * vi;
                    const float ti = wr * vi + wi * vr;
                    lower[2 * j] = ur + tr;
                    lower[2 * j + 1] = ui + ti;
                    upper[2 * j] = ur - tr;
                    upper[2 * j + 1] = ui - ti;
                }
            }
            if (width == n) break;
        }

        if constexpr (Inverse) {
            const float scale = 1.0f / static_cast<float>(n);
            for (std::size_t i = 0; i < 2 * n; ++i) data[i] *= scale;
        }
    }

    // Fixed small stages expose disjoint offsets to the optimizer. The same
    // body handles larger stages without generating a kernel for every size.
    template<std::size_t Quarter, bool Inverse>
    void radix4(float* data, std::size_t n, std::size_t quarter) const {
        constexpr float imaginarySign = Inverse ? -1.0f : 1.0f;
        if constexpr (Quarter != 0) quarter = Quarter;
        const std::size_t width = 4 * quarter;
        const float* w1 = twiddles_.data() + 2 * (quarter - 1);
        const float* w2 = twiddles_.data() + 2 * (2 * quarter - 1);
        const float* w3 = thirdTwiddles_.data() + 2 * ((quarter - 1) / 3);
        for (std::size_t start = 0; start < n; start += width) {
            float* a = data + 2 * start;
            float* b = a + 2 * quarter;
            float* c = b + 2 * quarter;
            float* d = c + 2 * quarter;
            for (std::size_t j = 0; j < quarter; ++j) {
                const std::size_t k = 2 * j;
                const float wr = w1[k], wi = imaginarySign * w1[k + 1];
                const float vr = w2[k], vi = imaginarySign * w2[k + 1];
                const float zr = w3[k], zi = imaginarySign * w3[k + 1];
                const float br = wr * b[k] - wi * b[k + 1];
                const float bi = wr * b[k + 1] + wi * b[k];
                const float dr = zr * d[k] - zi * d[k + 1];
                const float di = zr * d[k + 1] + zi * d[k];
                const float ar = a[k], ai = a[k + 1];
                const float cr = vr * c[k] - vi * c[k + 1];
                const float ci = vr * c[k + 1] + vi * c[k];
                const float er = cr + dr, ei = ci + di;
                const float fr = imaginarySign * (ci - di);
                const float fi = -imaginarySign * (cr - dr);
                a[k] = ar + br + er; a[k + 1] = ai + bi + ei;
                c[k] = ar + br - er; c[k + 1] = ai + bi - ei;
                b[k] = ar - br + fr; b[k + 1] = ai - bi + fi;
                d[k] = ar - br - fr; d[k + 1] = ai - bi - fi;
            }
        }
    }

    std::size_t size_;
    std::vector<std::size_t> bitReverse_;
    std::vector<float> twiddles_;
    std::vector<float> thirdTwiddles_;
    mutable std::vector<float> work_;
};
#endif
