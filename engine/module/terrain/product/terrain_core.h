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
    class ScalarField2D;
    class LayeredHeightfield2D;
    struct GridDomain2D;

    enum class TerrainValueKind : std::uint8_t
    {
        ScalarField2D,
        LayeredHeightfield2D,
    };

    class TerrainValue2D
    {
    public:
        virtual ~TerrainValue2D() = default;
        virtual TerrainValueKind Kind() const noexcept = 0;
        virtual const GridDomain2D &Domain() const noexcept = 0;
        virtual const std::vector<float> &Samples() const noexcept = 0;
        virtual float At(std::uint32_t x, std::uint32_t y) const = 0;
        virtual std::size_t ByteSize() const noexcept = 0;
        virtual const ScalarField2D *AsScalarField() const noexcept { return nullptr; }
        virtual const LayeredHeightfield2D *AsLayeredHeightfield() const noexcept { return nullptr; }
    };

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

    class ScalarField2D final : public TerrainValue2D
    {
    public:
        static std::shared_ptr<const ScalarField2D> Create(
            GridDomain2D domain, std::vector<float> samples,
            std::size_t maximum_samples, std::string &diagnostic);

        TerrainValueKind Kind() const noexcept override { return TerrainValueKind::ScalarField2D; }
        const GridDomain2D &Domain() const noexcept override { return domain_; }
        const std::vector<float> &Samples() const noexcept override { return samples_; }
        float At(std::uint32_t x, std::uint32_t y) const override;
        std::size_t ByteSize() const noexcept override { return samples_.size() * sizeof(float); }
        const ScalarField2D *AsScalarField() const noexcept override { return this; }

    private:
        ScalarField2D(GridDomain2D domain, std::vector<float> samples)
            : domain_(domain), samples_(std::move(samples)) {}

        GridDomain2D domain_;
        std::vector<float> samples_;
    };

    class LayeredHeightfield2D final : public TerrainValue2D
    {
    public:
        static std::shared_ptr<const LayeredHeightfield2D> Create(
            GridDomain2D domain, std::vector<float> bedrock_elevation_m,
            std::vector<float> soil_thickness_m, std::vector<float> sand_thickness_m,
            std::vector<float> water_depth_m, std::vector<float> suspended_sediment_kg_per_m2,
            std::size_t maximum_samples, std::string &diagnostic);

        TerrainValueKind Kind() const noexcept override { return TerrainValueKind::LayeredHeightfield2D; }
        const GridDomain2D &Domain() const noexcept override { return domain_; }
        const std::vector<float> &Samples() const noexcept override { return surface_elevation_m_; }
        float At(std::uint32_t x, std::uint32_t y) const override;
        std::size_t ByteSize() const noexcept override;
        const LayeredHeightfield2D *AsLayeredHeightfield() const noexcept override { return this; }

        const std::vector<float> &BedrockElevationMeters() const noexcept { return bedrock_elevation_m_; }
        const std::vector<float> &SoilThicknessMeters() const noexcept { return soil_thickness_m_; }
        const std::vector<float> &SandThicknessMeters() const noexcept { return sand_thickness_m_; }
        const std::vector<float> &WaterDepthMeters() const noexcept { return water_depth_m_; }
        const std::vector<float> &SuspendedSedimentKgPerSquareMeter() const noexcept
        { return suspended_sediment_kg_per_m2_; }

    private:
        LayeredHeightfield2D(GridDomain2D domain, std::vector<float> bedrock_elevation_m,
            std::vector<float> soil_thickness_m, std::vector<float> sand_thickness_m,
            std::vector<float> water_depth_m, std::vector<float> suspended_sediment_kg_per_m2,
            std::vector<float> surface_elevation_m)
            : domain_(domain), bedrock_elevation_m_(std::move(bedrock_elevation_m)),
              soil_thickness_m_(std::move(soil_thickness_m)), sand_thickness_m_(std::move(sand_thickness_m)),
              water_depth_m_(std::move(water_depth_m)),
              suspended_sediment_kg_per_m2_(std::move(suspended_sediment_kg_per_m2)),
              surface_elevation_m_(std::move(surface_elevation_m)) {}

        GridDomain2D domain_;
        std::vector<float> bedrock_elevation_m_;
        std::vector<float> soil_thickness_m_;
        std::vector<float> sand_thickness_m_;
        std::vector<float> water_depth_m_;
        std::vector<float> suspended_sediment_kg_per_m2_;
        std::vector<float> surface_elevation_m_;
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
    struct HeightfieldMeshOptions
    {
        double bottom_height_m = -100.0;
    };
    data::MeshData BuildHeightfieldMesh(
        const ScalarField2D &heightfield, HeightfieldMeshOptions options = {});
    DrainageNetwork RouteDrainage(const ScalarField2D &heightfield,
                                  DrainageOutletPolicy policy,
                                  std::span<const std::uint8_t> authored_lake_mask = {});
}

#endif
