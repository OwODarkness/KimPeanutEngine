#include "signal/spectrum.h"

#include "signal/fft.h"

#include <cmath>
#include <complex>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>

namespace kpengine::signal
{
    namespace
    {
        bool IsSupportedWindow(const WindowKind kind) noexcept
        {
            switch (kind)
            {
            case WindowKind::Rectangular:
            case WindowKind::PeriodicHann:
                return true;
            }
            return false;
        }
    }

    std::optional<std::vector<double>> BuildWindow(const WindowKind kind,
                                                   const std::size_t sample_count,
                                                   std::string &diagnostic)
    {
        diagnostic.clear();
        if (!IsSupportedWindow(kind))
        {
            diagnostic = "spectrum window kind is not supported";
            return std::nullopt;
        }
        if (sample_count == 0)
        {
            diagnostic = "spectrum window must contain at least one coefficient";
            return std::nullopt;
        }
        if (kind == WindowKind::PeriodicHann && sample_count < 2)
        {
            diagnostic = "periodic Hann window requires at least two samples";
            return std::nullopt;
        }

        try
        {
            std::vector<double> coefficients(sample_count, 1.0);
            if (kind == WindowKind::PeriodicHann)
            {
                const double count = static_cast<double>(sample_count);
                for (std::size_t index = 0; index < sample_count; ++index)
                {
                    const double phase = 2.0 * std::numbers::pi_v<double> *
                        static_cast<double>(index) / count;
                    coefficients[index] = 0.5 * (1.0 - std::cos(phase));
                }
            }
            return coefficients;
        }
        catch (const std::bad_alloc &)
        {
            diagnostic = "spectrum window allocation failed";
        }
        catch (const std::length_error &)
        {
            diagnostic = "spectrum window exceeds the container limit";
        }
        return std::nullopt;
    }

    std::optional<Spectrum> ComputeSpectrum(const std::span<const double> samples,
                                            const double sample_rate_hz,
                                            const WindowKind window,
                                            const std::size_t fft_size,
                                            std::string &diagnostic)
    {
        diagnostic.clear();
        if (samples.empty())
        {
            diagnostic = "spectrum input must contain at least one sample";
            return std::nullopt;
        }
        if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0)
        {
            diagnostic = "spectrum sample rate must be finite and greater than zero";
            return std::nullopt;
        }
        if (fft_size < samples.size())
        {
            diagnostic = "spectrum FFT size cannot be smaller than the observation";
            return std::nullopt;
        }
        if (fft_size == 0 || fft_size > kMaximumRadix2FftSize ||
            (fft_size & (fft_size - 1)) != 0)
        {
            diagnostic = "spectrum FFT size must be a supported power of two";
            return std::nullopt;
        }

        const auto coefficients = BuildWindow(window, samples.size(), diagnostic);
        if (!coefficients)
        {
            return std::nullopt;
        }

        double window_sum = 0.0;
        double window_energy = 0.0;
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            if (!std::isfinite(samples[index]))
            {
                diagnostic = "spectrum input samples must be finite";
                return std::nullopt;
            }
            window_sum += (*coefficients)[index];
            window_energy += (*coefficients)[index] * (*coefficients)[index];
        }
        const double psd_denominator = sample_rate_hz * window_energy;
        if (!std::isfinite(window_sum) || window_sum <= 0.0 ||
            !std::isfinite(window_energy) || window_energy <= 0.0 ||
            !std::isfinite(psd_denominator) || psd_denominator <= 0.0)
        {
            diagnostic = "spectrum window normalization is not finite and positive";
            return std::nullopt;
        }

        try
        {
            std::vector<std::complex<double>> windowed(fft_size);
            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                windowed[index] = {samples[index] * (*coefficients)[index], 0.0};
            }

            const auto transformed = ForwardFft(windowed, diagnostic);
            if (!transformed)
            {
                return std::nullopt;
            }

            Spectrum result;
            result.sample_rate_hz = sample_rate_hz;
            result.observed_sample_count = samples.size();
            result.fft_size = fft_size;
            result.window = window;
            result.coherent_gain = window_sum / static_cast<double>(samples.size());
            result.window_energy = window_energy;
            const std::size_t one_sided_count = fft_size / 2 + 1;
            result.bins.resize(one_sided_count);

            for (std::size_t bin = 0; bin < one_sided_count; ++bin)
            {
                const bool is_dc = bin == 0;
                const bool is_nyquist = fft_size > 1 && bin == fft_size / 2;
                const double sided_factor = is_dc || is_nyquist ? 1.0 : 2.0;
                const double magnitude = std::abs((*transformed)[bin]);
                const double power = std::norm((*transformed)[bin]);
                SpectrumBin &result_bin = result.bins[bin];
                result_bin.frequency_hz = sample_rate_hz *
                    (static_cast<double>(bin) / static_cast<double>(fft_size));
                result_bin.peak_amplitude = (magnitude / window_sum) * sided_factor;
                result_bin.power_spectral_density =
                    (power / psd_denominator) * sided_factor;
                if (!std::isfinite(result_bin.frequency_hz) ||
                    !std::isfinite(result_bin.peak_amplitude) ||
                    !std::isfinite(result_bin.power_spectral_density))
                {
                    diagnostic = "spectrum measurement overflowed";
                    return std::nullopt;
                }
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            diagnostic = "spectrum output allocation failed";
        }
        catch (const std::length_error &)
        {
            diagnostic = "spectrum output exceeds the container limit";
        }
        return std::nullopt;
    }
}
