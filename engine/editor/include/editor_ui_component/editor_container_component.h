#ifndef KPENGINE_EDITOR_CONTAINER_COMPONENT_H
#define KPENGINE_EDITOR_CONTAINER_COMPONENT_H

#include <vector>
#include <memory>
#include "editor_ui_component.h"

namespace kpengine::editor
{
    class EditorContainerComponent : public EditorUIComponent
    {
    public:
        virtual ~EditorContainerComponent();

    public:
        void AddComponent(std::shared_ptr<EditorUIComponent> component);
        virtual void Render() override;

    private:
        std::vector<std::shared_ptr<EditorUIComponent>> items;
    };
}

#endif