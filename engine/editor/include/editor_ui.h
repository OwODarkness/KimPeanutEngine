#ifndef KPENGINE_EDITOR_UI_H
#define KPENGINE_EDITOR_UI_H

#include <vector>
#include <memory>
struct GLFWwindow;

namespace kpengine{

namespace ui{
    
    class EditorUIComponent;
    class EditorWindowComponent;

    class EditorUI{
    public:
        EditorUI();
        ~EditorUI();

        void Initialize(GLFWwindow* window);
        bool Render();
        void Close();
        void BeginDraw();
        void EndDraw();
    private:
        std::vector<std::unique_ptr<EditorUIComponent>> components_;
    };
    
}
}

#endif //KPENGINE_EDITOR_UI_H