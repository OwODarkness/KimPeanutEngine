#ifndef KPENGINE_EDITOR_EDITOR_H
#define KPENGINE_EDITOR_EDITOR_H

#include <memory>

namespace kpengine{
    namespace ui{
    class EditorUI;
}
namespace runtime{
    class Engine;
}
}

namespace kpengine{

namespace editor{
    class Editor{
    public:
        Editor();
        virtual ~Editor();
    
        void Initialize(kpengine::runtime::Engine* engine);
        void Run();
        void Clear();

        
    private:
        kpengine::runtime::Engine* engine_ = nullptr;
        std::shared_ptr<kpengine::ui::EditorUI> editor_ui;

    };
}
}


#endif