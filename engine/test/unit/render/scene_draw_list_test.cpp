#include <cstdint>

#include <gtest/gtest.h>

#include "render/render_world/scene_draw_list.h"

namespace
{
    using kpengine::graphics::PipelineHandle;
    using kpengine::render::SceneDrawItem;
    using kpengine::render::SceneDrawListBuilder;

    SceneDrawItem MakeItem(uint32_t pipeline_id, uint32_t material_id, uint32_t mesh_id)
    {
        SceneDrawItem item{};
        item.pipeline = {pipeline_id, 0};
        item.proxy.material = {material_id, 0};
        item.proxy.mesh = {mesh_id, 0};
        return item;
    }
}

TEST(SceneDrawListTest, SortsOpaqueItemsByPipelineThenMaterialThenMesh)
{
    std::vector<SceneDrawItem> items{
        MakeItem(2, 1, 1),
        MakeItem(1, 3, 1),
        MakeItem(1, 2, 2),
        MakeItem(1, 2, 1),
    };

    SceneDrawListBuilder::SortOpaque(items);

    ASSERT_EQ(items.size(), 4);
    EXPECT_EQ(items[0].pipeline.id, 1);
    EXPECT_EQ(items[0].proxy.material.id, 2);
    EXPECT_EQ(items[0].proxy.mesh.id, 1);
    EXPECT_EQ(items[1].pipeline.id, 1);
    EXPECT_EQ(items[1].proxy.material.id, 2);
    EXPECT_EQ(items[1].proxy.mesh.id, 2);
    EXPECT_EQ(items[2].pipeline.id, 1);
    EXPECT_EQ(items[2].proxy.material.id, 3);
    EXPECT_EQ(items[3].pipeline.id, 2);
}
