#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_MODEL_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_MODEL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "common.h"
#include "data/mesh.h"
#include "asset_product.h"
#include "spatial/aabb.h"

namespace kpengine::asset
{
    // V3 is the compact runtime profile. V1/V2 remain readable so existing
    // archive products can be used until their source is reimported.
    constexpr std::uint16_t kNativeModelVersion = 3;
    constexpr std::uint32_t kNativeModelFeatures = 1;
    constexpr std::size_t kNativeModelCompactVertexStride = 24;
    constexpr std::size_t kNativeModelHeaderSize = 88;
    constexpr std::size_t kNativeModelChunkEntrySize = 32;
    constexpr std::size_t kNativeModelDigestOffset = 40;
    constexpr std::size_t kNativeModelDigestSize = kModelArchiveHashSize;
    constexpr std::size_t kNativeModelMaxBytes = 512u * 1024u * 1024u;
    constexpr std::uint32_t kNativeModelMaxVertices = 16u * 1024u * 1024u;
    constexpr std::uint32_t kNativeModelMaxIndices = 48u * 1024u * 1024u;
    constexpr std::uint32_t kNativeModelMaxSections = 16u * 1024u * 1024u;
    constexpr std::uint32_t kNativeModelMaxMaterials = 65535u;

    enum class NativeModelChunkType : std::uint32_t
    {
        Vertices = 1,
        Indices = 2,
        Sections = 3,
        Bounds = 4,
        MaterialReferences = 5,
    };

    struct NativeModelMaterialReference
    {
        AssetType asset_type = AssetType::KPAT_Material;
        ContentHash content_hash{};

        bool operator==(const NativeModelMaterialReference &rhs) const noexcept
        {
            return asset_type == rhs.asset_type && content_hash == rhs.content_hash;
        }
    };

    struct NativeModelData
    {
        std::vector<data::Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<data::MeshSection> sections;
        spatial::AABB local_bounds{};
        std::vector<NativeModelMaterialReference> material_references;

        bool operator==(const NativeModelData &rhs) const noexcept;
    };

    enum class NativeModelErrorCode : std::uint8_t
    {
        InvalidArgument,
        IoError,
        Truncated,
        UnsupportedVersion,
        UnsupportedFeatures,
        Overflow,
        InvalidChunkTable,
        InvalidValue,
        IntegrityMismatch,
        UnsupportedReference,
    };

    class NativeModelError final : public std::runtime_error
    {
    public:
        NativeModelError(NativeModelErrorCode code, std::string message);

        NativeModelErrorCode Code() const noexcept;

    private:
        NativeModelErrorCode code_{};
    };

    struct NativeModelProduct
    {
        NativeModelData data;
        ContentHash integrity_digest{};
        ContentHash product_hash{};

        // Decode measurements are populated by DeserializeNativeModel. They
        // describe the on-disk chunks and the decoded payload, not allocator
        // overhead or GPU residency.
        std::uint16_t format_version{};
        std::uint32_t format_features{};
        std::uint32_t vertex_stride{};
        std::uint32_t index_stride{};
        std::size_t product_bytes{};
        std::size_t decoded_payload_bytes{};
    };

    // Serializes only explicit fields. The returned bytes are canonical and
    // include the integrity digest in the fixed header.
    std::vector<std::byte> SerializeNativeModel(const NativeModelData &data);

    // Validates the complete product before allocating payload vectors.
    NativeModelProduct DeserializeNativeModel(
        const std::vector<std::byte> &bytes,
        const ContentHashPair *verified_hashes = nullptr);

    // Validates only the bounded container structure and integrity digest.
    // Unlike DeserializeNativeModel, this never allocates decoded payloads.
    void ValidateNativeModelProductStructure(
        const std::vector<std::byte> &bytes,
        const ContentHashPair *verified_hashes = nullptr);

    ContentHash ComputeNativeModelProductHash(const std::vector<std::byte> &bytes);
}

#endif
