#pragma once

// Analysis windows (scipy shapes; periodic hanning/hamming, symmetric
// blackman/blackmanharris -- mixture matches the oracles, do not "fix").

#include "MorphexDspTypes.h"

#include <string>

namespace morphex::dsp::windowing
{
    inline Vector hanning (std::size_t size)
    {
        Vector w (size);
        for (std::size_t i = 0; i < size; ++i)
            w[i] = 0.5 - 0.5 * std::cos (2.0 * constPi() * (double) i / (double) size);
        return w;
    }

    inline Vector hamming (std::size_t size)
    {
        Vector w (size);
        for (std::size_t i = 0; i < size; ++i)
            w[i] = 0.54 - 0.46 * std::cos (2.0 * constPi() * (double) i / (double) size);
        return w;
    }

    inline Vector blackman (std::size_t size)
    {
        Vector w (size, 0.42);
        if (size < 2)
            return w;
        for (std::size_t i = 0; i < size; ++i)
        {
            const double x = 2.0 * constPi() * (double) i / (double) (size - 1);
            w[i] = 0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x);
        }
        return w;
    }

    inline Vector blackmanharris (std::size_t size)
    {
        Vector w (size, 0.35875);
        if (size < 2)
            return w;
        for (std::size_t i = 0; i < size; ++i)
        {
            const double x = 2.0 * constPi() * (double) i / (double) (size - 1);
            w[i] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x)
                 - 0.01168 * std::cos (3.0 * x);
        }
        return w;
    }

    inline Vector triang (std::size_t size)
    {
        Vector w (size, 0.0);
        if (size == 0)
            return w;
        const double n = (double) size;
        for (std::size_t i = 0; i < size; ++i)
            w[i] = 1.0 - std::abs (((double) i - n / 2.0) / (n / 2.0));
        return w;
    }

    inline Vector getWindow (const std::string& name, std::size_t size)
    {
        if (name == "rectangular")
            return Vector (size, 1.0);
        if (name == "hanning")
            return hanning (size);
        if (name == "hamming")
            return hamming (size);
        if (name == "blackman")
            return blackman (size);
        if (name == "blackmanharris")
            return blackmanharris (size);
        return blackman (size);
    }
} // namespace morphex::dsp::windowing
