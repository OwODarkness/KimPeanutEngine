#include <unordered_map>

#include <gtest/gtest.h>

#include "render/render_resource_resolver.h"

TEST(RenderResourceResolverTest, TextureCacheKeySeparatesColorSpaceAndGeneration)
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetType;
    using kpengine::graphics::TextureHandle;
    using kpengine::render::MaterialTextureColorSpace;
    using kpengine::render::TextureCacheKey;
    using kpengine::render::TextureCacheKeyHash;

    const AssetID source_asset{17, 10, AssetType::KPAT_Texture};
    const TextureCacheKey srgb{source_asset, MaterialTextureColorSpace::Srgb};
    const TextureCacheKey linear{source_asset, MaterialTextureColorSpace::Linear};
    const TextureCacheKey next_generation{
        AssetID{17, 11, AssetType::KPAT_Texture}, MaterialTextureColorSpace::Linear};

    std::unordered_map<TextureCacheKey, TextureHandle, TextureCacheKeyHash> cache;
    cache.emplace(srgb, TextureHandle{1, 1});
    cache.emplace(linear, TextureHandle{2, 1});
    cache.emplace(next_generation, TextureHandle{3, 1});

    ASSERT_EQ(cache.size(), 3u);
    EXPECT_EQ(cache.at(srgb), (TextureHandle{1, 1}));
    EXPECT_EQ(cache.at(linear), (TextureHandle{2, 1}));
    EXPECT_EQ(cache.at(next_generation), (TextureHandle{3, 1}));
}
