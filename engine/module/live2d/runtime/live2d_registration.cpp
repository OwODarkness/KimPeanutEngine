#include "live2d_registration.h"

#include "live2d_asset_runtime.h"
#include "live2d_model_resource.h"

#include <utility>

namespace kpengine::live2d
{
    bool RegisterLive2DAssetTypes(asset::AssetManager &manager,
                                  std::string &diagnostic)
    {
        asset::AssetTypeDescriptor descriptor{};
        descriptor.type = kLive2DModelAssetType;
        descriptor.name = "Live2DModel";
        descriptor.extensions = {"live2d"};
        descriptor.loader = &LoadLive2DProduct;
        return manager.RegisterAssetType(std::move(descriptor), diagnostic);
    }
}
