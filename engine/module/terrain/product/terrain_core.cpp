#include "terrain_core.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>

namespace kpengine::terrain
{
    std::size_t GridDomain2D::SampleCount(std::size_t maximum_samples) const
    {
        if (width < 2 || height < 2)
        {
            throw std::invalid_argument("grid dimensions must both be at least two");
        }
        if (!std::isfinite(origin_x_m) || !std::isfinite(origin_z_m) ||
            !std::isfinite(spacing_x_m) || !std::isfinite(spacing_z_m) ||
            !std::isfinite(datum_y_m) || spacing_x_m <= 0.0 || spacing_z_m <= 0.0)
        {
            throw std::invalid_argument("grid coordinates and positive spacing must be finite meters");
        }
        if (!std::isfinite(origin_x_m + static_cast<double>(width - 1) * spacing_x_m) ||
            !std::isfinite(origin_z_m + static_cast<double>(height - 1) * spacing_z_m))
        {
            throw std::invalid_argument("grid world-space extent must remain finite");
        }
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        if (h != 0 && w > std::numeric_limits<std::size_t>::max() / h)
        {
            throw std::length_error("grid sample count overflows size_t");
        }
        const std::size_t count = w * h;
        if (count > maximum_samples)
        {
            throw std::length_error("grid exceeds configured sample budget");
        }
        return count;
    }

    std::shared_ptr<const ScalarField2D> ScalarField2D::Create(
        GridDomain2D domain, std::vector<float> samples,
        std::size_t maximum_samples, std::string &diagnostic)
    {
        try
        {
            const auto count = domain.SampleCount(maximum_samples);
            if (samples.size() != count)
            {
                diagnostic = "scalar field sample count does not match its domain";
                return {};
            }
            for (const float sample : samples)
            {
                if (!std::isfinite(sample))
                {
                    diagnostic = "scalar field samples must be finite";
                    return {};
                }
            }
            diagnostic.clear();
            return std::shared_ptr<const ScalarField2D>(
                new ScalarField2D(domain, std::move(samples)));
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return {};
        }
    }

    float ScalarField2D::At(std::uint32_t x, std::uint32_t y) const
    {
        if (x >= domain_.width || y >= domain_.height)
        {
            throw std::out_of_range("scalar field coordinate is outside its domain");
        }
        return samples_[static_cast<std::size_t>(y) * domain_.width + x];
    }

    std::shared_ptr<const LayeredHeightfield2D> LayeredHeightfield2D::Create(
        GridDomain2D domain, std::vector<float> bedrock_elevation_m,
        std::vector<float> soil_thickness_m, std::vector<float> sand_thickness_m,
        std::vector<float> water_depth_m, std::vector<float> suspended_sediment_kg_per_m2,
        std::size_t maximum_samples, std::string &diagnostic)
    {
        try
        {
            const std::size_t count = domain.SampleCount(maximum_samples);
            const auto require_channel = [count](const std::vector<float> &samples,
                                                  const char *name) {
                if (samples.size() != count)
                    throw std::invalid_argument(std::string(name) +
                        " sample count does not match layered heightfield domain");
            };
            require_channel(bedrock_elevation_m, "bedrock elevation");
            require_channel(soil_thickness_m, "soil thickness");
            require_channel(sand_thickness_m, "sand thickness");
            require_channel(water_depth_m, "water depth");
            require_channel(suspended_sediment_kg_per_m2, "suspended sediment");

            std::vector<float> surface(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const float bedrock = bedrock_elevation_m[i];
                const float soil = soil_thickness_m[i];
                const float sand = sand_thickness_m[i];
                const float water = water_depth_m[i];
                const float suspended = suspended_sediment_kg_per_m2[i];
                if (!std::isfinite(bedrock) || !std::isfinite(soil) || !std::isfinite(sand) ||
                    !std::isfinite(water) || !std::isfinite(suspended))
                    throw std::invalid_argument("layered heightfield channels must be finite");
                if (soil < 0.0f || sand < 0.0f || water < 0.0f || suspended < 0.0f)
                    throw std::invalid_argument("soil, sand, water and suspended sediment must be nonnegative");
                surface[i] = bedrock + soil + sand;
                if (!std::isfinite(surface[i]))
                    throw std::invalid_argument("layered heightfield surface elevation must remain finite");
            }
            diagnostic.clear();
            return std::shared_ptr<const LayeredHeightfield2D>(new LayeredHeightfield2D(
                domain, std::move(bedrock_elevation_m), std::move(soil_thickness_m),
                std::move(sand_thickness_m), std::move(water_depth_m),
                std::move(suspended_sediment_kg_per_m2), std::move(surface)));
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return {};
        }
    }

