#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_PROCESSOR_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_PROCESSOR_H

#include "resource_processor.h"
#include <memory>
namespace kpengine::resource{
    class ShaderCompiler;
    class ShaderCache;

    class ShaderProcessor: public IResourceProcessor{
    public: 
        ShaderProcessor(ShaderCache* cache);
        ShaderProcessor() = delete;
        ~ShaderProcessor();
        virtual void Initialize(GraphicsAPIType api_type) override;
        virtual void PreProcess(const std::vector<AssetPtr>& assets) override;
        virtual void Process(const std::vector<AssetPtr>& assets) override;
    private:
        std::unique_ptr<ShaderCompiler> compiler_;
        ShaderCache* cache_;
        GraphicsAPIType api_;
    };
}

#endif