#ifndef KPENGINE_RUNTIME_ASSET_IMPORTED_MODEL_H
#define KPENGINE_RUNTIME_ASSET_IMPORTED_MODEL_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "data/mesh.h"
#include "spatial/aabb.h"

namespace kpengine::asset
{
    enum class ImportedDependencyKind : std::uint8_t
    {
        PrimarySource,
        ExternalFile,
        EmbeddedData,
    };

    struct ImportedSourceDependency
    {
        ImportedDependencyKind kind{ImportedDependencyKind::ExternalFile};
        // The spelling used by the foreign source, or the primary filename.
        std::string path;
        // The file to hash/read later. Empty for embedded data.
        std::filesystem::path resolved_path;
        std::vector<std::byte> embedded_bytes;
    };

    enum class ImportedImageStorage : std::uint8_t
    {
        ExternalFile,
        EmbeddedBytes,
    };

    struct ImportedImageSource
    {
        // This is the material-facing reference, for example "textures/a.png"
        // or Assimp's embedded-texture spelling "*0".
        std::string path;
        ImportedImageStorage storage{ImportedImageStorage::ExternalFile};
        std::filesystem::path resolved_path;
        std::string format_hint;
        std::vector<std::byte> embedded_bytes;
        // Assimp exposes uncompressed embedded texels separately from encoded
        // image payloads. These fields preserve that distinction for the
        // standalone material converter.
        std::uint32_t embedded_width{};
        std::uint32_t embedded_height{};
        bool embedded_is_raw_rgba8{false};
    };

    enum class ImportedAlphaMode : std::uint8_t
    {
        Opaque,
        Mask,
        Blend,
    };

    struct ImportedMaterialSource
    {
        std::string name;
        Vector4f base_color{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic{1.0f};
        float roughness{1.0f};
        Vector4f emissive{0.0f, 0.0f, 0.0f, 1.0f};
        std::string base_color_texture;
        std::string normal_texture;
        std::string metallic_roughness_texture;
        std::string occlusion_texture;
        std::string emissive_texture;
        bool double_sided{false};
        ImportedAlphaMode alpha_mode{ImportedAlphaMode::Opaque};
        float alpha_cutoff{0.5f};
        float normal_scale{1.0f};
        float occlusion_strength{1.0f};
    };

    struct ImportedMeshSource
    {
        std::vector<data::Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<data::MeshSection> sections;
        spatial::AABB local_bounds{};
        std::uint32_t face_count{};
        std::uint32_t vertex_count{};
    };

    enum class ImportedDiagnosticSeverity : std::uint8_t
    {
        Warning,
        Error,
    };

    struct ImportedSourceDiagnostic
    {
        ImportedDiagnosticSeverity severity{ImportedDiagnosticSeverity::Error};
        std::string code;
        std::string source_path;
        std::string message;
    };

    enum class ImportedModelErrorCode : std::uint8_t
    {
        InvalidArgument,
        UnsupportedExtension,
        SourceMissing,
        DependencyMissing,
        AssimpFailure,
        MalformedSource,
        UnsupportedSemantics,
    };

    class ImportedModelDecodeError final : public std::runtime_error
    {
    public:
        ImportedModelDecodeError(ImportedModelErrorCode code, std::string message,
                                 std::vector<ImportedSourceDiagnostic> diagnostics = {});

        ImportedModelErrorCode Code() const noexcept;
        const std::vector<ImportedSourceDiagnostic> &Diagnostics() const noexcept;

    private:
        ImportedModelErrorCode code_{};
        std::vector<ImportedSourceDiagnostic> diagnostics_;
    };

    struct ImportedModelDocument
    {
        std::filesystem::path source_path;
        ImportedMeshSource mesh;
        std::vector<ImportedMaterialSource> materials;
        std::vector<ImportedImageSource> images;
        std::vector<ImportedSourceDependency> source_dependencies;
        std::vector<ImportedSourceDiagnostic> diagnostics;
    };
}

#endif
