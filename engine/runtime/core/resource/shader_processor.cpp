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

    ShaderProcessor::ShaderProcessor() : 
    compiler_(std::make_unique<SPIRVCompiler>())
    {
    }
    ShaderProcessor::~ShaderProcessor()
    {
    }

    void ShaderProcessor::Initialize(GraphicsAPIType api_type)
    {
        api_ = api_type;
        compiler_->Initialize(api_);
    }

    void ShaderProcessor::Process(ShaderCache* cache, const std::vector<std::shared_ptr<asset::ShaderResource>> &assets)
    {
        if(!cache)
        {
            return ;
        }

        for(const auto& shader: assets)
        {
            std::string file_name = shader->meta.file;
            std::string content = ReadText(file_name);
            std::string stage_str = std::string(magic_enum::enum_name(shader->meta.stage));

            uint64_t hash = GenerateShaderHash(content, stage_str, shader->meta.entry, {});
            std::vector<uint8_t> byte_codes;
            if(cache->Has(hash))
            {
                KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s has been cached", file_name.c_str());
                byte_codes = cache->Load(hash);
            }
            else
            {
                ShaderCompileInput input;
                input.file_name = file_name;
                input.format = shader->format;
                input.source = content;
                input.stage = shader->meta.stage;
                shader->status = asset::ShaderStatus::Compiling;

                byte_codes = compiler_->Compile(input);
                if(byte_codes.empty())
                {
                    KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s failed to compile with binary", file_name.c_str());
                    return ;
                }
                else
                {
                    cache->Save(hash, byte_codes);
                    KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s ready to cache", file_name.c_str());          
                }

            }
            //TODO: write back
            if(!shader->resource)
            {
                shader->resource = std::make_shared<data::ShaderData>();
            }
            shader->resource->byte_code = std::move(byte_codes);
            shader->resource->stage = shader->meta.stage;
            shader->resource->entry = shader->meta.entry;
            shader->status = asset::ShaderStatus::Ready;
            //shader->resource
        }
    }
}