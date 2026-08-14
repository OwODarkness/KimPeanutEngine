#ifndef KPENGINE_RUNTIME_RESOURCE_PIPELINE_H
#define KPENGINE_RUNTIME_RESOURCE_PIPELINE_H

#include <cstddef>
#include <memory>
#include <vector>
#include "base/type.h"
#include "asset/asset_manager.h"
#include "shader_processor.h"

namespace kpengine::resource{
    class ShaderCache;

    struct ResourcePipelineContext{
        GraphicsAPIType graphics_type;
    };  

    class ResourcePipeline{
    public:
        ResourcePipeline();
        void Initialize(const ResourcePipelineContext& context);
        // Fire-and-forget progress reporting for a slow compile (ShaderProcessor's
        // observer). nullptr keeps the call silent — the pipeline runs identically.
        void ProcessShader(const std::vector<asset::ShaderPtr>& shaders,
                           ShaderProcessObserver observer = nullptr);
        // Distinct shader references successfully processed so far (see
        // ShaderProcessor::GetProcessedShaderCount).
        std::size_t GetProcessedShaderCount() const;
        ~ResourcePipeline();
    private:
        ResourcePipelineContext context_;
        std::unique_ptr<ShaderProcessor> shader_processor_;
        std::unique_ptr<ShaderCache> shader_cache_;
    };
}

#endif