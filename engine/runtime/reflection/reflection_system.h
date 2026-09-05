#ifndef KPENGINE_RUNTIME_REFLECTION_SYSTEM_H
#define KPENGINE_RUNTIME_REFLECTION_SYSTEM_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "i_reflection_access.h"
#include "i_reflection_catalog.h"

namespace kpengine::reflection
{
    class EnttReflectionRegistrar;
    class EnttReflectionRegistry;

    using ReflectionRegistrationFunction =
        std::function<ReflectionResult(EnttReflectionRegistrar &)>;

    class ReflectionSystem
    {
    public:
        enum class State : uint8_t
        {
            Constructed,
            Frozen,
            ShuttingDown,
            ShutDown,
        };

        ReflectionSystem() = default;
        ~ReflectionSystem();

        ReflectionSystem(const ReflectionSystem &) = delete;
        ReflectionSystem &operator=(const ReflectionSystem &) = delete;
        ReflectionSystem(ReflectionSystem &&) = delete;
        ReflectionSystem &operator=(ReflectionSystem &&) = delete;

        ReflectionResult Initialize(const std::vector<ReflectionRegistrationFunction> &registrations);
        void Shutdown() noexcept;

        State GetState() const noexcept { return state_; }
        const std::string &GetLastDiagnostic() const noexcept { return last_diagnostic_; }
        const IReflectionCatalog *GetCatalog() const noexcept;
        const IReflectionAccess *GetAccess() const noexcept;

    private:
        State state_ = State::Constructed;
        std::string last_diagnostic_;
        std::unique_ptr<EnttReflectionRegistry> registry_;
    };
}

#endif
