#ifndef KPENGINE_LIVE2D_PRODUCT_H
#define KPENGINE_LIVE2D_PRODUCT_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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

    struct Live2DAuthoredMotion
    {
        std::string group;
        std::uint32_t index = 0;
        bool has_fade_in = false;
        double fade_in_time = 0.0;
        bool has_fade_out = false;
        double fade_out_time = 0.0;
        std::vector<std::byte> motion_bytes;
        bool has_sound = false;
        std::vector<std::byte> sound_bytes;
    };

    struct Live2DAuthoredExpression
    {
        std::string name;
        std::vector<std::byte> expression_bytes;
    };

    struct Live2DParameterGroup
    {
        std::string target;
        std::string name;
        std::vector<std::string> ids;
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
        std::vector<Live2DAuthoredMotion> motions;
        std::vector<Live2DAuthoredExpression> expressions;
        std::vector<Live2DParameterGroup> parameter_groups;
    };

    // Product V1 keeps the original layout. Product V2 adds typed authored
    // animation sections after the V1 header fields and remains V1-readable.
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
