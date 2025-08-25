#include "input_system.h"

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "runtime/input/input_context.h"
#include "runtime/runtime_global_context.h"

namespace kpengine::input
{

    void InputSystem::Initialize(GLFWwindow *window)
    {
        glfwSetMouseButtonCallback(window, InputSystem::OnMouseButtonCallback);
        glfwSetKeyCallback(window, InputSystem::OnKeyCallback);
        glfwSetCursorPosCallback(window, InputSystem::OnCursorPosCallback);
        glfwSetScrollCallback(window, InputSystem::OnScrollCallback);
    }

    void InputSystem::AddContext(const std::string &name, std::shared_ptr<InputContext> context)
    {
        if (context == nullptr)
        {
            return;
        }
        contexts_[name] = context;
    }
    void InputSystem::SetActiveContext(const std::string &name)
    {
        active_context_ = name;
    }

    std::shared_ptr<InputContext> InputSystem::GetInputContext(const std::string &name)
    {
        auto it = contexts_.find(name);
        if (it == contexts_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    void InputSystem::MouseButtonExec(int code, int action, int mods)
    {

        auto it = contexts_.find(active_context_);
        if (it == contexts_.end())
        {
            return;
        }
        InputTriggleType triggle_type;
        switch (action)
        {
        case GLFW_PRESS:
            triggle_type = InputTriggleType::Pressed;
            break;
        case GLFW_RELEASE:
            triggle_type = InputTriggleType::Released;
        default:
            break;
        }
        it->second->ProcessKeyInput({InputDevice::Mouse, code}, triggle_type, mods);
    }
    void InputSystem::KeyExec(int key, int code, int action, int mods)
    {
        auto it = contexts_.find(active_context_);
        if (it == contexts_.end())
        {
            return;
        }
        // KEY
        InputTriggleType triggle_type;
        switch (action)
        {
        case GLFW_PRESS:
            triggle_type = InputTriggleType::Pressed;
            break;
        case GLFW_RELEASE:
            triggle_type = InputTriggleType::Released;
        case GLFW_REPEAT:
            triggle_type = InputTriggleType::Held;
            break;
        default:
            triggle_type = InputTriggleType::Pressed;
        }
        it->second->ProcessKeyInput({InputDevice::Keyboard, key}, triggle_type, mods);
    }
    void InputSystem::CursorPosExec(double xpos, double ypos)
    {
        if (is_first_cursor_ == true)
        {
            is_first_cursor_ = false;
            last_cursor_xpos_ = xpos;
            last_cursor_ypos_ = ypos;
            return;
        }

        float delta_x = (float)(xpos - last_cursor_xpos_);
        float delta_y = (float)(ypos - last_cursor_ypos_);
        last_cursor_xpos_ = xpos;
        last_cursor_ypos_ = ypos;

        auto it = contexts_.find(active_context_);
        if (it == contexts_.end())
        {
            return;
        }
        it->second->ProcessAxis2DInput({InputDevice::Mouse, KPENGINE_MOUSE_CURSOR}, delta_x, delta_y);
    }

    void InputSystem::ScrollExec(double xoffset, double yoffset)
    {
        auto it = contexts_.find(active_context_);
        if (it == contexts_.end())
        {
            return;
        }
        it->second->ProcessAxis1DInput({InputDevice::Mouse, KPENGINE_MOUSE_SCROLL}, static_cast<float>(yoffset));
    }

    void InputSystem::OnMouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
    {
        GLFWAppContext *glfw_context = static_cast<GLFWAppContext *>(glfwGetWindowUserPointer(window));
        if (!glfw_context || !glfw_context->input_sys)
            return;
        InputSystem *input_system = glfw_context->input_sys;
        input_system->MouseButtonExec(button, action, mods);
    }

    void InputSystem::OnKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
    {
        GLFWAppContext *glfw_context = static_cast<GLFWAppContext *>(glfwGetWindowUserPointer(window));
        if (!glfw_context || !glfw_context->input_sys)
            return;
        InputSystem *input_system = glfw_context->input_sys;
        input_system->KeyExec(key, scancode, action, mods);
    }

    void InputSystem::OnCursorPosCallback(GLFWwindow *window, double xpos, double ypos)
    {
        GLFWAppContext *glfw_context = static_cast<GLFWAppContext *>(glfwGetWindowUserPointer(window));
        if (!glfw_context || !glfw_context->input_sys)
            return;
        InputSystem *input_system = glfw_context->input_sys;
        input_system->CursorPosExec(xpos, ypos);
    }

    void InputSystem::OnScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
    {
        GLFWAppContext *glfw_context = static_cast<GLFWAppContext *>(glfwGetWindowUserPointer(window));
        if (!glfw_context || !glfw_context->input_sys)
            return;
        InputSystem *input_system = glfw_context->input_sys;
        input_system->ScrollExec(xoffset, yoffset);
    }

}