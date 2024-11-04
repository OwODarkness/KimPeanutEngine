#ifndef EDITOR_EDITOR_INPUT_MANAGER_H
#define DDITOR_EDITOR_INPUT_MANAGER_H


namespace kpengine{
namespace editor{

enum class InputMode: unsigned int{
    InputMode_Released = 0,
    InputMode_Pressed = 1,
    InputMode_Ongoing = 2
};

class EditorInputManager{
    public:
    EditorInputManager() =default;
    void Initialize();

    void KeyCallback(int key, int code, int action, int mods);
};
}
}

#endif