    float LayeredHeightfield2D::At(std::uint32_t x, std::uint32_t y) const
    {
        if (x >= domain_.width || y >= domain_.height)
            throw std::out_of_range("layered heightfield coordinate is outside its domain");
        return surface_elevation_m_[static_cast<std::size_t>(y) * domain_.width + x];
    }

    std::size_t LayeredHeightfield2D::ByteSize() const noexcept
    {
        return (bedrock_elevation_m_.size() + soil_thickness_m_.size() +
            sand_thickness_m_.size() + water_depth_m_.size() +
            suspended_sediment_kg_per_m2_.size() + surface_elevation_m_.size()) * sizeof(float);
    }

    namespace
    {
        double FirstDerivative(const ScalarField2D &field, std::uint32_t x,
                               std::uint32_t y, const bool along_x)
        {
            const auto &domain = field.Domain();
            const std::uint32_t length = along_x ? domain.width : domain.height;
            const double spacing = along_x ? domain.spacing_x_m : domain.spacing_z_m;
            const auto sample = [&field, x, y, along_x](std::uint32_t offset) {
                return static_cast<double>(along_x ? field.At(offset, y) : field.At(x, offset));
            };
            const std::uint32_t position = along_x ? x : y;
            if (length == 2)
                return (sample(1) - sample(0)) / spacing;
            if (position == 0)
                return (-3.0 * sample(0) + 4.0 * sample(1) - sample(2)) / (2.0 * spacing);
            if (position + 1 == length)
                return (3.0 * sample(position) - 4.0 * sample(position - 1) +
                        sample(position - 2)) / (2.0 * spacing);
            return (sample(position + 1) - sample(position - 1)) / (2.0 * spacing);
        }

        double SecondDerivative(const ScalarField2D &field, std::uint32_t x,
                                std::uint32_t y, const bool along_x)
        {
            const auto &domain = field.Domain();
            const std::uint32_t length = along_x ? domain.width : domain.height;
            const double spacing = along_x ? domain.spacing_x_m : domain.spacing_z_m;
            const auto sample = [&field, x, y, along_x](std::uint32_t offset) {
                return static_cast<double>(along_x ? field.At(offset, y) : field.At(x, offset));
            };
            const std::uint32_t position = along_x ? x : y;
            if (length == 2) return 0.0;
            if (position == 0)
            {
                if (length >= 4)
                    return (2.0 * sample(0) - 5.0 * sample(1) + 4.0 * sample(2) - sample(3)) /
                           (spacing * spacing);
                return (sample(0) - 2.0 * sample(1) + sample(2)) / (spacing * spacing);
            }
            if (position + 1 == length)
            {
                if (length >= 4)
                    return (2.0 * sample(position) - 5.0 * sample(position - 1) +
                            4.0 * sample(position - 2) - sample(position - 3)) /
                           (spacing * spacing);
                return (sample(position) - 2.0 * sample(position - 1) +
                        sample(position - 2)) / (spacing * spacing);
            }
            return (sample(position - 1) - 2.0 * sample(position) +
                    sample(position + 1)) / (spacing * spacing);
        }
    }

    std::vector<float> ComputeSlopeRadians(const ScalarField2D &heightfield)
    {
        const auto &domain = heightfield.Domain();
        std::vector<float> result(heightfield.Samples().size());
        for (std::uint32_t y = 0; y < domain.height; ++y)
            for (std::uint32_t x = 0; x < domain.width; ++x)
            {
                const double dx = FirstDerivative(heightfield, x, y, true);
                const double dz = FirstDerivative(heightfield, x, y, false);
                result[static_cast<std::size_t>(y) * domain.width + x] =
                    static_cast<float>(std::atan(std::hypot(dx, dz)));
            }
        return result;
    }

