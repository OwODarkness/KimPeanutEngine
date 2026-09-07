#include "render/render_profile.h"

#include <algorithm>

namespace
{
    double Percentile(std::vector<double> values, const double fraction)
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const double position = fraction * static_cast<double>(values.size() - 1);
        const size_t lower = static_cast<size_t>(position);
        const size_t upper = std::min(lower + 1, values.size() - 1);
        const double interpolation = position - static_cast<double>(lower);
        return values[lower] + (values[upper] - values[lower]) * interpolation;
    }
}

namespace kpengine::render
{
    static_assert(static_cast<size_t>(RenderProfilePass::Count) == 8);

    RenderProfileWindow::RenderProfileWindow(const uint32_t warmup_frames,
                                             const uint32_t sample_frames)
        : warmup_frames_(warmup_frames), sample_frames_(sample_frames)
    {
        cpu_total_samples_.reserve(sample_frames_);
        cpu_present_samples_.reserve(sample_frames_);
        for (auto &samples : gpu_samples_)
        {
            samples.reserve(sample_frames_);
        }
    }

    void RenderProfileWindow::Observe(const RenderProfileSnapshot &snapshot)
    {
        if (frames_observed_ < warmup_frames_)
        {
            ++frames_observed_;
            return;
        }
        if (sample_frames_ == 0 || cpu_total_samples_.size() >= sample_frames_)
        {
            return;
        }
        cpu_total_samples_.push_back(snapshot.cpu_total_ms);
        cpu_present_samples_.push_back(snapshot.cpu_present_ms);
        for (size_t index = 0; index < gpu_samples_.size(); ++index)
        {
            if (snapshot.passes[index].gpu_time_ms.has_value())
            {
                gpu_samples_[index].push_back(*snapshot.passes[index].gpu_time_ms);
            }
        }
        ++frames_observed_;
    }

    RenderProfileSummary RenderProfileWindow::GetSummary() const
    {
        RenderProfileSummary summary{};
        summary.warmup_frames_completed = std::min(frames_observed_, warmup_frames_);
        summary.samples_collected = static_cast<uint32_t>(cpu_total_samples_.size());
        summary.complete = summary.samples_collected >= sample_frames_ && sample_frames_ != 0;
        summary.cpu_total_p50_ms = Percentile(cpu_total_samples_, 0.50);
        summary.cpu_total_p95_ms = Percentile(cpu_total_samples_, 0.95);
        summary.cpu_present_p50_ms = Percentile(cpu_present_samples_, 0.50);
        summary.cpu_present_p95_ms = Percentile(cpu_present_samples_, 0.95);
        for (size_t index = 0; index < gpu_samples_.size(); ++index)
        {
            if (!gpu_samples_[index].empty())
            {
                summary.passes[index].gpu_p50_ms = Percentile(gpu_samples_[index], 0.50);
                summary.passes[index].gpu_p95_ms = Percentile(gpu_samples_[index], 0.95);
            }
        }
        return summary;
    }

    void RenderProfileWindow::Reset()
    {
        frames_observed_ = 0;
        cpu_total_samples_.clear();
        cpu_present_samples_.clear();
        for (auto &samples : gpu_samples_)
        {
            samples.clear();
        }
    }
}
