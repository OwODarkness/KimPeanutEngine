#include "editor/ui/component/editor_loading_view_model.h"

#include <algorithm>
#include <cstdint>

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
            case runtime::StartupPhase::Failed:
                return "Startup failed";
            case runtime::StartupPhase::Cancelled:
                return "Startup cancelled";
            case runtime::StartupPhase::RolledBack:
                return "Startup rolled back";
            }
            return "Starting";
        }

        const char *AssetPhaseLabel(const asset::AssetLoadPhase phase) noexcept
        {
            switch (phase)
            {
            case asset::AssetLoadPhase::CacheLookup:
                return "Cache lookup";
            case asset::AssetLoadPhase::WaitingForLoader:
                return "Waiting for loader";
            case asset::AssetLoadPhase::LoadSource:
                return "Loading source";
            case asset::AssetLoadPhase::ResolveDependencies:
                return "Resolving dependencies";
            case asset::AssetLoadPhase::Register:
                return "Registering";
            }
            return "Loading";
        }

        const asset::AssetLoadObservation *FindCurrentAsset(
            const asset::AssetLoadSnapshot &snapshot) noexcept
        {
            if (!snapshot.active_operations.empty())
            {
                return &*std::max_element(
                    snapshot.active_operations.begin(), snapshot.active_operations.end(),
                    [](const asset::AssetLoadObservation &left,
                       const asset::AssetLoadObservation &right)
                    { return left.operation < right.operation; });
            }
            if (!snapshot.recent_terminal_operations.empty())
            {
                return &snapshot.recent_terminal_operations.back();
            }
            return nullptr;
        }

        std::string BuildCountsLabel(const asset::AssetLoadSnapshot &snapshot)
        {
            const uint64_t completed =
                static_cast<uint64_t>(snapshot.summary.operations_succeeded) +
                static_cast<uint64_t>(snapshot.summary.operations_failed);
            const uint32_t started = snapshot.summary.operations_started;
            if (started == 0U)
            {
                return "Assets: waiting for work";
            }

            std::string result = "Assets processed: " + std::to_string(completed) +
                                 " / " + std::to_string(started);
            if (snapshot.summary.operations_active > 0U)
            {
                result += " (" + std::to_string(snapshot.summary.operations_active) +
                          " active)";
            }
            return result;
        }
    }

    EditorLoadingViewModel BuildEditorLoadingViewModel(
        const runtime::StartupSnapshot &snapshot)
    {
        EditorLoadingViewModel model{};
        model.revision = snapshot.revision;
        model.ready = snapshot.phase == runtime::StartupPhase::Ready;
        model.failed = snapshot.phase == runtime::StartupPhase::Failed ||
                       snapshot.phase == runtime::StartupPhase::Cancelled ||
                       snapshot.phase == runtime::StartupPhase::RolledBack;
        model.stage_label = snapshot.display_label.empty()
                                ? StartupPhaseLabel(snapshot.phase)
                                : snapshot.display_label;

        if (snapshot.progress.total_known && snapshot.progress.total_units > 0U)
        {
            model.determinate = true;
            model.fraction = std::clamp(snapshot.progress.fraction, 0.0f, 1.0f);
            if (!model.ready)
            {
                model.fraction = std::min(model.fraction, 0.999f);
            }
        }

        if (snapshot.asset.has_value())
        {
            const asset::AssetLoadSnapshot &asset_snapshot = *snapshot.asset;
            model.counts_label = BuildCountsLabel(asset_snapshot);
            const asset::AssetLoadObservation *current_asset =
                FindCurrentAsset(asset_snapshot);
            if (current_asset != nullptr && !current_asset->display_path.empty())
            {
                model.current_item = std::string{AssetPhaseLabel(current_asset->phase)} +
                                     ": " + current_asset->display_path;
            }
            model.diagnostic = asset_snapshot.summary.first_failure;
        }
        else
        {
            model.counts_label = "Assets: waiting for observation";
        }

        if (!snapshot.diagnostic.empty())
        {
            model.diagnostic = snapshot.diagnostic;
        }
        return model;
    }
}