    std::vector<float> ComputeCurvaturePerMeter(const ScalarField2D &heightfield)
    {
        const auto &domain = heightfield.Domain();
        std::vector<float> result(heightfield.Samples().size());
        for (std::uint32_t y = 0; y < domain.height; ++y)
            for (std::uint32_t x = 0; x < domain.width; ++x)
                result[static_cast<std::size_t>(y) * domain.width + x] =
                    static_cast<float>(SecondDerivative(heightfield, x, y, true) +
                                       SecondDerivative(heightfield, x, y, false));
        return result;
    }

    std::shared_ptr<const ScalarField2D> ProjectMeshToHeightfield(
        const data::MeshData &mesh, const GridDomain2D domain, const float no_hit_elevation_offset_m,
        const std::size_t maximum_samples, std::string &diagnostic)
    {
        try
        {
            const auto count = domain.SampleCount(maximum_samples);
            if (!std::isfinite(no_hit_elevation_offset_m))
                throw std::invalid_argument("mesh projection fallback elevation offset must be finite meters");
            if (mesh.indices.size() % 3 != 0)
                throw std::invalid_argument("mesh projection requires triangle indices");
            for (const auto index : mesh.indices)
                if (index >= mesh.vertices.size())
                    throw std::invalid_argument("mesh projection found an out-of-range vertex index");
            for (const auto &vertex : mesh.vertices)
                if (!std::isfinite(vertex.position.x_) || !std::isfinite(vertex.position.y_) ||
                    !std::isfinite(vertex.position.z_))
                    throw std::invalid_argument("mesh projection requires finite vertex positions");

            struct ProjectionTriangle
            {
                Vector3f a;
                Vector3f b;
                Vector3f c;
                double min_x;
                double max_x;
                double min_z;
                double max_z;
                double center_x;
                double center_z;
            };
            struct ProjectionNode
            {
                double min_x = 0.0;
                double max_x = 0.0;
                double min_z = 0.0;
                double max_z = 0.0;
                std::size_t begin = 0;
                std::size_t end = 0;
                std::size_t left = std::numeric_limits<std::size_t>::max();
                std::size_t right = std::numeric_limits<std::size_t>::max();
            };
            std::vector<ProjectionTriangle> triangles;
            triangles.reserve(mesh.indices.size() / 3);
            for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
            {
                const Vector3f &a = mesh.vertices[mesh.indices[index]].position;
                const Vector3f &b = mesh.vertices[mesh.indices[index + 1]].position;
                const Vector3f &c = mesh.vertices[mesh.indices[index + 2]].position;
                const double denominator =
                    (static_cast<double>(b.z_) - c.z_) * (a.x_ - c.x_) +
                    (static_cast<double>(c.x_) - b.x_) * (a.z_ - c.z_);
                if (std::abs(denominator) <= 1e-12) continue;
                const double min_x = std::min({static_cast<double>(a.x_),
                    static_cast<double>(b.x_), static_cast<double>(c.x_)});
                const double max_x = std::max({static_cast<double>(a.x_),
                    static_cast<double>(b.x_), static_cast<double>(c.x_)});
                const double min_z = std::min({static_cast<double>(a.z_),
                    static_cast<double>(b.z_), static_cast<double>(c.z_)});
                const double max_z = std::max({static_cast<double>(a.z_),
                    static_cast<double>(b.z_), static_cast<double>(c.z_)});
                triangles.push_back({a, b, c, min_x, max_x, min_z, max_z,
                    (min_x + max_x) * 0.5, (min_z + max_z) * 0.5});
            }

            std::vector<std::size_t> order(triangles.size());
            std::iota(order.begin(), order.end(), 0);
            std::vector<ProjectionNode> nodes;
            if (triangles.size() > nodes.max_size() / 2)
                throw std::length_error("mesh projection spatial index exceeds addressable size");
            nodes.reserve(triangles.size() * 2);
            const auto build_node = [&](auto &&self, const std::size_t begin,
                                        const std::size_t end) -> std::size_t {
                ProjectionNode node;
                node.begin = begin;
                node.end = end;
                node.min_x = node.min_z = std::numeric_limits<double>::infinity();
                node.max_x = node.max_z = -std::numeric_limits<double>::infinity();
                double min_center_x = std::numeric_limits<double>::infinity();
                double max_center_x = -std::numeric_limits<double>::infinity();
                double min_center_z = std::numeric_limits<double>::infinity();
                double max_center_z = -std::numeric_limits<double>::infinity();
                for (std::size_t i = begin; i < end; ++i)
                {
                    const ProjectionTriangle &triangle = triangles[order[i]];
                    node.min_x = std::min(node.min_x, triangle.min_x);
                    node.max_x = std::max(node.max_x, triangle.max_x);
                    node.min_z = std::min(node.min_z, triangle.min_z);
                    node.max_z = std::max(node.max_z, triangle.max_z);
                    min_center_x = std::min(min_center_x, triangle.center_x);
                    max_center_x = std::max(max_center_x, triangle.center_x);
                    min_center_z = std::min(min_center_z, triangle.center_z);
                    max_center_z = std::max(max_center_z, triangle.center_z);
                }
                const std::size_t node_index = nodes.size();
                nodes.push_back(node);
                constexpr std::size_t kLeafTriangleCount = 8;
                if (end - begin <= kLeafTriangleCount) return node_index;
                const bool split_x = max_center_x - min_center_x >=
                                     max_center_z - min_center_z;
                const std::size_t middle = begin + (end - begin) / 2;
                std::nth_element(order.begin() + static_cast<std::ptrdiff_t>(begin),
                    order.begin() + static_cast<std::ptrdiff_t>(middle),
                    order.begin() + static_cast<std::ptrdiff_t>(end),
                    [&](const std::size_t lhs, const std::size_t rhs) {
                        return split_x ? triangles[lhs].center_x < triangles[rhs].center_x
                                       : triangles[lhs].center_z < triangles[rhs].center_z;
                    });
                nodes[node_index].left = self(self, begin, middle);
                nodes[node_index].right = self(self, middle, end);
                return node_index;
            };
            if (!triangles.empty()) (void)build_node(build_node, 0, triangles.size());

            std::vector<float> heights(count, no_hit_elevation_offset_m);
            std::vector<std::size_t> traversal;
            traversal.reserve(64);
            for (std::uint32_t y = 0; y < domain.height; ++y)
                for (std::uint32_t x = 0; x < domain.width; ++x)
                {
                    const double px = domain.origin_x_m + x * domain.spacing_x_m;
                    const double pz = domain.origin_z_m + y * domain.spacing_z_m;
                    double highest = -std::numeric_limits<double>::infinity();
                    traversal.clear();
                    if (!nodes.empty()) traversal.push_back(0);
                    while (!traversal.empty())
                    {
                        const ProjectionNode &node = nodes[traversal.back()];
                        traversal.pop_back();
                        if (px < node.min_x - 1e-8 || px > node.max_x + 1e-8 ||
                            pz < node.min_z - 1e-8 || pz > node.max_z + 1e-8)
                            continue;
                        if (node.left != std::numeric_limits<std::size_t>::max())
                        {
                            traversal.push_back(node.left);
                            traversal.push_back(node.right);
                            continue;
                        }
                        for (std::size_t item = node.begin; item < node.end; ++item)
                        {
                        const ProjectionTriangle &triangle = triangles[order[item]];
                        if (px < triangle.min_x - 1e-8 || px > triangle.max_x + 1e-8 ||
                            pz < triangle.min_z - 1e-8 || pz > triangle.max_z + 1e-8)
                            continue;
                        const auto &a = triangle.a;
                        const auto &b = triangle.b;
                        const auto &c = triangle.c;
                        const double denominator =
                            (static_cast<double>(b.z_) - c.z_) * (a.x_ - c.x_) +
                            (static_cast<double>(c.x_) - b.x_) * (a.z_ - c.z_);
                        const double u = ((static_cast<double>(b.z_) - c.z_) * (px - c.x_) +
                                          (static_cast<double>(c.x_) - b.x_) * (pz - c.z_)) / denominator;
                        const double v = ((static_cast<double>(c.z_) - a.z_) * (px - c.x_) +
                                          (static_cast<double>(a.x_) - c.x_) * (pz - c.z_)) / denominator;
                        const double w = 1.0 - u - v;
                        if (u < -1e-8 || v < -1e-8 || w < -1e-8) continue;
                        const double height = u * a.y_ + v * b.y_ + w * c.y_;
                        if (!std::isfinite(height))
                            throw std::invalid_argument("mesh projection encountered non-finite vertex data");
                        highest = std::max(highest, height);
                        }
                    }
                    if (std::isfinite(highest))
                        heights[static_cast<std::size_t>(y) * domain.width + x] =
                            static_cast<float>(highest - domain.datum_y_m);
                }
            return ScalarField2D::Create(domain, std::move(heights), maximum_samples, diagnostic);
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return {};
        }
    }

