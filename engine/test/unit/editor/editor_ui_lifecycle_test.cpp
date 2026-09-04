#include <stdexcept>

#include <gtest/gtest.h>
#include <imgui.h>

#include "editor/platform/editor_imgui_renderer.h"
#include "editor/platform/editor_imgui_wsi.h"
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
