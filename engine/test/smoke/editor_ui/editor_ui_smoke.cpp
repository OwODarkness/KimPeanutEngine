#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "base/type.h"
#include "editor/ui/host/editor_ui_host.h"
#if defined(KPENGINE_EDITOR_UI_SMOKE_SETTINGS_FIELDS)
#include "editor/settings/settings_property_fields_view.h"
#endif
#include "graphics/backend/common/render_backend.h"
#include "window/window_system.h"

namespace
{
    kpengine::GraphicsAPIType ParseGraphicsApi(const int argc, char **argv)
    {
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (std::string_view{argv[index]} != "--graphics-api")
            {
                continue;
            }
            const std::string_view value{argv[index + 1]};
            if (value == "opengl")
            {
                return kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL;
            }
            if (value == "vulkan")
            {
                return kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN;
            }
            throw std::invalid_argument("--graphics-api must be opengl or vulkan");
        }
#if defined(KPENGINE_EDITOR_UI_SMOKE_DEFAULT_OPENGL)
        return kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL;
#else
        return kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN;
#endif
    }

    void DrawSmokeContent(kpengine::editor::ui::UiContext &ui, bool &enabled,
                          const std::string_view observation_report)
    {
        auto window = ui.Window({.id = "smoke.window", .label = "Editor UI Smoke"},
                                {24.0f, 28.0f}, {420.0f, 220.0f});
        if (!window.ContentVisible())
        {
            return;
        }

        ui.Section("Independent production presentation");
#if defined(KPENGINE_EDITOR_UI_SMOKE_SETTINGS_FIELDS)
        ui.Text("The real Settings field view runs on the production backend.");
#else
        ui.Text("This host links Window, Graphics, and Editor UI targets only.");
#endif
        ui.Text(observation_report);
#if defined(KPENGINE_EDITOR_UI_SMOKE_SETTINGS_FIELDS)
        static const kpengine::settings::SettingPropertyDefinition property = []
        {
            kpengine::settings::SettingPropertyDefinition value{};
            value.key = "enabled";
            value.value_type = kpengine::reflection::ReflectionValueType::Bool;
            value.metadata.display_name = "Neutral checkbox";
            return value;
        }();
        const auto edit = kpengine::editor::SettingsPropertyFieldsView::Draw(
            ui, "smoke", property, kpengine::reflection::ReflectionValue{enabled});
        if (edit.changed)
        {
            enabled = std::get<bool>(edit.value);
        }
#else
        const auto edit = ui.Checkbox(
            {.id = "smoke.enabled", .label = "Neutral checkbox"}, enabled);
        if (edit.changed)
        {
            enabled = edit.value;
        }
#endif
        ui.Text(enabled ? "Checkbox state: enabled" : "Checkbox state: disabled");
        (void)ui.Button({.id = "smoke.button", .label = "Neutral button"});
    }
}

