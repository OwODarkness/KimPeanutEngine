#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

#include <gtest/gtest.h>

#include "editor/ui/component/editor_loading_icosahedron.h"

namespace
{
    bool FaceContainsEdge(const kpengine::editor::LoadingIcosahedronFace &face,
                          const uint8_t a, const uint8_t b)
    {
        const std::array<uint8_t, 3> vertices{face.a, face.b, face.c};
        bool has_a = false;
        bool has_b = false;
        for (const uint8_t vertex : vertices)
        {
            has_a = has_a || vertex == a;
            has_b = has_b || vertex == b;
        }
        return has_a && has_b;
    }
}

TEST(EditorLoadingIcosahedronTest, HasClosedTwentyFaceThirtyEdgeTopology)
{
    using namespace kpengine::editor;
    const LoadingIcosahedronTopology &topology = GetLoadingIcosahedronTopology();
    EXPECT_EQ(topology.vertices.size(), 12U);
    EXPECT_EQ(topology.faces.size(), 20U);
    EXPECT_EQ(topology.edges.size(), 30U);

    for (const LoadingIcosahedronFace &face : topology.faces)
    {
        EXPECT_LT(face.a, topology.vertices.size());
        EXPECT_LT(face.b, topology.vertices.size());
        EXPECT_LT(face.c, topology.vertices.size());
        EXPECT_NE(face.a, face.b);
        EXPECT_NE(face.b, face.c);
        EXPECT_NE(face.c, face.a);
    }

    std::set<std::pair<uint8_t, uint8_t>> unique_edges;
    std::array<uint8_t, 12> edge_incidence{};
    for (const LoadingIcosahedronEdge &edge : topology.edges)
    {
        EXPECT_LT(edge.a, edge.b);
        EXPECT_LT(edge.b, topology.vertices.size());
        EXPECT_LT(edge.face_a, topology.faces.size());
        EXPECT_LT(edge.face_b, topology.faces.size());
        EXPECT_NE(edge.face_a, edge.face_b);
        EXPECT_TRUE(unique_edges.emplace(edge.a, edge.b).second);
        EXPECT_TRUE(FaceContainsEdge(topology.faces[edge.face_a], edge.a, edge.b));
        EXPECT_TRUE(FaceContainsEdge(topology.faces[edge.face_b], edge.a, edge.b));
        ++edge_incidence[edge.a];
        ++edge_incidence[edge.b];
    }
    EXPECT_EQ(unique_edges.size(), 30U);
    for (const uint8_t incidence : edge_incidence)
    {
        EXPECT_EQ(incidence, 5U);
    }
}

TEST(EditorLoadingIcosahedronTest, ProjectionIsBoundedAndFrameRateIndependent)
{
    using namespace kpengine::editor;
    constexpr LoadingIcosahedronPoint center{220.0f, 180.0f};
    constexpr float radius = 100.0f;
    const LoadingIcosahedronFrame first = ProjectLoadingIcosahedron(2.75, center, radius);
    const LoadingIcosahedronFrame repeated = ProjectLoadingIcosahedron(2.75, center, radius);
    const LoadingIcosahedronFrame rotated = ProjectLoadingIcosahedron(5.25, center, radius);

    bool changed_position = false;
    for (std::size_t index = 0; index < first.vertices.size(); ++index)
    {
        const LoadingIcosahedronPoint &point = first.vertices[index];
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
        EXPECT_LE(std::abs(point.x - center.x), radius * 1.5f);
        EXPECT_LE(std::abs(point.y - center.y), radius * 1.5f);
        EXPECT_FLOAT_EQ(point.x, repeated.vertices[index].x);
        EXPECT_FLOAT_EQ(point.y, repeated.vertices[index].y);
        changed_position = changed_position ||
                           std::abs(point.x - rotated.vertices[index].x) > 0.01f ||
                           std::abs(point.y - rotated.vertices[index].y) > 0.01f;
    }
    EXPECT_TRUE(changed_position);

    std::size_t front_edges = 0;
    std::size_t rear_edges = 0;
    for (const LoadingIcosahedronProjectedEdge &edge : first.edges)
    {
        EXPECT_LT(edge.a, first.vertices.size());
        EXPECT_LT(edge.b, first.vertices.size());
        if (edge.front_facing_weight >= 0.5f)
        {
            ++front_edges;
        }
        else
        {
            ++rear_edges;
        }
    }
    EXPECT_GT(front_edges, 0U);
    EXPECT_GT(rear_edges, 0U);
}

TEST(EditorLoadingIcosahedronTest, FacingHighlightsFadeContinuouslyDuringRotation)
{
    using namespace kpengine::editor;
    constexpr double sample_step = 0.002;
    constexpr double sample_end = 4.0;
    LoadingIcosahedronFrame previous =
        ProjectLoadingIcosahedron(0.0, {0.0f, 0.0f}, 100.0f);
    float largest_weight_change = 0.0f;
    bool saw_transition = false;

    for (double time = sample_step; time <= sample_end; time += sample_step)
    {
        const LoadingIcosahedronFrame current =
            ProjectLoadingIcosahedron(time, {0.0f, 0.0f}, 100.0f);
        for (std::size_t index = 0; index < current.edges.size(); ++index)
        {
            const float weight = current.edges[index].front_facing_weight;
            const float previous_weight = previous.edges[index].front_facing_weight;
            EXPECT_GE(weight, 0.0f);
            EXPECT_LE(weight, 1.0f);
            largest_weight_change = std::max(
                largest_weight_change, std::abs(weight - previous_weight));
            saw_transition = saw_transition ||
                             (weight > 0.0f && weight < 1.0f);
        }
        previous = current;
    }

    EXPECT_TRUE(saw_transition);
    EXPECT_LT(largest_weight_change, 0.03f);
}

TEST(EditorLoadingIcosahedronTest, SanitizesNonFiniteInputsWithoutInvalidVertices)
{
    using namespace kpengine::editor;
    const LoadingIcosahedronFrame frame = ProjectLoadingIcosahedron(
        std::numeric_limits<double>::infinity(),
        {std::numeric_limits<float>::quiet_NaN(), 12.0f},
        std::numeric_limits<float>::infinity());
    for (const LoadingIcosahedronPoint &point : frame.vertices)
    {
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
        EXPECT_LE(std::abs(point.x), 4096.0f);
        EXPECT_LE(std::abs(point.y - 12.0f), 6144.0f);
    }
}
