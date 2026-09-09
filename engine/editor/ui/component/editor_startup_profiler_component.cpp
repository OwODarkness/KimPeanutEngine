#include "editor/ui/component/editor_startup_profiler_component.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "asset/common.h"
#include "platform/memory_stats_sampler.h"

namespace kpengine::editor
{
    namespace
    {
        const char *StartupPhaseLabel(const runtime::StartupPhase phase) noexcept
        {
            switch (phase)
            {
            case runtime::StartupPhase::Cold:
                return "Starting";
            case runtime::StartupPhase::PresentationStarting:
                return "Starting presentation";
            case runtime::StartupPhase::PresentationReady:
                return "Presentation ready";
            case runtime::StartupPhase::LoadingAssets:
                return "Loading assets";
            case runtime::StartupPhase::PreparingCpuArtifacts:
                return "Preparing CPU artifacts";
            case runtime::StartupPhase::PromotingSceneRenderer:
                return "Promoting scene renderer";
            case runtime::StartupPhase::InstantiatingLevel:
                return "Instantiating level";
            case runtime::StartupPhase::ActivatingEditorWorkspace:
                return "Activating editor workspace";
            case runtime::StartupPhase::Ready:
                return "Ready";
            case runtime::StartupPhase::Closing:
                return "Closing engine";
            case runtime::StartupPhase::Failed:
                return "Startup failed";
            case runtime::StartupPhase::Cancelled:
                return "Startup cancelled";
            case runtime::StartupPhase::RolledBack:
                return "Startup rolled back";
            }
            return "Starting";
        }

        const char *AssetTypeLabel(const asset::AssetType type,
                                   char *custom_buffer,
                                   const std::size_t custom_buffer_size) noexcept
        {
            switch (type)
            {
            case asset::AssetType::Undefined:
                return "Undefined";
            case asset::AssetType::KPAT_Model:
                return "Model";
            case asset::AssetType::KPAT_Texture:
                return "Texture";
            case asset::AssetType::KPAT_Audio:
                return "Audio";
            case asset::AssetType::KPAT_Shader:
                return "Shader";
            case asset::AssetType::KPAT_ShaderProgram:
                return "ShaderProgram";
            case asset::AssetType::KPAT_Mesh:
                return "Mesh";
            case asset::AssetType::KPAT_Material:
                return "Material";
            case asset::AssetType::KPAT_Level:
                return "Level";
            }

            std::snprintf(custom_buffer, custom_buffer_size, "Custom(0x%04X)",
                          static_cast<unsigned int>(static_cast<uint16_t>(type)));
            return custom_buffer;
        }

        std::string FormatMilliseconds(const uint64_t microseconds)
        {
            char value[32]{};
            const double milliseconds = static_cast<double>(microseconds) / 1000.0;
            if (milliseconds >= 1000.0)
            {
                std::snprintf(value, sizeof(value), "%.3f s", milliseconds / 1000.0);
            }
            else
            {
                std::snprintf(value, sizeof(value), "%.3f ms", milliseconds);
            }
            return value;
        }

        std::string FormatBytes(const uint64_t bytes)
        {
            constexpr double kKilobyte = 1024.0;
            constexpr double kMegabyte = kKilobyte * 1024.0;
            constexpr double kGigabyte = kMegabyte * 1024.0;
            char value[32]{};
            if (bytes >= static_cast<uint64_t>(kGigabyte))
            {
                std::snprintf(value, sizeof(value), "%.2f GiB",
                              static_cast<double>(bytes) / kGigabyte);
            }
            else if (bytes >= static_cast<uint64_t>(kMegabyte))
            {
                std::snprintf(value, sizeof(value), "%.2f MiB",
                              static_cast<double>(bytes) / kMegabyte);
            }
            else if (bytes >= static_cast<uint64_t>(kKilobyte))
            {
                std::snprintf(value, sizeof(value), "%.2f KiB",
                              static_cast<double>(bytes) / kKilobyte);
            }
            else
            {
                std::snprintf(value, sizeof(value), "%llu B",
                              static_cast<unsigned long long>(bytes));
            }
            return value;
        }

