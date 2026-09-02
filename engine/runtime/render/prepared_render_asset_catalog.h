#ifndef KPENGINE_RUNTIME_RENDER_PREPARED_RENDER_ASSET_CATALOG_H
#define KPENGINE_RUNTIME_RENDER_PREPARED_RENDER_ASSET_CATALOG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "asset/asset.h"
#include "asset/common.h"
#include "base/type.h"
#include "resource/environment_ibl_processor.h"

namespace kpengine::runtime
{
    class RenderAssetPreparer;
}

namespace kpengine::render
{
    enum class BuiltInRenderAsset : uint8_t
    {
        DeferredLightingProgram,
        GBufferDebugProgram,
        ToneMapProgram,
        CaptureViewProgram,
        DirectionalShadowProgram,
        DefaultWhiteTexture,
        DefaultFlatNormalTexture,
        Count,
    };

    struct BuiltInRenderAssetRequirement
    {
        BuiltInRenderAsset role;
        const char *relative_path;
        asset::AssetType expected_type;
    };

    const std::array<BuiltInRenderAssetRequirement,
                     static_cast<size_t>(BuiltInRenderAsset::Count)> &
    GetBuiltInRenderAssetRequirements();

    using PreparedRenderPayload = std::variant<asset::MeshPtr, asset::TexturePtr,
                                               asset::ShaderPtr, asset::ShaderProgramPtr,
                                               asset::MaterialPtr>;

    struct PreparedRenderAssetRecord
    {
        asset::AssetID id;
        PreparedRenderPayload payload;
        std::vector<asset::AssetID> dependencies;
    };

    struct PreparedEnvironmentIbl
    {
        asset::AssetID source_texture;
        resource::EnvironmentIblData data;
    };

    struct PreparedRenderAssetCatalogBuild
    {
        GraphicsAPIType graphics_api = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        std::vector<PreparedRenderAssetRecord> records;
        std::array<asset::AssetID, static_cast<size_t>(BuiltInRenderAsset::Count)> built_ins{};
        std::vector<PreparedEnvironmentIbl> environment_ibl;
        std::size_t prepared_shader_count = 0;
    };

    class PreparedRenderAssetCatalog final
    {
    public:
        static std::optional<PreparedRenderAssetCatalog> Create(
            PreparedRenderAssetCatalogBuild build, std::string &diagnostic);

        template <typename T>
        std::shared_ptr<const T> Get(asset::AssetID id) const
        {
            const auto record = records_.find(id.Pack());
            if (record == records_.end() || !(record->second.id == id))
            {
                return nullptr;
            }
            const auto *payload = std::get_if<std::shared_ptr<T>>(&record->second.payload);
            return payload != nullptr ? *payload : nullptr;
        }

        template <typename T>
        std::shared_ptr<T> Get(asset::AssetID id)
        {
            const auto record = records_.find(id.Pack());
            if (record == records_.end() || !(record->second.id == id))
            {
                return nullptr;
            }
            auto *payload = std::get_if<std::shared_ptr<T>>(&record->second.payload);
            return payload != nullptr ? *payload : nullptr;
        }

        asset::AssetID GetBuiltIn(BuiltInRenderAsset role) const noexcept;
        asset::AssetID ResolveDependency(asset::AssetID owner, size_t index,
                                         asset::AssetType expected_type) const noexcept;
        const PreparedEnvironmentIbl *FindEnvironmentIbl(asset::AssetID source_texture) const;
        std::size_t GetPreparedShaderCount() const noexcept { return prepared_shader_count_; }

    private:
        friend class runtime::RenderAssetPreparer;

        std::unordered_map<uint64_t, PreparedRenderAssetRecord> records_;
        std::array<asset::AssetID, static_cast<size_t>(BuiltInRenderAsset::Count)> built_ins_{};
        std::unordered_map<uint64_t, PreparedEnvironmentIbl> environment_ibl_;
        std::size_t prepared_shader_count_ = 0;
    };
}

#endif
