#include "module/module_bootstrap.h"

#include "module/module_registration.h"

namespace kpengine::module
{
    void RegisterModules(kpengine::runtime::Engine &engine)
    {
        RegisterModuleContributions(engine);
    }
}
