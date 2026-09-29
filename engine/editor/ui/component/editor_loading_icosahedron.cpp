#include "editor/ui/component/editor_loading_icosahedron.h"

#include <algorithm>
#include <cmath>

#include "runtime/core/math/math_header.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr double kPhi = 1.6180339887498948482;
        constexpr double kInverseVertexLength = 0.5257311121191336060;
        constexpr double kCameraDistance = 3.5;
        constexpr double kTwoPi = 6.2831853071795864769;
        constexpr double kPrimaryRotationPeriod = kTwoPi / 0.42;
        constexpr double kSecondaryRotationPeriod = kTwoPi / 0.23;
        constexpr double kFacingTransitionStart = -0.16;
        constexpr double kFacingTransitionEnd = 0.16;
        constexpr uint8_t kNoFace = 0xffU;

        struct Vector3
        {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
        };

        struct Face
        {
            uint8_t a = 0;
            uint8_t b = 0;
            uint8_t c = 0;
        };

        struct Edge
        {
            uint8_t a = 0;
            uint8_t b = 0;
            uint8_t face_a = kNoFace;
            uint8_t face_b = kNoFace;
        };

        struct Topology
        {
            std::array<Vector3, 12> vertices{};
            std::array<Face, 20> faces{};
            std::array<Edge, 30> edges{};
            uint8_t face_count = 0;
            uint8_t edge_count = 0;
        };

        constexpr std::array<Vector3, 12> kRawVertices{{
            {0.0, -1.0, -kPhi}, {0.0, -1.0, kPhi},  {0.0, 1.0, -kPhi},
            {0.0, 1.0, kPhi},   {-1.0, -kPhi, 0.0}, {-1.0, kPhi, 0.0},
            {1.0, -kPhi, 0.0},  {1.0, kPhi, 0.0},   {-kPhi, 0.0, -1.0},
            {-kPhi, 0.0, 1.0},  {kPhi, 0.0, -1.0},  {kPhi, 0.0, 1.0},
        }};

        constexpr double DistanceSquared(const Vector3 &left,
                                         const Vector3 &right) noexcept
        {
            const double x = left.x - right.x;
            const double y = left.y - right.y;
            const double z = left.z - right.z;
            return x * x + y * y + z * z;
        }

        constexpr Vector3 Subtract(const Vector3 &left,
                                   const Vector3 &right) noexcept
        {
            return {left.x - right.x, left.y - right.y, left.z - right.z};
        }

        constexpr Vector3 Cross(const Vector3 &left,
                                const Vector3 &right) noexcept
        {
            return {left.y * right.z - left.z * right.y,
                    left.z * right.x - left.x * right.z,
                    left.x * right.y - left.y * right.x};
        }

        constexpr double Dot(const Vector3 &left,
                             const Vector3 &right) noexcept
        {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        }

        consteval Topology BuildTopology()
        {
            Topology topology{};
            for (std::size_t index = 0; index < kRawVertices.size(); ++index)
            {
                const Vector3 &raw = kRawVertices[index];
                topology.vertices[index] = {
                    raw.x * kInverseVertexLength,
                    raw.y * kInverseVertexLength,
                    raw.z * kInverseVertexLength,
                };
            }

            constexpr double kEdgeLengthSquared = 4.0;
            constexpr double kEdgeTolerance = 1.0e-10;
            for (uint8_t a = 0; a < kRawVertices.size(); ++a)
            {
                for (uint8_t b = static_cast<uint8_t>(a + 1U);
                     b < kRawVertices.size(); ++b)
                {
                    for (uint8_t c = static_cast<uint8_t>(b + 1U);
                         c < kRawVertices.size(); ++c)
                    {
                        const double ab = DistanceSquared(kRawVertices[a], kRawVertices[b]);
                        const double ac = DistanceSquared(kRawVertices[a], kRawVertices[c]);
                        const double bc = DistanceSquared(kRawVertices[b], kRawVertices[c]);
                        if (ab < kEdgeLengthSquared - kEdgeTolerance ||
                            ab > kEdgeLengthSquared + kEdgeTolerance ||
                            ac < kEdgeLengthSquared - kEdgeTolerance ||
                            ac > kEdgeLengthSquared + kEdgeTolerance ||
                            bc < kEdgeLengthSquared - kEdgeTolerance ||
                            bc > kEdgeLengthSquared + kEdgeTolerance)
                        {
                            continue;
                        }

                        const Vector3 &va = kRawVertices[a];
                        const Vector3 &vb = kRawVertices[b];
                        const Vector3 &vc = kRawVertices[c];
                        const Vector3 normal = Cross(Subtract(vb, va), Subtract(vc, va));
                        const Vector3 centroid{va.x + vb.x + vc.x,
                                               va.y + vb.y + vc.y,
                                               va.z + vb.z + vc.z};
                        if (Dot(normal, centroid) < 0.0)
                        {
                            topology.faces[topology.face_count++] = {a, c, b};
                        }
                        else
                        {
                            topology.faces[topology.face_count++] = {a, b, c};
                        }
                    }
                }
            }

            for (uint8_t face_index = 0; face_index < topology.face_count; ++face_index)
            {
                const Face face = topology.faces[face_index];
                const std::array<std::array<uint8_t, 2>, 3> face_edges{{
                    {{face.a, face.b}}, {{face.b, face.c}}, {{face.c, face.a}},
                }};
                for (const auto &face_edge : face_edges)
                {
                    const uint8_t edge_a = std::min(face_edge[0], face_edge[1]);
                    const uint8_t edge_b = std::max(face_edge[0], face_edge[1]);
                    uint8_t edge_index = 0;
                    while (edge_index < topology.edge_count &&
                           (topology.edges[edge_index].a != edge_a ||
                            topology.edges[edge_index].b != edge_b))
                    {
                        ++edge_index;
                    }

                    if (edge_index == topology.edge_count)
                    {
                        topology.edges[topology.edge_count++] = {
                            edge_a, edge_b, face_index, kNoFace};
                    }
                    else
                    {
                        topology.edges[edge_index].face_b = face_index;
                    }
                }
            }
            return topology;
        }

        constexpr Topology kTopology = BuildTopology();
        static_assert(kTopology.face_count == 20U);
        static_assert(kTopology.edge_count == 30U);

        double ReduceAngle(const double elapsed_seconds,
                           const double period, const double angular_speed) noexcept
        {
            if (!std::isfinite(elapsed_seconds))
            {
                return 0.0;
            }
            return std::fmod(elapsed_seconds, period) * angular_speed;
        }

        kpengine::Vector3d Rotate(const kpengine::Vector3d &vertex,
                                  const double primary_angle,
                                  const double secondary_angle) noexcept
        {
            const double primary_cos = std::cos(primary_angle);
            const double primary_sin = std::sin(primary_angle);
            const double secondary_cos = std::cos(secondary_angle);
            const double secondary_sin = std::sin(secondary_angle);

            const double x = vertex.x_ * primary_cos + vertex.z_ * primary_sin;
            const double z = -vertex.x_ * primary_sin + vertex.z_ * primary_cos;
            return kpengine::Vector3d(
                x, vertex.y_ * secondary_cos - z * secondary_sin,
                vertex.y_ * secondary_sin + z * secondary_cos);
        }

        double SmoothFacingWeight(const double facing) noexcept
        {
            const double t = std::clamp(
                (facing - kFacingTransitionStart) /
                    (kFacingTransitionEnd - kFacingTransitionStart),
                0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }
    }

    const LoadingIcosahedronTopology &GetLoadingIcosahedronTopology() noexcept
    {
        static const LoadingIcosahedronTopology topology = []
        {
            LoadingIcosahedronTopology result{};
            for (std::size_t index = 0; index < result.vertices.size(); ++index)
            {
                const Vector3 &vertex = kTopology.vertices[index];
                result.vertices[index] = {vertex.x, vertex.y, vertex.z};
            }
            for (std::size_t index = 0; index < result.faces.size(); ++index)
            {
                const Face &face = kTopology.faces[index];
                result.faces[index] = {face.a, face.b, face.c};
            }
            for (std::size_t index = 0; index < result.edges.size(); ++index)
            {
                const Edge &edge = kTopology.edges[index];
                result.edges[index] = {edge.a, edge.b, edge.face_a, edge.face_b};
            }
            return result;
        }();
        return topology;
    }

    LoadingIcosahedronFrame ProjectLoadingIcosahedron(
        const double elapsed_seconds, LoadingIcosahedronPoint center,
        float radius_pixels) noexcept
    {
        if (!std::isfinite(center.x))
        {
            center.x = 0.0f;
        }
        if (!std::isfinite(center.y))
        {
            center.y = 0.0f;
        }
        if (!std::isfinite(radius_pixels))
        {
            radius_pixels = 0.0f;
        }
        radius_pixels = std::clamp(radius_pixels, 0.0f, 4096.0f);

        const double primary_angle = ReduceAngle(elapsed_seconds,
                                                 kPrimaryRotationPeriod, 0.42);
        const double secondary_angle = ReduceAngle(elapsed_seconds,
                                                   kSecondaryRotationPeriod, 0.23);
        std::array<kpengine::Vector3d, 12> view_vertices{};
        for (std::size_t index = 0; index < kTopology.vertices.size(); ++index)
        {
            const Vector3 &vertex = kTopology.vertices[index];
            view_vertices[index] = Rotate(kpengine::Vector3d(vertex.x, vertex.y, vertex.z),
                                          primary_angle, secondary_angle);
        }

        std::array<double, 20> face_visibility{};
        for (std::size_t index = 0; index < kTopology.faces.size(); ++index)
        {
            const Face face = kTopology.faces[index];
            const kpengine::Vector3d edge_a =
                view_vertices[face.b] - view_vertices[face.a];
            const kpengine::Vector3d edge_b =
                view_vertices[face.c] - view_vertices[face.a];
            const kpengine::Vector3d normal = edge_a.CrossProduct(edge_b);
            const double normal_length = normal.Norm();
            const double facing = normal_length > 0.0 ? normal.z_ / normal_length : -1.0;
            face_visibility[index] = SmoothFacingWeight(facing);
        }

        LoadingIcosahedronFrame frame{};
        for (std::size_t index = 0; index < view_vertices.size(); ++index)
        {
            const kpengine::Vector3d &vertex = view_vertices[index];
            const double perspective = kCameraDistance /
                                       (kCameraDistance - vertex.z_);
            frame.vertices[index] = {
                center.x + static_cast<float>(vertex.x_ * perspective) * radius_pixels,
                center.y - static_cast<float>(vertex.y_ * perspective) * radius_pixels,
            };
        }
        for (std::size_t index = 0; index < kTopology.edges.size(); ++index)
        {
            const Edge &edge = kTopology.edges[index];
            const double weight_a = face_visibility[edge.face_a];
            const double weight_b = face_visibility[edge.face_b];
            const double combined_weight = 1.0 - (1.0 - weight_a) * (1.0 - weight_b);
            frame.edges[index] = {edge.a, edge.b,
                                  static_cast<float>(combined_weight)};
        }
        return frame;
    }
}
