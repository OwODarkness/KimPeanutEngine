#include "editor/ui/editor_extension_registry.h"

#include <utility>

namespace kpengine::editor
{
    bool EditorExtensionRegistry::RegisterWorkspaceComponentFactory(
        EditorWorkspaceComponentFactory factory)
    {
        if (!factory)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        workspace_factories_.push_back(std::move(factory));
        return true;
    }

    std::vector<EditorWorkspaceComponentFactory>
    EditorExtensionRegistry::SnapshotWorkspaceComponentFactories() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return workspace_factories_;
    }

    EditorExtensionRegistry &GetEditorExtensionRegistry() noexcept
    {
        static EditorExtensionRegistry registry;
        return registry;
    }
}
