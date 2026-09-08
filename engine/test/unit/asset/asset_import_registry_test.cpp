#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "asset/asset_import_registry.h"

namespace
{
    using kpengine::asset::ImportProviderDescriptor;
    using kpengine::asset::ImportProviderKind;
    using kpengine::asset::ImportProviderRegistry;
    using kpengine::asset::ImportProviderRequest;
    using kpengine::asset::ImportProviderStatus;
    using kpengine::asset::TypedImportProduct;

    ImportProviderDescriptor Provider(std::string id,
                                      std::vector<std::string> suffixes,
                                      ImportProviderKind kind = ImportProviderKind::Custom)
    {
        ImportProviderDescriptor descriptor{};
        descriptor.id = std::move(id);
        descriptor.version = 1;
        descriptor.kind = kind;
        descriptor.source_suffixes = std::move(suffixes);
        descriptor.callback = [](const ImportProviderRequest &)
        {
            kpengine::asset::ImportProviderResult result{};
            result.status = ImportProviderStatus::Imported;
            return result;
        };
        return descriptor;
    }
}

TEST(AssetImportRegistryTest, LongestCompoundSuffixWinsOverGenericSuffix)
{
    ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(registry.Register(Provider("json", {"json"}), diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Register(Provider("live2d", {"model3.json"}, ImportProviderKind::Model),
                                  diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    const auto *const selected =
        registry.Resolve("characters/kurisu.model3.json", {}, diagnostic);
    ASSERT_NE(selected, nullptr) << diagnostic;
    EXPECT_EQ(selected->id, "live2d");
}

TEST(AssetImportRegistryTest, ExplicitProviderSelectionBypassesAutomaticSuffixChoice)
{
    ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(registry.Register(Provider("generic", {"json"}), diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Register(Provider("special", {"model3.json"}), diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    const auto *const selected =
        registry.Resolve("characters/kurisu.model3.json", "generic", diagnostic);
    ASSERT_NE(selected, nullptr) << diagnostic;
    EXPECT_EQ(selected->id, "generic");
}

TEST(AssetImportRegistryTest, EqualLengthAutomaticMatchesAreRejectedAsAmbiguous)
{
    ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(registry.Register(Provider("first", {"json"}), diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Register(Provider("second", {"json"}), diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    EXPECT_EQ(registry.Resolve("asset.json", {}, diagnostic), nullptr);
    EXPECT_EQ(diagnostic, "automatic import provider selection is ambiguous");
    ASSERT_NE(registry.Resolve("asset.json", "second", diagnostic), nullptr);
}

TEST(AssetImportRegistryTest, ExecuteReturnsProviderIdentityAndLateRegistrationIsRejected)
{
    ImportProviderRegistry registry;
    std::string diagnostic;
    ImportProviderDescriptor descriptor = Provider("texture", {"png"}, ImportProviderKind::Texture);
    descriptor.callback = [](const ImportProviderRequest &)
    {
        kpengine::asset::ImportProviderResult result{};
        result.status = ImportProviderStatus::Cooked;
        result.product = std::make_shared<
            TypedImportProduct<std::string, ImportProviderKind::Texture>>("cooked");
        return result;
    };
    ASSERT_TRUE(registry.Register(std::move(descriptor), diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    const auto result = registry.Execute({{}, {}, "albedo.png"}, {}, diagnostic);
    EXPECT_TRUE(diagnostic.empty());
    EXPECT_EQ(result.provider_id, "texture");
    EXPECT_EQ(result.status, ImportProviderStatus::Cooked);
    ASSERT_NE(result.product, nullptr);
    EXPECT_EQ(result.product->GetProviderKind(), ImportProviderKind::Texture);

    EXPECT_FALSE(registry.Register(Provider("late", {"late"}), diagnostic));
    EXPECT_EQ(diagnostic, "import provider registry is already sealed");
}
