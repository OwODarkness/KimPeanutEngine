#include "live2d_asset_runtime.h"

#include <filesystem>
#include <memory>

#include "live2d_model_resource.h"
#include "live2d_product.h"

namespace kpengine::live2d
{
    bool LoadLive2DProduct(const std::string &path, asset::AssetRegisterInfo &info)
    {
        Live2DProductData product;
        std::string diagnostic;
        if (!ReadLive2DProduct(path, product, diagnostic))
        {
            return false;
        }

        const std::filesystem::path product_path{path};
        for (const Live2DTextureDependency &texture : product.textures)
        {
            const std::filesystem::path reference{texture.path};
            if (reference.empty() || reference.is_absolute() || reference.has_root_name() ||
                reference.has_root_directory())
            {
                return false;
            }
            const std::filesystem::path dependency =
                (product_path.parent_path() / reference).lexically_normal();
            info.dependency_requests.push_back(
                {dependency.generic_string(), asset::AssetType::KPAT_Texture});
        }

        auto immutable_product =
            std::make_shared<const Live2DProductData>(std::move(product));
        info.type = kLive2DModelAssetType;
        info.path = path;
        info.name = "Live2D" + product_path.stem().string();
        info.resource = std::make_shared<Live2DModelResource>(std::move(immutable_product));
        return true;
    }
}
