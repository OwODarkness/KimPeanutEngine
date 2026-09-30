#ifndef KPENGINE_MODULE_MODULE_REGISTRATION_H
#define KPENGINE_MODULE_MODULE_REGISTRATION_H

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::module
{
    using ModuleRegistrationCallback = void (*)(runtime::Engine &engine);

    class ModuleRegistration final
    {
    public:
        explicit ModuleRegistration(ModuleRegistrationCallback callback);
    };

    void RegisterModuleContributions(runtime::Engine &engine);
}

#endif
