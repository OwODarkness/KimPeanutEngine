#include "live2d_cubism_lifecycle.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <malloc.h>
#include <mutex>

#include "CubismFramework.hpp"
#include "ICubismAllocator.hpp"
#include "Live2DCubismCore.h"
#include "log/logger.h"

namespace kpengine::live2d::detail
{
    struct CubismInstanceState
    {
        std::size_t count{};
        bool owner_alive{true};
    };
}

namespace
{
    using kpengine::live2d::CubismLifecycle;
    using kpengine::live2d::CubismLifecycleState;
    using kpengine::live2d::CubismModelInstanceLease;
    using kpengine::live2d::CubismVersionInfo;
    using kpengine::live2d::detail::CubismInstanceState;
    using Live2D::Cubism::Framework::ICubismAllocator;

    std::mutex g_lifecycle_mutex;
    CubismLifecycle* g_active_lifecycle{};

    class CubismAllocator final : public ICubismAllocator
    {
    public:
        void* Allocate(const std::size_t size) override
        {
            return std::malloc(size == 0 ? 1 : size);
        }

        void Deallocate(void* memory) override
        {
            std::free(memory);
        }

        void* AllocateAligned(const std::size_t size,
                              const std::uint32_t alignment) override
        {
            const std::size_t safe_alignment =
                std::max<std::size_t>(alignment, sizeof(void*));
            return _aligned_malloc(size == 0 ? 1 : size, safe_alignment);
        }

        void DeallocateAligned(void* memory) override
        {
            _aligned_free(memory);
        }
    };

    void CubismLogBridge(const char* message) noexcept
    {
        try
        {
            KP_LOG("Live2D.Cubism", LOG_LEVEL_DEBUG,
                   "%s", message == nullptr ? "<null>" : message);
        }
        catch (...)
        {
            // Cubism's C callback must never allow an engine exception to
            // cross the SDK boundary.
        }
    }

    CubismAllocator& GetAllocator()
    {
        static CubismAllocator allocator;
        return allocator;
    }

    Live2D::Cubism::Framework::CubismFramework::Option MakeFrameworkOption()
    {
        Live2D::Cubism::Framework::CubismFramework::Option option{};
        option.LogFunction = &CubismLogBridge;
        option.LoggingLevel =
            Live2D::Cubism::Framework::CubismFramework::Option::LogLevel_Verbose;
        option.LoadFileFunction = nullptr;
        option.ReleaseBytesFunction = nullptr;
        return option;
    }

    std::uint32_t ReadCoreVersion()
    {
        return static_cast<std::uint32_t>(Live2D::Cubism::Core::csmGetVersion());
    }

    std::uint32_t ReadLatestMocVersion()
    {
        return static_cast<std::uint32_t>(
            Live2D::Cubism::Core::csmGetLatestMocVersion());
    }
}

namespace kpengine::live2d
{
    CubismModelInstanceLease::CubismModelInstanceLease(
        std::shared_ptr<detail::CubismInstanceState> state) noexcept
        : state_(std::move(state))
    {
    }

    CubismModelInstanceLease::~CubismModelInstanceLease() noexcept
    {
        if (state_ != nullptr && state_->count > 0)
        {
            --state_->count;
        }
    }

    CubismModelInstanceLease::CubismModelInstanceLease(
        CubismModelInstanceLease&& other) noexcept
        : state_(std::move(other.state_))
    {
    }

    CubismModelInstanceLease& CubismModelInstanceLease::operator=(
        CubismModelInstanceLease&& other) noexcept
    {
        if (this != &other)
        {
            if (state_ != nullptr && state_->count > 0)
            {
                --state_->count;
            }
            state_ = std::move(other.state_);
        }
        return *this;
    }

    bool CubismModelInstanceLease::IsValid() const noexcept
    {
        return state_ != nullptr;
    }

    CubismLifecycle::CubismLifecycle()
        : state_(CubismLifecycleState::Uninitialized)
        , instance_state_(std::make_shared<detail::CubismInstanceState>())
    {
    }

