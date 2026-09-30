#include "live2d_viewer_editor.h"

#include <algorithm>

#include <imgui.h>

#include "editor/log/editor_log_component.h"

namespace kpengine::live2d::editor
{
    struct Live2DViewerEditor::Impl final
    {
        std::unique_ptr<kpengine::editor::EditorUI> ui;
        std::unique_ptr<kpengine::editor::EditorLogComponent> log;
        Live2DViewerEditorState state{};
        Live2DViewerEditorActions actions{};

        void RenderViewer()
        {
            ImGuiViewport *const viewport = ImGui::GetMainViewport();
            const float viewer_width = viewport->WorkSize.x * 0.70f;
            const float viewer_height = viewport->WorkSize.y * 0.75f;
            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(viewer_width, viewer_height),
                                     ImGuiCond_Always);
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin("Live2D Viewer", nullptr, flags))
            {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                constexpr float model_aspect = 720.0f / 960.0f;
                const float image_height = std::min(available.y,
                                                    available.x / model_aspect);
                const ImVec2 image_size(image_height * model_aspect, image_height);
                const ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(
                    cursor.x + (available.x - image_size.x) * 0.5f,
                    cursor.y + (available.y - image_size.y) * 0.5f));
                const ImVec2 image_min = ImGui::GetCursorScreenPos();
                if (actions.report_image_rect)
                {
                    actions.report_image_rect(
                        kpengine::Vector2f{image_min.x, image_min.y},
                        kpengine::Vector2f{image_size.x, image_size.y});
                }
                ui->DrawRenderTarget(state.output_view, image_size);
            }
            ImGui::End();
        }

        void RenderDebugControls()
        {
            static constexpr const char *gaze_labels[] = {
                "Neutral", "Follow Mouse", "Fixed Target"};
            static constexpr const char *behavior_labels[] = {
                "Blink", "Gaze", "Breath", "Physics", "Pose"};
            static constexpr std::uint32_t behavior_bits[] = {
                kLive2DBehaviorBlink, kLive2DBehaviorGaze, kLive2DBehaviorBreath,
                kLive2DBehaviorPhysics, kLive2DBehaviorPose};
            constexpr std::size_t behavior_count = sizeof(behavior_labels) /
                                                    sizeof(behavior_labels[0]);

            if (ImGui::BeginTabBar("##live2d_control_tabs"))
            {
                if (ImGui::BeginTabItem("Gaze"))
                {
                    ImGui::Text("GAZE TARGETING");
                    ImGui::TextDisabled("The runtime smooths this target before applying it.");
                    int gaze_mode = static_cast<int>(state.gaze_mode);
                    gaze_mode = std::clamp(gaze_mode, 0, 2);
                    if (ImGui::BeginCombo("Mode", gaze_labels[gaze_mode]))
                    {
                        for (int index = 0; index < 3; ++index)
                        {
                            const bool selected = gaze_mode == index;
                            if (ImGui::Selectable(gaze_labels[index], selected) &&
                                actions.set_gaze)
                            {
                                actions.set_gaze(static_cast<Live2DGazeMode>(index),
                                                 state.fixed_gaze_target);
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (state.gaze_mode == Live2DGazeMode::FixedTarget)
                    {
                        float target[2] = {state.fixed_gaze_target[0],
                                           state.fixed_gaze_target[1]};
                        if (ImGui::SliderFloat2("Fixed target", target, -1.0f, 1.0f) &&
                            actions.set_gaze)
                        {
                            actions.set_gaze(state.gaze_mode,
                                             kpengine::Vector2f{target[0], target[1]});
                        }
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("CURRENT TARGET");
                    ImGui::Text("(%+.2f, %+.2f)", state.last_gaze_target[0],
                                state.last_gaze_target[1]);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Playback"))
                {
                    ImGui::Text("PLAYBACK CONTROL");
                    ImGui::TextDisabled("Freeze authored motion while inspecting a pose.");
                    if (state.paused)
                    {
                        if (ImGui::Button("RESUME", ImVec2(-1.0f, 0.0f)) &&
                            actions.set_paused)
                        {
                            actions.set_paused(false);
                        }
                    }
                    else if (ImGui::Button("PAUSE", ImVec2(-1.0f, 0.0f)) &&
                             actions.set_paused)
                    {
                        actions.set_paused(true);
                    }
                    if (ImGui::Button("STEP ONE FRAME", ImVec2(-1.0f, 0.0f)))
                    {
                        if (actions.set_paused)
                        {
                            actions.set_paused(true);
                        }
                        if (actions.request_step)
                        {
                            actions.request_step();
                        }
                    }
                    if (ImGui::Button("RESET PARAMETERS", ImVec2(-1.0f, 0.0f)) &&
                        actions.request_reset_parameters)
                    {
                        actions.request_reset_parameters();
                    }
                    ImGui::Separator();
                    ImGui::Text("State: %s", state.paused ? "PAUSED" : "PLAYING");
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Telemetry"))
                {
                    ImGui::Text("RUNTIME TELEMETRY");
                    if (!state.renderer_ready)
                    {
                        ImGui::TextDisabled("Live2D runtime is not ready");
                    }
                    else
                    {
                        ImGui::Text("Update sequence  %llu",
                                    static_cast<unsigned long long>(state.update_sequence));
                        ImGui::Text("Snapshot sequence %llu",
                                    static_cast<unsigned long long>(state.frame_sequence));
                        ImGui::Text("Parameters  %llu   Drawables  %u",
                                    static_cast<unsigned long long>(state.parameter_count),
                                    state.features.drawable_count);
                        ImGui::Separator();
                        ImGui::TextDisabled("ACTIVE BEHAVIORS");
                        for (std::size_t index = 0; index < behavior_count; ++index)
                        {
                            const bool active = (state.behavior_mask & behavior_bits[index]) != 0u;
                            ImGui::Text(active ? "[ ON ]  %s" : "[ -- ]  %s",
                                        behavior_labels[index]);
                            if ((index % 2u) == 0u && index + 1u < behavior_count)
                            {
                                ImGui::SameLine(150.0f);
                            }
                        }
                        ImGui::Separator();
                        ImGui::Text("Hit areas  %s",
                                    state.capabilities.has_hit_areas ? "available" : "none");
                        ImGui::Text("User data   %s",
                                    state.capabilities.has_user_data ? "available" : "none");
                        ImGui::Text("Secondary   %s",
                                    state.capabilities.has_secondary_behavior
                                        ? "available"
                                        : "reimport required");
                        ImGui::Text("Behavior mask  0x%08X",
                                    static_cast<unsigned>(state.behavior_mask));
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Emotion"))
                {
                    ImGui::Text("EMOTION");
                    ImGui::TextDisabled("Choose a face and body-language preset.");
                    const ImVec2 button_size(-1.0f, 30.0f);
                    constexpr std::string_view presets[] = {"Normal", "Sad", "Angry", "Happy"};
                    constexpr const char *labels[] = {"NORMAL", "SAD", "ANGRY", "HAPPY"};
                    for (std::size_t index = 0; index < 4u; ++index)
                    {
                        if (ImGui::Button(labels[index], button_size) && actions.apply_emotion)
                        {
                            actions.apply_emotion(presets[index]);
                        }
                    }
                    ImGui::Separator();
                    ImGui::Text("Current  %s", state.emotion_status.c_str());
                    if (!state.emotion_diagnostic.empty())
                    {
                        ImGui::TextWrapped("%s", state.emotion_diagnostic.c_str());
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }

        void RenderControlPanel()
        {
            ImGuiViewport *const viewport = ImGui::GetMainViewport();
            const ImVec2 panel_pos(viewport->WorkPos.x + viewport->WorkSize.x * 0.70f,
                                   viewport->WorkPos.y);
            const ImVec2 panel_size(viewport->WorkSize.x * 0.30f,
                                    viewport->WorkSize.y * 0.43f);
            ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(panel_size, ImGuiCond_Always);
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoCollapse |
                                                ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("Live2D Control Deck", nullptr, flags))
            {
                ImGui::TextDisabled("RUNTIME DEBUG");
                ImGui::Separator();
                RenderDebugControls();
            }
            ImGui::End();
        }

        void RenderProfilerWindow()
        {
            ImGuiViewport *const viewport = ImGui::GetMainViewport();
            const ImVec2 profiler_pos(viewport->WorkPos.x + viewport->WorkSize.x * 0.70f,
                                      viewport->WorkPos.y + viewport->WorkSize.y * 0.44f);
            const ImVec2 profiler_size(viewport->WorkSize.x * 0.30f,
                                       viewport->WorkSize.y * 0.31f);
            ImGui::SetNextWindowPos(profiler_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(profiler_size, ImGuiCond_Always);
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoCollapse |
                                                ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("Performance Profiler", nullptr, flags))
            {
                ImGui::TextDisabled("GPU submission and frame timing");
                ImGui::Separator();
                ImGui::Text("API: %s",
                            state.graphics_api == GraphicsAPIType::GRAPHICS_API_VULKAN
                                ? "Vulkan"
                                : "OpenGL");
                ImGui::Separator();
                ImGui::Text("Frame %.2f ms", state.frame_total_ms);
                ImGui::Text("Render work %.2f ms", state.render_work_ms);
                ImGui::Text("ImGui work %.2f ms", state.imgui_work_ms);
                ImGui::Text("Tick work %.2f ms", state.game_tick_work_ms);
                ImGui::Separator();
                ImGui::TextDisabled("Live2D / backend");
                ImGui::Text("Draw calls %llu",
                            static_cast<unsigned long long>(
                                state.recorder_counters.draw_calls_emitted));
                ImGui::Text("Pipeline binds %llu",
                            static_cast<unsigned long long>(
                                state.recorder_counters.pipeline_bind_emitted));
                ImGui::Text("Resource binds %llu",
                            static_cast<unsigned long long>(
                                state.recorder_counters.resource_binding_bind_emitted));
                ImGui::Text("Descriptor updates %llu",
                            static_cast<unsigned long long>(state.backend_counters.descriptor_updates));
                ImGui::Text("Module GPU handles %u", state.live_gpu_handles);
                ImGui::Text("Frame sequence %llu",
                            static_cast<unsigned long long>(state.frame_sequence));
                ImGui::Separator();
                ImGui::TextDisabled("ImGui");
                ImGui::Text("Build %.2f ms", ui->GetLastImGuiBuildTimeMs());
                ImGui::Text("Submit %.2f ms", ui->GetLastImGuiSubmitTimeMs());
                ImGui::Text("Total %.2f ms", ui->GetLastRenderTimeMs());
            }
            ImGui::End();
        }

        void Render()
        {
            RenderViewer();
            if (log)
            {
                log->Render();
            }
            RenderControlPanel();
            RenderProfilerWindow();
        }
    };

    Live2DViewerEditor::Live2DViewerEditor() : impl_(std::make_unique<Impl>()) {}

    Live2DViewerEditor::~Live2DViewerEditor() = default;

    bool Live2DViewerEditor::Initialize(
        const kpengine::editor::EditorUIInitInfo &init_info,
        const kpengine::editor::LogLevelColorTable &log_colors,
                                        std::string &diagnostic)
    {
        diagnostic.clear();
        impl_->log = std::make_unique<kpengine::editor::EditorLogComponent>(
            init_info.log_system, log_colors,
            kpengine::editor::EditorWindowConfig{0.0f, 0.75f, 1.0f, 0.25f, true});
        impl_->ui = std::make_unique<kpengine::editor::EditorUI>();
        impl_->ui->InitializeViewer(init_info, [this] { impl_->Render(); });
        return true;
    }

    bool Live2DViewerEditor::Render(const Live2DViewerEditorState &state,
                                    Live2DViewerEditorActions actions,
                                    std::string &diagnostic)
    {
        diagnostic.clear();
        if (impl_->ui == nullptr)
        {
            diagnostic = "Live2D editor UI is not initialized";
            return false;
        }
        impl_->state = state;
        impl_->actions = std::move(actions);
        if (!impl_->ui->Render())
        {
            diagnostic = "Live2D viewer ImGui presentation failed";
            return false;
        }
        return true;
    }

    void Live2DViewerEditor::Close() noexcept
    {
        if (impl_ && impl_->ui)
        {
            impl_->ui->Close();
        }
        if (impl_)
        {
            impl_->ui.reset();
            impl_->log.reset();
        }
    }

    double Live2DViewerEditor::GetLastImGuiBuildTimeMs() const noexcept
    {
        return impl_->ui ? impl_->ui->GetLastImGuiBuildTimeMs() : 0.0;
    }

    double Live2DViewerEditor::GetLastImGuiSubmitTimeMs() const noexcept
    {
        return impl_->ui ? impl_->ui->GetLastImGuiSubmitTimeMs() : 0.0;
    }

    double Live2DViewerEditor::GetLastRenderTimeMs() const noexcept
    {
        return impl_->ui ? impl_->ui->GetLastRenderTimeMs() : 0.0;
    }
}
