#include "editor/include/editor_input_manager.h"

#include <iostream>

#include "editor/include/editor_global_context.h"
// #include "editor/include/editor_scene_manager.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/window_system.h"
#include "runtime/render/render_camera.h"
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
            float camera_move_z = 0.f;
            float camera_move_x = 0.f;
            float camera_move_y = 0.f;
            if (static_cast<unsigned int>(EditorCommand::CAMERA_FORWARD) & editor_command_)
            {
                camera_move_z += -1.f;
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_BACKWARD) & editor_command_)
            {
                camera_move_z += 1.f;
            } 
            if (static_cast<unsigned int>(EditorCommand::CAMERA_RIGHT) & editor_command_)
            {
                camera_move_x += 1.f;
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_LEFT) & editor_command_)
            {
                camera_move_x += -1.f;
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_UP) & editor_command_)
            {
                camera_move_y += 1.f;
            }
            if (static_cast<unsigned int>(EditorCommand::CAMERA_DOWN) & editor_command_)
            {
                camera_move_y += -1.f;
            }
            camera_->Move(glm::vec3(camera_move_x, camera_move_y, camera_move_z));
            
        }

        void EditorInputManager::KeyCallback(int key, int code, int action, int mods)
        {
            // std::cout << "Test__key:" << key << " code:" << code << " action:" << action << " mods:" << mods << std::endl;

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
            double delta_x = xpos - last_cursor_xpos_;
            double delta_y = ypos - last_cursor_ypos_;

            float yaw_delta = delta_x > 0.f ? 1.f : -1.f;
            float pitch_delta = delta_y > 0.f ? -1.f : 1.f;
            
            camera_->Rotate(glm::vec2(pitch_delta, yaw_delta));
            //std::cout << "delta_x:" << delta_x << " delta_y:" << delta_y << std::endl;
            last_cursor_xpos_ = xpos;
            last_cursor_ypos_ = ypos;
        }
    }
}