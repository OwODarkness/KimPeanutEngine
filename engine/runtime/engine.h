#ifndef KPENGINE_RUNTIME_ENGINE_H
#define KPENGINE_RUNTIME_ENGINE_H

namespace kpengine{
namespace runtime{
    class Engine{
        public:
        Engine() = default;

        void Initialize();

        bool Tick();

    };
}
}

#endif