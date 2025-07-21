#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <memory>
#include <thread>
namespace kpengine
{
    class WindowSystem;
    class RenderSystem;
    class AssetSystem;
    class LevelSystem;
    class LogSystem;

    namespace runtime
    {
        enum class RuntimeMode
        {
            Editor,
            Game,
            Simulate,   
        };


        class RuntimeContext
        {
        public:
            RuntimeContext();
            ~RuntimeContext();
            void Initialize();
            void Clear();
            
            RuntimeMode GetRuntimeMode() const{return runtime_mode_;}

            bool IsGameMode() const{return runtime_mode_ == RuntimeMode::Game;}
            bool IsEditorMode() const{return runtime_mode_ == RuntimeMode::Editor;}
            bool IsSimulate() const{return runtime_mode_ == RuntimeMode::Simulate;}

            std::unique_ptr<WindowSystem> window_system_;
            std::unique_ptr<RenderSystem> render_system_;
            std::unique_ptr<LogSystem> log_system_;
            std::unique_ptr<AssetSystem> asset_system_;
            std::unique_ptr<LevelSystem> level_system_;

            std::thread::id game_thread_id_;
            std::thread::id render_thread_id_;
        private:
            RuntimeMode runtime_mode_;
        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif