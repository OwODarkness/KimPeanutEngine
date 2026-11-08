#include "signal/signal_statistics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kpengine::signal
{
    std::optional<SignalStatistics> Measure(const std::span<const double> samples,
                                            std::string &diagnostic)
    {
        diagnostic.clear();
        if (samples.empty())
        {
            diagnostic = "cannot measure an empty sample range";
            return std::nullopt;
        }

        double sum = 0.0;
        double sum_squares = 0.0;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (const double sample : samples)
        {
            if (!std::isfinite(sample))
            {
                diagnostic = "sample measurement input must be finite";
                return std::nullopt;
            }
            sum += sample;
            sum_squares += sample * sample;
            minimum = std::min(minimum, sample);
            maximum = std::max(maximum, sample);
            if (!std::isfinite(sum) || !std::isfinite(sum_squares))
            {
                diagnostic = "sample measurement accumulation overflowed";
                return std::nullopt;
            }
        }

        SignalStatistics result;
        result.sample_count = samples.size();
        result.mean = sum / static_cast<double>(samples.size());
        result.rms = std::sqrt(sum_squares / static_cast<double>(samples.size()));
        result.minimum = minimum;
        result.maximum = maximum;
        result.peak_to_peak = maximum - minimum;
        if (!std::isfinite(result.peak_to_peak))
        {
            diagnostic = "sample peak-to-peak measurement overflowed";
            return std::nullopt;
        }
        return result;
    }
}
