#ifndef KPENGINE_RUNTIME_CORE_DATA_MESH_H
#define KPENGINE_RUNTIME_CORE_DATA_MESH_H

#include <cstdint>
#include <string>
#include <vector>

#include "math/math_header.h"
#include "spatial/aabb.h"

namespace kpengine::data
{

    struct Vertex
    {
        Vector3f position;
        Vector3f normal;
        Vector2f tex_coord;
        Vector3f tangent;
        Vector3f bitangent;

        bool operator==(const Vertex &other) const noexcept
        {
            return position == other.position &&
                   normal == other.normal &&
                   tex_coord == other.tex_coord &&
                   tangent == other.tangent &&
                   bitangent == other.bitangent;
        }
    };

    struct VertexHash
    {
        std::size_t operator()(const Vertex &v) const noexcept
        {
            auto hashCombine = [](std::size_t seed, std::size_t h)
            {
                // boost::hash_combine style
                return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
            };

            std::size_t h = 0;
            h = hashCombine(h, std::hash<float>()(v.position.x_));
            h = hashCombine(h, std::hash<float>()(v.position.y_));
            h = hashCombine(h, std::hash<float>()(v.position.z_));

            h = hashCombine(h, std::hash<float>()(v.normal.x_));
            h = hashCombine(h, std::hash<float>()(v.normal.y_));
            h = hashCombine(h, std::hash<float>()(v.normal.z_));

            h = hashCombine(h, std::hash<float>()(v.tex_coord.x_));
            h = hashCombine(h, std::hash<float>()(v.tex_coord.y_));

            h = hashCombine(h, std::hash<float>()(v.tangent.x_));
            h = hashCombine(h, std::hash<float>()(v.tangent.y_));
            h = hashCombine(h, std::hash<float>()(v.tangent.z_));

            h = hashCombine(h, std::hash<float>()(v.bitangent.x_));
            h = hashCombine(h, std::hash<float>()(v.bitangent.y_));
            h = hashCombine(h, std::hash<float>()(v.bitangent.z_));

            return h;
        }
    };

    struct MeshSection
    {
        uint32_t index_start = 0;
        uint32_t index_count = 0;
        uint32_t material_index = 0;
        // Bounds of indexed vertices in mesh-local space.
        spatial::AABB local_bounds{};
    };

    // Imported material metadata remains CPU-side mesh data. Render policy
    // still comes from engine Material assets, but this preserves the source
    // GLTF/Assimp material for import tools and future conversion.
    struct MeshMaterial
    {
        std::string name;
        Vector4f base_color{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 1.0f;
        float roughness = 1.0f;
        Vector4f emissive{0.0f, 0.0f, 0.0f, 1.0f};
        std::string base_color_texture;
        std::string normal_texture;
        std::string metallic_roughness_texture;
        std::string occlusion_texture;
        std::string emissive_texture;
        bool double_sided = false;
        bool alpha_blended = false;
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<MeshSection> sections;
        std::vector<MeshMaterial> materials;
    };

}

#endif
