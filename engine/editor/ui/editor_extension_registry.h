#ifndef KPENGINE_EDITOR_EXTENSION_REGISTRY_H
#define KPENGINE_EDITOR_EXTENSION_REGISTRY_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::editor
{
    class EditorUIComponent;
    class IEditorImguiRenderer;

    using EditorWorkspaceComponentFactory =
        std::function<std::unique_ptr<EditorUIComponent>(
            kpengine::render::RenderSystem *, IEditorImguiRenderer *)>;

    class EditorExtensionRegistry final
    {
    public:
        bool RegisterWorkspaceComponentFactory(
            EditorWorkspaceComponentFactory factory);

        std::vector<EditorWorkspaceComponentFactory>
        SnapshotWorkspaceComponentFactories() const;

    private:
        mutable std::mutex mutex_;
        std::vector<EditorWorkspaceComponentFactory> workspace_factories_;
    };

    EditorExtensionRegistry &GetEditorExtensionRegistry() noexcept;
}

#endif