    data::MeshData BuildHeightfieldMesh(
        const ScalarField2D &heightfield, const HeightfieldMeshOptions options)
    {
        const auto &domain = heightfield.Domain();
        const std::size_t sample_count = heightfield.Samples().size();
        const std::size_t maximum_u32 = std::numeric_limits<std::uint32_t>::max();
        if (sample_count > maximum_u32)
            throw std::length_error("heightfield mesh exceeds the 32-bit index range");
        if (!std::isfinite(options.bottom_height_m) ||
            std::abs(options.bottom_height_m) > std::numeric_limits<float>::max())
            throw std::invalid_argument("terrain mesh bottom height must be finite float meters");

        const std::size_t width = domain.width;
        const std::size_t height = domain.height;
        const std::size_t cell_count = (width - 1) * (height - 1);
        if (cell_count > std::numeric_limits<std::size_t>::max() / 6)
            throw std::length_error("heightfield mesh index count overflows size_t");
        const std::size_t top_index_count = cell_count * 6;
        const std::size_t perimeter_count = 2 * width + 2 * height - 4;
        const std::size_t side_sample_count = 2 * width + 2 * height;
        if (cell_count > (std::numeric_limits<std::size_t>::max() - top_index_count) / 6 ||
            perimeter_count > (std::numeric_limits<std::size_t>::max() - top_index_count - cell_count * 6) / 6)
            throw std::length_error("closed terrain mesh index count overflows size_t");
        const std::size_t total_index_count = top_index_count + cell_count * 6 + perimeter_count * 6;
        if (total_index_count > maximum_u32)
            throw std::length_error("closed terrain mesh exceeds the 32-bit section range");
        if (side_sample_count > (maximum_u32 - sample_count) / 2 ||
            sample_count > maximum_u32 - sample_count - side_sample_count * 2)
            throw std::length_error("closed terrain mesh exceeds the 32-bit vertex range");

        const auto minimum_height = std::min_element(
            heightfield.Samples().begin(), heightfield.Samples().end());
        const double bottom_y = std::min(options.bottom_height_m,
            domain.datum_y_m + static_cast<double>(*minimum_height) - 1.0);
        if (std::abs(bottom_y) > std::numeric_limits<float>::max())
            throw std::overflow_error("terrain mesh bottom exceeds float meter range");

        data::MeshData mesh;
        mesh.vertices.reserve(sample_count * 2 + side_sample_count * 2);
        mesh.indices.reserve(total_index_count);
        const auto make_bounds = [&mesh](const std::uint32_t index_start,
                                         const std::uint32_t index_count) {
            const float limit = std::numeric_limits<float>::max();
            spatial::AABB bounds{{limit, limit, limit}, {-limit, -limit, -limit}};
            for (std::uint32_t i = index_start; i < index_start + index_count; ++i)
                bounds.ExpandToInclude(mesh.vertices[mesh.indices[i]].position);
            return bounds;
        };

        for (std::uint32_t y = 0; y < domain.height; ++y)
            for (std::uint32_t x = 0; x < domain.width; ++x)
            {
                const double dx = FirstDerivative(heightfield, x, y, true);
                const double dz = FirstDerivative(heightfield, x, y, false);
                const double inverse_length = 1.0 / std::hypot(std::hypot(dx, dz), 1.0);
                const double px = domain.origin_x_m + x * domain.spacing_x_m;
                const double py = domain.datum_y_m + heightfield.At(x, y);
                const double pz = domain.origin_z_m + y * domain.spacing_z_m;
                const double float_limit = std::numeric_limits<float>::max();
                if (std::abs(px) > float_limit || std::abs(py) > float_limit || std::abs(pz) > float_limit)
                    throw std::overflow_error("heightfield mesh positions exceed float meter range");
                data::Vertex vertex{};
                vertex.position = Vector3f{
                    static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
                vertex.normal = Vector3f{static_cast<float>(-dx * inverse_length),
                                        static_cast<float>(inverse_length),
                                        static_cast<float>(-dz * inverse_length)};
                vertex.tex_coord = Vector2f{static_cast<float>(x) / (domain.width - 1),
                                           static_cast<float>(y) / (domain.height - 1)};
                mesh.vertices.push_back(vertex);
            }
        const std::uint32_t top_index_start = 0;
        for (std::uint32_t y = 0; y + 1 < domain.height; ++y)
            for (std::uint32_t x = 0; x + 1 < domain.width; ++x)
            {
                const auto top_left = y * domain.width + x;
                const auto bottom_left = top_left + domain.width;
                const auto top_right = top_left + 1;
                const auto bottom_right = bottom_left + 1;
                mesh.indices.insert(mesh.indices.end(), {top_left, bottom_left, top_right,
                                                         top_right, bottom_left, bottom_right});
            }

        const auto top_index_end = static_cast<std::uint32_t>(mesh.indices.size());
        mesh.sections.push_back({top_index_start, top_index_end, 0,
                                 make_bounds(top_index_start, top_index_end)});
        const std::uint32_t side_index_start = top_index_end;
        const auto append_side = [&](const std::uint32_t count, const auto &sample_at,
                                     const Vector3f normal) {
            const std::uint32_t first_vertex = static_cast<std::uint32_t>(mesh.vertices.size());
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const auto [x, y] = sample_at(i);
                const double px = domain.origin_x_m + x * domain.spacing_x_m;
                const double py = domain.datum_y_m + heightfield.At(x, y);
                const double pz = domain.origin_z_m + y * domain.spacing_z_m;
                const float u = count > 1 ? static_cast<float>(i) / (count - 1) : 0.0f;
                data::Vertex top{};
                top.position = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
                top.normal = normal;
                top.tex_coord = {u, 1.0f};
                data::Vertex bottom = top;
                bottom.position.y_ = static_cast<float>(bottom_y);
                bottom.tex_coord.y_ = 0.0f;
                mesh.vertices.push_back(top);
                mesh.vertices.push_back(bottom);
            }
            for (std::uint32_t i = 0; i + 1 < count; ++i)
            {
                const std::uint32_t top_a = first_vertex + i * 2;
                const std::uint32_t bottom_a = top_a + 1;
                const std::uint32_t top_b = top_a + 2;
                const std::uint32_t bottom_b = top_a + 3;
                mesh.indices.insert(mesh.indices.end(), {top_a, bottom_a, top_b,
                                                         top_b, bottom_a, bottom_b});
            }
        };
        append_side(domain.width, [&domain](const std::uint32_t i) {
            return std::pair{domain.width - 1 - i, 0u};
        },
                    {0.0f, 0.0f, -1.0f});
        append_side(domain.height, [&domain](const std::uint32_t i) {
            return std::pair{domain.width - 1, domain.height - 1 - i};
        }, {1.0f, 0.0f, 0.0f});
        append_side(domain.width, [&domain](const std::uint32_t i) {
            return std::pair{i, domain.height - 1};
        }, {0.0f, 0.0f, 1.0f});
        append_side(domain.height, [&domain](const std::uint32_t i) {
            return std::pair{0u, i};
        }, {-1.0f, 0.0f, 0.0f});
        const auto side_index_end = static_cast<std::uint32_t>(mesh.indices.size());
        mesh.sections.push_back({side_index_start, side_index_end - side_index_start, 0,
                                 make_bounds(side_index_start, side_index_end - side_index_start)});

        const std::uint32_t bottom_vertex_start = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t y = 0; y < domain.height; ++y)
            for (std::uint32_t x = 0; x < domain.width; ++x)
            {
                data::Vertex vertex{};
                vertex.position = {
                    static_cast<float>(domain.origin_x_m + x * domain.spacing_x_m),
                    static_cast<float>(bottom_y),
                    static_cast<float>(domain.origin_z_m + y * domain.spacing_z_m)};
                vertex.normal = {0.0f, -1.0f, 0.0f};
                vertex.tex_coord = {static_cast<float>(x) / (domain.width - 1),
                                    static_cast<float>(y) / (domain.height - 1)};
                mesh.vertices.push_back(vertex);
            }
        const std::uint32_t bottom_index_start = static_cast<std::uint32_t>(mesh.indices.size());
        for (std::uint32_t y = 0; y + 1 < domain.height; ++y)
            for (std::uint32_t x = 0; x + 1 < domain.width; ++x)
            {
                const std::uint32_t top_left = bottom_vertex_start + y * domain.width + x;
                const std::uint32_t bottom_left = top_left + domain.width;
                const std::uint32_t top_right = top_left + 1;
                const std::uint32_t bottom_right = bottom_left + 1;
                mesh.indices.insert(mesh.indices.end(), {top_left, top_right, bottom_left,
                                                         top_right, bottom_right, bottom_left});
            }
        const auto bottom_index_end = static_cast<std::uint32_t>(mesh.indices.size());
        mesh.sections.push_back({bottom_index_start, bottom_index_end - bottom_index_start, 0,
                                 make_bounds(bottom_index_start, bottom_index_end - bottom_index_start)});

