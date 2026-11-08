#ifndef KPENGINE_RUNTIME_SIGNAL_FFT_H
#define KPENGINE_RUNTIME_SIGNAL_FFT_H

#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kpengine::signal
{
    inline constexpr std::size_t kMaximumRadix2FftSize = 1'048'576;

    // In-place radix-2 Cooley-Tukey transform on an owned copy of the input.
    std::optional<std::vector<std::complex<double>>> ForwardFft(
        std::span<const std::complex<double>> samples,
        std::string &diagnostic);

    // Uses the positive exponent and applies 1/N normalization.
    std::optional<std::vector<std::complex<double>>> InverseFft(
        std::span<const std::complex<double>> bins,
        std::string &diagnostic);
}

#endif
