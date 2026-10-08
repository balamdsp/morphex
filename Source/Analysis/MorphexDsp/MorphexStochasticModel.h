#pragma once

// MorphexStochasticModel — residual magnitude-envelope analysis/synthesis.
// Port of morphex_extra/.../dsp/stochastic.rs (stochasticModel.py).

#include "MorphexDspDft.h"

namespace morphex::dsp::stochastic
{
    inline Matrix stochasticModelAnalysis (const Vector& x, std::size_t h, std::size_t n,
                                           double stocf)
    {
        const std::size_t hN = n / 2 + 1;
        const std::size_t no2 = n / 2;

        if ((double) hN * stocf < 3.0)
            throw std::invalid_argument ("Stochastic decimation factor too small");
        if (stocf > 1.0)
            throw std::invalid_argument ("Stochastic decimation factor above 1");

        const Vector w = windowing::hanning (n);

        Vector xPadded (no2 + x.size() + no2, 0.0);
        std::copy (x.begin(), x.end(), xPadded.begin() + (Vector::difference_type) no2);

        Matrix stocEnv;
        std::size_t pin = no2;
        const std::size_t pend = xPadded.size() - no2;
        while (pin <= pend)
        {
            Vector xw (n);
            for (std::size_t i = 0; i < n; ++i)
                xw[i] = xPadded[pin - no2 + i] * w[i];

            ComplexVector fftbuf;
            fftbuf.reserve (n);
            for (auto v : xw)
                fftbuf.emplace_back (v, 0.0);
            fft::fftForward (fftbuf);

            Vector mX (hN);
            for (std::size_t i = 0; i < hN; ++i)
                mX[i] = std::max (20.0 * std::log10 (std::max (std::abs (fftbuf[i]),
                                                               std::numeric_limits<double>::epsilon())),
                                  -200.0);

            // Decimate (int() truncation, matching stochasticModel.py).
            // Cached resample matrix: same linear op, no per-frame DFT trig.
            const std::size_t decimLen = std::max ((std::size_t) ((double) hN * stocf), (std::size_t) 3);
            stocEnv.push_back (util::resampleFftCached (mX, decimLen));

            pin += h;
        }
        return stocEnv;
    }

    inline Vector stochasticModelSynth (const Matrix& stocEnv, std::size_t h, std::size_t n)
    {
        const std::size_t hN = n / 2 + 1;
        const std::size_t no2 = n / 2;
        const std::size_t nFrames = stocEnv.size();

        Vector y (h * (nFrames + 3), 0.0);

        const Vector han = windowing::hanning (n);
        Vector ws (n);
        for (std::size_t i = 0; i < n; ++i)
            ws[i] = 2.0 * han[i];

        std::size_t pout = 0;
        for (std::size_t l = 0; l < nFrames; ++l)
        {
            // Cached resample matrix: same linear op, no per-frame DFT trig.
            const Vector mY = util::resampleFftCached (stocEnv[l], hN);

            Vector pY (hN);
            for (auto& v : pY)
                v = util::uniformPhase();

            ComplexVector ySpec (n, Complex (0.0, 0.0));
            for (std::size_t i = 0; i < hN; ++i)
                ySpec[i] = std::polar (std::pow (10.0, mY[i] / 20.0), pY[i]);
            if (hN >= 2)
            {
                for (std::size_t k = 0; k < hN - 2; ++k)
                {
                    const std::size_t idx = hN + k;
                    const std::size_t src = hN - 2 - k;
                    ySpec[idx] = std::polar (std::pow (10.0, mY[src] / 20.0), -pY[src]);
                }
            }

            fft::fftInverse (ySpec);

            for (std::size_t i = 0; i < n; ++i)
            {
                if (pout + i < y.size())
                    y[pout + i] += ws[i] * ySpec[i].real();
            }
            pout += h;
        }

        const std::size_t start = no2;
        const std::size_t end = y.size() - no2;
        if (end > start)
            return Vector (y.begin() + (Vector::difference_type) start,
                           y.begin() + (Vector::difference_type) end);
        return y;
    }
} // namespace morphex::dsp::stochastic
