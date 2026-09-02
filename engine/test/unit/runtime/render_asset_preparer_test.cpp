#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "render_asset_preparer.h"

namespace
{
    using namespace kpengine;

    enum class ProcessingMode
    {
        Ready,
        WrongApi,
        Failed,
        InvalidEnvironment,
    };

    class PreparationFixture final
    {
    public:
        PreparationFixture()
        {
            vertex_ = AddShader(ShaderStage::SHADER_STAGE_VERTEX);
            fragment_ = AddShader(ShaderStage::SHADER_STAGE_FRAGMENT);

            auto program = std::make_shared<asset::ShaderProgramResource>();
            program->BindData(ShaderStage::SHADER_STAGE_VERTEX,
                              ShaderFormat::SHADER_FORMAT_GLSL, vertex_);
            program->BindData(ShaderStage::SHADER_STAGE_FRAGMENT,
                              ShaderFormat::SHADER_FORMAT_GLSL, fragment_);
            program_ = AddAsset(asset::AssetType::KPAT_ShaderProgram, program,
                                {vertex_, fragment_}, "preparer-program.shader");
            white_ = AddTexture("preparer-white.png");
            normal_ = AddTexture("preparer-normal.png");
            level_ = AddLevel(false);
        }

        ~PreparationFixture()
        {
            for (auto it = assets_.rbegin(); it != assets_.rend(); ++it)
            {
                manager_.UnRegisterAsset(*it);
            }
        }

        runtime::RenderAssetPreparationHooks Hooks(GraphicsAPIType api,
                                                   ProcessingMode mode,
                                                   asset::AssetID hidden = {})
        {
            runtime::RenderAssetPreparationHooks hooks;
            hooks.load_sync = [this](const std::string &path)
            {
                if (path.find("default_white") != std::string::npos)
                {
                    return white_;
                }
                if (path.find("default_flat_normal") != std::string::npos)
                {
                    return normal_;
                }
                return program_;
            };
            hooks.get_asset = [this, hidden](asset::AssetID id)
            {
                return id == hidden ? nullptr : manager_.GetAsset(id);
            };
            hooks.process_shaders = [this, api, mode](const std::vector<asset::ShaderPtr> &shaders)
            {
                for (const auto &shader : shaders)
                {
                    if (mode == ProcessingMode::Failed)
                    {
                        shader->status = asset::ShaderStatus::CompileFailed;
                        continue;
                    }
                    shader->status = asset::ShaderStatus::Ready;
                    shader->data->api = mode == ProcessingMode::WrongApi
                                             ? (api == GraphicsAPIType::GRAPHICS_API_OPENGL
                                                    ? GraphicsAPIType::GRAPHICS_API_VULKAN
                                                    : GraphicsAPIType::GRAPHICS_API_OPENGL)
                                             : api;
                    if (shader->data->api == GraphicsAPIType::GRAPHICS_API_VULKAN)
                    {
                        shader->data->byte_code = {1};
                    }
                    else
                    {
                        shader->data->source = "void main() {}";
                    }
                }
            };
            hooks.process_environment_ibl = [mode](const data::TextureData &)
                -> std::optional<resource::EnvironmentIblData>
            {
                if (mode == ProcessingMode::InvalidEnvironment)
                {
                    return std::nullopt;
                }
                resource::EnvironmentIblData result{};
                result.irradiance.width = 1;
                result.irradiance.height = 1;
                result.irradiance.pixels.resize(8, 0);
                result.prefiltered_radiance = result.irradiance;
                result.brdf_lut = result.irradiance;
                result.prefilter_level_count = 1;
                return result;
            };
            hooks.processed_shader_count = [this]() { return size_t{2}; };
            return hooks;
        }

        asset::AssetID AddLevel(bool with_environment)
        {
            auto level = std::make_shared<asset::LevelResource>();
            std::vector<asset::AssetID> dependencies;
            if (with_environment)
            {
                level->environment = asset::LevelEnvironmentRecord{
                    {"preparer-environment.hdr", asset::AssetType::KPAT_Texture, 0}, 1.0f};
                dependencies.push_back(environment_);
            }
            return AddAsset(asset::AssetType::KPAT_Level, level, dependencies,
                            with_environment ? "preparer-environment.level"
                                              : "preparer-level.level");
        }

        asset::AssetID AddEnvironmentLevel()
        {
            environment_ = AddTexture("preparer-environment.hdr", TextureFormat::TEXTURE_FORMAT_RGBA16F);
            return AddLevel(true);
        }

        asset::AssetID Program() const { return program_; }

        asset::AssetID level_;
        asset::AssetID environment_;

