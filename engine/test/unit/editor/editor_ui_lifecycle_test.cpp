#include <stdexcept>

#include <gtest/gtest.h>
#include <imgui.h>

#include "editor/platform/editor_imgui_renderer.h"
#include "editor/platform/editor_imgui_wsi.h"
#include "editor/ui/component/editor_loading_view_model.h"
#include "editor/ui/editor_theme.h"
#include "editor/ui/editor_ui.h"

namespace
{
    class WrongVulkanBridge final : public kpengine::graphics::IEditorPresentationBridge
    {
    public:
        kpengine::GraphicsAPIType GetGraphicsAPI() const override
        {
            return kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN;
        }
    };

    struct LifecycleProbe
    {
        int renderer_shutdowns = 0;
        int wsi_shutdowns = 0;
    };

    class ThrowingRenderer final : public kpengine::editor::IEditorImguiRenderer
    {
    public:
        explicit ThrowingRenderer(LifecycleProbe &probe) : probe_(probe) {}

        bool Initialize(kpengine::graphics::IEditorPresentationBridge *) override
        {
            throw std::runtime_error("synthetic native renderer failure");
        }
        void Shutdown() override { ++probe_.renderer_shutdowns; }
        void NewFrame() override {}
        void Render() override {}
        void SetBackgroundColor(const kpengine::editor::LogColor &) override {}
        ImTextureID GetTextureID(const kpengine::graphics::RenderTargetView &) override
        {
            return ImTextureID{};
        }
        void DrawSceneImage(ImTextureID, const ImVec2 &) override {}

    private:
        LifecycleProbe &probe_;
    };

    class FailingWsi final : public kpengine::editor::IEditorImguiWSI
    {
    public:
        explicit FailingWsi(LifecycleProbe &probe) : probe_(probe) {}

        bool Initialize(kpengine::WindowHandle, kpengine::GraphicsAPIType) override
        {
            return false;
        }
        void Shutdown() override { ++probe_.wsi_shutdowns; }
        void NewFrame() override {}

    private:
        LifecycleProbe &probe_;
    };
}

TEST(EditorLoadingViewModelTest, ReportsStageAssetAndDeterminateProgress)
{
    kpengine::runtime::StartupSnapshot snapshot{};
    snapshot.revision = 7;
    snapshot.phase = kpengine::runtime::StartupPhase::LoadingAssets;
    snapshot.display_label = "Loading startup assets";
    snapshot.progress = {4, 10, true, 0.4f};

    kpengine::asset::AssetLoadSnapshot asset_snapshot{};
    asset_snapshot.summary.operations_started = 3;
    asset_snapshot.summary.operations_succeeded = 2;
    asset_snapshot.summary.operations_active = 1;
    kpengine::asset::AssetLoadObservation operation{};
    operation.operation = 9;
    operation.phase = kpengine::asset::AssetLoadPhase::LoadSource;
    operation.display_path = "level/demo.level";
    asset_snapshot.active_operations.push_back(operation);
    snapshot.asset = asset_snapshot;

    const auto model = kpengine::editor::BuildEditorLoadingViewModel(snapshot);

    EXPECT_EQ(model.revision, 7U);
    EXPECT_EQ(model.stage_label, "Loading startup assets");
    EXPECT_EQ(model.current_item, "Loading source: level/demo.level");
    EXPECT_EQ(model.counts_label, "Assets processed: 2 / 3 (1 active)");
    EXPECT_TRUE(model.determinate);
    EXPECT_FLOAT_EQ(model.fraction, 0.4f);
    EXPECT_FALSE(model.ready);
    EXPECT_FALSE(model.failed);
}

TEST(EditorLoadingViewModelTest, KeepsUnknownProgressIndeterminateAndShowsFailure)
{
    kpengine::runtime::StartupSnapshot snapshot{};
    snapshot.phase = kpengine::runtime::StartupPhase::Failed;
    snapshot.diagnostic = "level/demo.level: missing camera";

    const auto model = kpengine::editor::BuildEditorLoadingViewModel(snapshot);

    EXPECT_FALSE(model.determinate);
    EXPECT_LT(model.fraction, 0.0f);
    EXPECT_TRUE(model.failed);
    EXPECT_EQ(model.stage_label, "Startup failed");
    EXPECT_EQ(model.diagnostic, "level/demo.level: missing camera");
}

TEST(EditorThemeTest, AppliesCodexSurfaceAccentAndTypography)
{
    ImGui::CreateContext();
    kpengine::editor::ApplyCodexTheme();

    const ImGuiStyle &style = ImGui::GetStyle();
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_WindowBg].x, 0x09 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_WindowBg].y, 0x0B / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_WindowBg].z, 0x14 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_Text].x, 0xD7 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_CheckMark].y, 0xE5 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_CheckMark].z, 1.0f);
    EXPECT_FLOAT_EQ(style.WindowRounding, 6.0f);
    EXPECT_FLOAT_EQ(style.FrameRounding, 4.0f);

    ImGui::DestroyContext();
}

TEST(EditorLoadingViewModelTest, NeverShowsCompletionBeforeReady)
{
    kpengine::runtime::StartupSnapshot snapshot{};
    snapshot.phase = kpengine::runtime::StartupPhase::PreparingCpuArtifacts;
    snapshot.progress = {1, 1, true, 1.0f};

    const auto model = kpengine::editor::BuildEditorLoadingViewModel(snapshot);

    EXPECT_TRUE(model.determinate);
    EXPECT_LT(model.fraction, 1.0f);
}

TEST(EditorUILifecycleTest, NullBridgeRollsBackContextAndCloseIsIdempotent)
{
    kpengine::editor::EditorUI ui;

    EXPECT_THROW(ui.Initialize({}), std::runtime_error);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
    EXPECT_NO_THROW(ui.Close());
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}

TEST(EditorUILifecycleTest, WrongBridgeTypeRollsBackPartialBackendSetup)
{
    WrongVulkanBridge bridge;
    kpengine::editor::EditorUI ui;
    kpengine::editor::EditorUIInitInfo init_info{};
    init_info.editor_presentation_bridge = &bridge;

    EXPECT_THROW(ui.Initialize(init_info), std::runtime_error);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
    EXPECT_NO_THROW(ui.Close());
}

TEST(EditorUILifecycleTest, NativeBackendFailureReleasesEveryAcquiredState)
{
    WrongVulkanBridge bridge;
    LifecycleProbe probe;
    kpengine::editor::EditorUI ui;
    kpengine::editor::EditorUIInitInfo init_info{};
    init_info.editor_presentation_bridge = &bridge;
    init_info.renderer_factory = [&probe](kpengine::GraphicsAPIType)
    {
        return std::make_unique<ThrowingRenderer>(probe);
    };
    init_info.wsi_factory = [&probe]
    {
        return std::make_unique<FailingWsi>(probe);
    };

    EXPECT_THROW(ui.Initialize(init_info), std::runtime_error);
    EXPECT_EQ(probe.renderer_shutdowns, 1);
    EXPECT_EQ(probe.wsi_shutdowns, 0);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}