        return mesh;
    }

    DrainageNetwork RouteDrainage(const ScalarField2D &heightfield,
                                  const DrainageOutletPolicy policy,
                                  const std::span<const std::uint8_t> authored_lake_mask)
    {
        const auto &domain = heightfield.Domain();
        const std::size_t count = heightfield.Samples().size();
        if (policy == DrainageOutletPolicy::AuthoredLakesAndPerimeter &&
            authored_lake_mask.size() != count)
            throw std::invalid_argument("authored lake mask must match the heightfield domain");
        if (policy == DrainageOutletPolicy::Perimeter && !authored_lake_mask.empty())
            throw std::invalid_argument("lake mask supplied when lake outlets are disabled");

        struct QueueEntry
        {
            float elevation;
            std::uint32_t index;
        };
        const auto compare = [](const QueueEntry &a, const QueueEntry &b) {
            if (a.elevation != b.elevation) return a.elevation > b.elevation;
            return a.index > b.index;
        };
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, decltype(compare)> queue(compare);
        DrainageNetwork result;
        result.downstream.assign(count, DrainageNetwork::NoDownstream);
        result.filled_elevation_m.assign(count, 0.0f);
        result.accumulation_cells.assign(count, 1.0);
        std::vector<std::uint8_t> visited(count, 0);
        const auto add_seed = [&](std::uint32_t index) {
            if (visited[index]) return;
            visited[index] = 1;
            result.filled_elevation_m[index] = heightfield.Samples()[index];
            queue.push({result.filled_elevation_m[index], index});
        };
        for (std::uint32_t y = 0; y < domain.height; ++y)
            for (std::uint32_t x = 0; x < domain.width; ++x)
            {
                const auto index = y * domain.width + x;
                if (x == 0 || y == 0 || x + 1 == domain.width || y + 1 == domain.height ||
                    (policy == DrainageOutletPolicy::AuthoredLakesAndPerimeter && authored_lake_mask[index]))
                    add_seed(index);
            }

        result.flood_order.reserve(count);
        constexpr int offsets[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                                       {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        while (!queue.empty())
        {
            const QueueEntry current = queue.top();
            queue.pop();
            result.flood_order.push_back(current.index);
            const auto x = current.index % domain.width;
            const auto y = current.index / domain.width;
            for (const auto &offset : offsets)
            {
                const int nx = static_cast<int>(x) + offset[0];
                const int ny = static_cast<int>(y) + offset[1];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(domain.width) ||
                    ny >= static_cast<int>(domain.height)) continue;
                const auto neighbor = static_cast<std::uint32_t>(ny) * domain.width +
                                      static_cast<std::uint32_t>(nx);
                if (visited[neighbor]) continue;
                visited[neighbor] = 1;
                result.downstream[neighbor] = current.index;
                result.filled_elevation_m[neighbor] =
                    std::max(heightfield.Samples()[neighbor], current.elevation);
                queue.push({result.filled_elevation_m[neighbor], neighbor});
            }
        }
        for (auto it = result.flood_order.rbegin(); it != result.flood_order.rend(); ++it)
        {
            const auto parent = result.downstream[*it];
            if (parent != DrainageNetwork::NoDownstream)
                result.accumulation_cells[parent] += result.accumulation_cells[*it];
        }
        return result;
    }
}
