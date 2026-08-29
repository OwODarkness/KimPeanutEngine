#include "pipeline_validation.h"

#include <unordered_set>

#include "data/shader.h"

namespace kpengine::graphics
{
    namespace
    {
        bool Fail(std::string *error, const char *message)
        {
            if (error)
            {
                *error = message;
            }
            return false;
        }

        bool ValidateShader(const data::ShaderData *shader, ShaderStage expected_stage,
                            GraphicsAPIType api, std::string *error)
        {
            if (!shader)
            {
                return Fail(error, "required shader is missing");
            }
            if (shader->stage != expected_stage)
            {
                return Fail(error, "shader stage does not match its pipeline slot");
            }
            if (shader->api != api)
            {
                return Fail(error, "shader artifact was baked for a different graphics API");
            }
            if (api == GraphicsAPIType::GRAPHICS_API_VULKAN &&
                (shader->byte_code.empty() || shader->byte_code.size() % sizeof(uint32_t) != 0))
            {
                return Fail(error, "Vulkan shader artifact must contain whole SPIR-V words");
            }
            if (api == GraphicsAPIType::GRAPHICS_API_OPENGL && shader->source.empty())
            {
                return Fail(error, "OpenGL shader artifact is empty");
            }
            return true;
        }
    }

    bool ValidatePipelineDesc(const PipelineDesc &desc, GraphicsAPIType api,
                              std::string *error)
    {
        if (api != GraphicsAPIType::GRAPHICS_API_VULKAN &&
            api != GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            return Fail(error, "pipeline validation requires a concrete graphics API");
        }
        if (!ValidateShader(desc.vert_shader, ShaderStage::SHADER_STAGE_VERTEX, api, error) ||
            !ValidateShader(desc.frag_shader, ShaderStage::SHADER_STAGE_FRAGMENT, api, error))
        {
            return false;
        }
        if (desc.geom_shader &&
            !ValidateShader(desc.geom_shader, ShaderStage::SHADER_STAGE_GEOMETRY, api, error))
        {
            return false;
        }
        // A pipeline may be depth-only (no color attachments) or color-only
        // (UNKNOW depth); only a pipeline with neither is invalid.
        if (desc.color_attachment_formats.empty() &&
            desc.depth_attachment_format == TextureFormat::TEXTURE_FORMAT_UNKNOW)
        {
            return Fail(error, "pipeline requires a color or depth attachment format");
        }

        std::unordered_set<uint32_t> vertex_bindings;
        for (const VertexBindingDesc &binding : desc.binding_descs)
        {
            if (binding.stride == 0 || !vertex_bindings.insert(binding.binding).second)
            {
                return Fail(error, "vertex bindings require unique non-zero strides");
            }
        }
        std::unordered_set<uint32_t> attribute_locations;
        for (const VertexAttributionDesc &attribute : desc.attri_descs)
        {
            if (vertex_bindings.find(attribute.binding) == vertex_bindings.end() ||
                !attribute_locations.insert(attribute.location).second)
            {
                return Fail(error, "vertex attributes require an existing binding and unique location");
            }
        }
        for (const std::vector<DescriptorBindingDesc> &set : desc.descriptor_binding_descs)
        {
            std::unordered_set<uint32_t> bindings;
            for (const DescriptorBindingDesc &binding : set)
            {
                if (binding.descriptor_count == 0 ||
                    binding.stage_flag == ShaderStage::SHADER_STAGE_UNKNOW ||
                    !bindings.insert(binding.binding).second)
                {
                    return Fail(error, "descriptor bindings require unique indices, a stage, and a non-zero count");
                }
            }
        }
        return true;
    }
}
