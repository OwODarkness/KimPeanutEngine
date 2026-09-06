#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/asset_load_observation.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/utility.h"
#include "log/logger.h"

#ifndef KPENGINE_MI1_1_FIXTURE_DIR
#error "Asset MI1.1 fixture directory must be provided by CMake"
#endif

namespace
{
    std::filesystem::path CharacterizationFixture(const char *relative_path)
    {
        return std::filesystem::path(KPENGINE_MI1_1_FIXTURE_DIR) / relative_path;
    }

    std::vector<uint8_t> DecodeBase64(const std::string &encoded)
    {
        std::vector<uint8_t> decoded;
        int value = 0;
        int bits = -8;
        for (const unsigned char character : encoded)
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
            if (digit < 0)
            {
                continue;
            }
            value = (value << 6) | digit;
            bits += 6;
            if (bits >= 0)
            {
                decoded.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
                bits -= 8;
            }
        }
        return decoded;
    }

    std::filesystem::path MaterializeGlbFixture()
    {
        const std::filesystem::path encoded_path =
            CharacterizationFixture("glb/triangle.glb.b64");
        std::ifstream encoded_file(encoded_path);
        EXPECT_TRUE(encoded_file.is_open()) << encoded_path.string();
        const std::string encoded((std::istreambuf_iterator<char>(encoded_file)),
                                  std::istreambuf_iterator<char>());
        const std::vector<uint8_t> bytes = DecodeBase64(encoded);
        EXPECT_FALSE(bytes.empty());

        const std::filesystem::path output_path =
            std::filesystem::temp_directory_path() / "kpengine_mi1_1_triangle.glb";
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        EXPECT_TRUE(output.is_open()) << output_path.string();
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        return output_path;
    }

    std::shared_ptr<kpengine::asset::MeshResource> LoadMeshFromModel(
        const kpengine::asset::AssetID model_id)
    {
        auto &assets = kpengine::asset::AssetManager::GetInstance();
        const auto model = assets.GetResource<kpengine::asset::ModelResource>(model_id);
        if (!model)
        {
            return nullptr;
        }
        return assets.GetResource<kpengine::asset::MeshResource>(
            model->GetData(kpengine::asset::ModelGeometryType::KPMG_Mesh));
    }

    std::filesystem::path MakeModelPath()
    {
        return std::filesystem::temp_directory_path() / "kpengine_assimp_face_order.obj";
    }

    std::filesystem::path MakeMultiSectionModelPath()
    {
        return std::filesystem::temp_directory_path() / "kpengine_assimp_multi_section.obj";
    }

    std::filesystem::path MakeTransformModelPath()
    {
        return std::filesystem::temp_directory_path() / "kpengine_assimp_node_transform.gltf";
    }

    std::filesystem::path MakeTransformBufferPath()
    {
        return std::filesystem::temp_directory_path() / "kpengine_assimp_node_transform.bin";
    }
}

TEST(AssimpModelLoaderTest, CharacterizesCheckedInObjGeometrySectionsAndMaterialPaths)
{
    const auto model_id = kpengine::asset::AssetManager::GetInstance().LoadSync(
        CharacterizationFixture("obj/triangle.obj").string());
    ASSERT_TRUE(model_id.IsValid());

    const auto mesh = LoadMeshFromModel(model_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    ASSERT_EQ(mesh->data->vertices.size(), 3u);
    ASSERT_EQ(mesh->data->indices.size(), 3u);
    ASSERT_EQ(mesh->data->sections.size(), 1u);
    ASSERT_EQ(mesh->data->materials.size(), 2u);

    const auto &section = mesh->data->sections.front();
    EXPECT_EQ(section.index_start, 0u);
    EXPECT_EQ(section.index_count, 3u);
    EXPECT_EQ(section.material_index, 1u);
    ASSERT_LT(section.material_index, mesh->data->materials.size());
    const auto &material = mesh->data->materials[section.material_index];
    EXPECT_EQ(material.name, "CharacterizationMaterial");
    EXPECT_FLOAT_EQ(material.base_color.x_, 0.2f);
    EXPECT_FLOAT_EQ(material.base_color.y_, 0.4f);
    EXPECT_FLOAT_EQ(material.base_color.z_, 0.6f);
    EXPECT_EQ(material.base_color_texture, "textures/albedo.ppm");
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.x_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.y_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.z_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.x_, 1.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.y_, 1.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.z_, 0.0f);
}

