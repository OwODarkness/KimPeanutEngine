#include <gtest/gtest.h>

#include "render/light/light_gpu_data.h"
#include "render/light/light_source_registry.h"

namespace
{
    kpengine::render::DirectionalLightSourceDesc MakeDirectionalSource(float intensity = 1.0f)
    {
        kpengine::render::DirectionalLightSourceDesc source{};
        source.direction = {0.0f, -1.0f, 0.0f};
        source.color = {1.0f, 0.5f, 0.25f};
        source.intensity = intensity;
        return source;
    }

    kpengine::render::PointLightSourceDesc MakePointSource(float range = 10.0f)
    {
        kpengine::render::PointLightSourceDesc source{};
        source.position = {1.0f, 2.0f, 3.0f};
        source.color = {0.25f, 0.5f, 1.0f};
        source.intensity = 12.0f;
        source.range = range;
        source.casts_shadow = false;
        return source;
    }

    kpengine::render::SpotLightSourceDesc MakeSpotSource(float outer_cone_radians = 0.75f)
    {
        kpengine::render::SpotLightSourceDesc source{};
        source.position = {1.0f, 2.0f, 3.0f};
        source.direction = {0.0f, -1.0f, 0.0f};
        source.color = {0.25f, 0.5f, 1.0f};
        source.intensity = 12.0f;
        source.range = 10.0f;
        source.inner_cone_radians = 0.25f;
        source.outer_cone_radians = outer_cone_radians;
        source.casts_shadow = false;
        return source;
    }

    kpengine::render::LightDesc MakeDirectionalLight(float intensity = 1.0f)
    {
        kpengine::render::LightDesc desc{};
        desc.color = {1.0f, 0.5f, 0.25f};
        desc.intensity = intensity;
        desc.type_data = kpengine::render::DirectionalLightData{{0.0f, -1.0f, 0.0f}};
        return desc;
    }

    kpengine::render::LightDesc MakePointLight()
    {
        kpengine::render::LightDesc desc{};
        desc.type = kpengine::render::LightType::Point;
        desc.type_data = kpengine::render::PointLightData{{1.0f, 2.0f, 3.0f}, 10.0f};
        return desc;
    }

    kpengine::render::LightDesc MakeSpotLight()
    {
        kpengine::render::LightDesc desc{};
        desc.type = kpengine::render::LightType::Spot;
        desc.type_data =
            kpengine::render::SpotLightData{{1.0f, 2.0f, 3.0f}, {0.0f, -1.0f, 0.0f},
                                            10.0f, 0.25f, 0.75f};
        return desc;
    }
}

TEST(LightSourceRegistryTest, ResolvesSourceCommandsAtDrainIntoImmutableLightSnapshots)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};

    const kpengine::render::LightSourceHandle source_handle =
        registry.EnqueueCreate(MakeDirectionalSource(1.0f));
    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, MakeDirectionalSource(4.0f)));
    EXPECT_TRUE(world.Snapshot().empty());

    registry.Drain(world);
    auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_TRUE(snapshot.front().handle.IsValid());
    EXPECT_EQ(snapshot.front().desc.intensity, 4.0f);
    EXPECT_EQ(snapshot.front().desc.color, (kpengine::Vector3f{1.0f, 0.5f, 0.25f}));

    const kpengine::render::LightHandle resolved_handle = snapshot.front().handle;
    snapshot.front().desc.intensity = 99.0f;
    const auto found = world.Find(resolved_handle);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->desc.intensity, 4.0f);

    ASSERT_TRUE(registry.EnqueueDestroy(source_handle));
    EXPECT_FALSE(registry.EnqueueUpdate(source_handle, MakeDirectionalSource(9.0f)));
    registry.Drain(world);
    EXPECT_TRUE(world.Snapshot().empty());
    EXPECT_FALSE(world.IsRegistered(resolved_handle));
}

