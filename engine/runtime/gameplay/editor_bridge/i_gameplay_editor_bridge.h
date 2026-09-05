#ifndef KPENGINE_RUNTIME_GAMEPLAY_EDITOR_BRIDGE_I_GAMEPLAY_EDITOR_BRIDGE_H
#define KPENGINE_RUNTIME_GAMEPLAY_EDITOR_BRIDGE_I_GAMEPLAY_EDITOR_BRIDGE_H

#include <memory>
#include <vector>

#include "gameplay/editor_bridge/gameplay_editor_bridge_types.h"

namespace kpengine::gameplay
{
    class IGameplayEditorSnapshotSource
    {
    public:
        virtual ~IGameplayEditorSnapshotSource() = default;

        virtual std::shared_ptr<const GameplayEditorSnapshot>
        GetLatestSnapshot() const = 0;
        virtual std::vector<PropertyEditResult> ConsumeEditResults() = 0;
    };

    class IGameplayEditorEditSink
    {
    public:
        virtual ~IGameplayEditorEditSink() = default;

        virtual PropertyEditSubmission SubmitPropertyEdit(
            PropertyEditCommand command) = 0;
    };
}

#endif
