#pragma once

// MorphexDspDft — zero-phase DFT analysis/synthesis.
// Port of morphex_extra/.../dsp/dft.rs (dftModel.py).

#include "MorphexDspFft.h"
#include "MorphexDspUtil.h"

#include <cassert>
#include <limits>

namespace morphex::dsp::dft
{
    // Returns (magnitude dB, unwrapped phase).
    inline std::pair<Vector, Vector> dftAnal (const Vector& x, const Vector& w, std::size_t n)
    {
        const std::size_t m = w.size();
        assert (n >= m);
        assert (isPowerOfTwo (n));

        const std::size_t hN = n / 2 + 1;
        const std::size_t hM1 = (m + 1) / 2;
        const std::size_t hM2 = m / 2;

        double wSum = 0.0;
        for (auto v : w)
            wSum += v;

        Vector xw (m);
        for (std::size_t i = 0; i < m && i < x.size(); ++i)
            xw[i] = x[i] * w[i] / wSum;

        ComplexVector fftbuffer (n, Complex (0.0, 0.0));
        for (std::size_t i = 0; i < hM1; ++i)
            fftbuffer[i] = Complex (xw[hM2 + i], 0.0);
        for (std::size_t i = 0; i < hM2; ++i)
            fftbuffer[n - hM2 + i] = Complex (xw[i], 0.0);

        fft::fftForward (fftbuffer);

        const double eps = std::numeric_limits<double>::epsilon();
        Vector mX;
        mX.reserve (hN);
        for (std::size_t i = 0; i < hN; ++i)
            mX.push_back (20.0 * std::log10 (std::max (std::abs (fftbuffer[i]), eps)));

        const double tol = 1e-14;
        Vector phases;
        phases.reserve (hN);
        for (std::size_t i = 0; i < hN; ++i)
        {
            double re = fftbuffer[i].real();
            double im = fftbuffer[i].imag();
            if (std::abs (re) < tol)
                re = 0.0;
            if (std::abs (im) < tol)
                im = 0.0;
            phases.push_back (std::atan2 (im, re));
        }

        return { mX, util::unwrapPhase (phases) };
    }

    inline Vector dftSynth (const Vector& mX, const Vector& pX, std::size_t m)
    {
        const std::size_t hN = mX.size();
        const std::size_t n = (hN - 1) * 2;
        assert (isPowerOfTwo (n));

        const std::size_t hM1 = (m + 1) / 2;
        const std::size_t hM2 = m / 2;

        ComplexVector ySpec (n, Complex (0.0, 0.0));
        for (std::size_t i = 0; i < hN; ++i)
        {
            const double mag = std::pow (10.0, mX[i] / 20.0);
            ySpec[i] = std::polar (mag, pX[i]);
        }
        // Negative frequencies (conjugate symmetric, matching dftModel.py)
        if (hN >= 2)
        {
            for (std::size_t k = 0; k < hN - 2; ++k)
            {
                const std::size_t idx = hN + k;
                const std::size_t src = hN - 2 - k;
                const double mag = std::pow (10.0, mX[src] / 20.0);
                ySpec[idx] = std::polar (mag, -pX[src]);
            }
        }

        fft::fftInverse (ySpec);

        Vector y (m, 0.0);
        for (std::size_t i = 0; i < hM2; ++i)
            y[i] = ySpec[n - hM2 + i].real();
        for (std::size_t i = 0; i < hM1; ++i)
            y[hM2 + i] = ySpec[i].real();
        return y;
    }
} // namespace morphex::dsp::dft
