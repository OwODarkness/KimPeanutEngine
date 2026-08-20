#include "opengl_pipeline_manager.h"

#include "opengl_pipeline.h"

namespace kpengine::graphics
{
    PipelineHandle OpenglPipelineManager::CreatePipelineResource(const PipelineDesc &pipeline_desc)
    {
        const PipelineHandle handle = handle_system_.Create();
        if (handle.id == resources_.size())
        {
            resources_.emplace_back();
        }

        auto pipeline = std::make_unique<OpenglPipeline>();
        pipeline->Initialize(pipeline_desc);
        resources_[handle.id] = std::move(pipeline);
        return handle;
    }

    bool OpenglPipelineManager::DestroyPipelineResource(PipelineHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        if (index >= resources_.size() || !resources_[index])
        {
            return false;
        }

        resources_[index]->Destroy();
        resources_[index].reset();
        return handle_system_.Destroy(handle);
    }

    void OpenglPipelineManager::DestroyAll()
    {
        for (auto &pipeline : resources_)
        {
            if (pipeline)
            {
                pipeline->Destroy();
                pipeline.reset();
            }
        }
    }

    OpenglPipeline *OpenglPipelineManager::GetPipelineResource(PipelineHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        return index < resources_.size() ? resources_[index].get() : nullptr;
    }
}