int main(int argc, char **argv)
{
    std::unique_ptr<kpengine::WindowSystem> window;
    std::unique_ptr<kpengine::graphics::RenderBackend> graphics;
    std::unique_ptr<kpengine::editor::EditorUiHost> ui_host;
    bool graphics_initialized = false;
    bool frame_active = false;

    try
    {
        const kpengine::GraphicsAPIType api = ParseGraphicsApi(argc, argv);
        window = kpengine::WindowSystem::CreateWindowSystem(
            kpengine::WindowAPIType::WINDOW_API_GLFW);
        if (!window->Initialize({.width = 1280,
                                 .height = 720,
                                 .title = "Editor UI Smoke",
                                 .graphics_api_type = api,
                                 .vsync = true}))
        {
            throw std::runtime_error("Window system initialization failed");
        }

        graphics = kpengine::graphics::RenderBackend::CreateGraphicsBackEnd(api);
        if (!graphics)
        {
            throw std::runtime_error("Requested graphics backend is not built");
        }
        graphics->Initialize(window->GetNativeHandle());
        graphics_initialized = true;
        graphics->BindWindowResize(window->resize_event_dispatcher_);

        ui_host = std::make_unique<kpengine::editor::EditorUiHost>();
        ui_host->Initialize({
            .window = window->GetNativeHandle(),
            .presentation_bridge = graphics->GetEditorPresentationBridge()});

        bool enabled = true;
        bool observation_verified = false;
        std::string observation_report = "Bounds: pending";
        while (!window->ShouldClose())
        {
            window->PollEvents();
            graphics->BeginFrame();
            frame_active = true;
            if (!ui_host->RenderFrame(
                    [&enabled, &observation_report](kpengine::editor::ui::UiContext &context)
                    {
                        DrawSmokeContent(context, enabled, observation_report);
                    }, {}, true))
            {
                throw std::runtime_error("Production UI host could not render a frame");
            }
            if (!observation_verified)
            {
                const auto &observations = ui_host->LastObservation();
                const auto checkbox = std::find_if(
                    observations.begin(), observations.end(),
                    [](const kpengine::editor::ui::UiNodeObservation &observation)
                    {
                        return observation.label == "Neutral checkbox";
                    });
#if defined(KPENGINE_EDITOR_UI_SMOKE_SETTINGS_FIELDS)
                const auto expected_id =
                    kpengine::editor::SettingsPropertyFieldsView::StableId("smoke", "enabled");
#else
                const auto expected_id =
                    kpengine::editor::ui::StableUiNodeId("smoke.enabled");
#endif
                if (checkbox == observations.end() ||
                    checkbox->id != expected_id ||
                    checkbox->parent !=
                        kpengine::editor::ui::StableUiNodeId("smoke.window") ||
                    checkbox->role != kpengine::editor::ui::UiRole::CheckBox ||
                    checkbox->label != "Neutral checkbox" ||
                    !std::holds_alternative<bool>(checkbox->value) ||
                    !std::get<bool>(checkbox->value) ||
                    !checkbox->enabled || !checkbox->disabled_reason.empty() ||
                    checkbox->last_action.has_value() || checkbox->geometry !=
                        kpengine::editor::ui::UiGeometryProvenance::ActualBackend ||
                    checkbox->bounds.max.x <= checkbox->bounds.min.x ||
                    checkbox->bounds.max.y <= checkbox->bounds.min.y ||
                    std::find(checkbox->actions.begin(), checkbox->actions.end(),
                              kpengine::editor::ui::UiActionKind::Toggle) ==
                        checkbox->actions.end())
                {
                    throw std::runtime_error(
                        "Production UI observation lacks checkbox semantics or actual bounds");
                }
                observation_verified = true;
                observation_report = "Bounds: " +
                    std::to_string(static_cast<int>(checkbox->bounds.min.x)) + "," +
                    std::to_string(static_cast<int>(checkbox->bounds.min.y)) + " - " +
                    std::to_string(static_cast<int>(checkbox->bounds.max.x)) + "," +
                    std::to_string(static_cast<int>(checkbox->bounds.max.y));
                std::cout << "Verified production checkbox observation and bounds\n";
            }

            graphics->EndFrame();
            frame_active = false;
            window->SwapBuffers();
        }

        ui_host->Close();
        ui_host.reset();

        if (const auto diagnostic = graphics->GetValidationDiagnostic(); diagnostic.has_value())
        {
            throw std::runtime_error("Graphics validation reported: " + *diagnostic);
        }
        graphics->Cleanup();
        graphics_initialized = false;
        graphics.reset();
        window->Cleanup();
        window.reset();
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "EditorUiSmoke failed: " << error.what() << '\n';
    }

    if (frame_active && graphics)
    {
        graphics->EndFrame();
    }
    if (ui_host) ui_host->Close();
    if (graphics && graphics_initialized)
    {
        graphics->WaitIdle();
        graphics->Cleanup();
    }
    if (window)
    {
        window->Cleanup();
    }
    return EXIT_FAILURE;
}
