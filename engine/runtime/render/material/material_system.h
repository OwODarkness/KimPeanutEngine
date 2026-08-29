#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_MATERIAL_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_MATERIAL_SYSTEM_H

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "asset/common.h"
#include "base/handle.h"
#include "math/math_header.h"

namespace kpengine::render
{
    struct MaterialTemplateTag
    {
    };

    struct MaterialInstanceTag
    {
    };

    using MaterialTemplateHandle = Handle<MaterialTemplateTag>;
    using MaterialInstanceHandle = Handle<MaterialInstanceTag>;

    enum class MaterialDomain : uint8_t
    {
        Surface,
    };

    enum class MaterialBlendMode : uint8_t
    {
        Opaque,
        AlphaBlend,
    };

    enum class MaterialCullMode : uint8_t
    {
        Back,
        Front,
    };

    enum class MaterialShadingModel : uint8_t
    {
        Unlit,
        StandardPbr,
    };

    enum class MaterialSamplerFilter : uint8_t
    {
        Nearest,
        Linear,
    };

    enum class MaterialSamplerAddressMode : uint8_t
    {
        Repeat,
        ClampToEdge,
    };

    // Render describes sampler intent; M3 translates it to the common RHI
    // sampler settings and resolves the resulting handle through RenderSystem.
    struct MaterialSamplerDesc
    {
        MaterialSamplerFilter min_filter = MaterialSamplerFilter::Linear;
        MaterialSamplerFilter mag_filter = MaterialSamplerFilter::Linear;
        MaterialSamplerAddressMode address_u = MaterialSamplerAddressMode::Repeat;
        MaterialSamplerAddressMode address_v = MaterialSamplerAddressMode::Repeat;
        MaterialSamplerAddressMode address_w = MaterialSamplerAddressMode::Repeat;
    };

    // Whether a texture is sampled as sRGB (hardware-linearized on fetch) or as
    // raw linear data. Render expresses intent; the resolver maps it to the GPU
    // texture format. Base-color maps are sRGB; PBR scalar/normal maps are
    // linear and must never be sRGB-decoded.
    enum class MaterialTextureColorSpace : uint8_t
    {
        Srgb,
        Linear,
    };

    struct MaterialTextureSamplerValue
    {
        asset::AssetID texture_asset;
        MaterialSamplerDesc sampler;
        MaterialTextureColorSpace color_space = MaterialTextureColorSpace::Srgb;
    };

    // The variant is the source of truth for value type. This removes the M1
    // manual tag + unused-fields representation.
    using MaterialParameterValue =
        std::variant<float, Vector4f, MaterialTextureSamplerValue>;

    struct MaterialParameterDesc
    {
        std::string name;
        MaterialParameterValue default_value = 0.0f;
        // Texture parameters opt into a concrete descriptor binding. Constants
        // are packed into the common material-constant binding by FrameContext.
        std::optional<uint32_t> resource_binding;
    };

    using MaterialParameterLayout = std::vector<MaterialParameterDesc>;

    struct MaterialParameterID
    {
        uint32_t value = std::numeric_limits<uint32_t>::max();

        bool IsValid() const { return value != std::numeric_limits<uint32_t>::max(); }
        bool operator==(const MaterialParameterID &rhs) const { return value == rhs.value; }
    };

    struct MaterialParameterOverride
    {
        MaterialParameterID parameter_id;
        MaterialParameterValue value;
    };

    enum class MaterialPass : uint8_t
    {
        Scene,
        ShadowDepth, // reserved for the D4 directional shadow pass family
        GBuffer,
    };

    struct MaterialPipelineState
    {
        MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
        MaterialCullMode cull_mode = MaterialCullMode::Back;
        bool double_sided = false;
    };

    struct MaterialTemplateDesc
    {
        asset::AssetID shader_program;
        MaterialDomain domain = MaterialDomain::Surface;
        MaterialShadingModel shading_model = MaterialShadingModel::StandardPbr;
        MaterialPipelineState pipeline_state;
        // Serialized pipeline metadata: the shader program follows the V1
        // sampled-texture-table ABI and consumes texture indices rather than
        // per-draw texture bindings when the backend supports it. Both variants
        // are entries of shader_program; Render selects one by capability.
        bool bindless_texture_table_compatible = false;
        MaterialParameterLayout parameters;
        std::vector<MaterialPass> compatible_passes{MaterialPass::Scene};
    };

