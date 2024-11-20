#include "editor/include/editor_input_manager.h"

#include <iostream>

#include "editor/include/editor_global_context.h"
#include "editor/include/editor_scene_manager.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/window_system.h"
#include "runtime/render/render_camera.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"

namespace kpengine
{
    namespace editor
    {
        void EditorInputManager::Initialize()
        {
            global_editor_context.window_system_->RegisterOnKeyFunc(std::bind(
                &EditorInputManager::KeyCallback,
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3,
                std::placeholders::_4));
            global_editor_context.window_system_->RegisterOnMouseButtionFunc(std::bind(
                &EditorInputManager::MouseButtonCallback,
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));

            global_editor_context.window_system_->RegisterOnCursorPosFunc(std::bind(
                &EditorInputManager::CursorPosCallback,
                this,
                std::placeholders::_1,
                std::placeholders::_2));

             camera_ = global_editor_context.render_system_->GetRenderCamera();

        }

        void EditorInputManager::Tick(float DeltaTime)
        {
            //HandleInput();
        }

        void EditorInputManager::HandleInput()
        {
            if (static_cast<unsigned int>(EditorCommand::CAMERA_FORWARD) & editor_command_)
            {
                camera_->MoveForward(1.f);
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_BACKWARD) & editor_command_)
            {
                camera_->MoveForward(-1.f);
            } 
            if (static_cast<unsigned int>(EditorCommand::CAMERA_RIGHT) & editor_command_)
            {
                camera_->MoveRight(1.f);
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_LEFT) & editor_command_)
            {
                camera_->MoveRight(-1.f);
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_UP) & editor_command_)
            {
                camera_->MoveUp(1.f);
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_DOWN) & editor_command_)
            {
                camera_->MoveUp(-1.f);
            }
            
        }

        void EditorInputManager::KeyCallback(int key, int code, int action, int mods)
        {
            // std::cout << "Test__key:" << key << " code:" << code << " action:" << action << " mods:" << mods << std::endl;
            bool is_focus = global_editor_context.editor_scene_manager_->IsSCeneFocus();
            if(!is_focus)
            {
                return ;
            }
            if (GLFW_PRESS == action)
            {
                switch (key)
                {
                case GLFW_KEY_W:
                    editor_command_ |= static_cast<unsigned int>(EditorCommand::CAMERA_FORWARD);
                    break;
                case GLFW_KEY_S:
                    editor_command_ |= static_cast<unsigned int>(EditorCommand::CAMERA_BACKWARD);
                    break;
                case GLFW_KEY_A:
                    editor_command_ |= static_cast<unsigned int>(EditorCommand::CAMERA_LEFT);
                    break;
                case GLFW_KEY_D:
                    editor_command_ |= static_cast<unsigned int>(EditorCommand::CAMERA_RIGHT);
                    break;
                case GLFW_KEY_SPACE:
                    editor_command_ |= static_cast<unsigned int>(EditorCommand::CAMERA_UP);
                    break;
                case GLFW_KEY_LEFT_CONTROL:
                    editor_command_ |= static_cast<unsigned int>(EditorCommand::CAMERA_DOWN);
                    break;
                default:
                    break;
                }
            }
            else if (GLFW_RELEASE == action)
            {
                switch (key)
                {
                case GLFW_KEY_W:
                    editor_command_ &= !(static_cast<unsigned int>(EditorCommand::CAMERA_FORWARD));
                    break;
                case GLFW_KEY_S:
                    editor_command_ &= !(static_cast<unsigned int>(EditorCommand::CAMERA_BACKWARD));
                    break;
                case GLFW_KEY_A:
                    editor_command_ &= !(static_cast<unsigned int>(EditorCommand::CAMERA_LEFT));
                    break;
                case GLFW_KEY_D:
                    editor_command_ &= !(static_cast<unsigned int>(EditorCommand::CAMERA_RIGHT));
                    break;
                case GLFW_KEY_SPACE:
                    editor_command_ &= !(static_cast<unsigned int>(EditorCommand::CAMERA_UP));
                    break;
                case GLFW_KEY_LEFT_CONTROL:
                    editor_command_ &= !(static_cast<unsigned int>(EditorCommand::CAMERA_DOWN));
                    break;
                default:
                    break;
                }
            }

            HandleInput();
        }

        void EditorInputManager::MouseButtonCallback(int code, int action, int mods)
        {
            // std::cout << "Test__mouse:"  << " code:" << code << " action:" << action << " mods:" << mods << std::endl;

        }

        void EditorInputManager::CursorPosCallback(double xpos, double ypos)
        {
            bool is_focus = global_editor_context.editor_scene_manager_->IsSCeneFocus();
            if(!is_focus)
            {
                is_first_cursor = true;
                return ;
            }
            if(!is_first_cursor)
            {
            double delta_x = xpos - last_cursor_xpos_;
            double delta_y = ypos - last_cursor_ypos_;
            camera_->Rotate(glm::vec2(-delta_y , delta_x));

            }
            is_first_cursor = false;
            //std::cout << "delta_x:" << delta_x << " delta_y:" << delta_y << std::endl;
            last_cursor_xpos_ = xpos;
            last_cursor_ypos_ = ypos;

            
        }
    }
}