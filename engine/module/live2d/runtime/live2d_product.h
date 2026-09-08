#ifndef KPENGINE_LIVE2D_PRODUCT_H
#define KPENGINE_LIVE2D_PRODUCT_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kpengine::live2d
{
    struct Live2DTextureDependency
    {
        std::string path;
    };

    struct Live2DOptionalChunk
    {
        std::string name;
        std::vector<std::byte> bytes;
    };

    // Database-free native product data shared by the offline importer and
    // the runtime Asset adapter.
    struct Live2DProductData
    {
        std::uint32_t product_version = 1;
        std::uint32_t model3_version = 3;
        std::vector<std::byte> moc_bytes;
        std::vector<Live2DTextureDependency> textures;
        std::vector<Live2DOptionalChunk> optional_chunks;
    };

    // The first native product is intentionally small and self-describing:
    // fixed-width little-endian fields, exact MOC bytes, ordered texture
    // paths, and named optional source chunks. L2D3 may extend it only by
    // adding a new product version.
    bool SerializeLive2DProduct(const Live2DProductData &product,
                                std::vector<std::byte> &bytes,
                                std::string &diagnostic);

    bool ParseLive2DProduct(const std::vector<std::byte> &bytes,
                            Live2DProductData &product,
                            std::string &diagnostic);

    bool ReadLive2DProduct(const std::filesystem::path &path,
                           Live2DProductData &product,
                           std::string &diagnostic);

}

#endif
