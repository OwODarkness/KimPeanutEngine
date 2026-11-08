#include "signal/dft.h"

#include <cmath>
#include <numbers>
#include <new>
#include <stdexcept>

namespace kpengine::signal
{
    namespace
    {
        std::optional<std::vector<std::complex<double>>> Transform(
            const std::span<const std::complex<double>> input,
            const bool inverse,
            std::string &diagnostic)
        {
            diagnostic.clear();
            if (input.empty())
            {
                diagnostic = "DFT input must contain at least one value";
                return std::nullopt;
            }
            if (input.size() > kMaximumDirectDftSize)
            {
                diagnostic = "direct DFT input exceeds the supported size limit";
                return std::nullopt;
            }

            for (const std::complex<double> value : input)
            {
                if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
                {
                    diagnostic = "DFT input values must be finite";
                    return std::nullopt;
                }
            }

            try
            {
                std::vector<std::complex<double>> output(input.size());
                const double direction = inverse ? 1.0 : -1.0;
                const double count = static_cast<double>(input.size());
                const double normalization = inverse ? 1.0 / count : 1.0;

                for (std::size_t bin = 0; bin < input.size(); ++bin)
                {
                    std::complex<double> sum{};
                    for (std::size_t sample = 0; sample < input.size(); ++sample)
                    {
                        const double angle = direction * 2.0 * std::numbers::pi_v<double> *
                            static_cast<double>(bin) * static_cast<double>(sample) / count;
                        const std::complex<double> twiddle{std::cos(angle), std::sin(angle)};
                        sum += input[sample] * twiddle;
                    }

                    sum *= normalization;
                    if (!std::isfinite(sum.real()) || !std::isfinite(sum.imag()))
                    {
                        diagnostic = "DFT accumulation overflowed";
                        return std::nullopt;
                    }
                    output[bin] = sum;
                }
                return output;
            }
            catch (const std::bad_alloc &)
            {
                diagnostic = "DFT output allocation failed";
            }
            catch (const std::length_error &)
            {
                diagnostic = "DFT output exceeds the container limit";
            }
            return std::nullopt;
        }
    }

    std::optional<std::vector<std::complex<double>>> ForwardDft(
        const std::span<const std::complex<double>> samples,
        std::string &diagnostic)
    {
        return Transform(samples, false, diagnostic);
    }

    std::optional<std::vector<std::complex<double>>> InverseDft(
        const std::span<const std::complex<double>> bins,
        std::string &diagnostic)
    {
        return Transform(bins, true, diagnostic);
    }
}
