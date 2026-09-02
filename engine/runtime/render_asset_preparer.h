#ifndef KPENGINE_RUNTIME_RENDER_ASSET_PREPARER_H
#define KPENGINE_RUNTIME_RENDER_ASSET_PREPARER_H

#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "asset/asset.h"
#include "asset/common.h"
#include "base/type.h"
#include "render/prepared_render_asset_catalog.h"
#include "resource/environment_ibl_processor.h"

namespace kpengine::runtime
{
    struct RenderAssetPreparationHooks
    {
        std::function<asset::AssetID(const std::string &)> load_sync;
        std::function<asset::Asset *(asset::AssetID)> get_asset;
        std::function<void(const std::vector<asset::ShaderPtr> &)> process_shaders;
        std::function<std::optional<resource::EnvironmentIblData>(const data::TextureData &)>
            process_environment_ibl;
        std::function<std::size_t()> processed_shader_count;
    };

    struct RenderAssetPreparationResult
    {
        std::shared_ptr<const render::PreparedRenderAssetCatalog> catalog;
        std::string diagnostic;

        explicit operator bool() const { return catalog != nullptr; }
    };

    class RenderAssetPreparer final
    {
    public:
        RenderAssetPreparer() = default;
        explicit RenderAssetPreparer(RenderAssetPreparationHooks hooks)
            : hooks_(std::move(hooks))
        {
        }

        RenderAssetPreparationResult Prepare(asset::AssetID level_asset,
                                             GraphicsAPIType api_type) const;

    private:
        RenderAssetPreparationHooks hooks_;
    };
}

#endif