TEST(AssimpModelLoaderTest, CharacterizesCheckedInFbxGeometry)
{
    const auto model_id = kpengine::asset::AssetManager::GetInstance().LoadSync(
        CharacterizationFixture("fbx/triangle.fbx").string());
    ASSERT_TRUE(model_id.IsValid());

    const auto mesh = LoadMeshFromModel(model_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    EXPECT_EQ(mesh->data->vertices.size(), 3u);
    EXPECT_EQ(mesh->data->indices.size(), 3u);
    ASSERT_EQ(mesh->data->sections.size(), 1u);
    EXPECT_EQ(mesh->data->sections.front().index_start, 0u);
    EXPECT_EQ(mesh->data->sections.front().index_count, 3u);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.x_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.x_, 1.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.y_, 1.0f);
}

TEST(AssimpModelLoaderTest, CharacterizesCheckedInGltfTransformsAndMaterialMetadata)
{
    const auto model_id = kpengine::asset::AssetManager::GetInstance().LoadSync(
        CharacterizationFixture("gltf/transform.gltf").string());
    ASSERT_TRUE(model_id.IsValid());

    const auto mesh = LoadMeshFromModel(model_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    ASSERT_EQ(mesh->data->vertices.size(), 3u);
    ASSERT_EQ(mesh->data->indices.size(), 3u);
    ASSERT_EQ(mesh->data->sections.size(), 1u);
    ASSERT_EQ(mesh->data->materials.size(), 2u);
    ASSERT_LT(mesh->data->sections.front().material_index, mesh->data->materials.size());
    const auto &material = mesh->data->materials[mesh->data->sections.front().material_index];
    EXPECT_EQ(material.name, "CharacterizationGltfMaterial");
    EXPECT_FLOAT_EQ(material.base_color.x_, 0.25f);
    EXPECT_FLOAT_EQ(material.base_color.y_, 0.5f);
    EXPECT_FLOAT_EQ(material.base_color.z_, 0.75f);
    EXPECT_FLOAT_EQ(material.metallic, 0.4f);
    EXPECT_FLOAT_EQ(material.roughness, 0.2f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.x_, 10.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.x_, 11.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.y_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.y_, 1.0f);
}

TEST(AssimpModelLoaderTest, CharacterizesCheckedInGlbContainer)
{
    const std::filesystem::path model_path = MaterializeGlbFixture();
    const auto model_id = kpengine::asset::AssetManager::GetInstance().LoadSync(
        model_path.string());
    ASSERT_TRUE(model_id.IsValid());

    const auto mesh = LoadMeshFromModel(model_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    ASSERT_EQ(mesh->data->vertices.size(), 3u);
    ASSERT_EQ(mesh->data->indices.size(), 3u);
    ASSERT_EQ(mesh->data->sections.size(), 1u);
    ASSERT_EQ(mesh->data->materials.size(), 2u);
    ASSERT_LT(mesh->data->sections.front().material_index, mesh->data->materials.size());
    EXPECT_EQ(mesh->data->materials[mesh->data->sections.front().material_index].name,
              "GlbMaterial");
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.x_, 2.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.x_, 3.0f);

    std::error_code error;
    std::filesystem::remove(model_path, error);
}

TEST(AssimpModelLoaderTest, RecordsMalformedAndMissingCompanionFailures)
{
    auto &assets = kpengine::asset::AssetManager::GetInstance();
    for (const char *fixture : {"failure/malformed.gltf", "failure/missing_buffer.gltf"})
    {
        const auto path = CharacterizationFixture(fixture);
        auto session = assets.BeginLoadObservation();
        EXPECT_FALSE(assets.LoadSync(path.string(), session).IsValid()) << path.string();
        session.Seal();
        const auto snapshot = session.GetSnapshot();
        ASSERT_EQ(snapshot.summary.operations_failed, 1u) << path.string();
        ASSERT_EQ(snapshot.recent_terminal_operations.size(), 1u) << path.string();
        const auto &observation = snapshot.recent_terminal_operations.front();
        EXPECT_EQ(observation.display_path, path.filename().generic_string());
        EXPECT_EQ(observation.phase, kpengine::asset::AssetLoadPhase::LoadSource);
        EXPECT_NE(observation.diagnostic.find("source load: loader rejected the source"),
                  std::string::npos);
    }
}

TEST(AssimpModelLoaderTest, RecordsUnsupportedStlDispatchBaseline)
{
    const auto stl_path = CharacterizationFixture("failure/unsupported.stl");
    EXPECT_FALSE(kpengine::asset::IsModelExtension("stl"));
    EXPECT_EQ(kpengine::asset::ExtractAssetType("stl"),
              kpengine::asset::AssetType::Undefined);
    EXPECT_FALSE(kpengine::asset::AssetManager::GetInstance()
                     .LoadSync(stl_path.string())
                     .IsValid());
}

TEST(AssimpModelLoaderTest, EmitsStableCompatibilityDiagnosticForForeignExtensions)
{
    auto &assets = kpengine::asset::AssetManager::GetInstance();
    auto &logger = kpengine::program::Logger::GetLogger();
    const std::string diagnostic_prefix =
        std::string(kpengine::asset::kForeignModelCompatibilityDiagnostic) + ".";

    for (const char *extension : {"obj", "fbx", "gltf", "glb"})
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                           (std::string("kpengine_r6_missing_foreign.") +
                                            extension);
        std::error_code error;
        std::filesystem::remove(path, error);

        const std::size_t before = logger.GetSnapshot().size();
        EXPECT_FALSE(assets.LoadSync(path.string()).IsValid()) << path.string();
        const std::vector<kpengine::program::LogEntry> entries = logger.GetSnapshot();
        ASSERT_GE(entries.size(), before);

        bool found = false;
        for (std::size_t index = before; index < entries.size(); ++index)
        {
            if (entries[index].message.find(diagnostic_prefix) != std::string::npos)
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << path.string();
    }
}

TEST(AssimpModelLoaderTest, PreservesFaceIndexTopology)
{
    const std::filesystem::path path = MakeModelPath();
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << "v 0 0 0\n"
                "v 1 0 0\n"
                "v 0 1 0\n"
                "v 0 0 1\n"
                "f 1 3 2\n"
                "f 1 2 4\n";
    }

    const kpengine::asset::AssetID model_id =
        kpengine::asset::AssetManager::GetInstance().LoadSync(path.string());
    const auto model = kpengine::asset::AssetManager::GetInstance()
                           .GetResource<kpengine::asset::ModelResource>(model_id);
    ASSERT_TRUE(model_id.IsValid());
    ASSERT_NE(model, nullptr);

    const auto mesh_id = model->GetData(kpengine::asset::ModelGeometryType::KPMG_Mesh);
    const auto mesh = kpengine::asset::AssetManager::GetInstance()
                          .GetResource<kpengine::asset::MeshResource>(mesh_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.x_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.y_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.z_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.x_, 1.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.y_, 1.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.z_, 1.0f);
    ASSERT_EQ(mesh->data->sections.size(), 1u);
    ASSERT_EQ(mesh->data->indices.size(), 6u);
    const auto expect_position = [&](size_t index, float x, float y, float z)
    {
        ASSERT_LT(index, mesh->data->indices.size());
        const uint32_t vertex_index = mesh->data->indices[index];
        ASSERT_LT(vertex_index, mesh->data->vertices.size());
        const auto &position = mesh->data->vertices[vertex_index].position;
        EXPECT_FLOAT_EQ(position.x_, x);
        EXPECT_FLOAT_EQ(position.y_, y);
        EXPECT_FLOAT_EQ(position.z_, z);
    };
    expect_position(0, 0.f, 0.f, 0.f);
    expect_position(1, 0.f, 1.f, 0.f);
    expect_position(2, 1.f, 0.f, 0.f);
    expect_position(3, 0.f, 0.f, 0.f);
    expect_position(4, 1.f, 0.f, 0.f);
    expect_position(5, 0.f, 0.f, 1.f);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(AssimpModelLoaderTest, PreservesMultipleMeshSectionsAndMaterialIndices)
{
    const std::filesystem::path path = MakeMultiSectionModelPath();
    const std::filesystem::path material_path =
        path.parent_path() / "kpengine_assimp_multi_section.mtl";
    {
        std::ofstream material_file(material_path);
        ASSERT_TRUE(material_file.is_open());
        material_file << "newmtl first\nKd 1 0 0\n"
                         "newmtl second\nKd 0 1 0\n";
    }
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << "mtllib kpengine_assimp_multi_section.mtl\n"
                "v 0 0 0\n"
                "v 1 0 0\n"
                "v 0 1 0\n"
                "v 0 0 1\n"
                "v 1 0 1\n"
                "v 0 1 1\n"
                "o first\n"
                "usemtl first\n"
                "f 1 3 2\n"
                "o second\n"
                "usemtl second\n"
                "f 4 5 6\n";
    }

    const kpengine::asset::AssetID model_id =
        kpengine::asset::AssetManager::GetInstance().LoadSync(path.string());
    const auto model = kpengine::asset::AssetManager::GetInstance()
                           .GetResource<kpengine::asset::ModelResource>(model_id);
    ASSERT_TRUE(model_id.IsValid());
    ASSERT_NE(model, nullptr);

    const auto mesh_id = model->GetData(kpengine::asset::ModelGeometryType::KPMG_Mesh);
    const auto mesh = kpengine::asset::AssetManager::GetInstance()
                          .GetResource<kpengine::asset::MeshResource>(mesh_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    ASSERT_EQ(mesh->data->sections.size(), 2u);
    EXPECT_EQ(mesh->data->sections[0].index_start, 0u);
    EXPECT_EQ(mesh->data->sections[0].index_count, 3u);
    EXPECT_EQ(mesh->data->sections[1].index_start, 3u);
    EXPECT_EQ(mesh->data->sections[1].index_count, 3u);
    EXPECT_NE(mesh->data->sections[0].material_index,
              mesh->data->sections[1].material_index);

    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(material_path, error);
}

TEST(AssimpModelLoaderTest, RecognizesGlbModelExtension)
{
    EXPECT_TRUE(kpengine::asset::IsModelExtension("glb"));
    EXPECT_EQ(kpengine::asset::ExtractAssetType("glb"),
              kpengine::asset::AssetType::KPAT_Model);
}

TEST(AssimpModelLoaderTest, BakesGltfNodeTransformAndPreservesMaterialMetadata)
{
    const std::filesystem::path model_path = MakeTransformModelPath();
    const std::filesystem::path buffer_path = MakeTransformBufferPath();
    {
        std::ofstream buffer(buffer_path, std::ios::binary);
        ASSERT_TRUE(buffer.is_open());
        const float positions[] = {0.0f, 0.0f, 0.0f,
                                   1.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f};
        const uint16_t indices[] = {0, 1, 2};
        buffer.write(reinterpret_cast<const char *>(positions), sizeof(positions));
        buffer.write(reinterpret_cast<const char *>(indices), sizeof(indices));
    }
    {
        std::ofstream model(model_path);
        ASSERT_TRUE(model.is_open());
        model << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [10.0, 0.0, 0.0]}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
  "materials": [{
    "name": "TransformMaterial",
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.25, 0.5, 0.75, 1.0],
      "metallicFactor": 0.4,
      "roughnessFactor": 0.2
    }
  }],
  "buffers": [{"uri": "kpengine_assimp_node_transform.bin", "byteLength": 42}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})";
    }

    const kpengine::asset::AssetID model_id =
        kpengine::asset::AssetManager::GetInstance().LoadSync(model_path.string());
    ASSERT_TRUE(model_id.IsValid());
    const auto model = kpengine::asset::AssetManager::GetInstance()
                           .GetResource<kpengine::asset::ModelResource>(model_id);
    ASSERT_NE(model, nullptr);
    const auto mesh_id = model->GetData(kpengine::asset::ModelGeometryType::KPMG_Mesh);
    const auto mesh = kpengine::asset::AssetManager::GetInstance()
                          .GetResource<kpengine::asset::MeshResource>(mesh_id);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->data, nullptr);
    ASSERT_EQ(mesh->data->sections.size(), 1u);
    const uint32_t material_index = mesh->data->sections[0].material_index;
    ASSERT_LT(material_index, mesh->data->materials.size());
    const auto &material = mesh->data->materials[material_index];
    EXPECT_EQ(material.name, "TransformMaterial");
    EXPECT_FLOAT_EQ(material.base_color.x_, 0.25f);
    EXPECT_FLOAT_EQ(material.metallic, 0.4f);
    EXPECT_FLOAT_EQ(material.roughness, 0.2f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.x_, 10.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.x_, 11.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.min_.y_, 0.0f);
    EXPECT_FLOAT_EQ(mesh->local_bounds.max_.y_, 1.0f);

    std::error_code error;
    std::filesystem::remove(model_path, error);
    std::filesystem::remove(buffer_path, error);
}
