#ifndef KPENGINE_RUNTIME_ASSET_MATERIAL_H
#define KPENGINE_RUNTIME_ASSET_MATERIAL_H

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace kpengine::asset
{
    enum class MaterialShadingModel : uint8_t
    {
        Unlit,
        StandardPbr,
    };

    enum class MaterialBlendMode : uint8_t
    {
        Opaque,
        AlphaBlend,
    };

    enum class MaterialCullMode : uint8_t
    {
        None,
        Back,
        Front,
    };

    struct MaterialSurfaceSource
    {
        MaterialShadingModel shading_model = MaterialShadingModel::Unlit;
        MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
        MaterialCullMode cull_mode = MaterialCullMode::Back;
        bool double_sided = false;
    };

    enum class MaterialParameterSourceType : uint8_t
    {
        Scalar,
        Vector4,
        Texture,
    };

    // Texture references are material-relative paths at the Asset boundary.
    // Render resolves them to AssetIDs only when it builds a material instance.
    using MaterialParameterSourceValue = std::variant<float, std::array<float, 4>, std::string>;

    struct MaterialParameterSource
    {
        std::string name;
        MaterialParameterSourceType type = MaterialParameterSourceType::Scalar;
        MaterialParameterSourceValue value = 0.0f;
        uint32_t dependency_index = std::numeric_limits<uint32_t>::max();
    };

    // Parsed, API-neutral authoring data from one .material file. It contains
    // no render handles, resolved AssetIDs, or graphics implementation state.
    struct MaterialResource
    {
        int version = 1;
        std::string shader_path;
        uint32_t shader_dependency_index = std::numeric_limits<uint32_t>::max();
        MaterialSurfaceSource surface;
        std::vector<MaterialParameterSource> parameters;
    };
}

#endif