TEST(LightSourceRegistryTest, SpotShadowIntentOwnsPrivateHandleAndRetiresOnDisable)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};
    auto source = MakeSpotSource();
    source.casts_shadow = true;
    const auto source_handle = registry.EnqueueCreate(source);
    registry.Drain(world);
    auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot.front().desc.shadow.has_value());
    const auto shadow = *snapshot.front().desc.shadow;
    EXPECT_TRUE(registry.IsShadowHandleValid(shadow));

    source.casts_shadow = false;
    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, source));
    registry.Drain(world);
    snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
    EXPECT_FALSE(registry.IsShadowHandleValid(shadow));

    ASSERT_TRUE(registry.EnqueueDestroy(source_handle));
    registry.Drain(world);
    EXPECT_TRUE(world.Snapshot().empty());
}

TEST(LightSourceRegistryTest, RejectsStaleSourceHandlesAndClearRetiresResolvedLights)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};

    const kpengine::render::LightSourceHandle original =
        registry.EnqueueCreate(MakeDirectionalSource());
    registry.Drain(world);
    ASSERT_EQ(world.Snapshot().size(), 1U);

    ASSERT_TRUE(registry.EnqueueDestroy(original));
    registry.Drain(world);
    const kpengine::render::LightSourceHandle replacement =
        registry.EnqueueCreate(MakeDirectionalSource(2.0f));
    EXPECT_EQ(replacement.id, original.id);
    EXPECT_NE(replacement.generation, original.generation);
    EXPECT_FALSE(registry.EnqueueUpdate(original, MakeDirectionalSource(3.0f)));

    registry.Drain(world);
    ASSERT_EQ(world.Snapshot().size(), 1U);
    registry.Clear(world);
    EXPECT_TRUE(world.Snapshot().empty());
    EXPECT_FALSE(registry.EnqueueUpdate(replacement, MakeDirectionalSource(5.0f)));
}

TEST(LightSourceRegistryTest, KeepsShadowIdentityRenderPrivateAndRetiresItWhenDisabled)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};
    auto source = MakeDirectionalSource();
    source.casts_shadow = true;
    const kpengine::render::LightSourceHandle handle = registry.EnqueueCreate(source);
    registry.Drain(world);
    auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot.front().desc.shadow.has_value());
    const kpengine::render::ShadowHandle shadow = *snapshot.front().desc.shadow;

    source.casts_shadow = false;
    ASSERT_TRUE(registry.EnqueueUpdate(handle, source));
    registry.Drain(world);
    snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
    EXPECT_TRUE(shadow.IsValid()); // The source API never exposes this identity.
}

TEST(LightSourceRegistryTest, ResolvesPointSourcesWithoutCreatingShadowIdentity)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};

    const kpengine::render::LightSourceHandle source_handle =
        registry.EnqueueCreate(MakePointSource());
    registry.Drain(world);
    auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.front().desc.type, kpengine::render::LightType::Point);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
    const auto *point =
        std::get_if<kpengine::render::PointLightData>(&snapshot.front().desc.type_data);
    ASSERT_NE(point, nullptr);
    EXPECT_EQ(point->position, (kpengine::Vector3f{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(point->range, 10.0f);

    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, MakePointSource(24.0f)));
    registry.Drain(world);
    snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    point = std::get_if<kpengine::render::PointLightData>(&snapshot.front().desc.type_data);
    ASSERT_NE(point, nullptr);
    EXPECT_EQ(point->range, 24.0f);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
}

TEST(LightSourceRegistryTest, PointShadowIntentOwnsPrivateHandleAndRetiresOnDisable)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};
    auto source = MakePointSource();
    source.casts_shadow = true;
    const auto source_handle = registry.EnqueueCreate(source);
    registry.Drain(world);
    auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot.front().desc.shadow.has_value());
    const auto shadow = *snapshot.front().desc.shadow;
    EXPECT_TRUE(registry.IsShadowHandleValid(shadow));

    source.casts_shadow = false;
    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, source));
    registry.Drain(world);
    snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
    EXPECT_FALSE(registry.IsShadowHandleValid(shadow));
}

