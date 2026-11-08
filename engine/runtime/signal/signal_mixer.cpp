#include "signal/signal_mixer.h"

#include <cmath>
#include <vector>

namespace kpengine::signal
{
    std::optional<SampleBlock> MixSignals(const SampleBlockView &first,
                                          const SampleBlockView &second,
                                          std::string &diagnostic)
    {
        diagnostic.clear();
        if (first.Channels().size() != 1 || second.Channels().size() != 1)
        {
            diagnostic = "signal mixing requires one channel per input";
            return std::nullopt;
        }
        if (first.SampleRateHz() != second.SampleRateHz() ||
            first.FirstSampleIndex() != second.FirstSampleIndex() ||
            first.Epoch() != second.Epoch() ||
            first.SampleCount() != second.SampleCount())
        {
            diagnostic = "signal inputs must share the same sample clock and extent";
            return std::nullopt;
        }
        if (first.Channels()[0].unit != second.Channels()[0].unit)
        {
            diagnostic = "signal inputs must use the same sample unit";
            return std::nullopt;
        }

        auto mixed = SampleBlock::Create(first.SampleRateHz(), first.FirstSampleIndex(),
            first.Epoch(), first.HasDiscontinuity() || second.HasDiscontinuity(),
            {{0, first.Channels()[0].unit}}, first.SampleCount(), diagnostic);
        if (!mixed.has_value())
        {
            return std::nullopt;
        }

        const std::span<const double> first_samples = first.ChannelSamples(0);
        const std::span<const double> second_samples = second.ChannelSamples(0);
        std::span<double> mixed_samples = mixed->MutableChannelSamples(0);
        for (std::size_t sample = 0; sample < mixed_samples.size(); ++sample)
        {
            mixed_samples[sample] = first_samples[sample] + second_samples[sample];
            if (!std::isfinite(mixed_samples[sample]))
            {
                diagnostic = "signal mixing produced a non-finite sample";
                return std::nullopt;
            }
        }
        return mixed;
    }
}
