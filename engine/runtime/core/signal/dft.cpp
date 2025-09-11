#include "dft.h"

#include "math/math_header.h"

namespace kpengine
{

    std::vector<std::complex<float>> DFT::Forward(const std::vector<std::complex<float>> &x_t)
    {
        std::vector<std::complex<float>> res;
        size_t N = x_t.size();
        for (size_t k = 0; k < N; k++)
        {
            std::complex<float> sum = 0.f;
            for (size_t n = 0; n < N; n++)
            {
                float tmp = static_cast<float>(k) * 2.f * math::Math_PI_FLOAT * static_cast<float>(n) / static_cast<float>(N);
                std::complex<float> euler = {std::cos(tmp), -std::sin(tmp)};
                sum += x_t[n] * euler;
            }
            res.push_back(sum);
        }
        return res;
    }

    std::vector<std::complex<float>> DFT::Inverse(const std::vector<std::complex<float>> &x_f)
    {
        std::vector<std::complex<float>> res;
        size_t N = x_f.size();
        std::complex<float> complex_N{static_cast<float>(N), static_cast<float>(N)};
        for (size_t k = 0; k < N; k++)
        {
            std::complex<float> sum = 0.f;
            for (size_t n = 0; n < N; n++)
            {
                float tmp = static_cast<float>(k) * 2.f * math::Math_PI_FLOAT * static_cast<float>(n) / static_cast<float>(N);
                std::complex<float> euler = {std::cos(tmp), std::sin(tmp)};
                sum += x_f[n] * euler / complex_N;
            }
            res.push_back(sum);
        }
        return res;
    }
}