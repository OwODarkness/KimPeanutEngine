#include "live2d_model_instance.h"

#include <limits>
#include <utility>

#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"
#include "live2d_cubism_lifecycle.h"
#include "live2d_model_resource.h"

namespace kpengine::live2d
{
    namespace
    {
        using Live2D::Cubism::Framework::csmByte;
        using Live2D::Cubism::Framework::csmFloat32;
        using Live2D::Cubism::Framework::csmInt32;
        using Live2D::Cubism::Framework::csmSizeInt;
        using Live2D::Cubism::Framework::csmUint32;
        using Live2D::Cubism::Framework::CubismMoc;
        using Live2D::Cubism::Framework::CubismModel;

        bool IsIndexRepresentable(const std::size_t index) noexcept
        {
            return index <= static_cast<std::size_t>(
                                std::numeric_limits<csmInt32>::max());
        }
    }

    struct Live2DModelInstance::Impl final
    {
        std::optional<CubismModelInstanceLease> lease;
        CubismMoc *moc{};
        CubismModel *model{};

        ~Impl() noexcept
        {
            if (model != nullptr && moc != nullptr)
            {
                moc->DeleteModel(model);
                model = nullptr;
            }
            if (moc != nullptr)
            {
                CubismMoc::Delete(moc);
                moc = nullptr;
            }
            lease.reset();
        }

        bool IsValid() const noexcept
        {
            return lease.has_value() && lease->IsValid() && moc != nullptr &&
                   model != nullptr;
        }
    };

    Live2DModelInstance::Live2DModelInstance(
        std::shared_ptr<const Live2DModelResource> resource,
        std::unique_ptr<Impl> impl) noexcept
        : resource_(std::move(resource))
        , impl_(std::move(impl))
    {
    }

    Live2DModelInstance::~Live2DModelInstance() noexcept = default;

    Live2DModelInstance::Live2DModelInstance(Live2DModelInstance &&) noexcept =
        default;

    Live2DModelInstance &Live2DModelInstance::operator=(
        Live2DModelInstance &&) noexcept = default;

    std::unique_ptr<Live2DModelInstance> Live2DModelInstance::Create(
        std::shared_ptr<const Live2DModelResource> resource,
        CubismLifecycle &lifecycle)
    {
        if (resource == nullptr || !lifecycle.IsInitialized() ||
            resource->Product().moc_bytes.empty())
        {
            return nullptr;
        }

        std::optional<CubismModelInstanceLease> lease =
            lifecycle.AcquireModelInstance();
        if (!lease.has_value())
        {
            return nullptr;
        }

        auto impl = std::make_unique<Impl>();
        impl->lease = std::move(lease);
        const std::vector<std::byte> &moc_bytes = resource->Product().moc_bytes;
        impl->moc = CubismMoc::Create(
            reinterpret_cast<const csmByte *>(moc_bytes.data()),
            static_cast<csmSizeInt>(moc_bytes.size()), true);
        if (impl->moc == nullptr)
        {
            return nullptr;
        }

        impl->model = impl->moc->CreateModel();
        if (impl->model == nullptr)
        {
            return nullptr;
        }

        return std::unique_ptr<Live2DModelInstance>(new Live2DModelInstance(
            std::move(resource), std::move(impl)));
    }

    bool Live2DModelInstance::IsValid() const noexcept
    {
        return resource_ != nullptr && impl_ != nullptr && impl_->IsValid();
    }

    const Live2DModelResource &Live2DModelInstance::Resource() const noexcept
    {
        return *resource_;
    }

    std::size_t Live2DModelInstance::ParameterCount() const noexcept
    {
        if (!IsValid())
        {
            return 0;
        }
        return static_cast<std::size_t>(impl_->model->GetParameterCount());
    }

    bool Live2DModelInstance::GetParameterRange(const std::size_t index,
                                                float &minimum,
                                                float &maximum) const noexcept
    {
        if (!IsValid() || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        const auto parameter_index = static_cast<csmUint32>(index);
        minimum = impl_->model->GetParameterMinimumValue(parameter_index);
        maximum = impl_->model->GetParameterMaximumValue(parameter_index);
        return true;
    }

    bool Live2DModelInstance::GetParameterValue(const std::size_t index,
                                                float &value) const noexcept
    {
        if (!IsValid() || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        value = impl_->model->GetParameterValue(static_cast<csmInt32>(index));
        return true;
    }

    bool Live2DModelInstance::SetParameterValue(const std::size_t index,
                                                const float value) noexcept
    {
        if (!IsValid() || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        impl_->model->SetParameterValue(static_cast<csmInt32>(index),
                                         static_cast<csmFloat32>(value));
        return true;
    }

    bool Live2DModelInstance::Update() noexcept
    {
        if (!IsValid())
        {
            return false;
        }
        impl_->model->Update();
        return true;
    }
}
