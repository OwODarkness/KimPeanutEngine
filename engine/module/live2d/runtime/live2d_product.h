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

    struct Live2DHitAreaDefinition
    {
        std::string name;
        std::string drawable_id;
    };

    struct Live2DUserDataEntry
    {
        std::string target_type;
        std::string target_id;
        std::string value;
    };

    struct Live2DSecondaryBehaviorData
    {
        std::vector<std::byte> physics_bytes;
        std::vector<std::byte> pose_bytes;
        std::vector<Live2DHitAreaDefinition> hit_areas;
        std::vector<Live2DUserDataEntry> user_data;
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
        Live2DSecondaryBehaviorData secondary_behavior;
    };

    // Product V1/V2 remain readable; Product V3 appends typed secondary
    // behavior data after the existing animation sections.
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
