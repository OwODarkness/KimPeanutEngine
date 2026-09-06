#include <cstdint>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/utility.h"

namespace
{
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
