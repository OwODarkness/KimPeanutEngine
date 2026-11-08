#ifndef KPENGINE_RUNTIME_SIGNAL_DFT_H
#define KPENGINE_RUNTIME_SIGNAL_DFT_H

#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kpengine::signal
{
    inline constexpr std::size_t kMaximumDirectDftSize = 1'024;

    // Scalar O(N^2) reference transform. Forward bins use the negative exponent.
    // The size bound keeps this reference path from becoming an accidental FFT.
    std::optional<std::vector<std::complex<double>>> ForwardDft(
        std::span<const std::complex<double>> samples,
        std::string &diagnostic);

    // Inverse bins use the positive exponent and include the 1/N normalization.
    std::optional<std::vector<std::complex<double>>> InverseDft(
        std::span<const std::complex<double>> bins,
        std::string &diagnostic);
}

#endif
