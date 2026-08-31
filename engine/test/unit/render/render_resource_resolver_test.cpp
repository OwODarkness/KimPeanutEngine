#include <unordered_map>

#include <gtest/gtest.h>

#include "render/render_resource_resolver.h"

TEST(RenderResourceResolverTest, TextureCacheKeySeparatesGpuFormatAndGeneration)
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetType;
    using kpengine::graphics::TextureHandle;
    using kpengine::render::TextureCacheKey;
    using kpengine::render::TextureCacheKeyHash;

    const AssetID source_asset{17, 10, AssetType::KPAT_Texture};
    const TextureCacheKey srgb{source_asset, TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
    const TextureCacheKey linear{source_asset, TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM};
    const TextureCacheKey hdr{source_asset, TextureFormat::TEXTURE_FORMAT_RGBA16F};
    const TextureCacheKey next_generation{
        AssetID{17, 11, AssetType::KPAT_Texture}, TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM};

    std::unordered_map<TextureCacheKey, TextureHandle, TextureCacheKeyHash> cache;
    cache.emplace(srgb, TextureHandle{1, 1});
    cache.emplace(linear, TextureHandle{2, 1});
    cache.emplace(hdr, TextureHandle{3, 1});
    cache.emplace(next_generation, TextureHandle{4, 1});

    ASSERT_EQ(cache.size(), 4u);
    EXPECT_EQ(cache.at(srgb), (TextureHandle{1, 1}));
    EXPECT_EQ(cache.at(linear), (TextureHandle{2, 1}));
    EXPECT_EQ(cache.at(hdr), (TextureHandle{3, 1}));
    EXPECT_EQ(cache.at(next_generation), (TextureHandle{4, 1}));
}