    private:
        asset::AssetID AddShader(ShaderStage stage)
        {
            auto shader = std::make_shared<asset::ShaderResource>();
            shader->desc.stage = stage;
            shader->format = ShaderFormat::SHADER_FORMAT_GLSL;
            shader->data = std::make_shared<data::ShaderData>();
            shader->data->stage = stage;
            return AddAsset(asset::AssetType::KPAT_Shader, shader, {},
                            stage == ShaderStage::SHADER_STAGE_VERTEX
                                ? "preparer-vertex.shader"
                                : "preparer-fragment.shader");
        }

        asset::AssetID AddTexture(const std::string &path,
                                  TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM)
        {
            auto texture = std::make_shared<asset::TextureResource>();
            texture->data->width = 1;
            texture->data->height = 1;
            texture->data->format = format;
            texture->data->pixels.resize(8, 0);
            return AddAsset(asset::AssetType::KPAT_Texture, texture, {}, path);
        }

        template <typename T>
        asset::AssetID AddAsset(asset::AssetType type, std::shared_ptr<T> resource,
                                std::vector<asset::AssetID> dependencies,
                                const std::string &path)
        {
            asset::AssetRegisterInfo info{};
            info.resource = std::move(resource);
            info.path = path;
            info.name = path;
            info.type = type;
            info.dependencies = std::move(dependencies);
            const asset::AssetID id = manager_.RegisterAsset(info);
            EXPECT_TRUE(id.IsValid());
            assets_.push_back(id);
            return id;
        }

        asset::AssetManager &manager_ = asset::AssetManager::GetInstance();
        std::vector<asset::AssetID> assets_;
        asset::AssetID vertex_;
        asset::AssetID fragment_;
        asset::AssetID program_;
        asset::AssetID white_;
        asset::AssetID normal_;
    };

    runtime::RenderAssetPreparationResult Prepare(PreparationFixture &fixture,
                                                  GraphicsAPIType api,
                                                  ProcessingMode mode,
                                                  asset::AssetID hidden = {})
    {
        return runtime::RenderAssetPreparer(fixture.Hooks(api, mode, hidden))
            .Prepare(fixture.level_, api);
    }
}

TEST(RenderAssetPreparerTest, PublishesValidatedCatalogForBothBackends)
{
    for (const GraphicsAPIType api : {GraphicsAPIType::GRAPHICS_API_VULKAN,
                                     GraphicsAPIType::GRAPHICS_API_OPENGL})
    {
        PreparationFixture fixture;
        const auto result = Prepare(fixture, api, ProcessingMode::Ready);
        ASSERT_TRUE(result) << result.diagnostic;
        EXPECT_EQ(result.catalog->GetPreparedShaderCount(), 2U);
    }
}

TEST(RenderAssetPreparerTest, RejectsMissingDependencyAndFailedBuiltIn)
{
    PreparationFixture missing;
    const auto missing_result = Prepare(missing, GraphicsAPIType::GRAPHICS_API_VULKAN,
                                        ProcessingMode::Ready, missing.Program());
    EXPECT_FALSE(missing_result);
    EXPECT_EQ(missing_result.catalog, nullptr);

    PreparationFixture failed_builtin;
    auto hooks = failed_builtin.Hooks(GraphicsAPIType::GRAPHICS_API_VULKAN,
                                      ProcessingMode::Ready);
    hooks.load_sync = [](const std::string &) { return asset::AssetID{}; };
    const auto failed_result = runtime::RenderAssetPreparer(std::move(hooks)).Prepare(
        failed_builtin.level_, GraphicsAPIType::GRAPHICS_API_VULKAN);
    EXPECT_FALSE(failed_result);
    EXPECT_EQ(failed_result.catalog, nullptr);
}

TEST(RenderAssetPreparerTest, RejectsShaderApiMismatchAndCompileFailure)
{
    PreparationFixture wrong_api;
    EXPECT_FALSE(Prepare(wrong_api, GraphicsAPIType::GRAPHICS_API_VULKAN,
                         ProcessingMode::WrongApi));

    PreparationFixture failed;
    EXPECT_FALSE(Prepare(failed, GraphicsAPIType::GRAPHICS_API_VULKAN,
                         ProcessingMode::Failed));
}

TEST(RenderAssetPreparerTest, RejectsFailedEnvironmentPreparationTransactionally)
{
    PreparationFixture fixture;
    fixture.level_ = fixture.AddEnvironmentLevel();
    const auto result = Prepare(fixture, GraphicsAPIType::GRAPHICS_API_VULKAN,
                                ProcessingMode::InvalidEnvironment);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.catalog, nullptr);
}
