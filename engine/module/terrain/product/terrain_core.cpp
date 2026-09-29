#include "terrain_core.h"

#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

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
            std::vector<float> heights(count, no_hit_elevation_offset_m);
            for (std::uint32_t y = 0; y < domain.height; ++y)
                for (std::uint32_t x = 0; x < domain.width; ++x)
                {
                    const double px = domain.origin_x_m + x * domain.spacing_x_m;
                    const double pz = domain.origin_z_m + y * domain.spacing_z_m;
                    double highest = -std::numeric_limits<double>::infinity();
                    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
                    {
                        const auto &a = mesh.vertices[mesh.indices[triangle]].position;
                        const auto &b = mesh.vertices[mesh.indices[triangle + 1]].position;
                        const auto &c = mesh.vertices[mesh.indices[triangle + 2]].position;
                        const double denominator =
                            (static_cast<double>(b.z_) - c.z_) * (a.x_ - c.x_) +
                            (static_cast<double>(c.x_) - b.x_) * (a.z_ - c.z_);
                        if (std::abs(denominator) <= 1e-12) continue;
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

    data::MeshData BuildHeightfieldMesh(const ScalarField2D &heightfield)
    {
        const auto &domain = heightfield.Domain();
        if (heightfield.Samples().size() > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("heightfield mesh exceeds the 32-bit index range");
        const std::size_t cell_width = static_cast<std::size_t>(domain.width - 1);
        const std::size_t cell_height = static_cast<std::size_t>(domain.height - 1);
        if (cell_height != 0 && cell_width > std::numeric_limits<std::size_t>::max() / cell_height / 6)
            throw std::length_error("heightfield mesh index count overflows size_t");
        data::MeshData mesh;
        mesh.vertices.reserve(heightfield.Samples().size());
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
        mesh.indices.reserve(cell_width * cell_height * 6);
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
