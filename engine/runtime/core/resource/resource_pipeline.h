#ifndef KPENGINE_RUNTIME_RESOURCE_PIPELINE_H
#define KPENGINE_RUNTIME_RESOURCE_PIPELINE_H

#include <memory>
#include <vector>
#include "base/type.h"
#include "asset/asset_manager.h"

namespace kpengine::resource{
    class ShaderProcessor;
    class ShaderCache;

    struct ResourcePipelineContext{
        GraphicsAPIType graphics_type;
    };  

    class ResourcePipeline{
    public:
        ResourcePipeline();
        void Initialize(const ResourcePipelineContext& context);
        void ProcessShader(const std::vector<asset::ShaderPtr>& shaders);
        ~ResourcePipeline();
    private:
        ResourcePipelineContext context_;
        std::unique_ptr<ShaderProcessor> shader_processor_;
        std::unique_ptr<ShaderCache> shader_cache_;
    };
}

#endif