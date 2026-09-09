#include "native_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace kpengine::asset
{
    namespace
    {
        constexpr std::array<std::uint8_t, 8> kMagic{{'K', 'P', 'M', 'O', 'D', 'E', 'L', '1'}};
        constexpr std::size_t kVertexStride = 14u * sizeof(float);
        constexpr std::size_t kIndexStride = sizeof(std::uint32_t);
        constexpr std::size_t kSectionStride = 3u * sizeof(std::uint32_t) +
                                                6u * sizeof(float);
        constexpr std::size_t kBoundsStride = 6u * sizeof(float);
        constexpr std::size_t kMaterialReferenceStride = sizeof(std::uint16_t) * 2u +
                                                           kModelArchiveHashSize;

        struct Chunk
        {
            NativeModelChunkType type{};
            std::uint64_t offset{};
            std::uint64_t byte_size{};
            std::uint32_t element_size{};
            std::uint32_t count{};
        };

        bool IsFinite(float value)
        {
            return std::isfinite(value);
        }

        bool IsZeroHash(const ContentHash &hash)
        {
            return std::all_of(hash.bytes.begin(), hash.bytes.end(), [](std::uint8_t value)
                               { return value == 0; });
        }

        bool CheckedAdd(std::size_t lhs, std::size_t rhs, std::size_t &result)
        {
            if (rhs > std::numeric_limits<std::size_t>::max() - lhs)
            {
                return false;
            }
            result = lhs + rhs;
            return true;
        }

        bool CheckedMultiply(std::size_t lhs, std::size_t rhs, std::size_t &result)
        {
            if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs)
            {
                return false;
            }
            result = lhs * rhs;
            return true;
        }

        void Require(bool condition, NativeModelErrorCode code, const char *message)
        {
            if (!condition)
            {
                throw NativeModelError(code, message);
            }
        }

        void AppendByte(std::vector<std::byte> &bytes, std::uint8_t value)
        {
            bytes.push_back(static_cast<std::byte>(value));
        }

        void AppendU16(std::vector<std::byte> &bytes, std::uint16_t value)
        {
            AppendByte(bytes, static_cast<std::uint8_t>(value));
            AppendByte(bytes, static_cast<std::uint8_t>(value >> 8));
        }

        void AppendU32(std::vector<std::byte> &bytes, std::uint32_t value)
        {
            for (std::size_t shift = 0; shift < 32; shift += 8)
            {
                AppendByte(bytes, static_cast<std::uint8_t>(value >> shift));
            }
        }

        void AppendU64(std::vector<std::byte> &bytes, std::uint64_t value)
        {
            for (std::size_t shift = 0; shift < 64; shift += 8)
            {
                AppendByte(bytes, static_cast<std::uint8_t>(value >> shift));
            }
        }

        void AppendFloat(std::vector<std::byte> &bytes, float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            AppendU32(bytes, bits);
        }

        void AppendHash(std::vector<std::byte> &bytes, const ContentHash &hash)
        {
            for (const std::uint8_t value : hash.bytes)
            {
                AppendByte(bytes, value);
            }
        }

        void WriteAtU64(std::vector<std::byte> &bytes, std::size_t offset, std::uint64_t value)
        {
            Require(offset <= bytes.size() && bytes.size() - offset >= sizeof(value),
                    NativeModelErrorCode::InvalidArgument, "native model patch offset is invalid");
            for (std::size_t shift = 0; shift < 64; shift += 8)
            {
                bytes[offset + shift / 8] = static_cast<std::byte>(value >> shift);
            }
        }

        void WriteAtHash(std::vector<std::byte> &bytes, std::size_t offset, const ContentHash &hash)
        {
            Require(offset <= bytes.size() && bytes.size() - offset >= hash.bytes.size(),
                    NativeModelErrorCode::InvalidArgument, "native model digest offset is invalid");
            for (std::size_t index = 0; index < hash.bytes.size(); ++index)
            {
                bytes[offset + index] = static_cast<std::byte>(hash.bytes[index]);
            }
        }

        std::uint16_t ReadU16(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                   static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8;
        }

        std::uint32_t ReadU32(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            std::uint32_t value = 0;
            for (std::size_t shift = 0; shift < 32; shift += 8)
            {
                value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + shift / 8])) << shift;
            }
            return value;
        }

        std::uint64_t ReadU64(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            std::uint64_t value = 0;
            for (std::size_t shift = 0; shift < 64; shift += 8)
            {
                value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + shift / 8])) << shift;
            }
            return value;
        }

        float ReadFloat(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            const std::uint32_t bits = ReadU32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        ContentHash ReadHash(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            ContentHash hash{};
            for (std::size_t index = 0; index < hash.bytes.size(); ++index)
            {
                hash.bytes[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
            }
            return hash;
        }

        void ValidateVertex(const data::Vertex &vertex)
        {
            const float values[] = {
                vertex.position.x_, vertex.position.y_, vertex.position.z_,
                vertex.normal.x_, vertex.normal.y_, vertex.normal.z_,
                vertex.tex_coord.x_, vertex.tex_coord.y_,
                vertex.tangent.x_, vertex.tangent.y_, vertex.tangent.z_,
                vertex.bitangent.x_, vertex.bitangent.y_, vertex.bitangent.z_};
            for (const float value : values)
            {
                Require(IsFinite(value), NativeModelErrorCode::InvalidValue,
                        "native model contains a non-finite vertex value");
            }
        }

        void ValidateData(const NativeModelData &data)
        {
            Require(data.vertices.size() <= kNativeModelMaxVertices,
                    NativeModelErrorCode::Overflow, "native model vertex count exceeds the limit");
            Require(data.indices.size() <= kNativeModelMaxIndices,
                    NativeModelErrorCode::Overflow, "native model index count exceeds the limit");
            Require(data.sections.size() <= kNativeModelMaxSections,
                    NativeModelErrorCode::Overflow, "native model section count exceeds the limit");
            Require(data.material_references.size() <= kNativeModelMaxMaterials,
                    NativeModelErrorCode::Overflow, "native model material count exceeds the limit");

            for (const data::Vertex &vertex : data.vertices)
            {
                ValidateVertex(vertex);
            }
            for (const std::uint32_t index : data.indices)
            {
                Require(index < data.vertices.size(), NativeModelErrorCode::InvalidValue,
                        "native model index is outside the vertex payload");
            }
            for (const data::MeshSection &section : data.sections)
            {
                std::size_t end = 0;
                Require(CheckedAdd(section.index_start, section.index_count, end) &&
                            end <= data.indices.size() && section.index_count % 3 == 0,
                        NativeModelErrorCode::InvalidValue,
                        "native model section range is invalid");
                Require(data.material_references.empty() ? section.material_index == 0
                                                          : section.material_index < data.material_references.size(),
                        NativeModelErrorCode::InvalidValue,
                        "native model section material slot is invalid");
                const float section_bounds[] = {
                    section.local_bounds.min_.x_, section.local_bounds.min_.y_,
                    section.local_bounds.min_.z_, section.local_bounds.max_.x_,
                    section.local_bounds.max_.y_, section.local_bounds.max_.z_};
                for (const float value : section_bounds)
                {
                    Require(IsFinite(value), NativeModelErrorCode::InvalidValue,
                            "native model contains non-finite section bounds");
                }
                Require(section.local_bounds.IsValid(), NativeModelErrorCode::InvalidValue,
                        "native model section bounds are inverted");
                for (std::size_t index = section.index_start; index < end; ++index)
                {
                    const data::Vertex &vertex = data.vertices[data.indices[index]];
                    Require(vertex.position.x_ >= section.local_bounds.min_.x_ &&
                                vertex.position.x_ <= section.local_bounds.max_.x_ &&
                                vertex.position.y_ >= section.local_bounds.min_.y_ &&
                                vertex.position.y_ <= section.local_bounds.max_.y_ &&
                                vertex.position.z_ >= section.local_bounds.min_.z_ &&
                                vertex.position.z_ <= section.local_bounds.max_.z_,
                            NativeModelErrorCode::InvalidValue,
                            "native model section bounds do not contain indexed vertices");
                }
            }

            const float bounds[] = {data.local_bounds.min_.x_, data.local_bounds.min_.y_,
                                    data.local_bounds.min_.z_, data.local_bounds.max_.x_,
                                    data.local_bounds.max_.y_, data.local_bounds.max_.z_};
            for (const float value : bounds)
            {
                Require(IsFinite(value), NativeModelErrorCode::InvalidValue,
                        "native model contains non-finite bounds");
            }
            Require(data.local_bounds.IsValid(), NativeModelErrorCode::InvalidValue,
                    "native model bounds are inverted");

            for (const NativeModelMaterialReference &reference : data.material_references)
            {
                Require(reference.asset_type == AssetType::KPAT_Material,
                        NativeModelErrorCode::UnsupportedReference,
                        "native model contains a non-material reference");
                Require(!IsZeroHash(reference.content_hash), NativeModelErrorCode::InvalidValue,
                        "native model contains an empty material product hash");
            }
        }

        void AppendVertex(std::vector<std::byte> &bytes, const data::Vertex &vertex)
        {
            AppendFloat(bytes, vertex.position.x_);
            AppendFloat(bytes, vertex.position.y_);
            AppendFloat(bytes, vertex.position.z_);
            AppendFloat(bytes, vertex.normal.x_);
            AppendFloat(bytes, vertex.normal.y_);
            AppendFloat(bytes, vertex.normal.z_);
            AppendFloat(bytes, vertex.tex_coord.x_);
            AppendFloat(bytes, vertex.tex_coord.y_);
            AppendFloat(bytes, vertex.tangent.x_);
            AppendFloat(bytes, vertex.tangent.y_);
            AppendFloat(bytes, vertex.tangent.z_);
            AppendFloat(bytes, vertex.bitangent.x_);
            AppendFloat(bytes, vertex.bitangent.y_);
            AppendFloat(bytes, vertex.bitangent.z_);
        }

        std::vector<std::byte> BuildChunk(const NativeModelData &data, NativeModelChunkType type)
        {
            std::vector<std::byte> bytes;
            switch (type)
            {
            case NativeModelChunkType::Vertices:
                bytes.reserve(data.vertices.size() * kVertexStride);
                for (const data::Vertex &vertex : data.vertices)
                {
                    AppendVertex(bytes, vertex);
                }
                break;
            case NativeModelChunkType::Indices:
                bytes.reserve(data.indices.size() * kIndexStride);
                for (const std::uint32_t index : data.indices)
                {
                    AppendU32(bytes, index);
                }
                break;
            case NativeModelChunkType::Sections:
                bytes.reserve(data.sections.size() * kSectionStride);
                for (const data::MeshSection &section : data.sections)
                {
                    AppendU32(bytes, section.index_start);
                    AppendU32(bytes, section.index_count);
                    AppendU32(bytes, section.material_index);
                    AppendFloat(bytes, section.local_bounds.min_.x_);
                    AppendFloat(bytes, section.local_bounds.min_.y_);
                    AppendFloat(bytes, section.local_bounds.min_.z_);
                    AppendFloat(bytes, section.local_bounds.max_.x_);
                    AppendFloat(bytes, section.local_bounds.max_.y_);
                    AppendFloat(bytes, section.local_bounds.max_.z_);
                }
                break;
            case NativeModelChunkType::Bounds:
                bytes.reserve(kBoundsStride);
                AppendFloat(bytes, data.local_bounds.min_.x_);
                AppendFloat(bytes, data.local_bounds.min_.y_);
                AppendFloat(bytes, data.local_bounds.min_.z_);
                AppendFloat(bytes, data.local_bounds.max_.x_);
                AppendFloat(bytes, data.local_bounds.max_.y_);
                AppendFloat(bytes, data.local_bounds.max_.z_);
                break;
            case NativeModelChunkType::MaterialReferences:
                bytes.reserve(data.material_references.size() * kMaterialReferenceStride);
                for (const NativeModelMaterialReference &reference : data.material_references)
                {
                    AppendU16(bytes, static_cast<std::uint16_t>(reference.asset_type));
                    AppendU16(bytes, 0);
                    AppendHash(bytes, reference.content_hash);
                }
                break;
            }
            return bytes;
        }

        std::uint32_t ChunkCount(const NativeModelData &data, NativeModelChunkType type)
        {
            switch (type)
            {
            case NativeModelChunkType::Vertices:
                return static_cast<std::uint32_t>(data.vertices.size());
            case NativeModelChunkType::Indices:
                return static_cast<std::uint32_t>(data.indices.size());
            case NativeModelChunkType::Sections:
                return static_cast<std::uint32_t>(data.sections.size());
            case NativeModelChunkType::Bounds:
                return 1;
            case NativeModelChunkType::MaterialReferences:
                return static_cast<std::uint32_t>(data.material_references.size());
            }
            return 0;
        }

        std::uint32_t ChunkElementSize(NativeModelChunkType type)
        {
            switch (type)
            {
            case NativeModelChunkType::Vertices:
                return static_cast<std::uint32_t>(kVertexStride);
            case NativeModelChunkType::Indices:
                return static_cast<std::uint32_t>(kIndexStride);
            case NativeModelChunkType::Sections:
                return static_cast<std::uint32_t>(kSectionStride);
            case NativeModelChunkType::Bounds:
                return static_cast<std::uint32_t>(kBoundsStride);
            case NativeModelChunkType::MaterialReferences:
                return static_cast<std::uint32_t>(kMaterialReferenceStride);
            }
            return 0;
        }

        std::size_t ChunkOffset(const Chunk &chunk)
        {
            Require(chunk.offset <= std::numeric_limits<std::size_t>::max(),
                    NativeModelErrorCode::Overflow, "native model chunk offset overflows the host size");
            return static_cast<std::size_t>(chunk.offset);
        }

        void RequireChunk(const std::vector<std::byte> &bytes, const Chunk &chunk,
                          std::size_t expected_size, std::uint32_t expected_count,
                          std::uint32_t expected_element_size)
        {
            Require(chunk.element_size == expected_element_size && chunk.count == expected_count,
                    NativeModelErrorCode::InvalidChunkTable, "native model chunk metadata is invalid");
            Require(chunk.byte_size <= std::numeric_limits<std::size_t>::max(),
                    NativeModelErrorCode::Overflow, "native model chunk size overflows the host size");
            const std::size_t offset = ChunkOffset(chunk);
            const std::size_t size = static_cast<std::size_t>(chunk.byte_size);
            std::size_t end = 0;
            Require(CheckedAdd(offset, size, end) && end <= bytes.size() && size == expected_size,
                    NativeModelErrorCode::InvalidChunkTable, "native model chunk bounds are invalid");
        }

        const Chunk &FindChunk(const std::array<Chunk, 5> &chunks, NativeModelChunkType type)
        {
            for (const Chunk &chunk : chunks)
            {
                if (chunk.type == type)
                {
                    return chunk;
                }
            }
            throw NativeModelError(NativeModelErrorCode::InvalidChunkTable,
                                   "native model is missing a required chunk");
        }
    }

    bool NativeModelData::operator==(const NativeModelData &rhs) const noexcept
    {
        if (vertices != rhs.vertices || indices != rhs.indices ||
            local_bounds != rhs.local_bounds || material_references != rhs.material_references ||
            sections.size() != rhs.sections.size())
        {
            return false;
        }
        return std::equal(sections.begin(), sections.end(), rhs.sections.begin(),
                          [](const data::MeshSection &lhs, const data::MeshSection &rhs)
                          {
                              return lhs.index_start == rhs.index_start &&
                                     lhs.index_count == rhs.index_count &&
                                     lhs.material_index == rhs.material_index &&
                                     lhs.local_bounds == rhs.local_bounds;
                          });
    }

    NativeModelError::NativeModelError(NativeModelErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    NativeModelErrorCode NativeModelError::Code() const noexcept
    {
        return code_;
    }

    std::vector<std::byte> SerializeNativeModel(const NativeModelData &data)
    {
        ValidateData(data);

        constexpr std::array<NativeModelChunkType, 5> chunk_types{{
            NativeModelChunkType::Vertices,
            NativeModelChunkType::Indices,
            NativeModelChunkType::Sections,
            NativeModelChunkType::Bounds,
            NativeModelChunkType::MaterialReferences}};

        std::array<std::vector<std::byte>, chunk_types.size()> chunk_bytes;
        std::array<Chunk, chunk_types.size()> chunks{};
        for (std::size_t index = 0; index < chunk_types.size(); ++index)
        {
            chunk_bytes[index] = BuildChunk(data, chunk_types[index]);
            chunks[index].type = chunk_types[index];
            chunks[index].element_size = ChunkElementSize(chunk_types[index]);
            chunks[index].count = ChunkCount(data, chunk_types[index]);
        }

        std::vector<std::byte> bytes;
        bytes.reserve(kNativeModelHeaderSize + kNativeModelChunkEntrySize * chunks.size());
        for (const std::uint8_t value : kMagic)
        {
            AppendByte(bytes, value);
        }
        AppendU16(bytes, kNativeModelVersion);
        AppendU16(bytes, static_cast<std::uint16_t>(kNativeModelHeaderSize));
        AppendU32(bytes, kNativeModelFeatures);
        AppendU64(bytes, 0);
        AppendU64(bytes, kNativeModelHeaderSize);
        AppendU32(bytes, static_cast<std::uint32_t>(chunks.size()));
        AppendU32(bytes, 0);
        for (std::size_t index = 0; index < kNativeModelDigestSize; ++index)
        {
            AppendByte(bytes, 0);
        }
        AppendU32(bytes, static_cast<std::uint32_t>(data.vertices.size()));
        AppendU32(bytes, static_cast<std::uint32_t>(data.indices.size()));
        AppendU32(bytes, static_cast<std::uint32_t>(data.sections.size()));
        AppendU32(bytes, static_cast<std::uint32_t>(data.material_references.size()));
        Require(bytes.size() == kNativeModelHeaderSize, NativeModelErrorCode::InvalidArgument,
                "native model header size is inconsistent");

        const std::size_t directory_end = kNativeModelHeaderSize +
                                           kNativeModelChunkEntrySize * chunks.size();
        std::size_t next_offset = directory_end;
        for (std::size_t index = 0; index < chunks.size(); ++index)
        {
            chunks[index].offset = next_offset;
            chunks[index].byte_size = chunk_bytes[index].size();
            Require(CheckedAdd(next_offset, chunk_bytes[index].size(), next_offset),
                    NativeModelErrorCode::Overflow, "native model size overflows the host size");
        }
        Require(next_offset <= kNativeModelMaxBytes,
                NativeModelErrorCode::Overflow, "native model exceeds the product size limit");

        for (const Chunk &chunk : chunks)
        {
            AppendU32(bytes, static_cast<std::uint32_t>(chunk.type));
            AppendU32(bytes, 0);
            AppendU64(bytes, chunk.offset);
            AppendU64(bytes, chunk.byte_size);
            AppendU32(bytes, chunk.element_size);
            AppendU32(bytes, chunk.count);
        }
        for (const std::vector<std::byte> &chunk : chunk_bytes)
        {
            bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        }
        Require(bytes.size() == next_offset, NativeModelErrorCode::InvalidArgument,
                "native model chunk layout is inconsistent");
        WriteAtU64(bytes, 16, bytes.size());

        const auto hashes = Sha256WithZeroedRange(bytes, kNativeModelDigestOffset,
                                                   kNativeModelDigestSize);
        Require(hashes.has_value(), NativeModelErrorCode::Truncated,
                "native model digest is truncated");
        const ContentHash integrity_digest = hashes->zeroed_range_hash;
        WriteAtHash(bytes, kNativeModelDigestOffset, integrity_digest);
        return bytes;
    }

    NativeModelProduct DeserializeNativeModel(const std::vector<std::byte> &bytes,
                                              const ContentHashPair *verified_hashes)
    {
        Require(bytes.size() <= kNativeModelMaxBytes, NativeModelErrorCode::Overflow,
                "native model exceeds the product size limit");
        Require(bytes.size() >= kNativeModelHeaderSize, NativeModelErrorCode::Truncated,
                "native model is shorter than its header");
        for (std::size_t index = 0; index < kMagic.size(); ++index)
        {
            Require(std::to_integer<std::uint8_t>(bytes[index]) == kMagic[index],
                    NativeModelErrorCode::InvalidArgument, "native model magic is invalid");
        }

        const std::uint16_t version = ReadU16(bytes, 8);
        const std::uint16_t header_size = ReadU16(bytes, 10);
        const std::uint32_t features = ReadU32(bytes, 12);
        const std::uint64_t total_size = ReadU64(bytes, 16);
        const std::uint64_t directory_offset = ReadU64(bytes, 24);
        const std::uint32_t chunk_count = ReadU32(bytes, 32);
        const std::uint32_t vertex_count = ReadU32(bytes, 72);
        const std::uint32_t index_count = ReadU32(bytes, 76);
        const std::uint32_t section_count = ReadU32(bytes, 80);
        const std::uint32_t material_count = ReadU32(bytes, 84);

        Require(version == 1 || version == kNativeModelVersion,
                NativeModelErrorCode::UnsupportedVersion,
                "native model version is unsupported");
        Require(header_size == kNativeModelHeaderSize, NativeModelErrorCode::InvalidChunkTable,
                "native model header size is invalid");
        Require(features == kNativeModelFeatures, NativeModelErrorCode::UnsupportedFeatures,
                "native model features are unsupported");
        Require(total_size == bytes.size(), NativeModelErrorCode::InvalidChunkTable,
                "native model total size is invalid");
        Require(directory_offset == kNativeModelHeaderSize && chunk_count == 5,
                NativeModelErrorCode::InvalidChunkTable, "native model chunk directory is invalid");
        Require(vertex_count <= kNativeModelMaxVertices && index_count <= kNativeModelMaxIndices &&
                    section_count <= kNativeModelMaxSections && material_count <= kNativeModelMaxMaterials,
                NativeModelErrorCode::Overflow, "native model count exceeds the limit");

        const std::size_t directory_end = kNativeModelHeaderSize +
                                           kNativeModelChunkEntrySize * chunk_count;
        Require(directory_end <= bytes.size(), NativeModelErrorCode::Truncated,
                "native model chunk directory is truncated");

        std::array<Chunk, 5> chunks{};
        for (std::size_t index = 0; index < chunks.size(); ++index)
        {
            const std::size_t offset = kNativeModelHeaderSize + index * kNativeModelChunkEntrySize;
            chunks[index].type = static_cast<NativeModelChunkType>(ReadU32(bytes, offset));
            Require(ReadU32(bytes, offset + 4) == 0, NativeModelErrorCode::InvalidChunkTable,
                    "native model chunk reserved field is nonzero");
            chunks[index].offset = ReadU64(bytes, offset + 8);
            chunks[index].byte_size = ReadU64(bytes, offset + 16);
            chunks[index].element_size = ReadU32(bytes, offset + 24);
            chunks[index].count = ReadU32(bytes, offset + 28);
            Require(chunks[index].offset >= directory_end,
                    NativeModelErrorCode::InvalidChunkTable,
                    "native model chunk overlaps its directory");
        }
        for (std::size_t left = 0; left < chunks.size(); ++left)
        {
            for (std::size_t right = left + 1; right < chunks.size(); ++right)
            {
                Require(chunks[left].type != chunks[right].type,
                        NativeModelErrorCode::InvalidChunkTable, "native model has duplicate chunks");
            }
        }

        ContentHashPair hashes{};
        if (verified_hashes != nullptr)
        {
            hashes = *verified_hashes;
        }
        else
        {
            const auto computed = Sha256WithZeroedRange(bytes, kNativeModelDigestOffset,
                                                        kNativeModelDigestSize);
            Require(computed.has_value(), NativeModelErrorCode::Truncated,
                    "native model digest is truncated");
            hashes = *computed;
        }
        const ContentHash stored_digest = ReadHash(bytes, kNativeModelDigestOffset);
        Require(stored_digest == hashes.zeroed_range_hash,
                NativeModelErrorCode::IntegrityMismatch,
                "native model integrity digest does not match");

        const std::size_t vertex_size = static_cast<std::size_t>(vertex_count) * kVertexStride;
        const std::size_t index_size = static_cast<std::size_t>(index_count) * kIndexStride;
        const std::size_t section_stride = version == 1
                                                ? 3u * sizeof(std::uint32_t)
                                                : kSectionStride;
        const std::size_t section_size = static_cast<std::size_t>(section_count) * section_stride;
        const std::size_t material_size = static_cast<std::size_t>(material_count) * kMaterialReferenceStride;
        RequireChunk(bytes, FindChunk(chunks, NativeModelChunkType::Vertices), vertex_size,
                     vertex_count, static_cast<std::uint32_t>(kVertexStride));
        RequireChunk(bytes, FindChunk(chunks, NativeModelChunkType::Indices), index_size,
                     index_count, static_cast<std::uint32_t>(kIndexStride));
        RequireChunk(bytes, FindChunk(chunks, NativeModelChunkType::Sections), section_size,
                     section_count, static_cast<std::uint32_t>(section_stride));
        RequireChunk(bytes, FindChunk(chunks, NativeModelChunkType::Bounds), kBoundsStride,
                     1, static_cast<std::uint32_t>(kBoundsStride));
        RequireChunk(bytes, FindChunk(chunks, NativeModelChunkType::MaterialReferences), material_size,
                     material_count, static_cast<std::uint32_t>(kMaterialReferenceStride));

        for (std::size_t left = 0; left < chunks.size(); ++left)
        {
            const std::size_t left_begin = ChunkOffset(chunks[left]);
            const std::size_t left_end = left_begin + static_cast<std::size_t>(chunks[left].byte_size);
            for (std::size_t right = left + 1; right < chunks.size(); ++right)
            {
                const std::size_t right_begin = ChunkOffset(chunks[right]);
                const std::size_t right_end = right_begin + static_cast<std::size_t>(chunks[right].byte_size);
                Require(left_end <= right_begin || right_end <= left_begin,
                        NativeModelErrorCode::InvalidChunkTable, "native model chunks overlap");
            }
        }

        NativeModelProduct product;
        product.integrity_digest = stored_digest;
        product.product_hash = hashes.content_hash;
        product.data.vertices.resize(vertex_count);
        product.data.indices.resize(index_count);
        product.data.sections.resize(section_count);
        product.data.material_references.resize(material_count);

        const Chunk &vertex_chunk = FindChunk(chunks, NativeModelChunkType::Vertices);
        for (std::size_t index = 0; index < product.data.vertices.size(); ++index)
        {
            const std::size_t offset = ChunkOffset(vertex_chunk) + index * kVertexStride;
            data::Vertex &vertex = product.data.vertices[index];
            vertex.position = {ReadFloat(bytes, offset), ReadFloat(bytes, offset + 4), ReadFloat(bytes, offset + 8)};
            vertex.normal = {ReadFloat(bytes, offset + 12), ReadFloat(bytes, offset + 16), ReadFloat(bytes, offset + 20)};
            vertex.tex_coord = {ReadFloat(bytes, offset + 24), ReadFloat(bytes, offset + 28)};
            vertex.tangent = {ReadFloat(bytes, offset + 32), ReadFloat(bytes, offset + 36), ReadFloat(bytes, offset + 40)};
            vertex.bitangent = {ReadFloat(bytes, offset + 44), ReadFloat(bytes, offset + 48), ReadFloat(bytes, offset + 52)};
            ValidateVertex(vertex);
        }

        const Chunk &index_chunk = FindChunk(chunks, NativeModelChunkType::Indices);
        for (std::size_t index = 0; index < product.data.indices.size(); ++index)
        {
            product.data.indices[index] = ReadU32(bytes, ChunkOffset(index_chunk) + index * kIndexStride);
        }

        const Chunk &section_chunk = FindChunk(chunks, NativeModelChunkType::Sections);
        for (std::size_t index = 0; index < product.data.sections.size(); ++index)
        {
            const std::size_t offset = ChunkOffset(section_chunk) + index * section_stride;
            product.data.sections[index] = {
                ReadU32(bytes, offset), ReadU32(bytes, offset + 4), ReadU32(bytes, offset + 8),
                {}};
            if (version == 1)
            {
                data::MeshSection &section = product.data.sections[index];
                const bool range_valid =
                    section.index_start <= product.data.indices.size() &&
                    section.index_count <= product.data.indices.size() - section.index_start;
                const std::size_t end = static_cast<std::size_t>(section.index_start) +
                                        section.index_count;
                if (range_valid && section.index_count != 0)
                {
                    section.local_bounds = {
                        product.data.vertices[product.data.indices[section.index_start]].position,
                        product.data.vertices[product.data.indices[section.index_start]].position};
                    for (std::size_t vertex_index = section.index_start + 1;
                         vertex_index < end; ++vertex_index)
                    {
                        section.local_bounds.ExpandToInclude(
                            product.data.vertices[product.data.indices[vertex_index]].position);
                    }
                }
            }
            else
            {
                data::MeshSection &section = product.data.sections[index];
                section.local_bounds = {
                    {ReadFloat(bytes, offset + 12), ReadFloat(bytes, offset + 16),
                     ReadFloat(bytes, offset + 20)},
                    {ReadFloat(bytes, offset + 24), ReadFloat(bytes, offset + 28),
                     ReadFloat(bytes, offset + 32)}};
            }
        }

        const Chunk &bounds_chunk = FindChunk(chunks, NativeModelChunkType::Bounds);
        const std::size_t bounds_offset = ChunkOffset(bounds_chunk);
        product.data.local_bounds.min_ = {ReadFloat(bytes, bounds_offset), ReadFloat(bytes, bounds_offset + 4), ReadFloat(bytes, bounds_offset + 8)};
        product.data.local_bounds.max_ = {ReadFloat(bytes, bounds_offset + 12), ReadFloat(bytes, bounds_offset + 16), ReadFloat(bytes, bounds_offset + 20)};

        const Chunk &material_chunk = FindChunk(chunks, NativeModelChunkType::MaterialReferences);
        for (std::size_t index = 0; index < product.data.material_references.size(); ++index)
        {
            const std::size_t offset = ChunkOffset(material_chunk) + index * kMaterialReferenceStride;
            Require(ReadU16(bytes, offset + 2) == 0, NativeModelErrorCode::InvalidValue,
                    "native model material reference reserved field is nonzero");
            product.data.material_references[index].asset_type = static_cast<AssetType>(ReadU16(bytes, offset));
            product.data.material_references[index].content_hash = ReadHash(bytes, offset + 4);
        }
        ValidateData(product.data);
        return product;
    }

    ContentHash ComputeNativeModelProductHash(const std::vector<std::byte> &bytes)
    {
        return Sha256(bytes);
    }
}
