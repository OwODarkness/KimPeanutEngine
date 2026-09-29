#ifndef KPENGINE_TERRAIN_CORE_H
#define KPENGINE_TERRAIN_CORE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
}

#endif
