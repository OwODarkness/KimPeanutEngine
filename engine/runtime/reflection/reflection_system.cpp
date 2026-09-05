#include "reflection_system.h"

#include "entt/entt_reflection_registrar.h"

namespace kpengine::reflection
{
    ReflectionSystem::~ReflectionSystem()
    {
        Shutdown();
    }

    ReflectionResult ReflectionSystem::Initialize(
        const std::vector<ReflectionRegistrationFunction> &registrations)
    {
        if (state_ != State::Constructed)
        {
            return {ReflectionResultStatus::AlreadyInitialized,
                    "reflection system cannot be initialized twice"};
        }

        auto candidate = std::make_unique<EnttReflectionRegistry>();
        EnttReflectionRegistrar registrar{*candidate};
        for (const ReflectionRegistrationFunction &registration : registrations)
        {
            if (!registration)
            {
                last_diagnostic_ = "reflection registration function is empty";
                candidate->Shutdown();
                return {ReflectionResultStatus::InvalidArgument,
                        last_diagnostic_};
            }
            const ReflectionResult result = registration(registrar);
            if (!result)
            {
                last_diagnostic_ = result.diagnostic;
                candidate->Shutdown();
                return result;
            }
        }

        const ReflectionResult frozen = candidate->Freeze();
        if (!frozen)
        {
            last_diagnostic_ = frozen.diagnostic;
            candidate->Shutdown();
            return frozen;
        }

        registry_ = std::move(candidate);
        state_ = State::Frozen;
        last_diagnostic_.clear();
        return {};
    }

    void ReflectionSystem::Shutdown() noexcept
    {
        if (state_ == State::ShutDown)
        {
            return;
        }
        state_ = State::ShuttingDown;
        if (registry_ != nullptr)
        {
            registry_->Shutdown();
            registry_.reset();
        }
        state_ = State::ShutDown;
    }

    const IReflectionCatalog *ReflectionSystem::GetCatalog() const noexcept
    {
        return state_ == State::Frozen && registry_ != nullptr ? registry_.get() : nullptr;
    }

    const IReflectionAccess *ReflectionSystem::GetAccess() const noexcept
    {
        return state_ == State::Frozen && registry_ != nullptr ? registry_.get() : nullptr;
    }
}
