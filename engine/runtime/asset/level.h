#ifndef KPENGINE_RUNTIME_ASSET_LEVEL_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_LEVEL_RESOURCE_H

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "common.h"
#include "math/math_header.h"

namespace kpengine::asset
{
    inline constexpr uint32_t kInvalidLevelDependencyIndex =
        std::numeric_limits<uint32_t>::max();

    struct LevelAssetReference
    {
        std::string path;
        AssetType expected_type = AssetType::Undefined;
        uint32_t dependency_index = kInvalidLevelDependencyIndex;
    };

    struct LevelTransform
    {
        Vector3f position{};
        Vector3f rotation_degrees{};
        Vector3f scale{1.0f};
    };

    enum class LevelProjection : uint8_t
    {
        Perspective,
        Orthographic,
    };

    struct LevelStaticMeshRecord
    {
        std::string id;
        std::string name;
        LevelTransform transform{};
        LevelAssetReference model;
        LevelAssetReference material;
        bool visible = true;
        bool casts_shadow = true;
        int lod_bias = 0;
    };

    struct LevelDirectionalLightRecord
    {
        std::string id;
        std::string name;
        Vector3f direction{};
        Vector3f color{1.0f};
        float intensity = 1.0f;
        bool enabled = true;
        bool casts_shadow = true;
    };

    struct LevelPointLightRecord
    {
        std::string id;
        std::string name;
        Vector3f position{};
        Vector3f color{1.0f};
        float intensity = 1.0f;
        float range = 1.0f;
        bool enabled = true;
        bool casts_shadow = true;
    };

    struct LevelSpotLightRecord
    {
        std::string id;
        std::string name;
        Vector3f position{};
        Vector3f direction{};
        Vector3f color{1.0f};
        float intensity = 1.0f;
        float range = 1.0f;
        float inner_cone_radians = 0.0f;
        float outer_cone_radians = 0.0f;
        bool enabled = true;
        bool casts_shadow = true;
    };

    struct LevelCameraRecord
    {
        std::string id;
        std::string name;
        LevelTransform transform{};
        LevelProjection projection = LevelProjection::Perspective;
        float near_plane = 0.1f;
        float far_plane = 2000.0f;
        float field_of_view_degrees = 45.0f;
        float orthographic_height = 10.0f;
        bool enabled = true;
        int priority = 0;
    };

    using LevelObject = std::variant<LevelStaticMeshRecord,
                                     LevelDirectionalLightRecord,
                                     LevelPointLightRecord,
                                     LevelSpotLightRecord,
                                     LevelCameraRecord>;

    struct LevelEnvironmentRecord
    {
        LevelAssetReference texture;
        float ibl_intensity = 0.25f;
    };

    // Immutable-by-convention CPU authoring data. Runtime instantiation and
    // dependency IDs are deliberately kept outside this payload.
    struct LevelResource
    {
        int version = 1;
        std::optional<LevelEnvironmentRecord> environment;
        std::vector<LevelObject> objects;
    };
}

#endif
