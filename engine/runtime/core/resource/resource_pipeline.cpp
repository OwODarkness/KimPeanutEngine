#include "resource_pipeline.h"
#include "shader_cache.h"
#include "shader_processor.h"

namespace kpengine::resource{
        ResourcePipeline::ResourcePipeline()
    {
        
    }
    void ResourcePipeline::Initialize(const ResourcePipelineContext& context)
    {
        context_ = context;
        shader_cache_ = std::make_unique<ShaderCache>();
        shader_cache_->Initialize(context_.graphics_type);

        shader_processor_  = std::make_unique<ShaderProcessor>(shader_cache_.get());
        shader_processor_->Initialize(context_.graphics_type);

    }

    void ResourcePipeline::ProcessShader(const std::vector<asset::ShaderPtr>& shaders)
    {
        shader_processor_->Process(shaders);
    }



    ResourcePipeline::~ResourcePipeline() = default;
}