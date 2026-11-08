#ifndef KPENGINE_MODULE_SIGNAL_TOOLS_SIGNAL_SCOPE_SESSION_H
#define KPENGINE_MODULE_SIGNAL_TOOLS_SIGNAL_SCOPE_SESSION_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "signal/signal_generator.h"
#include "signal/signal_statistics.h"

namespace kpengine::signal
{
    enum class ScopeTriggerEdge : std::uint8_t { Rising, Falling };
    enum class ScopeTriggerMode : std::uint8_t { Auto, Normal, Single };
    enum class ScopeTransport : std::uint8_t { Running, Paused, Stopped };

    struct SignalScopeSnapshot
    {
        std::shared_ptr<const std::vector<double>> capture;
        SignalStatistics measurements{};
        SignalGeneratorConfig source{};
        ScopeTransport transport = ScopeTransport::Running;
        ScopeTriggerMode trigger_mode = ScopeTriggerMode::Auto;
        ScopeTriggerEdge trigger_edge = ScopeTriggerEdge::Rising;
        std::optional<std::uint64_t> trigger_sample;
        std::uint64_t first_sample = 0;
        std::uint64_t sample_position = 0;
        std::uint64_t epoch = 0;
        std::uint64_t capture_id = 0;
        double trigger_level = 0.0;
        double trigger_hysteresis = 0.0;
        double timebase_ms = 10.0;
        std::size_t capture_size = 480;
        std::size_t pretrigger_samples = 120;
        double measured_frequency_hz = 0.0;
        double measured_frequency_uncertainty_hz = 0.0;
        std::uint64_t holdoff_samples = 0;
        bool frequency_available = false;
        bool stale = false;
        std::string state_label = "Waiting for samples";
    };

    // Signal Lab owns simulated acquisition. The mutable ring stays private;
    // renderers only receive immutable, completed capture snapshots.
    class SignalScopeSession final
    {
    public:
        static constexpr std::size_t kBlockSize = 256;
        static constexpr std::size_t kCaptureSize = 480;
        static constexpr std::size_t kPretriggerSamples = 120;
        static constexpr std::size_t kRingCapacity = 65'536;

        SignalScopeSession();

        bool SetSource(SignalGeneratorConfig source, std::string &diagnostic);
        bool SetTimebaseMs(double timebase_ms, std::string &diagnostic);
        bool SetTrigger(double level, double hysteresis,
                        ScopeTriggerEdge edge, ScopeTriggerMode mode,
                        std::string &diagnostic);
        bool SetHoldoffSamples(std::uint64_t samples, std::string &diagnostic);
        void Run() noexcept;
        void Pause() noexcept;
        void Stop() noexcept;
        bool Reset(std::string &diagnostic);
        bool Arm(std::string &diagnostic);
        bool Step(std::string &diagnostic);
        bool Tick(double delta_seconds, std::string &diagnostic);
        std::shared_ptr<const SignalScopeSnapshot> Snapshot() const noexcept;

    private:
        bool ResetSourceLocked(std::string &diagnostic);
        bool GenerateBlockLocked(std::string &diagnostic);
        void ConsumeLocked(std::span<const double> samples);
        void ConsumeSampleLocked(double sample);
        void PublishLocked(std::vector<double> samples,
                           std::optional<std::uint64_t> trigger_sample,
                           bool stale, std::string label);
        void PublishStateLocked(std::string label, bool stale);
        void PrimeCaptureLocked(std::uint64_t trigger_sample, double sample);
        void FinishCaptureLocked();
        double SampleAtLocked(std::uint64_t index) const noexcept;
        bool HasHistoryLocked(std::uint64_t first, std::uint64_t last) const noexcept;

        mutable std::mutex mutex_;
        std::vector<double> ring_;
        std::optional<SignalGenerator> generator_;
        SignalGeneratorConfig source_{};
        double timebase_ms_ = 10.0;
        std::size_t capture_size_ = kCaptureSize;
        std::size_t pretrigger_samples_ = kPretriggerSamples;
        ScopeTransport transport_ = ScopeTransport::Running;
        ScopeTriggerMode trigger_mode_ = ScopeTriggerMode::Auto;
        ScopeTriggerEdge trigger_edge_ = ScopeTriggerEdge::Rising;
        double trigger_level_ = 0.0;
        double trigger_hysteresis_ = 0.01;
        std::uint64_t holdoff_samples_ = 0;
        std::uint64_t sample_position_ = 0;
        std::uint64_t epoch_ = 0;
        std::uint64_t capture_id_ = 0;
        std::uint64_t holdoff_until_ = 0;
        std::uint64_t auto_timeout_samples_ = 4'800;
        std::uint64_t last_auto_capture_sample_ = 0;
        std::size_t history_count_ = 0;
        bool have_previous_ = false;
        bool trigger_armed_ = false;
        double previous_sample_ = 0.0;
        std::optional<std::uint64_t> active_trigger_sample_;
        std::vector<double> active_capture_;
        std::size_t active_capture_count_ = 0;
        double fractional_samples_ = 0.0;
        std::shared_ptr<const SignalScopeSnapshot> completed_snapshot_;
        std::atomic<std::shared_ptr<const SignalScopeSnapshot>> published_snapshot_;
        std::string state_label_ = "Waiting for samples";
        bool stale_ = false;
    };
}

#endif
