#ifndef KPENGINE_EDITOR_H
#define KPENGINE_EDITOR_H

#include <memory>

namespace kpengine{

namespace runtime{
    class Engine;
}
}

namespace kpengine::editor{

    class Editor{
    public:
        Editor();
        virtual ~Editor();
    
        void Initialize(kpengine::runtime::Engine* engine);
        void Tick();
        void Clear();
        
    private:
        kpengine::runtime::Engine* engine_ = nullptr;
        std::shared_ptr<class EditorUI> editor_ui;
        bool is_initialized_;
    };
}


#endif