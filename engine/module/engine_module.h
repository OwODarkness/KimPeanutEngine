#ifndef KPENGINE_MODULE_ENGINE_MODULE_H
#define KPENGINE_MODULE_ENGINE_MODULE_H

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::module
{
    // Runtime-owned lifecycle contract for optional engine modules. The
    // application composition root creates modules; Engine owns them after
    // RegisterModule() accepts them.
    class EngineModule
    {
    public:
        virtual ~EngineModule() = default;

        virtual const char *Name() const noexcept = 0;
        virtual void OnRegister(runtime::Engine &engine) = 0;
        virtual bool Initialize(runtime::Engine &engine) = 0;
        virtual void Tick(float delta_time) = 0;
        virtual void Shutdown() noexcept = 0;
    };
}

#endif
