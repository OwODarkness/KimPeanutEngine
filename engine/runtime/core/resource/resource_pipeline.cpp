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

        shader_processor_  = std::make_unique<ShaderProcessor>();
        shader_processor_->Initialize(context_.graphics_type);

    }

    void ResourcePipeline::ProcessShader(const std::vector<asset::ShaderPtr>& shaders,
                                         ShaderProcessObserver observer)
    {
        shader_processor_->Process(shader_cache_.get(), shaders, std::move(observer));
    }

    std::size_t ResourcePipeline::GetProcessedShaderCount() const
    {
        return shader_processor_ ? shader_processor_->GetProcessedShaderCount() : 0;
    }

    std::optional<EnvironmentIblData> ResourcePipeline::ProcessEnvironmentIbl(
        const data::TextureData &source, const EnvironmentIblSettings &settings) const
    {
        return BuildEnvironmentIbl(source, settings);
    }



    ResourcePipeline::~ResourcePipeline() = default;
}
