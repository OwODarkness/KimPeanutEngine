#ifndef KPENGINE_RUNTIME_SIGNAL_SAMPLE_BLOCK_H
#define KPENGINE_RUNTIME_SIGNAL_SAMPLE_BLOCK_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kpengine::signal
{
    class SampleBlockView;

    enum class SampleUnit : std::uint8_t
    {
        Dimensionless,
        Volt,
        NormalizedFullScale,
    };

    struct ChannelDescriptor
    {
        std::uint32_t id = 0;
        SampleUnit unit = SampleUnit::Dimensionless;
    };

    class SampleBlock final
    {
    public:
        static constexpr std::size_t kMaximumChannelCount = 8;
        static constexpr std::size_t kMaximumSampleCount = 1'048'576;

        static std::optional<SampleBlock> Create(
            double sample_rate_hz,
            std::uint64_t first_sample_index,
            std::uint64_t epoch,
            bool discontinuity,
            std::vector<ChannelDescriptor> channels,
            std::size_t sample_count,
            std::string &diagnostic)
        {
            diagnostic.clear();
            if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0)
            {
                diagnostic = "sample rate must be finite and greater than zero";
                return std::nullopt;
            }
            if (channels.empty() || channels.size() > kMaximumChannelCount)
            {
                diagnostic = "sample block channel count must be between 1 and 8";
                return std::nullopt;
            }
            for (std::size_t i = 0; i < channels.size(); ++i)
            {
                for (std::size_t j = i + 1; j < channels.size(); ++j)
                {
                    if (channels[i].id == channels[j].id)
                    {
                        diagnostic = "sample block channel IDs must be unique";
                        return std::nullopt;
                    }
                }
            }
            if (sample_count > kMaximumSampleCount / channels.size())
            {
                diagnostic = "sample block exceeds the total sample limit";
                return std::nullopt;
            }
            if (sample_count > std::numeric_limits<std::uint64_t>::max() -
                                   first_sample_index)
            {
                diagnostic = "sample block index range overflows uint64";
                return std::nullopt;
            }

            try
            {
                return SampleBlock{sample_rate_hz,
                                   first_sample_index,
                                   epoch,
                                   discontinuity,
                                   std::move(channels),
                                   sample_count};
            }
            catch (const std::bad_alloc &)
            {
                diagnostic = "sample block allocation failed";
            }
            catch (const std::length_error &)
            {
                diagnostic = "sample block allocation exceeds the container limit";
            }
            return std::nullopt;
        }

        double SampleRateHz() const noexcept { return sample_rate_hz_; }
        std::uint64_t FirstSampleIndex() const noexcept { return first_sample_index_; }
        std::uint64_t Epoch() const noexcept { return epoch_; }
        bool HasDiscontinuity() const noexcept { return discontinuity_; }
        std::size_t SampleCount() const noexcept { return sample_count_; }
        std::span<const ChannelDescriptor> Channels() const noexcept { return channels_; }

        std::span<double> MutableChannelSamples(const std::size_t channel_index) noexcept
        {
            if (channel_index >= channels_.size() || sample_count_ == 0)
            {
                return {};
            }
            return std::span<double>{samples_}.subspan(channel_index * sample_count_,
                                                        sample_count_);
        }

        std::span<const double> ChannelSamples(const std::size_t channel_index) const noexcept
        {
            if (channel_index >= channels_.size() || sample_count_ == 0)
            {
                return {};
            }
            return std::span<const double>{samples_}.subspan(channel_index * sample_count_,
                                                              sample_count_);
        }

        SampleBlockView View() const noexcept;

    private:
        SampleBlock(const double sample_rate_hz,
                    const std::uint64_t first_sample_index,
                    const std::uint64_t epoch,
                    const bool discontinuity,
                    std::vector<ChannelDescriptor> channels,
                    const std::size_t sample_count)
            : sample_rate_hz_(sample_rate_hz),
              first_sample_index_(first_sample_index),
              epoch_(epoch),
              discontinuity_(discontinuity),
              channels_(std::move(channels)),
              sample_count_(sample_count),
              samples_(channels_.size() * sample_count_)
        {
        }

        double sample_rate_hz_;
        std::uint64_t first_sample_index_;
        std::uint64_t epoch_;
        bool discontinuity_;
        std::vector<ChannelDescriptor> channels_;
        std::size_t sample_count_;
        std::vector<double> samples_;
    };

    // A borrowed, read-only observation. The source block must remain alive
    // and unchanged for the lifetime of the view.
    class SampleBlockView final
    {
    public:
        double SampleRateHz() const noexcept { return sample_rate_hz_; }
        std::uint64_t FirstSampleIndex() const noexcept { return first_sample_index_; }
        std::uint64_t Epoch() const noexcept { return epoch_; }
        bool HasDiscontinuity() const noexcept { return discontinuity_; }
        std::size_t SampleCount() const noexcept { return sample_count_; }
        std::span<const ChannelDescriptor> Channels() const noexcept { return channels_; }

        std::span<const double> ChannelSamples(const std::size_t channel_index) const noexcept
        {
            if (channel_index >= channels_.size() || sample_count_ == 0)
            {
                return {};
            }
            return samples_.subspan(channel_index * sample_count_, sample_count_);
        }

    private:
        friend class SampleBlock;

        SampleBlockView(const double sample_rate_hz,
                        const std::uint64_t first_sample_index,
                        const std::uint64_t epoch,
                        const bool discontinuity,
                        const std::size_t sample_count,
                        const std::span<const ChannelDescriptor> channels,
                        const std::span<const double> samples) noexcept
            : sample_rate_hz_(sample_rate_hz),
              first_sample_index_(first_sample_index),
              epoch_(epoch),
              discontinuity_(discontinuity),
              sample_count_(sample_count),
              channels_(channels),
              samples_(samples)
        {
        }

        double sample_rate_hz_;
        std::uint64_t first_sample_index_;
        std::uint64_t epoch_;
        bool discontinuity_;
        std::size_t sample_count_;
        std::span<const ChannelDescriptor> channels_;
        std::span<const double> samples_;
    };

    inline SampleBlockView SampleBlock::View() const noexcept
    {
        return SampleBlockView{sample_rate_hz_,
                               first_sample_index_,
                               epoch_,
                               discontinuity_,
                               sample_count_,
                               channels_,
                               samples_};
    }
}

#endif