TEST(LightSourceRegistryTest, ResolvesSpotSourcesWithoutCreatingShadowIdentity)
{
    kpengine::render::LightSourceRegistry registry{};
    kpengine::render::LightWorld world{};

    const kpengine::render::LightSourceHandle source_handle =
        registry.EnqueueCreate(MakeSpotSource());
    registry.Drain(world);
    auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.front().desc.type, kpengine::render::LightType::Spot);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
    const auto *spot =
        std::get_if<kpengine::render::SpotLightData>(&snapshot.front().desc.type_data);
    ASSERT_NE(spot, nullptr);
    EXPECT_EQ(spot->direction, (kpengine::Vector3f{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ(spot->inner_cone_radians, 0.25f);
    EXPECT_EQ(spot->outer_cone_radians, 0.75f);

    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, MakeSpotSource(1.0f)));
    registry.Drain(world);
    snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    spot = std::get_if<kpengine::render::SpotLightData>(&snapshot.front().desc.type_data);
    ASSERT_NE(spot, nullptr);
    EXPECT_EQ(spot->outer_cone_radians, 1.0f);
    EXPECT_FALSE(snapshot.front().desc.shadow.has_value());
}

TEST(LightWorldTest, RejectsForgedAndStaleResolvedHandles)
{
    kpengine::render::LightWorld world{};
    const kpengine::render::LightHandle handle = world.EnqueueCreate(MakeDirectionalLight());
    world.ApplyPendingCommands();
    ASSERT_TRUE(world.IsRegistered(handle));

    const kpengine::render::LightHandle forged{handle.id, handle.generation + 1U};
    EXPECT_FALSE(world.EnqueueUpdate(forged, MakeDirectionalLight(2.0f)));
    EXPECT_FALSE(world.EnqueueDestroy(forged));

    ASSERT_TRUE(world.EnqueueDestroy(handle));
    world.ApplyPendingCommands();
    EXPECT_FALSE(world.IsRegistered(handle));
    EXPECT_FALSE(world.EnqueueUpdate(handle, MakeDirectionalLight(3.0f)));
    EXPECT_FALSE(world.EnqueueDestroy(handle));
}

TEST(LightWorldTest, ValidatesTypedLightPayloadsAndShadowJobDescriptions)
{
    EXPECT_TRUE(kpengine::render::IsLightDescValid(MakeDirectionalLight()));
    EXPECT_TRUE(kpengine::render::IsLightDescValid(MakePointLight()));
    EXPECT_TRUE(kpengine::render::IsLightDescValid(MakeSpotLight()));

    auto mismatched = MakeDirectionalLight();
    mismatched.type = kpengine::render::LightType::Point;
    EXPECT_FALSE(kpengine::render::IsLightDescValid(mismatched));

    auto invalid_spot = MakeSpotLight();
    auto &spot = std::get<kpengine::render::SpotLightData>(invalid_spot.type_data);
    spot.inner_cone_radians = 0.8f;
    spot.outer_cone_radians = 0.2f;
    EXPECT_FALSE(kpengine::render::IsLightDescValid(invalid_spot));

    const kpengine::render::LightHandle light_handle{4, 1};
    const kpengine::render::ShadowJobDesc directional_job{
        light_handle, kpengine::render::ShadowKind::Directional2D, 1024, 0};
    EXPECT_TRUE(kpengine::render::IsShadowJobDescValid(directional_job));
    EXPECT_TRUE(kpengine::render::IsShadowKindCompatible(
        kpengine::render::LightType::Directional, directional_job.kind));
    EXPECT_FALSE(kpengine::render::IsShadowKindCompatible(
        kpengine::render::LightType::Point, directional_job.kind));
    EXPECT_FALSE(kpengine::render::IsShadowJobDescValid(
        {light_handle, kpengine::render::ShadowKind::Directional2D, 0, 0}));
}

