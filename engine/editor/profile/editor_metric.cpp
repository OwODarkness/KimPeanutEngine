#include "editor/profile/editor_metric.h"

#include <utility>

namespace kpengine::editor
{
    EditorMetric::EditorMetric(size_t history_capacity)
        : history_capacity_(history_capacity)
    {
        history_.reserve(history_capacity);
    }

    void EditorMetric::RecordPlotValue(float value)
    {
        history_.push_back(value);
        if (history_.size() > history_capacity_)
        {
            history_.erase(history_.begin());
        }
    }

    EditorFuncMetric::EditorFuncMetric(const char *name, ValueSampler value_sampler,
                                       PlotSampler plot_sampler, size_t history_capacity)
        : EditorMetric(history_capacity),
          name_(name),
          value_sampler_(std::move(value_sampler)),
          plot_sampler_(std::move(plot_sampler))
    {
    }

    const char *EditorFuncMetric::Name() const
    {
        return name_.c_str();
    }

    std::string EditorFuncMetric::Sample()
    {
        if (HasPlot() && plot_sampler_)
        {
            RecordPlotValue(plot_sampler_());
        }
        return value_sampler_();
    }
}
