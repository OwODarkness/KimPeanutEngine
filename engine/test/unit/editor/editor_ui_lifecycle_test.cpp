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
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_WindowBg].x, 0x0B / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_WindowBg].y, 0x0E / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_WindowBg].z, 0x16 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_Text].x, 0xD4 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_CheckMark].x, 0xA2 / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_CheckMark].y, 0xBF / 255.0f);
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_CheckMark].z, 0xCC / 255.0f);
    EXPECT_FLOAT_EQ(style.WindowRounding, 6.0f);
    EXPECT_FLOAT_EQ(style.FrameRounding, 4.0f);

    ImGui::DestroyContext();
}

// Can a real ImGui frame run with no window, no renderer, and no GPU? This is a
// capability probe, not a behavioural test: it fixes the boundary of what the current
// harness can reach, so nobody has to rediscover it.
//
// PROVEN HERE, and it contradicts what the ED1/ED2 journals claim: the blocker is only
// EditorUI::Render(), which needs the WSI and renderer seams. A COMPONENT-level frame runs
// headlessly. Context, font atlas, NewFrame, Begin/End, widget layout, item rectangles and
// EndFrame all work with no backend, so layout and hit-testing geometry are testable today.
//
// STILL OPEN: scripting a CLICK. With io.ConfigInputTrickleEventQueue disabled and the
// event queue fed exactly as a backend does it, the frame reports hovered=1, mouseDown=1
// and mouseClicked=1 while Button still never activates — so the state ImGui needs is
// present but something in 1.91's input-ownership path is not satisfied. Until that is
// understood, interaction scripts (tab clicks, drag gestures, menu activation) are not
// available, and that is the concrete thing a harness stage must solve. Do not assume a
// harness works until a scripted click is demonstrated.
TEST(EditorImguiHarnessProbe, AComponentFrameRunsHeadlesslyWithoutABackend)
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    ASSERT_TRUE(io.Fonts->Build()) << "font atlas must build before NewFrame";
    // Trickling spreads queued events over frames; a test feeds one state per frame.
    io.ConfigInputTrickleEventQueue = false;

    ImVec2 button_size(0.0f, 0.0f);
    for (int frame = 0; frame < 3; ++frame)
    {
        io.AddMousePosEvent(15.0f, 45.0f);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_Always);
        if (ImGui::Begin("probe"))
        {
            ImGui::SetCursorScreenPos(ImVec2(10.0f, 40.0f));
            (void)ImGui::Button("target", ImVec2(100.0f, 24.0f));
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            button_size = ImVec2(item_max.x - item_min.x, item_max.y - item_min.y);
            EXPECT_FALSE(ImGui::IsAnyItemActive()) << "no input is scripted here";
        }
        ImGui::End();
        // No backend, so no ImGui::Render(): EndFrame is the whole frame boundary.
        ImGui::EndFrame();
    }

    EXPECT_FLOAT_EQ(button_size.x, 100.0f) << "layout produced the requested item size";
    EXPECT_FLOAT_EQ(button_size.y, 24.0f);
    EXPECT_FLOAT_EQ(io.DisplaySize.x, 1280.0f) << "the frame consumed the injected display";

    ImGui::DestroyContext();
}