TEST(LightWorldTest, UsesCanonicalPointShadowFaceOrderAndDominantAxisTieBreak)
{
    const auto &faces = kpengine::render::GetPointShadowFaceTable();
    ASSERT_EQ(faces.size(), 6U);
    EXPECT_EQ(faces[0].direction, (kpengine::Vector3f{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(faces[1].tile_x, 1U);
    EXPECT_EQ(faces[2].tile_x, 2U);
    EXPECT_EQ(faces[3].tile_y, 1U);
    EXPECT_EQ(faces[4].tile_x, 1U);
    EXPECT_EQ(faces[5].tile_x, 2U);
    EXPECT_EQ(kpengine::render::SelectPointShadowFace({1.0f, 1.0f, 0.0f}), 0U);
    EXPECT_EQ(kpengine::render::SelectPointShadowFace({0.0f, -2.0f, 2.0f}), 3U);
}

TEST(LightGpuDataTest, EncodesEveryLightTypeAndMakesUnresolvedShadowsExplicitlyUnshadowed)
{
    auto directional = MakeDirectionalLight(2.0f);
    directional.enabled = false;
    directional.layer_mask = 0xABCD1234U;
    directional.shadow = kpengine::render::ShadowHandle{3, 1};
    const auto directional_gpu = kpengine::render::EncodeLightGpuData(directional);
    ASSERT_TRUE(directional_gpu.has_value());
    EXPECT_EQ(directional_gpu->type,
              static_cast<uint32_t>(kpengine::render::LightGpuType::Directional));
    EXPECT_EQ(directional_gpu->enabled, 0U);
    EXPECT_EQ(directional_gpu->layer_mask, 0xABCD1234U);
    EXPECT_EQ(directional_gpu->shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::Unshadowed));
    EXPECT_EQ(directional_gpu->shadow_binding_slot, 0U);
    EXPECT_EQ(directional_gpu->color_intensity,
              (kpengine::Vector4f{1.0f, 0.5f, 0.25f, 2.0f}));
    EXPECT_FLOAT_EQ(directional_gpu->direction_inner_cone.y_, -1.0f);

    auto invalid_directional = directional;
    invalid_directional.type_data = kpengine::render::DirectionalLightData{{0.0f, 0.0f, 0.0f}};
    EXPECT_FALSE(kpengine::render::EncodeLightGpuData(invalid_directional).has_value());

    const auto point_gpu = kpengine::render::EncodeLightGpuData(MakePointLight());
    ASSERT_TRUE(point_gpu.has_value());
    EXPECT_EQ(point_gpu->type, static_cast<uint32_t>(kpengine::render::LightGpuType::Point));
    EXPECT_FLOAT_EQ(point_gpu->position_range.x_, 1.0f);
    EXPECT_FLOAT_EQ(point_gpu->position_range.w_, 10.0f);

    const auto spot_gpu = kpengine::render::EncodeLightGpuData(MakeSpotLight());
    ASSERT_TRUE(spot_gpu.has_value());
    EXPECT_EQ(spot_gpu->type, static_cast<uint32_t>(kpengine::render::LightGpuType::Spot));
    EXPECT_FLOAT_EQ(spot_gpu->direction_inner_cone.w_, 0.25f);
    EXPECT_FLOAT_EQ(spot_gpu->outer_cone_radians, 0.75f);
}

TEST(LightGpuDataTest, UsesAVersionedBoundedFramePayload)
{
    std::vector<kpengine::render::Light> lights{};
    lights.reserve(kpengine::render::kMaxFrameLights + 1U);
    for (uint32_t index = 0; index < kpengine::render::kMaxFrameLights + 1U; ++index)
    {
        lights.push_back({kpengine::render::LightHandle{index + 1U, 1U}, MakeDirectionalLight()});
    }
    const kpengine::render::LightGpuFrameData frame_data =
        kpengine::render::BuildLightGpuFrameData(lights);
    EXPECT_EQ(frame_data.header.abi_version, kpengine::render::kLightGpuDataAbiVersion);
    EXPECT_EQ(frame_data.header.light_count, kpengine::render::kMaxFrameLights);
    EXPECT_EQ(frame_data.header.light_stride, sizeof(kpengine::render::LightGpuData));
    EXPECT_EQ(kpengine::render::kFrameLightingDescriptorSet, 0U);
    EXPECT_EQ(kpengine::render::kFrameLightingDescriptorBinding, 4U);
    EXPECT_TRUE(kpengine::render::IsLightGpuFrameHeaderCompatible(frame_data.header));

    auto incompatible_version = frame_data.header;
    incompatible_version.abi_version++;
    EXPECT_FALSE(kpengine::render::IsLightGpuFrameHeaderCompatible(incompatible_version));
    auto incompatible_stride = frame_data.header;
    incompatible_stride.light_stride--;
    EXPECT_FALSE(kpengine::render::IsLightGpuFrameHeaderCompatible(incompatible_stride));
    auto incompatible_count = frame_data.header;
    incompatible_count.light_count++;
    EXPECT_FALSE(kpengine::render::IsLightGpuFrameHeaderCompatible(incompatible_count));
}

TEST(LightGpuDataTest, ResolvesOnlyTheMatchingScheduledShadowIntoTheGpuRecord)
{
    const kpengine::render::LightHandle light_handle{7U, 2U};
    const kpengine::render::ShadowHandle shadow_handle{3U, 4U};
    auto directional = MakeDirectionalLight();
    directional.shadow = shadow_handle;
    const std::vector<kpengine::render::Light> lights{{light_handle, directional}};
    const kpengine::render::ResolvedLightShadowBinding resolved{
        light_handle, shadow_handle, kpengine::render::ShadowKind::Directional2D, 0U};

    const kpengine::render::LightGpuFrameData frame_data =
        kpengine::render::BuildLightGpuFrameData(lights, resolved);
    EXPECT_EQ(frame_data.lights[0].shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::Directional2D));
    EXPECT_EQ(frame_data.lights[0].shadow_binding_slot, 0U);

    auto stale = resolved;
    stale.shadow = kpengine::render::ShadowHandle{3U, 5U};
    const kpengine::render::LightGpuFrameData stale_data =
        kpengine::render::BuildLightGpuFrameData(lights, stale);
    EXPECT_EQ(stale_data.lights[0].shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::Unshadowed));

    auto incompatible = resolved;
    incompatible.kind = kpengine::render::ShadowKind::PointCube;
    const kpengine::render::LightGpuFrameData incompatible_data =
        kpengine::render::BuildLightGpuFrameData(lights, incompatible);
    EXPECT_EQ(incompatible_data.lights[0].shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::Unshadowed));
}

