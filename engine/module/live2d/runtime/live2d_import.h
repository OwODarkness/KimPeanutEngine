#ifndef KPENGINE_LIVE2D_IMPORT_H
#define KPENGINE_LIVE2D_IMPORT_H

#include <vector>

#include "asset/asset_import_registry.h"
#include "live2d_product.h"

namespace kpengine::live2d
{
    struct Live2DImportProduct
    {
        Live2DProductData product;
        std::vector<std::byte> product_bytes;
    };

    // Registers the .model3.json source provider. The provider is database-
    // free and returns a deterministic product; archive publication remains a
    // caller/tool concern.
    bool RegisterLive2DImporters(asset::ImportProviderRegistry &registry,
                                 std::string &diagnostic);
}

#endif