    CubismLifecycle::~CubismLifecycle() noexcept
    {
        if (Shutdown())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
        instance_state_->owner_alive = false;
        if (g_active_lifecycle == this)
        {
            // Do not leave a dangling process-global owner. Framework globals
            // intentionally remain started because live instances still use
            // them; a later lifecycle cannot silently take ownership.
            g_active_lifecycle = nullptr;
        }
        try
        {
            KP_LOG("Live2D.Cubism", LOG_LEVEL_ERROR,
                   "CubismLifecycle destroyed with %zu live model instance(s); "
                   "Framework shutdown was refused.",
                   instance_state_->count);
        }
        catch (...)
        {
        }
    }

    bool CubismLifecycle::Initialize()
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

        if (state_ == CubismLifecycleState::Initialized)
        {
            return true;
        }
        if (state_ == CubismLifecycleState::Disposing)
        {
            return false;
        }
        if (g_active_lifecycle != nullptr && g_active_lifecycle != this)
        {
            KP_LOG("Live2D.Cubism", LOG_LEVEL_ERROR,
                   "CubismFramework is already owned by another lifecycle.");
            return false;
        }
        if (Live2D::Cubism::Framework::CubismFramework::IsStarted() &&
            g_active_lifecycle == nullptr)
        {
            KP_LOG("Live2D.Cubism", LOG_LEVEL_ERROR,
                   "CubismFramework was started outside CubismLifecycle.");
            return false;
        }

        auto option = MakeFrameworkOption();
        if (!Live2D::Cubism::Framework::CubismFramework::IsStarted())
        {
            if (!Live2D::Cubism::Framework::CubismFramework::StartUp(
                    &GetAllocator(), &option))
            {
                KP_LOG("Live2D.Cubism", LOG_LEVEL_ERROR,
                       "CubismFramework::StartUp failed.");
                state_ = CubismLifecycleState::Uninitialized;
                return false;
            }
        }

        g_active_lifecycle = this;
        state_ = CubismLifecycleState::Started;
        Live2D::Cubism::Framework::CubismFramework::Initialize();

        version_.core_version = ReadCoreVersion();
        version_.latest_moc_version = ReadLatestMocVersion();
        version_.sdk_version = "5-r.5";
        state_ = CubismLifecycleState::Initialized;

        KP_LOG("Live2D.Cubism", LOG_LEVEL_INFO,
               "Cubism initialized: Core version 0x%08x, latest MOC %u, SDK %s.",
               version_.core_version, version_.latest_moc_version,
               version_.sdk_version.c_str());
        return true;
    }

    bool CubismLifecycle::Shutdown() noexcept
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

        if (state_ == CubismLifecycleState::Uninitialized ||
            state_ == CubismLifecycleState::ShutDown)
        {
            return true;
        }
        if (g_active_lifecycle != this)
        {
            return false;
        }
        if (instance_state_->count != 0)
        {
            try
            {
                KP_LOG("Live2D.Cubism", LOG_LEVEL_ERROR,
                       "Refusing Cubism shutdown with %zu live model instance(s).",
                       instance_state_->count);
            }
            catch (...)
            {
            }
            return false;
        }

        state_ = CubismLifecycleState::Disposing;
        try
        {
            if (Live2D::Cubism::Framework::CubismFramework::IsInitialized())
            {
                Live2D::Cubism::Framework::CubismFramework::Dispose();
            }
            if (Live2D::Cubism::Framework::CubismFramework::IsStarted())
            {
                Live2D::Cubism::Framework::CubismFramework::CleanUp();
            }
            g_active_lifecycle = nullptr;
            state_ = CubismLifecycleState::ShutDown;
            return true;
        }
        catch (...)
        {
            state_ = CubismLifecycleState::Initialized;
            return false;
        }
    }

    CubismLifecycleState CubismLifecycle::State() const noexcept
    {
        return state_;
    }

    bool CubismLifecycle::IsInitialized() const noexcept
    {
        return state_ == CubismLifecycleState::Initialized;
    }

    const CubismVersionInfo& CubismLifecycle::Version() const noexcept
    {
        return version_;
    }

    std::size_t CubismLifecycle::LiveModelInstanceCount() const noexcept
    {
        return instance_state_->count;
    }

    std::optional<CubismModelInstanceLease> CubismLifecycle::AcquireModelInstance()
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
        if (state_ != CubismLifecycleState::Initialized ||
            g_active_lifecycle != this || !instance_state_->owner_alive)
        {
            return std::nullopt;
        }
        ++instance_state_->count;
        return CubismModelInstanceLease{instance_state_};
    }
}
