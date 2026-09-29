#ifndef KPENGINE_TERRAIN_CORE_H
#define KPENGINE_TERRAIN_CORE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "data/mesh.h"

namespace kpengine::terrain
{
    struct GridDomain2D
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        double origin_x_m = 0.0;
        double origin_z_m = 0.0;
        double spacing_x_m = 1.0;
        double spacing_z_m = 1.0;
        double datum_y_m = 0.0;

        std::size_t SampleCount(std::size_t maximum_samples) const;
        bool operator==(const GridDomain2D &) const = default;
    };

    class ScalarField2D final
    {
    public:
        static std::shared_ptr<const ScalarField2D> Create(
            GridDomain2D domain, std::vector<float> samples,
            std::size_t maximum_samples, std::string &diagnostic);

        const GridDomain2D &Domain() const noexcept { return domain_; }
        const std::vector<float> &Samples() const noexcept { return samples_; }
        float At(std::uint32_t x, std::uint32_t y) const;
        std::size_t ByteSize() const noexcept { return samples_.size() * sizeof(float); }

    private:
        ScalarField2D(GridDomain2D domain, std::vector<float> samples)
            : domain_(domain), samples_(std::move(samples)) {}

        GridDomain2D domain_;
        std::vector<float> samples_;
    };

    enum class DrainageOutletPolicy
    {
        Perimeter,
        AuthoredLakesAndPerimeter,
    };

    struct DrainageNetwork
    {
        static constexpr std::uint32_t NoDownstream = UINT32_MAX;
        std::vector<std::uint32_t> downstream;
        std::vector<float> filled_elevation_m;
        std::vector<double> accumulation_cells;
        std::vector<std::uint32_t> flood_order;
    };

    std::vector<float> ComputeSlopeRadians(const ScalarField2D &heightfield);
    std::vector<float> ComputeCurvaturePerMeter(const ScalarField2D &heightfield);
    std::shared_ptr<const ScalarField2D> ProjectMeshToHeightfield(
        const data::MeshData &mesh, GridDomain2D domain, float no_hit_elevation_offset_m,
        std::size_t maximum_samples, std::string &diagnostic);
    data::MeshData BuildHeightfieldMesh(const ScalarField2D &heightfield);
    DrainageNetwork RouteDrainage(const ScalarField2D &heightfield,
                                  DrainageOutletPolicy policy,
                                  std::span<const std::uint8_t> authored_lake_mask = {});
}

#endif
