#include "module_registration.h"

#include <mutex>
#include <stdexcept>
#include <vector>

#include "runtime/engine.h"

namespace kpengine::module
{
    namespace
    {
        struct Registry final
        {
            std::mutex mutex;
            std::vector<ModuleRegistrationCallback> callbacks;
        };

        Registry &GetRegistry()
        {
            static Registry registry;
            return registry;
        }
    }

    ModuleRegistration::ModuleRegistration(ModuleRegistrationCallback callback)
    {
        if (callback == nullptr)
        {
            throw std::invalid_argument("Module registration callback must be valid");
        }

        Registry &registry = GetRegistry();
        const std::lock_guard lock(registry.mutex);
        registry.callbacks.push_back(callback);
    }

    void RegisterModuleContributions(runtime::Engine &engine)
    {
        std::vector<ModuleRegistrationCallback> callbacks;
        {
            Registry &registry = GetRegistry();
            const std::lock_guard lock(registry.mutex);
            callbacks = registry.callbacks;
        }

        for (ModuleRegistrationCallback callback : callbacks)
        {
            callback(engine);
        }
    }
}
