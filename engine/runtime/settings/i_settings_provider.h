#ifndef KPENGINE_RUNTIME_SETTINGS_I_SETTINGS_PROVIDER_H
#define KPENGINE_RUNTIME_SETTINGS_I_SETTINGS_PROVIDER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "settings_types.h"

namespace kpengine::settings
{
    enum class SettingsEditOrigin : uint8_t
    {
        CommandLine,
        Command,
        Editor,
        Internal,
    };

    struct SettingsApplyRequest
    {
        uint64_t request_id = 0;
        uint64_t registration_generation = 0;
        uint64_t base_revision = 0;
        std::string section_key;
        SettingValues changed_values;
        SettingValues candidate_values;
        SettingsEditOrigin origin = SettingsEditOrigin::Internal;
    };

    enum class SettingsProviderOutcome : uint8_t
    {
        Applied,
        Rejected,
        Superseded,
    };

    struct SettingsProviderResult
    {
        SettingsProviderOutcome outcome = SettingsProviderOutcome::Rejected;
        SettingValues active_values;
        std::string diagnostic;
    };

    class ISettingsProvider;

    namespace detail
    {
        struct SettingsProviderRegistrationState
        {
            std::atomic<bool> active{true};
            std::atomic<ISettingsProvider *> provider{nullptr};
            uint64_t generation = 0;
            std::string section_key;
        };
    }

    class SettingsCompletionHandle final
    {
    public:
        SettingsCompletionHandle() = default;

        bool Complete(SettingsProviderResult result) const
        {
            if (!state_)
            {
                return false;
            }
            bool expected = false;
            if (!state_->completed.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel))
            {
                return false;
            }
            return state_->complete(std::move(result));
        }

    private:
        struct State
        {
            explicit State(std::function<bool(SettingsProviderResult)> callback)
                : complete(std::move(callback))
            {
            }

            std::atomic<bool> completed{false};
            std::function<bool(SettingsProviderResult)> complete;
        };

        explicit SettingsCompletionHandle(
            std::function<bool(SettingsProviderResult)> callback)
            : state_(std::make_shared<State>(std::move(callback)))
        {
        }

        std::shared_ptr<State> state_;

        friend class SettingsRegistry;
    };

    // Move-only lifetime token. Its owner must keep the provider alive until
    // queued owner-lane dispatch has finished, then release this token.
    class SettingsProviderRegistration final
    {
    public:
        SettingsProviderRegistration() = default;
        ~SettingsProviderRegistration() { Reset(); }

        SettingsProviderRegistration(const SettingsProviderRegistration &) = delete;
        SettingsProviderRegistration &operator=(const SettingsProviderRegistration &) = delete;
        SettingsProviderRegistration(SettingsProviderRegistration &&other) noexcept
            : state_(std::move(other.state_))
        {
        }
        SettingsProviderRegistration &operator=(SettingsProviderRegistration &&other) noexcept
        {
            if (this != &other)
            {
                Reset();
                state_ = std::move(other.state_);
            }
            return *this;
        }

        void Reset() noexcept
        {
            if (const auto state = state_.lock())
            {
                state->active.store(false, std::memory_order_release);
                state->provider.store(nullptr, std::memory_order_release);
            }
            state_.reset();
        }

        bool IsActive() const noexcept
        {
            const auto state = state_.lock();
            return state && state->active.load(std::memory_order_acquire);
        }

        uint64_t Generation() const noexcept
        {
            const auto state = state_.lock();
            return state ? state->generation : 0;
        }

    private:
        explicit SettingsProviderRegistration(
            const std::shared_ptr<detail::SettingsProviderRegistrationState> &state)
            : state_(state)
        {
        }

        std::weak_ptr<detail::SettingsProviderRegistrationState> state_;

        friend class SettingsRegistry;
    };

    class ISettingsProvider
    {
    public:
        virtual ~ISettingsProvider() = default;
        // Called by the registry's owner-lane pump. Complete only after the
        // subsystem has applied the candidate at its safe boundary.
        virtual void Apply(const SettingsApplyRequest &request,
                           SettingsCompletionHandle completion) = 0;
    };
}

#endif
