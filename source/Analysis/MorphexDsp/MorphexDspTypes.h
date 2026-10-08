#pragma once

// Shared scalar/complex types for the JUCE-free HPS DSP module.

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace morphex::dsp
{
    using Complex = std::complex<double>;
    using Vector = std::vector<double>;
    using Matrix = std::vector<std::vector<double>>;
    using ComplexVector = std::vector<Complex>;

    inline double constPi() noexcept
    {
        return 3.14159265358979323846;
    }

    inline bool isPowerOfTwo (std::size_t n) noexcept
    {
        return n > 0 && (n & (n - 1)) == 0;
    }
} // namespace morphex::dsp
