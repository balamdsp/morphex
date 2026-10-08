#pragma once

// MorphexSineModel — sinusoidal tracking, analysis and synthesis.
// Port of morphex_extra/.../dsp/sine.rs (sineModel.py).

#include "MorphexDspDft.h"

namespace morphex::dsp::sine
{
    using FrameTracks = std::tuple<Vector, Vector, Vector>;
    using TrackMatrices = std::tuple<Matrix, Matrix, Matrix>;

    inline FrameTracks sineTracking (const Vector& pfreq, const Vector& pmag, const Vector& pphase,
                                const Vector& tfreq, double freqDevOffset, double freqDevSlope)
    {
        const std::size_t nTracks = tfreq.size();
        Vector tfreqn (nTracks, 0.0), tmagn (nTracks, 0.0), tphasen (nTracks, 0.0);

        std::vector<std::size_t> pindexes, incoming;
        for (std::size_t i = 0; i < pfreq.size(); ++i)
        {
            if (pfreq[i] > 0.0)
                pindexes.push_back (i);
        }
        for (std::size_t i = 0; i < tfreq.size(); ++i)
        {
            if (tfreq[i] > 0.0)
                incoming.push_back (i);
        }

        if (incoming.empty() || pindexes.empty())
        {
            std::vector<std::size_t> sorted = pindexes;
            std::stable_sort (sorted.begin(), sorted.end(),
                       [&] (std::size_t a, std::size_t b) { return pmag[a] > pmag[b]; });
            const std::size_t fill = std::min (nTracks, sorted.size());
            for (std::size_t i = 0; i < fill; ++i)
            {
                tfreqn[i] = pfreq[sorted[i]];
                tmagn[i] = pmag[sorted[i]];
                tphasen[i] = pphase[sorted[i]];
            }
            for (std::size_t i = nTracks; i < sorted.size(); ++i)
            {
                tfreqn.push_back (pfreq[sorted[i]]);
                tmagn.push_back (pmag[sorted[i]]);
                tphasen.push_back (pphase[sorted[i]]);
            }
            return { tfreqn, tmagn, tphasen };
        }

        std::vector<std::size_t> magOrder = pindexes;
        std::stable_sort (magOrder.begin(), magOrder.end(),
                   [&] (std::size_t a, std::size_t b) { return pmag[a] > pmag[b]; });

        std::vector<int> newTracks (nTracks, -1);
        std::vector<std::size_t> remaining = incoming;

        for (auto pi : magOrder)
        {
            if (remaining.empty())
                break;
            std::size_t bestTrack = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (auto ti : remaining)
            {
                const double dist = std::abs (pfreq[pi] - tfreq[ti]);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestTrack = ti;
                }
            }
            if (bestDist < freqDevOffset + freqDevSlope * pfreq[pi])
            {
                newTracks[bestTrack] = (int) pi;
                remaining.erase (std::remove (remaining.begin(), remaining.end(), bestTrack),
                                 remaining.end());
            }
        }

        std::vector<bool> usedPeaks (pfreq.size(), false);
        for (std::size_t ti = 0; ti < nTracks; ++ti)
        {
            if (newTracks[ti] >= 0)
            {
                const std::size_t pi = (std::size_t) newTracks[ti];
                tfreqn[ti] = pfreq[pi];
                tmagn[ti] = pmag[pi];
                tphasen[ti] = pphase[pi];
                usedPeaks[pi] = true;
            }
        }

        std::vector<std::size_t> emptyTracks;
        for (std::size_t i = 0; i < tfreq.size(); ++i)
        {
            if (tfreq[i] == 0.0)
                emptyTracks.push_back (i);
        }
        std::vector<std::size_t> unusedPeaks;
        for (std::size_t i = 0; i < pfreq.size(); ++i)
        {
            if (! usedPeaks[i] && pfreq[i] > 0.0)
                unusedPeaks.push_back (i);
        }
        std::stable_sort (unusedPeaks.begin(), unusedPeaks.end(),
                   [&] (std::size_t a, std::size_t b) { return pmag[a] > pmag[b]; });

