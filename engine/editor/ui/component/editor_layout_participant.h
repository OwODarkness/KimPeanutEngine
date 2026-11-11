#ifndef KPENGINE_EDITOR_LAYOUT_PARTICIPANT_H
#define KPENGINE_EDITOR_LAYOUT_PARTICIPANT_H

#include <optional>

#include "editor/ui/component/editor_layout_model.h"

namespace kpengine::editor
{
    // Workspace placement is an optional capability, not part of every UI component.
    class EditorLayoutParticipant
    {
    public:
        virtual ~EditorLayoutParticipant() = default;
        virtual std::optional<EditorLayoutSlot> GetLayoutSlot() const noexcept = 0;
        virtual void ApplyLayout(std::optional<EditorRect> rect) noexcept = 0;
    };
}

#endif // KPENGINE_EDITOR_LAYOUT_PARTICIPANT_H
