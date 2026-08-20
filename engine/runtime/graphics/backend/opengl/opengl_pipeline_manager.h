#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_PIPELINE_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_PIPELINE_MANAGER_H

#include <memory>
#include <vector>

#include "common/api.h"

namespace kpengine::graphics
{
    class OpenglPipeline;
    struct PipelineDesc;

    class OpenglPipelineManager
    {
    public:
        PipelineHandle CreatePipelineResource(const PipelineDesc &pipeline_desc);
        bool DestroyPipelineResource(PipelineHandle handle);
        void DestroyAll();
        OpenglPipeline *GetPipelineResource(PipelineHandle handle);

    private:
        std::vector<std::unique_ptr<OpenglPipeline>> resources_;
        HandleSystem<PipelineHandle> handle_system_;
    };
}

#endif