        for (auto pi : unusedPeaks)
        {
            if (emptyTracks.empty())
            {
                tfreqn.push_back (pfreq[pi]);
                tmagn.push_back (pmag[pi]);
                tphasen.push_back (pphase[pi]);
            }
            else
            {
                const std::size_t ti = emptyTracks.front();
                emptyTracks.erase (emptyTracks.begin());
                if (ti < tfreqn.size())
                {
                    tfreqn[ti] = pfreq[pi];
                    tmagn[ti] = pmag[pi];
                    tphasen[ti] = pphase[pi];
                }
            }
        }
        return { tfreqn, tmagn, tphasen };
    }

    inline TrackMatrices sineModelAnalysis (const Vector& x, double fs, const Vector& w, std::size_t n,
                                     std::size_t h, double threshold, std::size_t maxSines,
                                     double minSineDur, double freqDevOffset, double freqDevSlope)
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

        Vector tfreq;
        Matrix allTfreq, allTmag, allTphase;

        std::size_t pin = hM1;
        const std::size_t pend = xPadded.size() - hM1;
        while (pin < pend)
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

            auto [tf, tm, tp] = sineTracking (ipfreq, ipmag, ipphase, tfreq,
                                              freqDevOffset, freqDevSlope);
            if (tf.size() > maxSines)
            {
                tf.resize (maxSines);
                tm.resize (maxSines);
                tp.resize (maxSines);
            }
            allTfreq.push_back (tf);
            allTmag.push_back (tm);
            allTphase.push_back (tp);
            tfreq = tf;

            pin += h;
        }

        const std::size_t minTrackLen =
            (std::size_t) std::round (fs * minSineDur / (double) h);
        util::cleaningSineTracks (allTfreq, minTrackLen);
        return { allTfreq, allTmag, allTphase };
    }

    inline Vector sineModelSynth (const Matrix& tfreq, const Matrix& tmag, const Matrix& tphase,
                                  std::size_t n, std::size_t h, double fs)
    {
        const std::size_t hN = n / 2;
        const std::size_t nFrames = tfreq.size();
        if (nFrames == 0)
            return {};

        Vector y (h * (nFrames + 3), 0.0);

        Vector sw (n, 0.0);
        const Vector tri = windowing::triang (2 * h);
        for (std::size_t i = 0; i < 2 * h; ++i)
            sw[hN - h + i] = tri[i];
        const Vector bh = windowing::blackmanharris (n);
        double bhSum = 0.0;
        for (auto v : bh)
            bhSum += v;
        for (std::size_t i = hN - h; i < hN + h; ++i)
        {
            const double normed = bh[i] / bhSum;
            if (std::abs (normed) > 1e-12)
                sw[i] /= normed;
        }

        std::size_t pout = 0;
        Vector lastYtfreq = tfreq[0];
        Vector ytphase (tfreq[0].size());
        for (auto& v : ytphase)
            v = util::uniformPhase();

        for (std::size_t l = 0; l < nFrames; ++l)
        {
            if (! tphase.empty() && l < tphase.size() && ! tphase[l].empty())
            {
                ytphase = tphase[l];
            }
            else
            {
                const std::size_t count = std::min (ytphase.size(), tfreq[l].size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const double f = i < lastYtfreq.size() ? lastYtfreq[i] : 0.0;
                    ytphase[i] += constPi() * (f + tfreq[l][i]) / fs * (double) h;
                }
            }

            const Vector mag = (l < tmag.size()) ? tmag[l] : Vector (tfreq[l].size(), 0.0);
            const ComplexVector ySpec = util::genSpecSines (tfreq[l], mag, ytphase, n, fs);
            lastYtfreq = tfreq[l];

            ComplexVector frame = ySpec;
            fft::fftInverse (frame);
            fft::fftShift (frame);

            for (std::size_t i = 0; i < n; ++i)
            {
                if (pout + i < y.size())
                    y[pout + i] += sw[i] * frame[i].real();
            }
            for (auto& v : ytphase)
                v = std::fmod (v, 2.0 * constPi());

            pout += h;
        }

        const std::size_t start = hN;
        const std::size_t end = y.size() - hN;
        if (end > start)
            return Vector (y.begin() + (Vector::difference_type) start,
                           y.begin() + (Vector::difference_type) end);
        return y;
    }
} // namespace morphex::dsp::sine
