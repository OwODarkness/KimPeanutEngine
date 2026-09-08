#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdint>
#include <vector>

#include "CubismFramework.hpp"
#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"
#include "live2d_cubism_lifecycle.h"

#ifndef KPENGINE_LIVE2D_MOC_PATH
#define KPENGINE_LIVE2D_MOC_PATH ""
#endif

namespace
{
    using kpengine::live2d::CubismLifecycle;
    using Live2D::Cubism::Framework::CubismMoc;
    using Live2D::Cubism::Framework::CubismModel;
    using Live2D::Cubism::Framework::csmByte;

    std::vector<csmByte> ReadFixture()
    {
        const std::filesystem::path path{KPENGINE_LIVE2D_MOC_PATH};
        if (path.empty() || !std::filesystem::is_regular_file(path))
        {
            return {};
        }

        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>()};
    }

    class CubismModelOwner final
    {
    public:
        CubismModelOwner(const std::vector<csmByte>& bytes)
            : moc_(CubismMoc::Create(
                  bytes.data(), static_cast<Live2D::Cubism::Framework::csmSizeInt>(bytes.size()), true))
        {
        }

        ~CubismModelOwner() noexcept
        {
            if (first_ != nullptr)
            {
                moc_->DeleteModel(first_);
            }
            if (second_ != nullptr)
            {
                moc_->DeleteModel(second_);
            }
            if (moc_ != nullptr)
            {
                CubismMoc::Delete(moc_);
            }
        }

        CubismModelOwner(const CubismModelOwner&) = delete;
        CubismModelOwner& operator=(const CubismModelOwner&) = delete;

        CubismMoc* Moc() const noexcept
        {
            return moc_;
        }

        CubismModel* CreateFirst()
        {
            first_ = moc_->CreateModel();
            return first_;
        }

        CubismModel* CreateSecond()
        {
            second_ = moc_->CreateModel();
            return second_;
        }

    private:
        CubismMoc* moc_{};
        CubismModel* first_{};
        CubismModel* second_{};
    };
}

TEST(Live2DCoreTest, InitializesReportsVersionAndRepeatsLifecycle)
{
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        CubismLifecycle lifecycle;
        ASSERT_TRUE(lifecycle.Initialize());
        EXPECT_TRUE(lifecycle.IsInitialized());
        EXPECT_EQ(lifecycle.State(),
                  kpengine::live2d::CubismLifecycleState::Initialized);
        EXPECT_NE(lifecycle.Version().core_version, 0u);
        EXPECT_NE(lifecycle.Version().latest_moc_version, 0u);
        EXPECT_EQ(lifecycle.Version().sdk_version, "5-r.5");

        void* aligned_memory =
            Live2D::Cubism::Framework::CubismFramework::AllocateAligned(128, 32);
        ASSERT_NE(aligned_memory, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned_memory) % 32u, 0u);
        Live2D::Cubism::Framework::CubismFramework::DeallocateAligned(aligned_memory);

        EXPECT_TRUE(lifecycle.Shutdown());
        EXPECT_EQ(lifecycle.State(),
                  kpengine::live2d::CubismLifecycleState::ShutDown);
    }
}

TEST(Live2DCoreTest, RejectsShutdownWhileModelLeasesAreAlive)
{
    CubismLifecycle lifecycle;
    ASSERT_TRUE(lifecycle.Initialize());

    auto first = lifecycle.AcquireModelInstance();
    auto second = lifecycle.AcquireModelInstance();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(lifecycle.LiveModelInstanceCount(), 2u);
    EXPECT_FALSE(lifecycle.Shutdown());
    EXPECT_TRUE(lifecycle.IsInitialized());

    second.reset();
    first.reset();
    EXPECT_EQ(lifecycle.LiveModelInstanceCount(), 0u);
    EXPECT_TRUE(lifecycle.Shutdown());
}

TEST(Live2DCoreTest, CreatesIndependentModelsFromOneMoc)
{
    CubismLifecycle lifecycle;
    ASSERT_TRUE(lifecycle.Initialize());

    const std::vector<csmByte> bytes = ReadFixture();
    if (bytes.empty())
    {
        GTEST_SKIP() << "No external .moc3 fixture found at '"
                     << KPENGINE_LIVE2D_MOC_PATH
                     << "'; pass -DKPENGINE_LIVE2D_MOC_PATH=<path> to configure.";
    }

    auto first_lease = lifecycle.AcquireModelInstance();
    auto second_lease = lifecycle.AcquireModelInstance();
    ASSERT_TRUE(first_lease.has_value());
    ASSERT_TRUE(second_lease.has_value());

    CubismModelOwner models(bytes);
    ASSERT_NE(models.Moc(), nullptr);
    CubismModel* first = models.CreateFirst();
    CubismModel* second = models.CreateSecond();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_GT(first->GetParameterCount(), 0);

    const float first_before = first->GetParameterValue(0);
    const float second_before = second->GetParameterValue(0);
    const float minimum = first->GetParameterMinimumValue(0);
    const float maximum = first->GetParameterMaximumValue(0);
    const float changed_value = first_before == minimum ? maximum : minimum;
    ASSERT_NE(changed_value, first_before);

    first->SetParameterValue(0, changed_value);
    first->Update();

    EXPECT_FLOAT_EQ(first->GetParameterValue(0), changed_value);
    EXPECT_FLOAT_EQ(second->GetParameterValue(0), second_before);
    EXPECT_EQ(lifecycle.LiveModelInstanceCount(), 2u);
}
