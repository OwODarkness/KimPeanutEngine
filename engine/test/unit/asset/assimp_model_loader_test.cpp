#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"

namespace
{
    std::filesystem::path MakeModelPath()
    {
        return std::filesystem::temp_directory_path() / "kpengine_assimp_face_order.obj";
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