TEST(LightGpuDataTest, CoexistsDirectionalAndSpotBindingsWithoutChangingStride)
{
    const kpengine::render::LightHandle directional_handle{7U, 2U};
    const kpengine::render::LightHandle spot_handle{8U, 1U};
    const kpengine::render::ShadowHandle directional_shadow{3U, 4U};
    const kpengine::render::ShadowHandle spot_shadow{4U, 2U};
    auto directional = MakeDirectionalLight();
    directional.shadow = directional_shadow;
    auto spot = MakeSpotLight();
    spot.shadow = spot_shadow;
    const std::vector<kpengine::render::Light> lights{
        {directional_handle, directional}, {spot_handle, spot}};
    const kpengine::render::ResolvedLightShadowBindings bindings{
        {directional_handle, directional_shadow, kpengine::render::ShadowKind::Directional2D, 0U},
        {spot_handle, spot_shadow, kpengine::render::ShadowKind::Spot2D, 1U}};
    const auto frame = kpengine::render::BuildLightGpuFrameData(lights, bindings);
    EXPECT_EQ(frame.header.light_stride, sizeof(kpengine::render::LightGpuData));
    EXPECT_EQ(frame.lights[0].shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::Directional2D));
    EXPECT_EQ(frame.lights[1].shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::Spot2D));
    EXPECT_EQ(frame.lights[1].shadow_binding_slot, 1U);
}

TEST(LightGpuDataTest, ResolvesPointCubeBindingAtSlotTwoWithoutChangingStride)
{
    const kpengine::render::LightHandle light_handle{9U, 1U};
    const kpengine::render::ShadowHandle shadow_handle{5U, 1U};
    auto point = MakePointLight();
    point.shadow = shadow_handle;
    const std::vector<kpengine::render::Light> lights{{light_handle, point}};
    const kpengine::render::ResolvedLightShadowBinding binding{
        light_handle, shadow_handle, kpengine::render::ShadowKind::PointCube, 2U};
    const auto frame = kpengine::render::BuildLightGpuFrameData(lights, binding);
    EXPECT_EQ(frame.header.light_stride, sizeof(kpengine::render::LightGpuData));
    EXPECT_EQ(frame.lights[0].shadow_kind,
              static_cast<uint32_t>(kpengine::render::LightGpuShadowKind::PointCube));
    EXPECT_EQ(frame.lights[0].shadow_binding_slot, 2U);
}
