#pragma once

// MorphexHarmonicModel — harmonic detection + f0-tracked analysis.
// Port of morphex_extra/.../dsp/harmonic.rs (harmonicModel.py).

#include "MorphexDspDft.h"

namespace morphex::dsp::harmonic
{
    using FrameTracks = std::tuple<Vector, Vector, Vector>;
    using TrackMatrices = std::tuple<Matrix, Matrix, Matrix>;

    inline FrameTracks harmonicDetection (const Vector& pfreq, const Vector& pmag,
                                     const Vector& pphase, double f0, std::size_t nHarm,
                                     const Vector& hfreqp, double fs, double harmDevSlope)
    {
        if (f0 <= 0.0)
            return { Vector (nHarm, 0.0), Vector (nHarm, 0.0), Vector (nHarm, 0.0) };

        Vector hfreq (nHarm, 0.0), hmag (nHarm, -100.0), hphase (nHarm, 0.0);

        for (std::size_t hi = 0; hi < nHarm; ++hi)
        {
            const double hf = f0 * (double) (hi + 1);
            if (hf >= fs / 2.0)
                break;

            std::size_t bestPei = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (std::size_t i = 0; i < pfreq.size(); ++i)
            {
                const double d = std::abs (pfreq[i] - hf);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestPei = i;
                }
            }

            const double dev1 = std::abs (pfreq[bestPei] - hf);
            const double dev2 = (hfreqp.size() > hi && hfreqp[hi] > 0.0)
                ? std::abs (pfreq[bestPei] - hfreqp[hi])
                : fs;
            const double threshold = f0 / 3.0 + harmDevSlope * pfreq[bestPei];

            if (dev1 < threshold || dev2 < threshold)
            {
                hfreq[hi] = pfreq[bestPei];
                hmag[hi] = pmag[bestPei];
                hphase[hi] = pphase[bestPei];
            }
        }
        return { hfreq, hmag, hphase };
    }

    inline TrackMatrices harmonicModelAnalysis (const Vector& x, double fs, const Vector& w,
                                         std::size_t n, std::size_t h, double threshold,
                                         std::size_t nHarm, double minf0, double maxf0,
                                         double f0et, double harmDevSlope, double minSineDur)
    {
        const std::size_t m = w.size();
        const std::size_t hM1 = (m + 1) / 2;
        const std::size_t hM2 = m / 2;

        Vector xPadded (hM2 + x.size() + hM2, 0.0);
        std::copy (x.begin(), x.end(), xPadded.begin() + (Vector::difference_type) hM2);

        double wSum = 0.0;
        for (auto v : w)
            wSum += v;
        Vector wNorm (m);
        for (std::size_t i = 0; i < m; ++i)
            wNorm[i] = w[i] / wSum;

        Vector hfreqp;
        double f0t = 0.0, f0stable = 0.0;
        Matrix xhfreq, xhmag, xhphase;

        std::size_t pin = hM1;
        const std::size_t pend = xPadded.size() - hM1;
        while (pin <= pend)
        {
            Vector x1 (m);
            for (std::size_t i = 0; i < m; ++i)
                x1[i] = xPadded[pin - hM1 + i];
            const auto [mX, pX] = dft::dftAnal (x1, wNorm, n);
            const auto ploc = util::peakDetection (mX, threshold);
            const auto [iploc, ipmag, ipphase] = util::peakInterp (mX, pX, ploc);
            Vector ipfreq (iploc.size());
            for (std::size_t i = 0; i < iploc.size(); ++i)
                ipfreq[i] = fs * iploc[i] / (double) n;

            f0t = util::f0Twm (ipfreq, ipmag, f0et, minf0, maxf0, f0stable);

            if ((f0stable == 0.0 && f0t > 0.0)
                || (f0stable > 0.0 && std::abs (f0stable - f0t) < f0stable / 5.0))
                f0stable = f0t;
            else
                f0stable = 0.0;

            auto [hfreq, hmag, hphase] =
                harmonicDetection (ipfreq, ipmag, ipphase, f0t, nHarm, hfreqp, fs, harmDevSlope);
            hfreqp = hfreq;

            xhfreq.push_back (hfreq);
            xhmag.push_back (hmag);
            xhphase.push_back (hphase);

            pin += h;
        }

        const std::size_t minTrackLen =
            (std::size_t) std::round (fs * minSineDur / (double) h);
        util::cleaningSineTracks (xhfreq, minTrackLen);
        return { xhfreq, xhmag, xhphase };
    }
} // namespace morphex::dsp::harmonic
