#ifndef KPENGINE_EDITOR_INPUT_MANAGER_H
#define KPENGINE_EDITOR_INPUT_MANAGER_H

#include <memory>

namespace kpengine
{
    class RenderCamera;
    namespace editor
    {
        enum class EditorCommand : unsigned int
        {
            CAMERA_LEFT = 1 << 0,
            CAMERA_RIGHT = 1 << 1,
            CAMERA_FORWARD = 1 << 2,
            CAMERA_BACKWARD = 1 << 3,
            CAMERA_UP = 1 << 4,
            CAMERA_DOWN = 1 << 5

        };
        class EditorInputManager
        {
        public:
            EditorInputManager() = default;
            void Initialize();

            void Tick(float DeltaTime);

            void KeyCallback(int key, int code, int action, int mods);

            void MouseButtonCallback(int code, int action, int mods);

            void CursorPosCallback(double xpos, double ypos);

            void HandleInput();

        private:
            double last_cursor_xpos_;
            double last_cursor_ypos_;
            unsigned int editor_command_{0};
            std::shared_ptr<RenderCamera> camera_;
            bool is_first_cursor = true;
        };
    }
}

#endif