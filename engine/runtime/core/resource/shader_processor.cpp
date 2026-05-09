#include "shader_processor.h"
#include <magic_enum/magic_enum.hpp>
#include "asset/shader.h"
#include "spirv_compiler.h"
#include "utility.h"
#include "log/logger.h"
#include "config/path.h"
#include "shader_cache.h"
namespace kpengine::resource
{

    ShaderProcessor::ShaderProcessor(ShaderCache* cache) : 
    compiler_(std::make_unique<SPIRVCompiler>()),
    cache_(cache)
    {
    }
    ShaderProcessor::~ShaderProcessor()
    {
    }

    void ShaderProcessor::Initialize(GraphicsAPIType api_type)
    {
        assert(cache_);
        api_ = api_type;
        compiler_->Initialize(api_);
    }

    void ShaderProcessor::PreProcess(const std::vector<std::shared_ptr<asset::ShaderResource>> &assets)
    {
        
    }

    void ShaderProcessor::Process(const std::vector<std::shared_ptr<asset::ShaderResource>> &assets)
    {
        for(const auto& shader: assets)
        {
            std::string file_name = shader->meta.file;
            std::string content = ReadText(file_name);
            std::string stage_str = std::string(magic_enum::enum_name(shader->meta.stage));

            uint64_t hash = GenerateShaderHash(content, stage_str, shader->meta.entry, {});

            if(cache_->Has(hash))
            {
                KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s has been cached", file_name.c_str());
                std::vector<uint8_t> byte_codes = cache_->Load(hash);
            }
            else
            {
                ShaderCompileInput input;
                input.file_name = file_name;
                input.format = shader->format;
                input.source = content;
                input.stage = shader->meta.stage;
                std::vector<uint8_t> byte_codes = compiler_->Compile(input);
                if(byte_codes.empty())
                {
                    KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s failed to compile with binary", file_name.c_str());
                }
                else
                {
                    cache_->Save(hash, byte_codes);
                    KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s ready to cache", file_name.c_str());          
                }

            }
        }
    }
}