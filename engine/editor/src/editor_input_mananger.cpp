#include "editor/include/editor_input_manager.h"

#include <iostream>

#include "editor/include/editor_global_context.h"
#include "runtime/core/system/window_system.h"
namespace kpengine{
    namespace editor{
        void EditorInputManager::Initialize()
        {
            global_editor_context.window_system_->RegisterOnKeyFunc(std::bind(
                &EditorInputManager::KeyCallback, 
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3,
                std::placeholders::_4));
        }

        void EditorInputManager::KeyCallback(int key, int code, int action, int mods)
        {
            std::cout << "Test__key:" << key << " code:" << code << " action:" << action << " mods:" << mods << std::endl; 
        }
    }
}