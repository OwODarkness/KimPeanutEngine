#include "assimp_model_decoder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include <nlohmann/json.hpp>

namespace kpengine::asset
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr float kMinimumImportedVectorLength = 1.0e-6f;

        std::string Lowercase(std::string value)
        {
            for (char &character : value)
            {
                character = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            }
            return value;
        }

        std::string Extension(const std::filesystem::path &path)
        {
            std::string extension = path.extension().string();
            if (!extension.empty() && extension.front() == '.')
            {
                extension.erase(0, 1);
            }
            return Lowercase(std::move(extension));
        }

        std::filesystem::path NormalizeSourcePath(const std::filesystem::path &path)
        {
            if (path.empty())
            {
                throw ImportedModelDecodeError(ImportedModelErrorCode::InvalidArgument,
                                               "foreign model source path is empty");
            }

            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(path, error);
            if (error)
            {
                throw ImportedModelDecodeError(ImportedModelErrorCode::InvalidArgument,
                                               "failed to resolve foreign model source path: " +
                                                   path.string());
            }
            const std::filesystem::path normalized = absolute.lexically_normal();
            if (!std::filesystem::is_regular_file(normalized, error) || error)
            {
                throw ImportedModelDecodeError(ImportedModelErrorCode::SourceMissing,
                                               "foreign model source is missing: " +
                                                   normalized.string());
            }
            return normalized;
        }

        bool IsSupportedExtension(std::string_view extension)
        {
            return extension == "stl" || extension == "obj" || extension == "fbx" ||
                   extension == "gltf" || extension == "glb";
        }

        std::string PortablePath(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        std::filesystem::path ResolveExternalPath(const std::filesystem::path &source_path,
                                                  std::string reference)
        {
            reference = PortablePath(std::move(reference));
            if (reference.rfind("file://", 0) == 0)
            {
                reference.erase(0, 7);
            }
            const std::filesystem::path referenced_path{reference};
            if (referenced_path.is_absolute() || referenced_path.has_root_name() ||
                referenced_path.has_root_directory())
            {
                return referenced_path.lexically_normal();
            }
            return (source_path.parent_path() / referenced_path).lexically_normal();
        }

        std::vector<std::byte> DecodeBase64(std::string_view value)
        {
            std::vector<std::byte> decoded;
            int accumulator = 0;
            int bits = -8;
            for (const unsigned char character : value)
            {
                if (std::isspace(character))
                {
                    continue;
                }
                if (character == '=')
                {
                    break;
                }
                int digit = -1;
                if (character >= 'A' && character <= 'Z')
                {
                    digit = character - 'A';
                }
                else if (character >= 'a' && character <= 'z')
                {
                    digit = character - 'a' + 26;
                }
                else if (character >= '0' && character <= '9')
                {
                    digit = character - '0' + 52;
                }
                else if (character == '+')
                {
                    digit = 62;
                }
                else if (character == '/')
                {
                    digit = 63;
                }
                else
                {
                    throw ImportedModelDecodeError(ImportedModelErrorCode::MalformedSource,
                                                   "foreign source contains an invalid base64 URI");
                }
                accumulator = (accumulator << 6) | digit;
                bits += 6;
                if (bits >= 0)
                {
                    decoded.push_back(static_cast<std::byte>((accumulator >> bits) & 0xff));
                    bits -= 8;
                }
            }
            return decoded;
        }

        std::vector<std::byte> DecodeDataUri(std::string_view uri)
        {
            const std::size_t comma = uri.find(',');
            if (comma == std::string_view::npos)
            {
                throw ImportedModelDecodeError(ImportedModelErrorCode::MalformedSource,
                                               "foreign source contains a malformed data URI");
            }
            const std::string_view metadata = uri.substr(5, comma - 5);
            const std::string_view payload = uri.substr(comma + 1);
            if (metadata.find(";base64") != std::string_view::npos)
            {
                return DecodeBase64(payload);
            }

            std::vector<std::byte> decoded;
            decoded.reserve(payload.size());
            for (std::size_t index = 0; index < payload.size(); ++index)
            {
                if (payload[index] != '%')
                {
                    decoded.push_back(static_cast<std::byte>(payload[index]));
                    continue;
                }
                if (index + 2 >= payload.size())
                {
                    throw ImportedModelDecodeError(ImportedModelErrorCode::MalformedSource,
                                                   "foreign source contains a malformed URI escape");
                }
                const auto hex = [](char character) -> int
                {
                    if (character >= '0' && character <= '9')
                    {
                        return character - '0';
                    }
                    if (character >= 'a' && character <= 'f')
                    {
                        return character - 'a' + 10;
                    }
                    if (character >= 'A' && character <= 'F')
                    {
                        return character - 'A' + 10;
                    }
                    return -1;
                };
                const int high = hex(payload[index + 1]);
                const int low = hex(payload[index + 2]);
                if (high < 0 || low < 0)
                {
                    throw ImportedModelDecodeError(ImportedModelErrorCode::MalformedSource,
                                                   "foreign source contains an invalid URI escape");
                }
                decoded.push_back(static_cast<std::byte>((high << 4) | low));
                index += 2;
            }
            return decoded;
        }

        void AddEmbeddedDependency(ImportedModelDocument &document, std::string path,
                                   const std::vector<std::byte> &bytes)
        {
            const auto existing = std::find_if(
                document.source_dependencies.begin(), document.source_dependencies.end(),
                [&path](const ImportedSourceDependency &dependency)
                {
                    return dependency.kind == ImportedDependencyKind::EmbeddedData &&
                           dependency.path == path;
                });
            if (existing == document.source_dependencies.end())
            {
                document.source_dependencies.push_back(
                    {ImportedDependencyKind::EmbeddedData, std::move(path), {}, bytes});
            }
        }

        void AddExternalDependency(ImportedModelDocument &document,
                                   const std::filesystem::path &source_path,
                                   const std::string &reference)
        {
            if (reference.empty() || reference.front() == '*')
            {
                return;
            }
            if (reference.rfind("data:", 0) == 0)
            {
                AddEmbeddedDependency(document, reference, DecodeDataUri(reference));
                return;
            }

            const std::filesystem::path resolved = ResolveExternalPath(source_path, reference);
            std::error_code error;
            if (!std::filesystem::is_regular_file(resolved, error) || error)
            {
                throw ImportedModelDecodeError(
                    ImportedModelErrorCode::DependencyMissing,
                    "foreign source dependency is missing: " + resolved.string(),
                    {{ImportedDiagnosticSeverity::Error, "MissingDependency", reference,
                      "resolved dependency does not exist: " + resolved.string()}});
            }

            const auto existing = std::find_if(
                document.source_dependencies.begin(), document.source_dependencies.end(),
                [&resolved](const ImportedSourceDependency &dependency)
                {
                    return dependency.kind == ImportedDependencyKind::ExternalFile &&
                           dependency.resolved_path == resolved;
                });
            if (existing == document.source_dependencies.end())
            {
                document.source_dependencies.push_back(
                    {ImportedDependencyKind::ExternalFile, PortablePath(reference), resolved, {}});
            }
        }

        void AddEmbeddedTexture(ImportedModelDocument &document, const aiScene &scene,
                                const std::string &reference)
        {
            const aiTexture *const texture = scene.GetEmbeddedTexture(reference.c_str());
            if (texture == nullptr)
            {
                throw ImportedModelDecodeError(
                    ImportedModelErrorCode::DependencyMissing,
                    "Assimp referenced an embedded texture that is not present: " + reference);
            }

            const std::size_t byte_count = texture->mHeight == 0
                                               ? texture->mWidth
                                               : static_cast<std::size_t>(texture->mWidth) *
                                                     static_cast<std::size_t>(texture->mHeight) *
                                                     sizeof(aiTexel);
            std::vector<std::byte> bytes(byte_count);
            if (byte_count != 0 && texture->pcData == nullptr)
            {
                throw ImportedModelDecodeError(
                    ImportedModelErrorCode::MalformedSource,
                    "Assimp returned an embedded texture without payload: " + reference);
            }
            if (byte_count != 0)
            {
                std::memcpy(bytes.data(), texture->pcData, byte_count);
            }
            if (texture->mHeight != 0)
            {
                const auto *const texels = texture->pcData;
                for (std::size_t index = 0; index < static_cast<std::size_t>(texture->mWidth) *
                                                       static_cast<std::size_t>(texture->mHeight);
                     ++index)
                {
                    const aiTexel &texel = texels[index];
                    bytes[index * 4 + 0] = static_cast<std::byte>(texel.r);
                    bytes[index * 4 + 1] = static_cast<std::byte>(texel.g);
                    bytes[index * 4 + 2] = static_cast<std::byte>(texel.b);
                    bytes[index * 4 + 3] = static_cast<std::byte>(texel.a);
                }
            }
            AddEmbeddedDependency(document, reference, bytes);

            const auto existing = std::find_if(
                document.images.begin(), document.images.end(),
                [&reference](const ImportedImageSource &image) { return image.path == reference; });
            if (existing == document.images.end())
            {
                ImportedImageSource image;
                image.path = reference;
                image.storage = ImportedImageStorage::EmbeddedBytes;
                image.format_hint = texture->achFormatHint;
                image.embedded_bytes = std::move(bytes);
                if (texture->mHeight != 0)
                {
                    image.embedded_width = texture->mWidth;
                    image.embedded_height = texture->mHeight;
                    image.embedded_is_raw_rgba8 = true;
                }
                document.images.push_back(std::move(image));
            }
        }

        void AddExternalImage(ImportedModelDocument &document,
                              const std::filesystem::path &source_path,
                              const std::string &reference)
        {
            if (reference.empty() || reference.front() == '*')
            {
                return;
            }
            const std::filesystem::path resolved = ResolveExternalPath(source_path, reference);
            AddExternalDependency(document, source_path, reference);
            const auto existing = std::find_if(
                document.images.begin(), document.images.end(),
                [&resolved](const ImportedImageSource &image)
                {
                    return image.storage == ImportedImageStorage::ExternalFile &&
                           image.resolved_path == resolved;
                });
            if (existing == document.images.end())
            {
                document.images.push_back(
                    {PortablePath(reference), ImportedImageStorage::ExternalFile, resolved, {}, {}});
            }
        }

        void DiscoverMaterialImages(ImportedModelDocument &document, const aiScene &scene,
                                    const std::filesystem::path &source_path)
        {
            for (unsigned int material_index = 0; material_index < scene.mNumMaterials;
                 ++material_index)
            {
                const aiMaterial *const material = scene.mMaterials[material_index];
                if (material == nullptr)
                {
                    continue;
                }
                for (int type_value = 0; type_value <= AI_TEXTURE_TYPE_MAX; ++type_value)
                {
                    const auto type = static_cast<aiTextureType>(type_value);
                    const unsigned int count = material->GetTextureCount(type);
                    for (unsigned int texture_index = 0; texture_index < count; ++texture_index)
                    {
                        aiString path;
                        if (material->GetTexture(type, texture_index, &path) != AI_SUCCESS)
                        {
                            continue;
                        }
                        const std::string reference = path.C_Str();
                        if (reference.empty())
                        {
                            continue;
                        }
                        if (reference.front() == '*')
                        {
                            AddEmbeddedTexture(document, scene, reference);
                        }
                        else
                        {
                            AddExternalImage(document, source_path, reference);
                        }
                    }
                }
            }
        }

        void DiscoverObjMaterialLibraries(ImportedModelDocument &document,
                                          const std::filesystem::path &source_path)
        {
            std::ifstream file(source_path);
            if (!file.is_open())
            {
                return;
            }
            std::string line;
            while (std::getline(file, line))
            {
                std::istringstream stream(line);
                std::string directive;
                stream >> directive;
                if (directive != "mtllib")
                {
                    continue;
                }
                std::string material_library;
                while (stream >> material_library)
                {
                    try
                    {
                        AddExternalDependency(document, source_path, material_library);
                    }
                    catch (const ImportedModelDecodeError &error)
                    {
                        if (error.Code() != ImportedModelErrorCode::DependencyMissing)
                        {
                            throw;
                        }
                        document.diagnostics.push_back(
                            {ImportedDiagnosticSeverity::Warning, "MissingOptionalDependency",
                             PortablePath(material_library),
                             "OBJ material library is missing; Assimp will use its fallback material"});
                    }
                }
            }
        }

        void DiscoverGltfUriDependencies(ImportedModelDocument &document,
                                          const std::filesystem::path &source_path)
        {
            std::ifstream file(source_path);
            if (!file.is_open())
            {
                throw ImportedModelDecodeError(ImportedModelErrorCode::SourceMissing,
                                               "failed to open glTF source: " + source_path.string());
            }
            const std::string text((std::istreambuf_iterator<char>{file}),
                                   std::istreambuf_iterator<char>{});
            Json root;
            try
            {
                root = Json::parse(text);
            }
            catch (const Json::exception &error)
            {
                throw ImportedModelDecodeError(
                    ImportedModelErrorCode::MalformedSource,
                    "glTF source JSON is malformed: " + std::string{error.what()},
                    {{ImportedDiagnosticSeverity::Error, "MalformedSource",
                      source_path.generic_string(),
                      "glTF JSON parsing failed"}});
            }

            const auto discover = [&](const char *collection_name)
            {
                const auto collection = root.find(collection_name);
                if (collection == root.end() || !collection->is_array())
                {
                    return;
                }
                for (std::size_t index = 0; index < collection->size(); ++index)
                {
                    const Json &entry = (*collection)[index];
                    const auto uri = entry.find("uri");
                    if (uri == entry.end() || !uri->is_string())
                    {
                        continue;
                    }
                    const std::string reference = uri->get<std::string>();
                    if (reference.rfind("data:", 0) == 0)
                    {
                        AddEmbeddedDependency(document,
                                              std::string{collection_name} + "[" +
                                                  std::to_string(index) + "]",
                                              DecodeDataUri(reference));
                    }
                    else
                    {
                        AddExternalDependency(document, source_path, reference);
                    }
                }
            };
            discover("buffers");
            discover("images");
        }

        Vector3f ToVector3(const aiVector3D &value)
        {
            return {static_cast<float>(value.x), static_cast<float>(value.y),
                    static_cast<float>(value.z)};
        }

        Vector3f NormalizeImportedVector(const aiVector3D &value)
        {
            const float length = static_cast<float>(value.Length());
            if (!std::isfinite(length) || length <= kMinimumImportedVectorLength)
            {
                return {};
            }
            return ToVector3(value) * (1.0f / length);
        }

        std::string ReadTexturePath(const aiMaterial &material, aiTextureType type)
        {
            aiString path;
            if (material.GetTexture(type, 0, &path) != AI_SUCCESS)
            {
                return {};
            }
            return path.C_Str();
        }

        void ReadMaterialMetadata(const aiScene &scene, ImportedModelDocument &document)
        {
            const unsigned int material_count = std::max(1u, scene.mNumMaterials);
            document.materials.resize(material_count);
            for (unsigned int index = 0; index < material_count; ++index)
            {
                ImportedMaterialSource &destination = document.materials[index];
                if (scene.mNumMaterials == 0 || scene.mMaterials[index] == nullptr)
                {
                    destination.name = "DefaultMaterial";
                    continue;
                }
                const aiMaterial &source = *scene.mMaterials[index];

                aiString name;
                if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
                {
                    destination.name = name.C_Str();
                }

                aiColor4D color{1.0f, 1.0f, 1.0f, 1.0f};
                if (source.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
                {
                    (void)source.Get(AI_MATKEY_COLOR_DIFFUSE, color);
                }
                destination.base_color = {color.r, color.g, color.b, color.a};

                float factor = 0.0f;
                if (source.Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS)
                {
                    destination.metallic = factor;
                }
                if (source.Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS)
                {
                    destination.roughness = factor;
                }
                if (source.Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
                {
                    destination.emissive = {color.r, color.g, color.b, color.a};
                }

                destination.base_color_texture =
                    PortablePath(ReadTexturePath(source, aiTextureType_BASE_COLOR));
                if (destination.base_color_texture.empty())
                {
                    destination.base_color_texture =
                        PortablePath(ReadTexturePath(source, aiTextureType_DIFFUSE));
                }
                destination.normal_texture =
                    PortablePath(ReadTexturePath(source, aiTextureType_NORMALS));
                destination.metallic_roughness_texture =
                    PortablePath(ReadTexturePath(source, aiTextureType_UNKNOWN));
                if (destination.metallic_roughness_texture.empty())
                {
                    destination.metallic_roughness_texture =
                        PortablePath(ReadTexturePath(source, aiTextureType_METALNESS));
                }
                destination.occlusion_texture =
                    PortablePath(ReadTexturePath(source, aiTextureType_AMBIENT_OCCLUSION));
                destination.emissive_texture =
                    PortablePath(ReadTexturePath(source, aiTextureType_EMISSIVE));

                int double_sided = 0;
                if (source.Get(AI_MATKEY_TWOSIDED, double_sided) == AI_SUCCESS)
                {
                    destination.double_sided = double_sided != 0;
                }

                aiString alpha_mode;
                if (source.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == AI_SUCCESS)
                {
                    const std::string mode = Lowercase(alpha_mode.C_Str());
                    if (mode == "blend")
                    {
                        destination.alpha_mode = ImportedAlphaMode::Blend;
                    }
                    else if (mode == "mask")
                    {
                        destination.alpha_mode = ImportedAlphaMode::Mask;
                    }
                }
                if (source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, factor) == AI_SUCCESS)
                {
                    destination.alpha_cutoff = factor;
                }
                if (source.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), factor) ==
                    AI_SUCCESS)
                {
                    destination.normal_scale = factor;
                }
                if (source.Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_AMBIENT_OCCLUSION, 0),
                               factor) == AI_SUCCESS)
                {
                    destination.occlusion_strength = factor;
                }
            }
        }

        void ProcessMesh(const aiMesh &mesh, const aiMatrix4x4 &node_transform,
                         ImportedMeshSource &destination,
                         std::unordered_map<data::Vertex, std::uint32_t, data::VertexHash> &unique_vertices)
        {
            const std::uint32_t index_start = static_cast<std::uint32_t>(destination.indices.size());
            spatial::AABB section_bounds{};
            bool has_section_vertex = false;
            const bool has_normal = mesh.HasNormals();
            const bool has_texcoord = mesh.mTextureCoords[0] != nullptr;
            const bool has_tangent_and_bitangent = mesh.HasTangentsAndBitangents();
            const aiMatrix3x3 linear_transform(node_transform);
            aiMatrix3x3 normal_transform = linear_transform;
            normal_transform.Inverse().Transpose();

            for (unsigned int face_index = 0; face_index < mesh.mNumFaces; ++face_index)
            {
                const aiFace &face = mesh.mFaces[face_index];
                for (unsigned int corner = 0; corner < face.mNumIndices; ++corner)
                {
                    const unsigned int vertex_index = face.mIndices[corner];
                    if (vertex_index >= mesh.mNumVertices)
                    {
                        throw ImportedModelDecodeError(
                            ImportedModelErrorCode::MalformedSource,
                            "Assimp returned a face index outside the mesh vertex range");
                    }

                    data::Vertex vertex{};
                    vertex.position = ToVector3(node_transform * mesh.mVertices[vertex_index]);
                    if (!has_section_vertex)
                    {
                        section_bounds = {vertex.position, vertex.position};
                        has_section_vertex = true;
                    }
                    else
                    {
                        section_bounds.ExpandToInclude(vertex.position);
                    }
                    if (has_normal)
                    {
                        vertex.normal = NormalizeImportedVector(
                            normal_transform * mesh.mNormals[vertex_index]);
                    }
                    if (has_texcoord)
                    {
                        vertex.tex_coord = {mesh.mTextureCoords[0][vertex_index].x,
                                            mesh.mTextureCoords[0][vertex_index].y};
                    }
                    if (has_tangent_and_bitangent)
                    {
                        vertex.tangent = NormalizeImportedVector(
                            linear_transform * mesh.mTangents[vertex_index]);
                        vertex.bitangent = NormalizeImportedVector(
                            linear_transform * mesh.mBitangents[vertex_index]);
                    }

                    const auto [iterator, inserted] = unique_vertices.emplace(
                        vertex, static_cast<std::uint32_t>(destination.vertices.size()));
                    if (inserted)
                    {
                        destination.vertices.push_back(vertex);
                    }
                    destination.indices.push_back(iterator->second);
                }
            }

            destination.sections.push_back(
                {index_start,
                 static_cast<std::uint32_t>(destination.indices.size()) - index_start,
                 mesh.mMaterialIndex,
                 section_bounds});
        }

        void ProcessNode(const aiNode &node, const aiScene &scene, const aiMatrix4x4 &parent,
                         ImportedMeshSource &destination,
                         std::unordered_map<data::Vertex, std::uint32_t, data::VertexHash> &unique_vertices)
        {
            const aiMatrix4x4 transform = parent * node.mTransformation;
            for (unsigned int index = 0; index < node.mNumMeshes; ++index)
            {
                const unsigned int mesh_index = node.mMeshes[index];
                if (mesh_index >= scene.mNumMeshes || scene.mMeshes[mesh_index] == nullptr)
                {
                    throw ImportedModelDecodeError(
                        ImportedModelErrorCode::MalformedSource,
                        "Assimp returned a node mesh index outside the scene mesh range");
                }
                ProcessMesh(*scene.mMeshes[mesh_index], transform, destination, unique_vertices);
            }
            for (unsigned int index = 0; index < node.mNumChildren; ++index)
            {
                if (node.mChildren[index] == nullptr)
                {
                    throw ImportedModelDecodeError(ImportedModelErrorCode::MalformedSource,
                                                   "Assimp returned a null child node");
                }
                ProcessNode(*node.mChildren[index], scene, transform, destination,
                            unique_vertices);
            }
        }

        void ComputeBounds(ImportedMeshSource &mesh)
        {
            if (mesh.vertices.empty())
            {
                return;
            }
            spatial::AABB bounds{mesh.vertices.front().position, mesh.vertices.front().position};
            for (const data::Vertex &vertex : mesh.vertices)
            {
                bounds.min_.x_ = std::min(bounds.min_.x_, vertex.position.x_);
                bounds.min_.y_ = std::min(bounds.min_.y_, vertex.position.y_);
                bounds.min_.z_ = std::min(bounds.min_.z_, vertex.position.z_);
                bounds.max_.x_ = std::max(bounds.max_.x_, vertex.position.x_);
                bounds.max_.y_ = std::max(bounds.max_.y_, vertex.position.y_);
                bounds.max_.z_ = std::max(bounds.max_.z_, vertex.position.z_);
            }
            mesh.local_bounds = bounds;
            mesh.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const data::MeshSection &section : mesh.sections)
            {
                if (std::numeric_limits<std::uint32_t>::max() - mesh.face_count <
                    section.index_count)
                {
                    throw ImportedModelDecodeError(ImportedModelErrorCode::MalformedSource,
                                                   "imported face count overflowed uint32_t");
                }
                mesh.face_count += section.index_count;
            }
        }

        void ValidateSections(const ImportedModelDocument &document)
        {
            for (const data::MeshSection &section : document.mesh.sections)
            {
                if (section.index_start > document.mesh.indices.size() ||
                    section.index_count > document.mesh.indices.size() - section.index_start ||
                    section.material_index >= document.materials.size())
                {
                    throw ImportedModelDecodeError(
                        ImportedModelErrorCode::MalformedSource,
                        "imported mesh section references data outside the document");
                }
            }
        }

        void SortDependencies(ImportedModelDocument &document)
        {
            std::stable_sort(document.source_dependencies.begin(),
                             document.source_dependencies.end(),
                             [](const ImportedSourceDependency &lhs,
                                const ImportedSourceDependency &rhs)
                             {
                                 if (lhs.kind != rhs.kind)
                                 {
                                     return lhs.kind < rhs.kind;
                                 }
                                 return lhs.path < rhs.path;
                             });
        }
    }

    ImportedModelDecodeError::ImportedModelDecodeError(
        ImportedModelErrorCode code, std::string message,
        std::vector<ImportedSourceDiagnostic> diagnostics)
        : std::runtime_error(std::move(message)),
          code_(code),
          diagnostics_(std::move(diagnostics))
    {
    }

    ImportedModelErrorCode ImportedModelDecodeError::Code() const noexcept
    {
        return code_;
    }

    const std::vector<ImportedSourceDiagnostic> &
    ImportedModelDecodeError::Diagnostics() const noexcept
    {
        return diagnostics_;
    }

    struct AssimpModelDecoder::Impl
    {
        Assimp::Importer importer;
    };

    AssimpModelDecoder::AssimpModelDecoder() : impl_(std::make_unique<Impl>())
    {
    }

    AssimpModelDecoder::~AssimpModelDecoder() noexcept = default;

    AssimpModelDecoder::AssimpModelDecoder(AssimpModelDecoder &&other) noexcept = default;

    AssimpModelDecoder &AssimpModelDecoder::operator=(AssimpModelDecoder &&other) noexcept =
        default;

    ImportedModelDocument AssimpModelDecoder::Decode(
        const std::filesystem::path &requested_source_path)
    {
        const std::filesystem::path source_path = NormalizeSourcePath(requested_source_path);
        const std::string extension = Extension(source_path);
        if (!IsSupportedExtension(extension))
        {
            throw ImportedModelDecodeError(
                ImportedModelErrorCode::UnsupportedExtension,
                "foreign model extension is not supported by the import boundary: " + extension,
                {{ImportedDiagnosticSeverity::Error, "UnsupportedExtension",
                  source_path.generic_string(),
                  "expected STL, OBJ, FBX, GLTF, or GLB"}});
        }

        ImportedModelDocument document;
        document.source_path = source_path;
        document.source_dependencies.push_back(
            {ImportedDependencyKind::PrimarySource, source_path.filename().generic_string(),
             source_path, {}});

        if (extension == "obj")
        {
            DiscoverObjMaterialLibraries(document, source_path);
        }
        else if (extension == "gltf")
        {
            DiscoverGltfUriDependencies(document, source_path);
        }

        const aiScene *const scene = impl_->importer.ReadFile(
            source_path.string(),
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
        if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
            scene->mRootNode == nullptr)
        {
            throw ImportedModelDecodeError(
                ImportedModelErrorCode::AssimpFailure,
                "Assimp failed to decode foreign model " + source_path.string() + ": " +
                    impl_->importer.GetErrorString(),
                {{ImportedDiagnosticSeverity::Error, "AssimpFailure", source_path.generic_string(),
                  impl_->importer.GetErrorString()}});
        }

        ReadMaterialMetadata(*scene, document);
        DiscoverMaterialImages(document, *scene, source_path);
        for (unsigned int index = 0; index < scene->mNumTextures; ++index)
        {
            AddEmbeddedTexture(document, *scene,
                               "*" + std::to_string(index));
        }

        std::unordered_map<data::Vertex, std::uint32_t, data::VertexHash> unique_vertices;
        ProcessNode(*scene->mRootNode, *scene, aiMatrix4x4{}, document.mesh, unique_vertices);
        ComputeBounds(document.mesh);
        ValidateSections(document);
        SortDependencies(document);
        return document;
    }
}
