#ifndef KPENGINE_LIVE2D_MODEL_REPORT_COMMAND_H
#define KPENGINE_LIVE2D_MODEL_REPORT_COMMAND_H

#include <cstdint>
#include <functional>
#include <string>

#include "command/command_registry.h"
#include "render/live2d_render_contract.h"

namespace kpengine::live2d
{
    // What the report command answers with: which product is loaded and the
    // blend distribution authored inside it. The path is carried so the answer
    // identifies its own fixture -- a run that silently fell back to the
    // configured product must not read as evidence about the requested one.
    struct Live2DModelReport
    {
        std::string model_path;
        Live2DRenderFeatureReport features{};
    };

    // Resolves the model the active host has loaded, or nullopt when no host
    // owns one. Resolved per dispatch because the viewer loads its product on
    // the render thread, after this command is registered.
    using Live2DModelReportResolver = std::function<bool(Live2DModelReport &)>;

    // Registers the read-only command that reports the loaded model's blended
    // drawable counts. V1 acceptance claims the representative clipped model
    // uses all three blend modes; that is authored data inside the .moc3, so it
    // is reported from the loaded model rather than inferred from a capture.
    runtime::command::CommandRegistrationResult RegisterLive2DModelReportCommand(
        runtime::command::CommandRegistry &registry, Live2DModelReportResolver resolver);
}

#endif // KPENGINE_LIVE2D_MODEL_REPORT_COMMAND_H
