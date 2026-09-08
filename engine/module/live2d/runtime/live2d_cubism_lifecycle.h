#ifndef KPENGINE_LIVE2D_CUBISM_LIFECYCLE_H
#define KPENGINE_LIVE2D_CUBISM_LIFECYCLE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace kpengine::live2d
{
    enum class CubismLifecycleState
    {
        Uninitialized,
        Started,
        Initialized,
        Disposing,
        ShutDown
    };

    struct CubismVersionInfo
    {
        std::uint32_t core_version{};
        std::uint32_t latest_moc_version{};
        std::string sdk_version{};
    };

    namespace detail
    {
        struct CubismInstanceState;
    }

    class CubismModelInstanceLease final
    {
    public:
        CubismModelInstanceLease() noexcept = default;
        ~CubismModelInstanceLease() noexcept;

        CubismModelInstanceLease(const CubismModelInstanceLease&) = delete;
        CubismModelInstanceLease& operator=(const CubismModelInstanceLease&) = delete;

        CubismModelInstanceLease(CubismModelInstanceLease&& other) noexcept;
        CubismModelInstanceLease& operator=(CubismModelInstanceLease&& other) noexcept;

        bool IsValid() const noexcept;

    private:
        explicit CubismModelInstanceLease(
            std::shared_ptr<detail::CubismInstanceState> state) noexcept;

        std::shared_ptr<detail::CubismInstanceState> state_{};

        friend class CubismLifecycle;
    };

    class CubismLifecycle final
    {
    public:
        CubismLifecycle();
        ~CubismLifecycle() noexcept;

        CubismLifecycle(const CubismLifecycle&) = delete;
        CubismLifecycle& operator=(const CubismLifecycle&) = delete;

        bool Initialize();
        bool Shutdown() noexcept;

        CubismLifecycleState State() const noexcept;
        bool IsInitialized() const noexcept;
        const CubismVersionInfo& Version() const noexcept;
        std::size_t LiveModelInstanceCount() const noexcept;

        std::optional<CubismModelInstanceLease> AcquireModelInstance();

    private:
        CubismLifecycleState state_;
        CubismVersionInfo version_{};
        std::shared_ptr<detail::CubismInstanceState> instance_state_{};
    };
}

#endif
