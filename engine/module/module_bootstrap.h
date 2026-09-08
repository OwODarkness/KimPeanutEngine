#ifndef KPENGINE_MODULE_BOOTSTRAP_H
#define KPENGINE_MODULE_BOOTSTRAP_H

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::module
{
    // Application composition root for statically linked modules. A future
    // dynamic loader can replace this function without changing Engine's
    // lifecycle contract.
    void RegisterModules(runtime::Engine &engine);
}

#endif
