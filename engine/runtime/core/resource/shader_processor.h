#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_PROCESSOR_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_PROCESSOR_H

#include <memory>
#include "asset/shader.h"
#include "base/type.h"
namespace kpengine::resource{
    class ShaderCompiler;
    class ShaderCache;

    class ShaderProcessor{
    public: 
        ShaderProcessor(ShaderCache* cache);
        ShaderProcessor() = delete;
        ~ShaderProcessor();
        void Initialize(GraphicsAPIType api_type);
        void PreProcess(const std::vector<std::shared_ptr<asset::ShaderResource>>& assets) ;
        void Process(const std::vector<std::shared_ptr<asset::ShaderResource>>& assets) ;
    private:
        std::unique_ptr<ShaderCompiler> compiler_;
        ShaderCache* cache_;
        GraphicsAPIType api_;
    };
}

#endif