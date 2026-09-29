#include "terrain_core.h"

#include <cmath>
#include <limits>
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
}
