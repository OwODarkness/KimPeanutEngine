#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "signal/sample_block.h"
#include "signal/signal_statistics.h"
#include "signal/signal_generator.h"

namespace
{
    using namespace kpengine::signal;

    SignalGenerator MakeSineGenerator()
    {
        std::string diagnostic;
        auto generator = SignalGenerator::Create({}, diagnostic);
        EXPECT_TRUE(generator.has_value()) << diagnostic;
        return std::move(*generator);
    }

    TEST(SampleBlockTest, ValidatesMetadataAndAllocationBounds)
    {
        std::string diagnostic;
        const std::vector<ChannelDescriptor> mono{{0, SampleUnit::Volt}};

        EXPECT_FALSE(SampleBlock::Create(0.0, 0, 0, false, mono, 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(std::numeric_limits<double>::infinity(),
                                         0, 0, false, mono, 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0, 0, 0, false, {}, 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0, 0, 0, false,
                                         {{1, SampleUnit::Volt}, {1, SampleUnit::Volt}},
                                         1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0, 0, 0, false, mono,
                                         SampleBlock::kMaximumSampleCount + 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0,
                                         std::numeric_limits<std::uint64_t>::max(),
                                         0, false, mono, 1, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SampleBlockTest, ChannelsAreViewsIntoOneContiguousPlanarAllocation)
    {
        std::string diagnostic;
        auto block = SampleBlock::Create(48'000.0, 12, 3, false,
            {{0, SampleUnit::Volt}, {1, SampleUnit::NormalizedFullScale}}, 4, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        auto channel_0 = block->MutableChannelSamples(0);
        auto channel_1 = block->MutableChannelSamples(1);
        ASSERT_EQ(channel_0.size(), 4u);
        ASSERT_EQ(channel_1.size(), 4u);
        EXPECT_EQ(channel_0.data() + channel_0.size(), channel_1.data());

        channel_0[2] = 0.5;
        const SampleBlockView view = block->View();
        EXPECT_DOUBLE_EQ(view.ChannelSamples(0)[2], 0.5);
        EXPECT_EQ(view.SampleRateHz(), 48'000.0);
        EXPECT_EQ(view.FirstSampleIndex(), 12u);
        EXPECT_EQ(view.Epoch(), 3u);
        EXPECT_EQ(view.Channels().size(), 2u);
        EXPECT_TRUE(view.ChannelSamples(2).empty());
        static_assert(std::is_same_v<decltype(view.ChannelSamples(0)),
                                     std::span<const double>>);
    }

    TEST(SignalGeneratorTest, EmitsQuarterCycleSineSamplesAndMeasurements)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Sine;
        config.sample_rate_hz = 4'000.0;
        config.frequency_hz = 1'000.0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto block = generator->Generate(4, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        const auto samples = block->ChannelSamples(0);
        ASSERT_EQ(samples.size(), 4u);
        EXPECT_NEAR(samples[0], 0.0, 1e-14);
        EXPECT_NEAR(samples[1], 1.0, 1e-14);
        EXPECT_NEAR(samples[2], 0.0, 1e-14);
        EXPECT_NEAR(samples[3], -1.0, 1e-14);

        const auto statistics = Measure(samples, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_NEAR(statistics->mean, 0.0, 1e-14);
        EXPECT_NEAR(statistics->rms, std::sqrt(0.5), 1e-14);
        EXPECT_NEAR(statistics->peak_to_peak, 2.0, 1e-14);
    }

    TEST(SignalGeneratorTest, OneKilohertzSineBlockHasExpectedStatistics)
    {
        std::string diagnostic;
        auto generator = MakeSineGenerator();
        auto block = generator.Generate(4'800, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        const auto statistics = Measure(block->ChannelSamples(0), diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_EQ(statistics->sample_count, 4'800u);
        EXPECT_NEAR(statistics->mean, 0.0, 1e-12);
        EXPECT_NEAR(statistics->rms, std::sqrt(0.5), 1e-12);
        EXPECT_NEAR(statistics->minimum, -1.0, 1e-12);
        EXPECT_NEAR(statistics->maximum, 1.0, 1e-12);
    }

    TEST(SignalGeneratorTest, SineIsPartitionInvariantAndEmptyCallsDoNotAdvance)
    {
        std::string diagnostic;
        auto contiguous_generator = MakeSineGenerator();
        auto partitioned_generator = MakeSineGenerator();
        auto contiguous = contiguous_generator.Generate(4'369, diagnostic);
        ASSERT_TRUE(contiguous.has_value()) << diagnostic;

        std::vector<double> partitioned_samples;
        for (const std::size_t count : std::array<std::size_t, 5>{1, 17, 255, 0, 4'096})
        {
            const std::uint64_t previous_index = partitioned_generator.NextSampleIndex();
            auto part = partitioned_generator.Generate(count, diagnostic);
            ASSERT_TRUE(part.has_value()) << diagnostic;
            if (count == 0)
            {
                EXPECT_EQ(partitioned_generator.NextSampleIndex(), previous_index);
            }
            const auto channel = part->ChannelSamples(0);
            partitioned_samples.insert(partitioned_samples.end(), channel.begin(), channel.end());
        }

        ASSERT_EQ(partitioned_samples.size(), contiguous->SampleCount());
        const auto expected = contiguous->ChannelSamples(0);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(partitioned_samples[i], expected[i]);
        }
        EXPECT_EQ(partitioned_generator.NextSampleIndex(), contiguous_generator.NextSampleIndex());
    }

    TEST(SignalGeneratorTest, SineResetStartsANewEpochAtSampleZero)
    {
        std::string diagnostic;
        auto generator = MakeSineGenerator();
        ASSERT_TRUE(generator.Generate(32, diagnostic).has_value()) << diagnostic;
        ASSERT_TRUE(generator.Reset(diagnostic)) << diagnostic;
        auto block = generator.Generate(8, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        EXPECT_EQ(block->Epoch(), 1u);
        EXPECT_EQ(block->FirstSampleIndex(), 0u);
        EXPECT_NEAR(block->ChannelSamples(0)[0], 0.0, 1e-14);
    }

    TEST(SignalGeneratorTest, GeneratesConstantValue)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Constant;
        config.constant_value = 2.0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto block = generator->Generate(64, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        ASSERT_EQ(block->ChannelSamples(0).size(), 64u);
        for (const double sample : block->ChannelSamples(0))
        {
            EXPECT_DOUBLE_EQ(sample, 2.0);
        }

        const auto statistics = Measure(block->ChannelSamples(0), diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_DOUBLE_EQ(statistics->mean, 2.0);
        EXPECT_DOUBLE_EQ(statistics->rms, 2.0);
        EXPECT_DOUBLE_EQ(statistics->peak_to_peak, 0.0);
    }

    TEST(SignalGeneratorTest, UnitImpulseMatchesStatisticsAcrossABlockBoundary)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Impulse;
        config.impulse_sample_index = 3;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto before_impulse = generator->Generate(3, diagnostic);
        ASSERT_TRUE(before_impulse.has_value()) << diagnostic;
        auto after_boundary = generator->Generate(61, diagnostic);
        ASSERT_TRUE(after_boundary.has_value()) << diagnostic;
        const auto baseline = before_impulse->ChannelSamples(0);
        const auto impulse = after_boundary->ChannelSamples(0);
        for (const double sample : baseline)
        {
            EXPECT_DOUBLE_EQ(sample, 0.0);
        }
        ASSERT_EQ(impulse.size(), 61u);
        EXPECT_DOUBLE_EQ(impulse[0], 1.0);
        for (std::size_t i = 1; i < impulse.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(impulse[i], 0.0);
        }

        std::vector<double> complete_run;
        complete_run.insert(complete_run.end(), baseline.begin(), baseline.end());
        complete_run.insert(complete_run.end(), impulse.begin(), impulse.end());
        const auto statistics = Measure(complete_run, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_EQ(statistics->sample_count, 64u);
        EXPECT_DOUBLE_EQ(statistics->mean, 1.0 / 64.0);
        EXPECT_DOUBLE_EQ(statistics->rms, 1.0 / 8.0);
        EXPECT_DOUBLE_EQ(statistics->minimum, 0.0);
        EXPECT_DOUBLE_EQ(statistics->maximum, 1.0);
        EXPECT_DOUBLE_EQ(statistics->peak_to_peak, 1.0);
    }

    TEST(SignalGeneratorTest, FailedOversizedRequestDoesNotAdvanceState)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::UniformNoise;
        config.seed = 0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        EXPECT_FALSE(generator->Generate(SampleBlock::kMaximumSampleCount + 1, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        EXPECT_EQ(generator->NextSampleIndex(), 0u);

        auto after_failure = generator->Generate(8, diagnostic);
        ASSERT_TRUE(after_failure.has_value()) << diagnostic;
        const std::array<double, 8> expected{
            0.7666216164272852,
            -0.13694400590298006,
            -0.9471324568148045,
            0.941763956307657,
            -0.7873066168655751,
            -0.3453484715637485,
            -0.6522642680806343,
            0.543093112663134};
        const auto actual = after_failure->ChannelSamples(0);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(actual[i], expected[i]) << "sample " << i;
        }
    }

    TEST(SignalGeneratorTest, SeededUniformNoiseMatchesSplitMix64GoldenSamples)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::UniformNoise;
        config.seed = 0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto block = generator->Generate(8, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        const std::array<double, 8> expected{
            0.7666216164272852,
            -0.13694400590298006,
            -0.9471324568148045,
            0.941763956307657,
            -0.7873066168655751,
            -0.3453484715637485,
            -0.6522642680806343,
            0.543093112663134};
        const auto actual = block->ChannelSamples(0);
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(actual[i], expected[i]) << "sample " << i;
        }
    }

    TEST(SignalGeneratorTest, SeededNoiseIsPartitionInvariantAndResetReplaysSequence)
    {
        std::string diagnostic;
        SignalGeneratorConfig config;
        config.kind = SignalKind::UniformNoise;
        config.seed = 0x123456789ABCDEF0ULL;
        config.amplitude = 2.0;
        config.offset = -0.25;
        auto contiguous_generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(contiguous_generator.has_value()) << diagnostic;
        auto partitioned_generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(partitioned_generator.has_value()) << diagnostic;

        auto contiguous = contiguous_generator->Generate(4'369, diagnostic);
        ASSERT_TRUE(contiguous.has_value()) << diagnostic;
        std::vector<double> partitioned_samples;
        for (const std::size_t count : std::array<std::size_t, 5>{1, 17, 255, 0, 4'096})
        {
            const std::uint64_t previous_index = partitioned_generator->NextSampleIndex();
            auto part = partitioned_generator->Generate(count, diagnostic);
            ASSERT_TRUE(part.has_value()) << diagnostic;
            if (count == 0)
            {
                EXPECT_EQ(partitioned_generator->NextSampleIndex(), previous_index);
            }
            const auto channel = part->ChannelSamples(0);
            partitioned_samples.insert(partitioned_samples.end(), channel.begin(), channel.end());
        }

        ASSERT_EQ(partitioned_samples.size(), contiguous->SampleCount());
        const auto expected = contiguous->ChannelSamples(0);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(partitioned_samples[i], expected[i]);
        }
        EXPECT_EQ(partitioned_generator->NextSampleIndex(),
                  contiguous_generator->NextSampleIndex());

        ASSERT_TRUE(partitioned_generator->Reset(diagnostic)) << diagnostic;
        auto replay = partitioned_generator->Generate(8, diagnostic);
        ASSERT_TRUE(replay.has_value()) << diagnostic;
        EXPECT_EQ(replay->Epoch(), 1u);
        EXPECT_EQ(replay->FirstSampleIndex(), 0u);
        auto same_seed = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(same_seed.has_value()) << diagnostic;
        auto first_run = same_seed->Generate(8, diagnostic);
        ASSERT_TRUE(first_run.has_value()) << diagnostic;
        EXPECT_TRUE(std::equal(replay->ChannelSamples(0).begin(),
                               replay->ChannelSamples(0).end(),
                               first_run->ChannelSamples(0).begin()));
    }

    TEST(SignalGeneratorTest, InvalidGeneratorConfigurationIsRejected)
    {
        std::string diagnostic;
        SignalGeneratorConfig config;
        config.kind = static_cast<SignalKind>(255);
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.kind = SignalKind::UniformNoise;
        config.amplitude = -1.0;
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.amplitude = std::numeric_limits<double>::quiet_NaN();
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.amplitude = 1.0;
        config.sample_rate_hz = 0.0;
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SignalStatisticsTest, RejectsEmptyAndNonFiniteInputs)
    {
        std::string diagnostic;
        EXPECT_FALSE(Measure({}, diagnostic));
        const std::array<double, 2> invalid{1.0, std::numeric_limits<double>::quiet_NaN()};
        EXPECT_FALSE(Measure(invalid, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }
}
