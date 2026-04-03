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
        KPAT_ShaderMeta,
        KPAT_Mesh,
    };

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