    struct MaterialInstanceDesc
    {
        MaterialTemplateHandle template_handle;
        std::vector<MaterialParameterOverride> overrides;
    };

    enum class MaterialResourceState : uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    struct MaterialResolution
    {
        MaterialResourceState state = MaterialResourceState::Pending;
        std::string diagnostic;
    };

    enum class MaterialDrawClass : uint8_t
    {
        Opaque,
        AlphaBlend,
    };

    class IMaterialResourceResolver
    {
    public:
        virtual ~IMaterialResourceResolver() = default;
        virtual MaterialResolution ResolveTemplate(MaterialTemplateHandle handle,
                                                    const MaterialTemplateDesc &desc) = 0;
        virtual MaterialResolution ResolveInstance(MaterialInstanceHandle handle,
            const MaterialTemplateDesc &desc,
            const std::vector<MaterialParameterValue> &effective_values) = 0;
        virtual void ReleaseTemplate(MaterialTemplateHandle handle) = 0;
        virtual void ReleaseInstance(MaterialInstanceHandle handle) = 0;
    };

    // MaterialSystem owns logical material identities and immutable template
    // descriptors. It intentionally has no Graphics or backend dependency.
    class MaterialSystem
    {
    public:
        void SetResourceResolver(IMaterialResourceResolver *resolver);
        void RefreshResources();
        MaterialTemplateHandle CreateTemplate(const MaterialTemplateDesc &desc);
        bool DestroyTemplate(MaterialTemplateHandle handle);
        const MaterialTemplateDesc *FindTemplate(MaterialTemplateHandle handle) const;
        bool IsTemplateValid(MaterialTemplateHandle handle) const;
        MaterialResolution GetTemplateResolution(MaterialTemplateHandle handle) const;

        MaterialParameterID FindParameterID(MaterialTemplateHandle template_handle,
                                            std::string_view name) const;

        MaterialInstanceHandle CreateInstance(const MaterialInstanceDesc &desc);
        bool UpdateInstance(MaterialInstanceHandle handle,
                            const std::vector<MaterialParameterOverride> &overrides);
        bool DestroyInstance(MaterialInstanceHandle handle);
        MaterialTemplateHandle GetInstanceTemplate(MaterialInstanceHandle handle) const;
        const MaterialParameterValue *GetParameterValue(MaterialInstanceHandle instance_handle,
                                                        MaterialParameterID parameter_id) const;
        bool IsInstanceValid(MaterialInstanceHandle handle) const;
        MaterialResolution GetInstanceResolution(MaterialInstanceHandle handle) const;
        std::optional<MaterialDrawClass> GetDrawClass(MaterialInstanceHandle handle) const;

    private:
        struct MaterialTemplateRecord
        {
            MaterialTemplateHandle handle;
            MaterialTemplateDesc desc;
            std::unordered_map<std::string, MaterialParameterID> parameter_ids;
            MaterialResolution resolution;
            uint32_t instance_count = 0;
        };

        struct MaterialInstanceRecord
        {
            MaterialInstanceHandle handle;
            MaterialTemplateHandle template_handle;
            std::unordered_map<uint32_t, MaterialParameterValue> overrides;
            MaterialResolution resolution;
        };

        bool IsTemplateDescValid(const MaterialTemplateDesc &desc) const;
        bool IsParameterValueValid(const MaterialParameterValue &value) const;
        bool AreOverridesValid(const MaterialTemplateRecord &template_record,
                               const std::vector<MaterialParameterOverride> &overrides) const;
        std::vector<MaterialParameterValue> GetEffectiveValues(
            const MaterialTemplateRecord &template_record,
            const MaterialInstanceRecord &instance_record) const;
        void ResolveTemplate(MaterialTemplateHandle handle, MaterialTemplateRecord &record);
        void ResolveInstance(MaterialInstanceHandle handle,
                             const MaterialTemplateRecord &template_record,
                             MaterialInstanceRecord &record);

        HandleSystem<MaterialTemplateHandle> template_handles_;
        HandleSystem<MaterialInstanceHandle> instance_handles_;
        std::unordered_map<uint32_t, MaterialTemplateRecord> templates_;
        std::unordered_map<uint32_t, MaterialInstanceRecord> instances_;
        IMaterialResourceResolver *resource_resolver_ = nullptr;
    };
}

#endif
