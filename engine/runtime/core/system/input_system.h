#ifndef KPENGINE_RUNTIME_INPUT_SYSTEM_H
#define KPENGINE_RUNTIME_INPUT_SYSTEM_H


#include <memory>
#include <unordered_map>
#include <string>
struct GLFWwindow;
namespace kpengine::input{
    class InputContext;

    class InputSystem{

  
    public:
        void Initialize(GLFWwindow* window);
        void AddContext(const std::string& name, std::shared_ptr<InputContext> context);
        void SetActiveContext(const std::string& name);
        std::shared_ptr<InputContext> GetInputContext(const std::string& name);
    private:
        std::unordered_map<std::string, std::shared_ptr<InputContext>> contexts_;
        std::string active_context_;

        double last_cursor_xpos_;
        double last_cursor_ypos_;
        bool is_first_cursor_;
    private:
        void MouseButtonExec(int code, int action, int mods);
        void KeyExec(int key, int code, int action, int mods);
        void CursorPosExec(double xpos, double ypos);
        void ScrollExec(double xoffset, double yoffset);
        
        static void OnMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void OnKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void OnCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void OnScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    
    };
}

#endif