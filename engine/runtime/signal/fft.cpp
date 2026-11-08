#include "signal/fft.h"

#include <cmath>
#include <numbers>
#include <new>
#include <stdexcept>
#include <utility>

namespace kpengine::signal
{
    namespace
    {
        std::optional<std::vector<std::complex<double>>> TransformRadix2(
            const std::span<const std::complex<double>> input,
            const bool inverse,
            std::string &diagnostic)
        {
            diagnostic.clear();
            if (input.empty())
            {
                diagnostic = "FFT input must contain at least one value";
                return std::nullopt;
            }
            if (input.size() > kMaximumRadix2FftSize)
            {
                diagnostic = "FFT input exceeds the supported size limit";
                return std::nullopt;
            }
            if ((input.size() & (input.size() - 1)) != 0)
            {
                diagnostic = "radix-2 FFT input size must be a power of two";
                return std::nullopt;
            }
            for (const std::complex<double> value : input)
            {
                if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
                {
                    diagnostic = "FFT input values must be finite";
                    return std::nullopt;
                }
            }

            try
            {
                std::vector<std::complex<double>> output(input.begin(), input.end());
                const std::size_t count = output.size();

                std::size_t reversed = 0;
                for (std::size_t index = 1; index < count; ++index)
                {
                    std::size_t bit = count >> 1;
                    while ((reversed & bit) != 0)
                    {
                        reversed ^= bit;
                        bit >>= 1;
                    }
                    reversed ^= bit;
                    if (index < reversed)
                    {
                        std::swap(output[index], output[reversed]);
                    }
                }

                const double direction = inverse ? 1.0 : -1.0;
                for (std::size_t stage_size = 2; stage_size <= count; stage_size *= 2)
                {
                    const std::size_t half_size = stage_size / 2;
                    const double angle = direction * 2.0 * std::numbers::pi_v<double> /
                        static_cast<double>(stage_size);
                    const std::complex<double> twiddle_step{std::cos(angle), std::sin(angle)};

                    std::complex<double> twiddle{1.0, 0.0};
                    for (std::size_t offset = 0; offset < half_size; ++offset)
                    {
                        for (std::size_t base = 0; base < count; base += stage_size)
                        {
                            const std::complex<double> even = output[base + offset];
                            const std::complex<double> odd =
                                output[base + offset + half_size] * twiddle;
                            output[base + offset] = even + odd;
                            output[base + offset + half_size] = even - odd;
                        }
                        twiddle *= twiddle_step;
                    }
                    if (stage_size == count)
                    {
                        break;
                    }
                }

                const double normalization = inverse ? 1.0 / static_cast<double>(count) : 1.0;
                for (std::complex<double> &value : output)
                {
                    value *= normalization;
                    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
                    {
                        diagnostic = "FFT accumulation overflowed";
                        return std::nullopt;
                    }
                }
                return output;
            }
            catch (const std::bad_alloc &)
            {
                diagnostic = "FFT output allocation failed";
            }
            catch (const std::length_error &)
            {
                diagnostic = "FFT output exceeds the container limit";
            }
            return std::nullopt;
        }
    }

    std::optional<std::vector<std::complex<double>>> ForwardFft(
        const std::span<const std::complex<double>> samples,
        std::string &diagnostic)
    {
        return TransformRadix2(samples, false, diagnostic);
    }

    std::optional<std::vector<std::complex<double>>> InverseFft(
        const std::span<const std::complex<double>> bins,
        std::string &diagnostic)
    {
        return TransformRadix2(bins, true, diagnostic);
    }
}
