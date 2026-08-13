#ifndef KPENGINE_EDITOR_METRIC_H
#define KPENGINE_EDITOR_METRIC_H

#include <functional>
#include <string>
#include <vector>

namespace kpengine::editor
{
    // One readout in the profile bar — the extension seam. A new metric is Name() +
    // Sample(), or wrap a sampler lambda with EditorFuncMetric. The optional plot history
    // (sparkline) is owned by the base; a capacity of 0 opts out.
    class EditorMetric
    {
    public:
        explicit EditorMetric(size_t history_capacity = 0);
        virtual ~EditorMetric() = default;

        virtual const char *Name() const = 0;
        // Refresh from the source and return the formatted value for this frame.
        virtual std::string Sample() = 0;

        bool HasPlot() const { return history_capacity_ > 0; }
        const std::vector<float> &History() const { return history_; }

    protected:
        void RecordPlotValue(float value);

    private:
        size_t history_capacity_;
        std::vector<float> history_;
    };

    // Ad-hoc metric: bind a name to sampler lambdas instead of writing a subclass.
    class EditorFuncMetric : public EditorMetric
    {
    public:
        using ValueSampler = std::function<std::string()>;
        using PlotSampler = std::function<float()>;

        EditorFuncMetric(const char *name, ValueSampler value_sampler,
                         PlotSampler plot_sampler = {}, size_t history_capacity = 0);

        const char *Name() const override;
        std::string Sample() override;

    private:
        std::string name_;
        ValueSampler value_sampler_;
        PlotSampler plot_sampler_;
    };
}

#endif // KPENGINE_EDITOR_METRIC_H
