#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/shader_program.h"

TEST(ShaderProgramVariantTest, GathersOnlyTheRequestedVariant)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "kpengine_shader_variants.shader";
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << R"({
        "version": 1,
        "variants": [
            {"name": "bound", "defines": ["KP_USE_BINDLESS 0"]},
            {"name": "bindless", "defines": ["KP_USE_BINDLESS 1"]}
        ],
        "shaders": [
            {"stage": "vertex", "format": "glsl", "file": "test.vert", "entry": "main"},
            {"stage": "fragment", "format": "glsl", "file": "test.frag", "entry": "main"}
        ]
    })";
    file.close();

    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const kpengine::asset::AssetID id = assets.LoadSync(path.string());
    const auto program = assets.GetResource<kpengine::asset::ShaderProgramResource>(id);

    ASSERT_NE(program, nullptr);
    const auto bound = program->GatherShaders(kpengine::asset::ShaderProgramVariant::Bound);
    const auto bindless = program->GatherShaders(kpengine::asset::ShaderProgramVariant::Bindless);
    ASSERT_EQ(bound.size(), 2u);
    ASSERT_EQ(bindless.size(), 2u);
    for (const auto &shader : bound)
    {
        ASSERT_NE(shader, nullptr);
        EXPECT_EQ(shader->variant, kpengine::asset::ShaderProgramVariant::Bound);
    }
    for (const auto &shader : bindless)
    {
        ASSERT_NE(shader, nullptr);
        EXPECT_EQ(shader->variant, kpengine::asset::ShaderProgramVariant::Bindless);
    }

    std::error_code error;
    std::filesystem::remove(path, error);
}
