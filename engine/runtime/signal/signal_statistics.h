#ifndef KPENGINE_RUNTIME_SIGNAL_SIGNAL_STATISTICS_H
#define KPENGINE_RUNTIME_SIGNAL_SIGNAL_STATISTICS_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace kpengine::signal
{
    struct SignalStatistics
    {
        std::size_t sample_count = 0;
        double mean = 0.0;
        double rms = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        double peak_to_peak = 0.0;
    };

    std::optional<SignalStatistics> Measure(std::span<const double> samples,
                                            std::string &diagnostic);
}

#endif