        const char *BuildConfiguration() noexcept
        {
#if defined(KPENGINE_BUILD_CONFIGURATION_DEBUG)
            return "Debug";
#elif defined(KPENGINE_BUILD_CONFIGURATION_RELWITHDEBINFO)
            return "RelWithDebInfo";
#elif defined(KPENGINE_BUILD_CONFIGURATION_RELEASE)
            return "Release";
#elif defined(KPENGINE_BUILD_CONFIGURATION_MINSIZEREL)
            return "MinSizeRel";
#elif defined(_DEBUG)
            return "Debug (compiler default)";
#else
            return "Unknown";
#endif
        }

        void DrawTimingRow(const char *label, const uint64_t microseconds)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatMilliseconds(microseconds).c_str());
        }

        uint64_t OperationBytes(const asset::AssetLoadObservation &operation) noexcept
        {
            return operation.size_cost.source_file_bytes.value_or(0) +
                   operation.size_cost.decoded_payload_bytes.value_or(0);
        }
    }

    EditorStartupProfilerComponent::EditorStartupProfilerComponent(
        std::function<runtime::StartupSnapshot()> snapshot_source,
        MemoryStatsSampler *memory_sampler)
        : EditorWindowComponent(
              "Startup Profiler",
              EditorWindowConfig{0.62f, 0.06f, 0.36f, 0.62f, false,
                                 ImGuiWindowFlags_HorizontalScrollbar}),
          snapshot_source_(std::move(snapshot_source)),
          memory_sampler_(memory_sampler)
    {
    }

    void EditorStartupProfilerComponent::RenderContent()
    {
        last_snapshot_ = snapshot_source_ ? snapshot_source_() : runtime::StartupSnapshot{};
        if (memory_sampler_ != nullptr)
        {
            peak_process_memory_mb_ = std::max(
                peak_process_memory_mb_, memory_sampler_->Sample().process_mb);
        }

        const runtime::StartupSnapshot &snapshot = last_snapshot_;
        const std::string stage = snapshot.display_label.empty()
                                      ? StartupPhaseLabel(snapshot.phase)
                                      : snapshot.display_label;
        ImGui::Text("AP1.0 baseline");
        ImGui::Text("Stage: %s", stage.c_str());
        ImGui::Text("Build: %s", BuildConfiguration());

        if (!snapshot.asset.has_value())
        {
            ImGui::TextUnformatted("Asset observation: waiting");
            return;
        }

        const asset::AssetLoadSnapshot &asset_snapshot = *snapshot.asset;
        const asset::AssetLoadSummary &summary = asset_snapshot.summary;
        const uint32_t source_reads = summary.operations_started >= summary.cache_hits
                                           ? summary.operations_started - summary.cache_hits
                                           : 0;
        ImGui::Separator();
        ImGui::Text("Asset wall: %s", FormatMilliseconds(summary.wall_elapsed_us).c_str());
        ImGui::Text("Operations: %u started, %u active, %u succeeded, %u failed",
                    summary.operations_started, summary.operations_active,
                    summary.operations_succeeded, summary.operations_failed);
        ImGui::Text("Cache evidence: %u Asset hits, %u source reads",
                    summary.cache_hits, source_reads);
        ImGui::TextUnformatted("Filesystem cache: not classified");
        ImGui::Text("Process peak: %.1f MiB", peak_process_memory_mb_);

        if (ImGui::BeginTable("##StartupProfileCosts", 2,
                              ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Phase");
            ImGui::TableSetupColumn("Exclusive cost");
            ImGui::TableHeadersRow();
            DrawTimingRow("Cache lookup", summary.cost.cumulative_cache_lookup_us);
            DrawTimingRow("Loader queue", summary.cost.cumulative_loader_queue_wait_us);
            DrawTimingRow("Source load", summary.cost.cumulative_source_load_us);
            DrawTimingRow("Dependency wait", summary.cost.cumulative_dependency_wait_us);
            DrawTimingRow("Registration", summary.cost.cumulative_registration_us);
            ImGui::EndTable();
        }

        ImGui::Text("Source bytes: %s (%u measured)",
                    FormatBytes(summary.cost.measured_source_file_bytes).c_str(),
                    summary.cost.source_file_measurement_count);
        ImGui::Text("Decoded bytes: %s (%u measured)",
                    FormatBytes(summary.cost.measured_decoded_payload_bytes).c_str(),
                    summary.cost.decoded_payload_measurement_count);

        if (ImGui::CollapsingHeader("By asset type", ImGuiTreeNodeFlags_DefaultOpen) &&
            ImGui::BeginTable("##StartupProfileTypes", 5,
                              ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Ops");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Decoded");
            ImGui::TableSetupColumn("Source cost");
            ImGui::TableHeadersRow();
            for (const asset::AssetLoadTypeSummary &type : asset_snapshot.type_summaries)
            {
                char custom_type[32]{};
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(AssetTypeLabel(type.type, custom_type,
                                                       sizeof(custom_type)));
                ImGui::TableNextColumn();
                ImGui::Text("%u", type.operations);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatBytes(type.source_file_bytes).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatBytes(type.decoded_payload_bytes).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatMilliseconds(type.source_load_us).c_str());
            }
            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Completed operations",
                                   ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Sort:");
            ImGui::SameLine();
            if (ImGui::Button("Index"))
            {
                sort_mode_ = StartupProfilerSortMode::Completion;
            }
            ImGui::SameLine();
            if (ImGui::Button("Time"))
            {
                sort_mode_ = StartupProfilerSortMode::TotalTime;
            }
            ImGui::SameLine();
            if (ImGui::Button("Source"))
            {
                sort_mode_ = StartupProfilerSortMode::SourceTime;
            }
            ImGui::SameLine();
            if (ImGui::Button("Memory"))
            {
                sort_mode_ = StartupProfilerSortMode::Memory;
            }

            std::vector<const asset::AssetLoadObservation *> operations;
            operations.reserve(asset_snapshot.completed_operations.size());
            for (const asset::AssetLoadObservation &operation :
                 asset_snapshot.completed_operations)
            {
                operations.push_back(&operation);
            }
            std::stable_sort(
                operations.begin(), operations.end(),
                [this](const asset::AssetLoadObservation *left,
                       const asset::AssetLoadObservation *right)
                {
                    switch (sort_mode_)
                    {
                    case StartupProfilerSortMode::Completion:
                        return left->completion_index < right->completion_index;
                    case StartupProfilerSortMode::TotalTime:
                        if (left->timing.inclusive_elapsed_us !=
                            right->timing.inclusive_elapsed_us)
                        {
                            return left->timing.inclusive_elapsed_us >
                                   right->timing.inclusive_elapsed_us;
                        }
                        break;
                    case StartupProfilerSortMode::SourceTime:
                        if (left->timing.source_load_us != right->timing.source_load_us)
                        {
                            return left->timing.source_load_us > right->timing.source_load_us;
                        }
                        break;
                    case StartupProfilerSortMode::Memory:
                        if (OperationBytes(*left) != OperationBytes(*right))
                        {
                            return OperationBytes(*left) > OperationBytes(*right);
                        }
                        break;
                    }
                    return left->completion_index < right->completion_index;
                });

            if (ImGui::BeginChild("##StartupProfileCompleted", ImVec2(0.0f, 240.0f),
                                  true))
            {
                if (ImGui::BeginTable("##StartupProfileCompletedTable", 6,
                                      ImGuiTableFlags_BordersInnerV |
                                          ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_ScrollY))
                {
                    ImGui::TableSetupColumn("#");
                    ImGui::TableSetupColumn("Path");
                    ImGui::TableSetupColumn("Total");
                    ImGui::TableSetupColumn("Source");
                    ImGui::TableSetupColumn("Memory");
                    ImGui::TableSetupColumn("State");
                    ImGui::TableHeadersRow();
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(operations.size()));
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                             ++row)
                        {
                            const asset::AssetLoadObservation &operation = *operations[
                                static_cast<std::size_t>(row)];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%llu", static_cast<unsigned long long>(
                                                     operation.completion_index));
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(operation.display_path.c_str());
                            if (ImGui::IsItemHovered() && !operation.diagnostic.empty())
                            {
                                ImGui::SetTooltip("%s", operation.diagnostic.c_str());
                            }
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(FormatMilliseconds(
                                operation.timing.inclusive_elapsed_us).c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(FormatMilliseconds(
                                operation.timing.source_load_us).c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(
                                FormatBytes(OperationBytes(operation)).c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(operation.state ==
                                                           asset::AssetLoadState::Succeeded
                                                       ? "Succeeded"
                                                       : "Failed");
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }
        }
    }
}
