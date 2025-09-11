#ifndef KPENGINE_RUNTIME_SIGNAL_DFT_H
#define KPENGINE_RUNTIME_SIGNAL_DFT_H

#include <vector>
#include <complex>

namespace kpengine
{
    class DFT
    {
    public:
        static std::vector<std::complex<float>> Forward(const std::vector<std::complex<float>>& x_t);

        static std::vector<std::complex<float>> Inverse(const std::vector<std::complex<float>>& x_f);
    };
    
}

#endif