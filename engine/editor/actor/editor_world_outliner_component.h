#ifndef KPENGINE_EDITOR_ACTOR_EDITOR_WORLD_OUTLINER_COMPONENT_H
#define KPENGINE_EDITOR_ACTOR_EDITOR_WORLD_OUTLINER_COMPONENT_H

#include "editor/actor/actor_editor_model.h"
#include "editor/ui/component/editor_window_component.h"

namespace kpengine::editor
{
    class EditorWorldOutlinerComponent final : public EditorWindowComponent
    {
    public:
        explicit EditorWorldOutlinerComponent(ActorEditorModel &model);

        void RenderContent() override;

    private:
        ActorEditorModel &model_;
    };
}

#endif
