#include <gtest/gtest.h>

#include <entt/core/hashed_string.hpp>
#include <entt/entity/registry.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

namespace {

struct Position {
    float x{};
    float y{};
};

class EnTTMetaTest : public ::testing::Test {
protected:
    void TearDown() override {
        entt::meta_reset();
    }
};

} // namespace

TEST(EnTTEcs, CreatesAndStoresComponents) {
    entt::registry registry;
    const entt::entity entity = registry.create();

    registry.emplace<Position>(entity, 1.0F, 2.0F);

    const Position &position = registry.get<Position>(entity);
    EXPECT_FLOAT_EQ(position.x, 1.0F);
    EXPECT_FLOAT_EQ(position.y, 2.0F);
}

TEST_F(EnTTMetaTest, RegistersAndReadsAProperty) {
    using namespace entt::literals;

    entt::meta_factory<Position>{}
        .type("Position"_hs)
        .data<&Position::x>("x"_hs);

    const entt::meta_type type = entt::resolve<Position>();
    ASSERT_TRUE(type);

    entt::meta_any instance{Position{3.0F, 4.0F}};
    const entt::meta_data property = type.data("x"_hs);
    ASSERT_TRUE(property);
    EXPECT_FLOAT_EQ(property.get(instance).cast<float>(), 3.0F);
}
