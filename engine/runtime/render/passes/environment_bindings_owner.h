#ifndef KPENGINE_RUNTIME_RENDER_PASSES_ENVIRONMENT_BINDINGS_OWNER_H
#define KPENGINE_RUNTIME_RENDER_PASSES_ENVIRONMENT_BINDINGS_OWNER_H

#include <optional>

#include "render/environment_frame_bindings.h"
#include "render/environment_source.h"

namespace kpengine::render
{
    class PreparedRenderAssetCatalog;
    class RenderResourceResolver;

    // Resolves CPU environment assets to borrowed render bindings. The resolver
    // remains the owner of the underlying GPU textures and samplers.
    class EnvironmentBindingsOwner final
    {
    public:
        void Update(const std::optional<EnvironmentSourceDesc> &source,
                    const std::optional<EnvironmentSourceHandle> &source_handle,
                    RenderResourceResolver &resolver,
                    const PreparedRenderAssetCatalog &prepared_assets);
        bool EnsureFallback(RenderResourceResolver &resolver);
        void Clear() noexcept;
        const EnvironmentFrameBindings &Active() const noexcept { return active_; }

    private:
        bool ResolveLevelEnvironment(const EnvironmentSourceDesc &source,
                                     RenderResourceResolver &resolver,
                                     const PreparedRenderAssetCatalog &prepared_assets,
                                     EnvironmentFrameBindings &bundle);
        bool PrepareEnvironmentIbl(asset::AssetID source_asset,
                                   RenderResourceResolver &resolver,
                                   const PreparedRenderAssetCatalog &prepared_assets,
                                   EnvironmentFrameBindings &bundle);

        EnvironmentFrameBindings level_;
        EnvironmentFrameBindings active_;
        std::optional<EnvironmentSourceHandle> failed_source_;
    };
}

#endif
