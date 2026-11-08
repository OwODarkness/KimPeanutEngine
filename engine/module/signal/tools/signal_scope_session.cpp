#include "signal_scope_session.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kpengine::signal
{
    SignalScopeSession::SignalScopeSession()
        : ring_(kRingCapacity)
    {
        std::string diagnostic;
        auto generator = SignalGenerator::Create(source_, diagnostic);
        if (generator.has_value())
        {
            generator_ = std::move(*generator);
        }
        active_capture_.reserve(capture_size_);
        PublishStateLocked(state_label_, false);
    }

    bool SignalScopeSession::SetSource(SignalGeneratorConfig source,
                                       std::string &diagnostic)
    {
        std::lock_guard lock(mutex_);
        auto generator = SignalGenerator::Create(source, diagnostic);
        if (!generator.has_value())
        {
            return false;
        }
        const double requested_samples = timebase_ms_ * source.sample_rate_hz / 1'000.0;
        if (!std::isfinite(requested_samples) || requested_samples < 2.0 ||
            requested_samples > static_cast<double>(kRingCapacity))
        {
            diagnostic = "timebase and sample rate must fit 2 to 65,536 capture samples";
            return false;
        }
        source_ = std::move(source);
        generator_ = std::move(*generator);
        capture_size_ = static_cast<std::size_t>(std::llround(requested_samples));
        pretrigger_samples_ = capture_size_ / 4;
        auto_timeout_samples_ = static_cast<std::uint64_t>(capture_size_) * 10;
        ++epoch_;
        sample_position_ = 0;
        fractional_samples_ = 0.0;
        history_count_ = 0;
        have_previous_ = false;
        trigger_armed_ = false;
        active_trigger_sample_.reset();
        active_capture_count_ = 0;
        holdoff_until_ = 0;
        last_auto_capture_sample_ = 0;
        state_label_ = "Source changed / priming history";
        stale_ = false;
        completed_snapshot_.reset();
        PublishStateLocked(state_label_, false);
        return true;
    }

    bool SignalScopeSession::SetTimebaseMs(const double timebase_ms,
                                           std::string &diagnostic)
    {
        diagnostic.clear();
        if (!std::isfinite(timebase_ms) || timebase_ms <= 0.0)
        {
            diagnostic = "scope timebase must be finite and greater than zero";
            return false;
        }
        std::lock_guard lock(mutex_);
        const double requested_samples = timebase_ms * source_.sample_rate_hz / 1'000.0;
        if (!std::isfinite(requested_samples) || requested_samples < 2.0 ||
            requested_samples > static_cast<double>(kRingCapacity))
        {
            diagnostic = "timebase must fit 2 to 65,536 samples at the current sample rate";
            return false;
        }
        timebase_ms_ = timebase_ms;
        capture_size_ = static_cast<std::size_t>(std::llround(requested_samples));
        capture_size_ = std::max<std::size_t>(capture_size_, 2);
        pretrigger_samples_ = capture_size_ / 4;
        auto_timeout_samples_ = static_cast<std::uint64_t>(capture_size_) * 10;
        last_auto_capture_sample_ = sample_position_;
        active_trigger_sample_.reset();
        active_capture_count_ = 0;
        trigger_armed_ = false;
        holdoff_until_ = sample_position_;
        completed_snapshot_.reset();
        stale_ = false;
        state_label_ = "Timebase changed / waiting for capture";
        PublishStateLocked(state_label_, false);
        return true;
    }

    bool SignalScopeSession::SetTrigger(const double level, const double hysteresis,
                                        const ScopeTriggerEdge edge,
                                        const ScopeTriggerMode mode,
                                        std::string &diagnostic)
    {
        diagnostic.clear();
        if (!std::isfinite(level) || !std::isfinite(hysteresis) || hysteresis < 0.0)
        {
            diagnostic = "trigger level must be finite and hysteresis non-negative";
            return false;
        }
        std::lock_guard lock(mutex_);
        trigger_level_ = level;
        trigger_hysteresis_ = hysteresis;
        trigger_edge_ = edge;
        trigger_mode_ = mode;
        trigger_armed_ = false;
        active_trigger_sample_.reset();
        active_capture_count_ = 0;
        holdoff_until_ = sample_position_;
        last_auto_capture_sample_ = sample_position_;
        state_label_ = mode == ScopeTriggerMode::Single
            ? "Single / waiting for trigger" : "Waiting for trigger";
        PublishStateLocked(state_label_, completed_snapshot_ != nullptr);
        return true;
    }

    bool SignalScopeSession::SetHoldoffSamples(const std::uint64_t samples,
                                               std::string &diagnostic)
    {
        diagnostic.clear();
        if (samples > kRingCapacity)
        {
            diagnostic = "trigger holdoff must not exceed the 65,536-sample history";
            return false;
        }
        std::lock_guard lock(mutex_);
        holdoff_samples_ = samples;
        if (!active_trigger_sample_.has_value())
        {
            holdoff_until_ = sample_position_;
            trigger_armed_ = false;
        }
        PublishStateLocked(state_label_, stale_);
        return true;
    }

    void SignalScopeSession::Run() noexcept
    {
        std::lock_guard lock(mutex_);
        transport_ = ScopeTransport::Running;
        state_label_ = active_trigger_sample_.has_value()
            ? "Capturing" : "Acquiring";
        PublishStateLocked(state_label_, completed_snapshot_ != nullptr);
    }

    void SignalScopeSession::Pause() noexcept
    {
        std::lock_guard lock(mutex_);
        transport_ = ScopeTransport::Paused;
        state_label_ = "Paused";
        PublishStateLocked(state_label_, completed_snapshot_ != nullptr);
    }

    void SignalScopeSession::Stop() noexcept
    {
        std::lock_guard lock(mutex_);
        transport_ = ScopeTransport::Stopped;
        state_label_ = "Stopped";
        PublishStateLocked(state_label_, completed_snapshot_ != nullptr);
    }

    bool SignalScopeSession::Reset(std::string &diagnostic)
    {
        std::lock_guard lock(mutex_);
        return ResetSourceLocked(diagnostic);
    }

    bool SignalScopeSession::ResetSourceLocked(std::string &diagnostic)
    {
        if (generator_ == std::nullopt || !generator_->Reset(diagnostic))
        {
            if (diagnostic.empty())
            {
                diagnostic = "signal generator is unavailable";
            }
            return false;
        }
        ++epoch_;
        std::fill(ring_.begin(), ring_.end(), 0.0);
        sample_position_ = 0;
        fractional_samples_ = 0.0;
        history_count_ = 0;
        have_previous_ = false;
        trigger_armed_ = false;
        active_trigger_sample_.reset();
        active_capture_count_ = 0;
        holdoff_until_ = 0;
        last_auto_capture_sample_ = 0;
        completed_snapshot_.reset();
        transport_ = ScopeTransport::Running;
        state_label_ = "Reset / priming history";
        stale_ = false;
        PublishStateLocked(state_label_, false);
        diagnostic.clear();
        return true;
    }

    bool SignalScopeSession::Arm(std::string &diagnostic)
    {
        diagnostic.clear();
        std::lock_guard lock(mutex_);
        if (trigger_mode_ != ScopeTriggerMode::Single)
        {
            diagnostic = "explicit arm is available in Single trigger mode";
            return false;
        }
        trigger_armed_ = false;
        active_trigger_sample_.reset();
        active_capture_count_ = 0;
        holdoff_until_ = sample_position_;
        transport_ = ScopeTransport::Running;
        state_label_ = "Single / waiting for trigger";
        PublishStateLocked(state_label_, completed_snapshot_ != nullptr);
        return true;
    }

    bool SignalScopeSession::Step(std::string &diagnostic)
    {
        std::lock_guard lock(mutex_);
        if (transport_ == ScopeTransport::Running)
        {
            diagnostic = "pause acquisition before stepping one block";
            return false;
        }
        if (transport_ == ScopeTransport::Stopped)
        {
            diagnostic = "stopped acquisition must be armed or reset before stepping";
            return false;
        }
        return GenerateBlockLocked(diagnostic);
    }

    bool SignalScopeSession::Tick(const double delta_seconds,
                                  std::string &diagnostic)
    {
        diagnostic.clear();
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0)
        {
            diagnostic = "scope tick duration must be finite and non-negative";
            return false;
        }
        std::lock_guard lock(mutex_);
        if (transport_ != ScopeTransport::Running || generator_ == std::nullopt)
        {
            return true;
        }
        fractional_samples_ += delta_seconds * source_.sample_rate_hz;
        while (fractional_samples_ >= static_cast<double>(kBlockSize))
        {
            if (!GenerateBlockLocked(diagnostic))
            {
                return false;
            }
            fractional_samples_ -= static_cast<double>(kBlockSize);
        }
        return true;
    }

    bool SignalScopeSession::GenerateBlockLocked(std::string &diagnostic)
    {
        if (generator_ == std::nullopt)
        {
            diagnostic = "signal generator is unavailable";
            return false;
        }
        if (trigger_mode_ == ScopeTriggerMode::Single)
        {
            for (std::size_t i = 0; i < kBlockSize; ++i)
            {
                auto sample = generator_->Generate(1, diagnostic);
                if (!sample.has_value())
                {
                    return false;
                }
                ConsumeLocked(sample->ChannelSamples(0));
                if (transport_ == ScopeTransport::Stopped)
                {
                    break;
                }
            }
            PublishStateLocked(state_label_, stale_);
            return true;
        }
        auto block = generator_->Generate(kBlockSize, diagnostic);
        if (!block.has_value())
        {
            return false;
        }
        ConsumeLocked(block->ChannelSamples(0));
        if (completed_snapshot_ != nullptr)
        {
            if (active_trigger_sample_.has_value())
            {
                stale_ = true;
            }
            else if (sample_position_ > completed_snapshot_->first_sample + capture_size_)
            {
                stale_ = true;
                state_label_ = completed_snapshot_->trigger_sample.has_value()
                    ? "Waiting / previous capture stale"
                    : "Auto / no trigger / stale";
            }
        }
        PublishStateLocked(state_label_, stale_);
        return true;
    }

    void SignalScopeSession::ConsumeLocked(const std::span<const double> samples)
    {
        for (const double sample : samples)
        {
            ConsumeSampleLocked(sample);
        }
    }

    void SignalScopeSession::ConsumeSampleLocked(const double sample)
    {
        const std::uint64_t index = sample_position_;
        ring_[static_cast<std::size_t>(index % kRingCapacity)] = sample;
        ++sample_position_;
        history_count_ = std::min(history_count_ + 1, kRingCapacity);

        if (active_trigger_sample_.has_value())
        {
            if (active_capture_count_ < capture_size_)
            {
                active_capture_.push_back(sample);
                ++active_capture_count_;
            }
            if (active_capture_count_ == capture_size_)
            {
                FinishCaptureLocked();
            }
        }
        else
        {
            const bool rising = trigger_edge_ == ScopeTriggerEdge::Rising;
            const bool crossed = trigger_armed_ && have_previous_ &&
                (rising ? previous_sample_ < trigger_level_ && sample >= trigger_level_
                        : previous_sample_ > trigger_level_ && sample <= trigger_level_);
            if (crossed && index >= pretrigger_samples_ && index >= holdoff_until_)
            {
                PrimeCaptureLocked(index, sample);
                if (active_capture_count_ == capture_size_)
                {
                    FinishCaptureLocked();
                }
            }
            else if (index >= holdoff_until_)
            {
                const double arm_level = rising
                    ? trigger_level_ - trigger_hysteresis_
                    : trigger_level_ + trigger_hysteresis_;
                if (rising ? sample <= arm_level : sample >= arm_level)
                {
                    trigger_armed_ = true;
                }
            }
        }

        previous_sample_ = sample;
        have_previous_ = true;
        if (trigger_mode_ == ScopeTriggerMode::Auto &&
            !active_trigger_sample_.has_value() &&
            history_count_ >= capture_size_ &&
            sample_position_ - last_auto_capture_sample_ >= auto_timeout_samples_)
        {
            std::vector<double> capture(capture_size_);
            const std::uint64_t first = sample_position_ - capture_size_;
            for (std::size_t i = 0; i < capture_size_; ++i)
            {
                capture[i] = SampleAtLocked(first + i);
            }
            last_auto_capture_sample_ = sample_position_;
            PublishLocked(std::move(capture), std::nullopt, false,
                          "Auto / no trigger");
        }
        else if (completed_snapshot_ != nullptr)
        {
            stale_ = true;
        }
    }

    void SignalScopeSession::PrimeCaptureLocked(const std::uint64_t trigger_sample,
                                                const double sample)
    {
        const std::uint64_t first = trigger_sample - pretrigger_samples_;
        if (!HasHistoryLocked(first, trigger_sample))
        {
            return;
        }
        active_capture_.clear();
        for (std::uint64_t index = first; index < trigger_sample; ++index)
        {
            active_capture_.push_back(SampleAtLocked(index));
        }
        active_capture_.push_back(sample);
        active_capture_count_ = active_capture_.size();
        active_trigger_sample_ = trigger_sample;
        trigger_armed_ = false;
        holdoff_until_ = trigger_sample + std::min<std::uint64_t>(
            holdoff_samples_ + 1, std::numeric_limits<std::uint64_t>::max() - trigger_sample);
        stale_ = completed_snapshot_ != nullptr;
        state_label_ = stale_ ? "Capturing / previous capture stale"
                              : "Trigger / capturing";
    }

    void SignalScopeSession::FinishCaptureLocked()
    {
        if (!active_trigger_sample_.has_value() || active_capture_count_ != capture_size_)
        {
            return;
        }
        const std::uint64_t trigger_sample = *active_trigger_sample_;
        PublishLocked(std::move(active_capture_), trigger_sample, false,
                      trigger_mode_ == ScopeTriggerMode::Single
                          ? "Single / captured" : "Triggered");
        active_capture_.clear();
        active_capture_.reserve(capture_size_);
        active_capture_count_ = 0;
        active_trigger_sample_.reset();
        holdoff_until_ = trigger_sample + std::min<std::uint64_t>(
            holdoff_samples_ + 1, std::numeric_limits<std::uint64_t>::max() - trigger_sample);
        last_auto_capture_sample_ = sample_position_;
        if (trigger_mode_ == ScopeTriggerMode::Single)
        {
            transport_ = ScopeTransport::Stopped;
            state_label_ = "Single / captured";
        }
    }

    void SignalScopeSession::PublishLocked(std::vector<double> samples,
                                           const std::optional<std::uint64_t> trigger_sample,
                                           const bool stale,
                                           std::string label)
    {
        std::string diagnostic;
        const auto measurements = Measure(samples, diagnostic);
        if (!measurements.has_value())
        {
            PublishStateLocked("Capture measurement failed", true);
            return;
        }
        auto next = std::make_shared<SignalScopeSnapshot>();
        next->capture = std::make_shared<const std::vector<double>>(std::move(samples));
        next->measurements = *measurements;
        next->source = source_;
        next->transport = transport_;
        next->trigger_mode = trigger_mode_;
        next->trigger_edge = trigger_edge_;
        next->trigger_sample = trigger_sample;
        next->first_sample = trigger_sample.has_value()
            ? *trigger_sample - pretrigger_samples_
            : sample_position_ - capture_size_;
        next->sample_position = sample_position_;
        next->epoch = epoch_;
        next->capture_id = ++capture_id_;
        next->trigger_level = trigger_level_;
        next->trigger_hysteresis = trigger_hysteresis_;
        next->holdoff_samples = holdoff_samples_;
        next->timebase_ms = timebase_ms_;
        next->capture_size = capture_size_;
        next->pretrigger_samples = pretrigger_samples_;
        next->state_label = std::move(label);
        next->stale = stale;

        if (trigger_sample.has_value() && source_.kind != SignalKind::UniformNoise &&
            source_.kind != SignalKind::Impulse && source_.kind != SignalKind::Constant)
        {
            std::vector<std::uint64_t> crossings;
            const auto &values = *next->capture;
            for (std::size_t i = 1; i < values.size(); ++i)
            {
                const bool crossed = trigger_edge_ == ScopeTriggerEdge::Rising
                    ? values[i - 1] < trigger_level_ && values[i] >= trigger_level_
                    : values[i - 1] > trigger_level_ && values[i] <= trigger_level_;
                if (crossed)
                {
                    crossings.push_back(next->first_sample + i);
                }
            }
            if (crossings.size() >= 2)
            {
                const std::uint64_t span = crossings.back() - crossings.front();
                if (span > 0)
                {
                    const double cycles = static_cast<double>(crossings.size() - 1);
                    next->measured_frequency_hz = cycles * source_.sample_rate_hz /
                                                   static_cast<double>(span);
                    next->measured_frequency_uncertainty_hz =
                        next->measured_frequency_hz / static_cast<double>(span);
                    next->frequency_available = true;
                }
            }
        }
        completed_snapshot_ = next;
        stale_ = stale;
        state_label_ = next->state_label;
        published_snapshot_.store(std::move(next), std::memory_order_release);
    }

    void SignalScopeSession::PublishStateLocked(std::string label, const bool stale)
    {
        auto next = std::make_shared<SignalScopeSnapshot>();
        if (completed_snapshot_ != nullptr)
        {
            *next = *completed_snapshot_;
        }
        next->source = source_;
        next->transport = transport_;
        next->trigger_mode = trigger_mode_;
        next->trigger_edge = trigger_edge_;
        next->sample_position = sample_position_;
        next->epoch = epoch_;
        next->trigger_level = trigger_level_;
        next->trigger_hysteresis = trigger_hysteresis_;
        next->holdoff_samples = holdoff_samples_;
        next->timebase_ms = timebase_ms_;
        next->capture_size = capture_size_;
        next->pretrigger_samples = pretrigger_samples_;
        next->state_label = std::move(label);
        next->stale = stale;
        stale_ = stale;
        published_snapshot_.store(std::move(next), std::memory_order_release);
    }

    double SignalScopeSession::SampleAtLocked(const std::uint64_t index) const noexcept
    {
        return ring_[static_cast<std::size_t>(index % kRingCapacity)];
    }

    bool SignalScopeSession::HasHistoryLocked(const std::uint64_t first,
                                              const std::uint64_t last) const noexcept
    {
        const std::uint64_t oldest = sample_position_ - history_count_;
        return first >= oldest && first <= last && last < sample_position_;
    }

    std::shared_ptr<const SignalScopeSnapshot> SignalScopeSession::Snapshot() const noexcept
    {
        return published_snapshot_.load(std::memory_order_acquire);
    }
}
