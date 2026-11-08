#ifndef KPENGINE_RUNTIME_SIGNAL_SPECTRUM_H
#define KPENGINE_RUNTIME_SIGNAL_SPECTRUM_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kpengine::signal
{
    enum class WindowKind
    {
        Rectangular,
        PeriodicHann
    };

    struct SpectrumBin
    {
        double frequency_hz = 0.0;
        // One-sided, coherent-gain corrected peak amplitude for on-bin tones.
        double peak_amplitude = 0.0;
        // One-sided power density in the input unit squared per hertz.
        double power_spectral_density = 0.0;
    };

    struct Spectrum final
    {
        double sample_rate_hz = 0.0;
        std::size_t observed_sample_count = 0;
        std::size_t fft_size = 0;
        WindowKind window = WindowKind::Rectangular;
        double coherent_gain = 0.0;
        double window_energy = 0.0;
        std::vector<SpectrumBin> bins;
    };

    std::optional<std::vector<double>> BuildWindow(WindowKind kind,
                                                   std::size_t sample_count,
                                                   std::string &diagnostic);

    // Produces a real-input one-sided spectrum without detrending. fft_size
    // explicitly controls zero padding and must be a power of two no smaller
    // than the observation.
    std::optional<Spectrum> ComputeSpectrum(std::span<const double> samples,
                                            double sample_rate_hz,
                                            WindowKind window,
                                            std::size_t fft_size,
                                            std::string &diagnostic);
}

#endif
