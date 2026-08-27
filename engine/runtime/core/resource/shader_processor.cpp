#include "shader_processor.h"
#include <magic_enum/magic_enum.hpp>
#include "asset/shader.h"
#include "preprocess_operation.h"
#include "spirv_compiler.h"
#include "utility.h"
#include "log/logger.h"
#include "config/path.h"
#include "shader_cache.h"
namespace kpengine::resource
{
    namespace
    {
        // The shared shader ABI is intentionally expressed as preprocessor
        // values so a compatible shader can declare the table without pulling
        // a Graphics backend header into Resource.
        void AddBindlessTextureTableDefines(std::vector<std::string> &defines)
        {
            defines.push_back("KP_BINDLESS_TEXTURE_TABLE_ABI_VERSION 1");
            defines.push_back("KP_BINDLESS_TEXTURE_TABLE_SET 1");
            defines.push_back("KP_BINDLESS_TEXTURE_TABLE_BINDING 0");
        }

        void AddTargetApiDefine(std::vector<std::string> &defines, GraphicsAPIType api)
        {
            if (api == GraphicsAPIType::GRAPHICS_API_VULKAN)
            {
                defines.push_back("KP_GRAPHICS_API_VULKAN 1");
            }
            else if (api == GraphicsAPIType::GRAPHICS_API_OPENGL)
            {
                defines.push_back("KP_GRAPHICS_API_OPENGL 1");
            }
        }
    }


    ShaderProcessor::ShaderProcessor()
    {
    }
    ShaderProcessor::~ShaderProcessor()
    {
    }

    void ShaderProcessor::Initialize(GraphicsAPIType api_type)
    {
        api_ = api_type;
        // OpenGL's artifact is the assembled GLSL source, produced cheaply, so it
        // skips the content-addressed cache; Vulkan's artifact is SPIR-V and goes
        // through it. keep_source_ drives both the operation built and the field
        // the write-back fills — no API checks scattered through Process.
        keep_source_ = (api_type == GraphicsAPIType::GRAPHICS_API_OPENGL);

        operations_.clear();
        if (keep_source_)
        {
            operations_.push_back(std::make_unique<PreprocessOperation>());
        }
        else
        {
            operations_.push_back(std::make_unique<SPIRVCompiler>());
        }
        for (auto &operation : operations_)
        {
            operation->Initialize(api_);
        }
    }

    void ShaderProcessor::Process(ShaderCache *cache,
                                  const std::vector<std::shared_ptr<asset::ShaderResource>> &assets,
                                  ShaderProcessObserver observer)
    {
        if (!cache)
        {
            return;
        }

        const int total = static_cast<int>(assets.size());
        int done = 0;
        const bool has_observer = static_cast<bool>(observer);

        for (const auto &shader : assets)
        {
            // The pipeline's data carrier: identity in, artifact out. Built once
            // per shader and threaded through every stage (preprocess -> compile
            // -> ...), each stage reading what it needs and writing what it
            // produces for the next one.
            ShaderProcessContext context;
            context.file_name = shader->desc.file;
            context.source = ReadText(context.file_name);
            context.stage = shader->desc.stage;
            context.format = shader->format;
            context.defines = shader->desc.defines;
            AddBindlessTextureTableDefines(context.defines);
            AddTargetApiDefine(context.defines, api_);

            const std::string stage_str = std::string(magic_enum::enum_name(shader->desc.stage));
            const uint64_t hash = GenerateShaderHash(context.source, stage_str, shader->desc.entry, context.defines);

            if (has_observer && !operations_.empty())
            {
                observer(operations_.front()->GetPhase(), done, total, shader.get());
            }

            if (!keep_source_ && cache->Has(hash))
            {
                KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s has been cached", context.file_name.c_str());
                context.byte_code = cache->Load(hash);
            }
            else
            {
                shader->status = asset::ShaderStatus::Compiling;
                bool ok = true;
                for (auto &operation : operations_)
                {
                    if (!operation->Run(context))
                    {
                        // One broken shader must not kill the whole batch (e.g. a
                        // warmup load). Mark it failed and move on.
                        KP_LOG("ShaderProcessorLog", LOG_LEVEL_ERROR, "%s %s failed",
                               context.file_name.c_str(), operation->GetName());
                        shader->status = asset::ShaderStatus::CompileFailed;
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                {
                    ++done;
                    continue;
                }
                if (!keep_source_)
                {
                    cache->Save(hash, context.byte_code);
                    KP_LOG("ShaderProcessorLog", LOG_LEVEL_DEBUG, "%s ready to cache", context.file_name.c_str());
                }
            }

            // Write back. The GL artifact is the assembled source, the Vulkan
            // artifact the SPIR-V bytes; keep_source_ picks the field.
            if (!shader->data)
            {
                shader->data = std::make_shared<data::ShaderData>();
            }
            if (keep_source_)
            {
                shader->data->source = std::move(context.source);
            }
            else
            {
                shader->data->byte_code = std::move(context.byte_code);
            }
            shader->data->stage = context.stage;
            shader->data->entry = shader->desc.entry;
            shader->data->api = api_;
            shader->status = asset::ShaderStatus::Ready;
            // Reference-based tally: same content hash (same stage/entry/defines)
            // is one shader, however many programs reference it.
            processed_hashes_.insert(hash);
            ++done;
        }
    }
}
