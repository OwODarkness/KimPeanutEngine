#ifndef KPENGINE_RUNTIME_ASSET_COMMON_H
#define KPENGINE_RUNTIME_ASSET_COMMON_H

#include <cstdint>
#include "base/handle.h"

namespace kpengine::asset
{
    enum class AssetType : uint16_t
    {
        Undefined,
        KPAT_Model,
        KPAT_Texture,
        KPAT_Audio,
        KPAT_Shader,
        KPAT_ShaderProgram,
        KPAT_Mesh,
        KPAT_Material,
        KPAT_Level,
    };

    // AssetID reserves the low range for Asset-core types and a separate range
    // for feature-module types. Existing values are append-only and must not
    // be renumbered because AssetID packs the type into its serialized form.
    inline constexpr uint16_t kFirstBuiltInAssetTypeValue = 1u;
    inline constexpr uint16_t kLastBuiltInAssetTypeValue = 0x0fffu;
    inline constexpr uint16_t kFirstCustomAssetTypeValue = 0x1000u;
    inline constexpr uint16_t kLastCustomAssetTypeValue = 0xefffu;

    constexpr bool IsBuiltInAssetType(AssetType type) noexcept
    {
        switch (type)
        {
        case AssetType::KPAT_Model:
        case AssetType::KPAT_Texture:
        case AssetType::KPAT_Audio:
        case AssetType::KPAT_Shader:
        case AssetType::KPAT_ShaderProgram:
        case AssetType::KPAT_Mesh:
        case AssetType::KPAT_Material:
        case AssetType::KPAT_Level:
            return true;
        case AssetType::Undefined:
            return false;
        }
        return false;
    }

    constexpr bool IsCustomAssetType(AssetType type) noexcept
    {
        const uint16_t value = static_cast<uint16_t>(type);
        return value >= kFirstCustomAssetTypeValue &&
               value <= kLastCustomAssetTypeValue;
    }

    constexpr bool IsAssetTypeValueInExtensionRange(AssetType type) noexcept
    {
        return IsBuiltInAssetType(type) || IsCustomAssetType(type);
    }

    enum class ModelGeometryType : uint8_t
    {
        KPMG_Mesh,
        KPMG_Pointcloud
    };

    struct AssetID
    {
        uint32_t id;
        uint16_t generation;
        AssetType type;

        AssetID() :  id(KPENGINE_NULL_HANDLE), type(AssetType::Undefined),generation(0)
        {
        }

        AssetID(uint32_t in_id, uint16_t in_generation, AssetType in_type) :  id(in_id), generation(in_generation), type(in_type)
        {
        }

        bool IsValid() const
        {
            return id != KPENGINE_NULL_HANDLE;
        }

        bool operator==(const AssetID &rhs) const
        {
            return (type == rhs.type) && (id == rhs.id) && (generation == rhs.generation);
        }

            uint64_t Pack() const
    {
        return (static_cast<uint64_t>(type) << 48) |
               (static_cast<uint64_t>(generation) << 32) |
               static_cast<uint64_t>(id);
    }

    static AssetID Unpack(uint64_t packed)
    {
        AssetID result;
        result.type       = static_cast<AssetType>((packed >> 48) & 0xFFFF);
        result.generation = static_cast<uint16_t>((packed >> 32) & 0xFFFF);
        result.id         = static_cast<uint32_t>(packed & 0xFFFFFFFF);
        return result;
    }
    };


}

#endif
