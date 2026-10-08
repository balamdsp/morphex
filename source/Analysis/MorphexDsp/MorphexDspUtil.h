#pragma once

// Spectral helpers (peaks, BH92 lobe, sine synth, TWM f0, subtraction,
// resampling); port of the Rust util.rs.

#include "MorphexDspFft.h"
#include "MorphexDspWindowing.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <tuple>

namespace morphex::dsp::util
{
    inline Vector unwrapPhase (const Vector& phase)
    {
        if (phase.empty())
            return {};
        Vector result;
        result.reserve (phase.size());
        result.push_back (phase[0]);
        for (std::size_t i = 1; i < phase.size(); ++i)
        {
            double diff = phase[i] - phase[i - 1];
            while (diff > constPi())
                diff -= 2.0 * constPi();
            while (diff < -constPi())
                diff += 2.0 * constPi();
            result.push_back (result[i - 1] + diff);
        }
        return result;
    }

    inline std::vector<std::size_t> peakDetection (const Vector& mX, double threshold)
    {
        std::vector<std::size_t> ploc;
        const std::size_t len = mX.size();
        if (len < 3)
            return ploc;
        for (std::size_t i = 1; i < len - 1; ++i)
        {
            if (mX[i] > threshold && mX[i] > mX[i - 1] && mX[i] > mX[i + 1])
                ploc.push_back (i);
        }
        return ploc;
    }

    inline std::tuple<Vector, Vector, Vector> peakInterp (const Vector& mX, const Vector& pX,
                                                          const std::vector<std::size_t>& ploc)
    {
        Vector iploc, ipmag, ipphase;
        iploc.reserve (ploc.size());
        ipmag.reserve (ploc.size());
        ipphase.reserve (ploc.size());

        for (auto loc : ploc)
        {
            if (loc == 0 || loc >= mX.size() - 1)
                continue;
            const double val = mX[loc];
            const double lval = mX[loc - 1];
            const double rval = mX[loc + 1];
            const double denom = lval - 2.0 * val + rval;
            if (std::abs (denom) < 1e-12)
            {
                iploc.push_back ((double) loc);
                ipmag.push_back (val);
                ipphase.push_back (pX[loc]);
                continue;
            }
            const double offset = 0.5 * (lval - rval) / denom;
            iploc.push_back ((double) loc + offset);
            ipmag.push_back (val - 0.25 * (lval - rval) * offset);
            const double frac = offset - std::floor (offset);
            const double idx = (double) loc + offset;
            const std::size_t idxFloor = (std::size_t) std::floor (idx);
            const std::size_t idxCeil = std::min ((std::size_t) std::ceil (idx), pX.size() - 1);
            ipphase.push_back (pX[idxFloor] * (1.0 - frac) + pX[idxCeil] * frac);
        }
        return { iploc, ipmag, ipphase };
    }

    namespace detail
    {
        inline double sinc (double x, double n)
        {
            if (std::abs (x) < 1e-12)
                return n;
            return std::sin (n * x / 2.0) / (x / 2.0);
        }

        inline double genBh92Lobe (double x)
        {
            constexpr double c[4] = { 0.35875, 0.48829, 0.14128, 0.01168 };
            constexpr double N = 512.0;
            const double f = x * constPi() * 2.0 / N;
            const double df = 2.0 * constPi() / N;
            double y = 0.0;
            for (int m = 0; m < 4; ++m)
            {
                const double md = (double) m;
                y += c[m] / 2.0 * (sinc (f - df * md, N) + sinc (f + df * md, N));
            }
            return y / N / c[0];
        }
    } // namespace detail

    // Complex spectrum of sinusoids (port of genspecsines_C).
    inline ComplexVector genSpecSines (const Vector& ipfreq, const Vector& ipmag,
                                       const Vector& ipphase, std::size_t n, double fs)
    {
        Vector real (n, 0.0), imag (n, 0.0);
        const int sizeSpecHalf = (int) std::floor ((double) n / 2.0);

        for (std::size_t ii = 0; ii < ipfreq.size(); ++ii)
        {
            const double loc = (double) n * ipfreq[ii] / fs;
            if (loc <= 0.0 || loc >= (double) sizeSpecHalf - 1.0)
                continue;
            const int plocInt = (int) std::floor (loc + 0.5);
            const double binRemainder = (double) plocInt - loc;
            const double mag = std::pow (10.0, ipmag[ii] / 20.0);
            // One phasor per partial; the 9 lobe bins share it (same values
            // up to fp reassociation, ~9x fewer trig calls in hot synth loops).
            const double phe = ipphase[ii];
            const double mcos = mag * std::cos (phe);
            const double msin = mag * std::sin (phe);

            for (int jj = -4; jj < 5; ++jj)
            {
                const int b = plocInt + jj;
                const double lobeVal = detail::genBh92Lobe (binRemainder + (double) jj);

                if (b < 0)
                {
                    const int idx = -b;
                    if (idx >= 0 && (std::size_t) idx < n)
                    {
                        real[(std::size_t) idx] += lobeVal * mcos;
                        imag[(std::size_t) idx] -= lobeVal * msin;
                    }
                }
                else if (b == 0)
                {
                    real[0] += 2.0 * lobeVal * mcos;
                }
                else if (b > sizeSpecHalf)
                {
                    const int idx = (int) n - b;
                    if (idx >= 0 && (std::size_t) idx < n)
                    {
                        real[(std::size_t) idx] += lobeVal * mcos;
                        imag[(std::size_t) idx] -= lobeVal * msin;
                    }
                }
                else if (b == sizeSpecHalf)
                {
                    real[(std::size_t) b] += 2.0 * lobeVal * mcos;
                }
                else
                {
                    const std::size_t idx = (std::size_t) b;
                    if (idx < n)
                    {
                        real[idx] += lobeVal * mcos;
                        imag[idx] += lobeVal * msin;
                    }
                }
            }
        }

        for (int ii = 1; ii < sizeSpecHalf; ++ii)
        {
            if (ii < (int) n && sizeSpecHalf + ii < (int) n)
            {
                real[(std::size_t) (sizeSpecHalf + ii)] = real[(std::size_t) (sizeSpecHalf - ii)];
                imag[(std::size_t) (sizeSpecHalf + ii)] = -imag[(std::size_t) (sizeSpecHalf - ii)];
            }
        }

        ComplexVector result;
        result.reserve (n);
        for (std::size_t i = 0; i < n; ++i)
            result.emplace_back (real[i], imag[i]);
        return result;
    }

    // Two-Way Mismatch f0 detection (port of TWM_C). Returns (f0, error).
    inline std::pair<double, double> twm (const Vector& pfreq, const Vector& pmag,
                                          const Vector& f0c)
    {
        constexpr double P = 0.5, Q = 1.4, R = 0.5, RHO = 0.33;
        constexpr std::size_t MAXNPEAKS = 10;

        if (f0c.empty() || pfreq.empty())
            return { 0.0, std::numeric_limits<double>::max() };

        const double amax = *std::max_element (pmag.begin(), pmag.end());
        const std::size_t maxnpeaks = std::min (MAXNPEAKS, pfreq.size());
        const std::size_t nf0c = f0c.size();

        Vector errorPm (nf0c, 0.0), errorMp (nf0c, 0.0);

        for (std::size_t ii = 0; ii < nf0c; ++ii)
        {
            for (std::size_t jj = 0; jj < maxnpeaks; ++jj)
            {
                const double predicted = (double) (jj + 1) * f0c[ii];
                // Hoisted out of the peak scan: same value, bit-identical.
                const double predPow = std::pow (predicted, -P);
                double minDist = std::numeric_limits<double>::max();
                std::size_t minIdx = 0;
                for (std::size_t k = 0; k < pfreq.size(); ++k)
                {
                    const double d = std::abs (pfreq[k] - predicted);
                    if (d < minDist)
                    {
                        minDist = d;
                        minIdx = k;
                    }
                }
                const double ponddif = minDist * predPow;
                const double magFactor = std::pow (10.0, (pmag[minIdx] - amax) / 20.0);
                errorPm[ii] += ponddif + magFactor * (Q * ponddif - R);
            }
        }

        // Peak-powers hoisted out of the candidate loop: same values,
        // bit-identical (each depends only on jj).
        Vector peakPow (pfreq.size(), 0.0), peakMagF (pfreq.size(), 0.0);
        for (std::size_t jj = 0; jj < pfreq.size(); ++jj)
        {
            peakPow[jj] = std::pow (pfreq[jj], -P);
            peakMagF[jj] = std::pow (10.0, (pmag[jj] - amax) / 20.0);
        }

        for (std::size_t ii = 0; ii < nf0c; ++ii)
        {
            for (std::size_t jj = 0; jj < maxnpeaks; ++jj)
            {
                if (jj >= pfreq.size())
                    break;
                const double nharm = std::max (std::round (pfreq[jj] / f0c[ii]), 1.0);
                const double freqDistance = std::abs (pfreq[jj] - nharm * f0c[ii]);
                const double ponddif = freqDistance * peakPow[jj];
                const double magFactor = peakMagF[jj];
                errorMp[ii] += magFactor * (ponddif + magFactor * (Q * ponddif - R));
            }
        }

        std::size_t bestIdx = 0;
        double bestErr = std::numeric_limits<double>::max();
        for (std::size_t ii = 0; ii < nf0c; ++ii)
        {
            const double total = (errorPm[ii] + RHO * errorMp[ii]) / (double) maxnpeaks;
            if (total < bestErr)
            {
                bestErr = total;
                bestIdx = ii;
            }
        }
        return { f0c[bestIdx], bestErr };
    }

    inline double f0Twm (const Vector& pfreq, const Vector& pmag, double ef0max,
                         double minf0, double maxf0, double f0t)
    {
        if (pfreq.size() < 3 && f0t == 0.0)
            return 0.0;

        Vector f0c, f0cm;
        for (std::size_t i = 0; i < pfreq.size(); ++i)
        {
            if (pfreq[i] > minf0 && pfreq[i] < maxf0)
            {
                f0c.push_back (pfreq[i]);
                f0cm.push_back (pmag[i]);
            }
        }
        if (f0c.empty())
            return 0.0;

        if (f0t > 0.0)
        {
            std::vector<std::size_t> shortlist;
            for (std::size_t i = 0; i < f0c.size(); ++i)
            {
                if (std::abs (f0c[i] - f0t) < f0t / 2.0)
                    shortlist.push_back (i);
            }
            std::size_t maxc = 0;
            for (std::size_t i = 1; i < f0cm.size(); ++i)
            {
                if (f0cm[i] > f0cm[maxc])
                    maxc = i;
            }
            if (std::find (shortlist.begin(), shortlist.end(), maxc) == shortlist.end())
            {
                double maxcfd = std::fmod (f0c[maxc], f0t);
                if (maxcfd > f0t / 2.0)
                    maxcfd = f0t - maxcfd;
                if (maxcfd > f0t / 4.0)
                {
                    Vector next;
                    next.push_back (pfreq[maxc]);
                    for (auto i : shortlist)
                        next.push_back (pfreq[i]);
                    f0c = next;
                }
                else
                {
                    Vector next;
                    for (auto i : shortlist)
                        next.push_back (pfreq[i]);
                    f0c = next;
                }
            }
        }

        if (f0c.empty())
            return 0.0;

        const auto [f0, err] = twm (pfreq, pmag, f0c);
        if (f0 > 0.0 && err < ef0max)
            return f0;
        return 0.0;
    }

    inline Vector sineSubtraction (const Vector& x, std::size_t ns, std::size_t h,
                                   const Matrix& sfreq, const Matrix& smag,
                                   const Matrix& sphase, double fs)
    {
        const std::size_t hN = ns / 2;

        Vector xPadded (hN + x.size() + hN, 0.0);
        std::copy (x.begin(), x.end(), xPadded.begin() + (std::vector<double>::difference_type) hN);

        const Vector bh = windowing::blackmanharris (ns);
        double bhSum = 0.0;
        for (auto v : bh)
            bhSum += v;
        Vector w (ns);
        for (std::size_t i = 0; i < ns; ++i)
            w[i] = bh[i] / bhSum;

        Vector sw (ns, 0.0);
        const Vector tri = windowing::triang (2 * h);
        for (std::size_t i = 0; i < 2 * h; ++i)
            sw[hN - h + i] = std::abs (w[hN - h + i]) > 1e-12 ? tri[i] / w[hN - h + i] : 0.0;

        const std::size_t nFrames = sfreq.size();
        Vector xr (xPadded.size(), 0.0);
        std::size_t pin = 0;

        for (std::size_t l = 0; l < nFrames; ++l)
        {
            if (pin + ns > xPadded.size())
                break;
            Vector xw (ns);
            for (std::size_t i = 0; i < ns; ++i)
                xw[i] = xPadded[pin + i] * w[i];

            ComplexVector fftbuf;
            fftbuf.reserve (ns);
            for (auto v : xw)
                fftbuf.emplace_back (v, 0.0);
            fft::fftShift (fftbuf);
            fft::fftForward (fftbuf);

            const ComplexVector specSines = genSpecSines (sfreq[l], smag[l], sphase[l], ns, fs);

            ComplexVector xrSpec (ns);
            for (std::size_t i = 0; i < ns; ++i)
                xrSpec[i] = fftbuf[i] - specSines[i];

            fft::fftInverse (xrSpec);
            fft::fftShift (xrSpec);

            for (std::size_t i = 0; i < ns; ++i)
            {
                if (pin + i < xr.size())
                    xr[pin + i] += xrSpec[i].real() * sw[i];
            }
            pin += h;
        }

        return Vector (xr.begin() + (std::vector<double>::difference_type) hN,
                       xr.begin() + (std::vector<double>::difference_type) (hN + x.size()));
    }

    inline void cleaningSineTracks (Matrix& tfreq, std::size_t minTrackLength)
    {
        if (tfreq.empty() || tfreq[0].empty())
            return;
        const std::size_t nFrames = tfreq.size();
        const std::size_t nTracks = tfreq[0].size();

        for (std::size_t t = 0; t < nTracks; ++t)
        {
            std::vector<std::size_t> begs, ends;
            for (std::size_t i = 0; i + 1 < nFrames; ++i)
            {
                if (tfreq[i][t] <= 0.0 && tfreq[i + 1][t] > 0.0)
                    begs.push_back (i + 1);
                if (tfreq[i][t] > 0.0 && tfreq[i + 1][t] <= 0.0)
                    ends.push_back (i + 1);
            }
            if (tfreq[0][t] > 0.0)
                begs.insert (begs.begin(), 0);
            if (tfreq[nFrames - 1][t] > 0.0)
                ends.push_back (nFrames - 1);

            for (std::size_t s = 0; s < begs.size() && s < ends.size(); ++s)
            {
                const std::size_t beg = begs[s];
                const std::size_t end = ends[s];
                const std::size_t length = 1 + end - beg;
                if (length <= minTrackLength)
                {
                    const std::size_t endVal = std::min (end, nFrames - 1);
                    for (std::size_t i = beg; i <= endVal; ++i)
                        tfreq[i][t] = 0.0;
                }
            }
        }
    }

    inline Vector resampleLinear (const Vector& input, std::size_t newLen)
    {
        if (input.empty() || newLen == 0)
            return Vector (newLen, 0.0);
        if (newLen == 1)
            return Vector (1, input.back()); // matches Rust f64-division saturation
        const std::size_t oldLen = input.size();
        Vector out (newLen);
        for (std::size_t i = 0; i < newLen; ++i)
        {
            const double pos = (double) i * (double) (oldLen - 1) / (double) (newLen - 1);
            const std::size_t idx = (std::size_t) std::floor (pos);
            const double frac = pos - (double) idx;
            out[i] = (idx + 1 < oldLen) ? input[idx] * (1.0 - frac) + input[idx + 1] * frac
                                        : input[oldLen - 1];
        }
        return out;
    }

    // FFT-method resampling (scipy.signal.resample for real 1D input).
    inline Vector resampleFft (const Vector& input, std::size_t newLen)
    {
        const std::size_t oldLen = input.size();
        if (oldLen == newLen)
            return input;
        if (input.empty() || newLen == 0)
            return Vector (newLen, 0.0);

        const double sFac = (double) oldLen / (double) newLen;
        const std::size_t m = std::min (newLen, oldLen);
        const std::size_t m2 = m / 2 + 1;

        ComplexVector buf;
        buf.reserve (oldLen);
        for (auto v : input)
            buf.emplace_back (v, 0.0);
        fft::fftForward (buf);

        ComplexVector y;
        y.reserve (m2);
        for (std::size_t k = 0; k < m2; ++k)
        {
            Complex v = buf[k] / sFac;
            if (m % 2 == 0 && k == m / 2 && newLen != oldLen)
                v *= (newLen < oldLen) ? 2.0 : 0.5;
            y.push_back (v);
        }

        ComplexVector spec (newLen, Complex (0.0, 0.0));
        for (std::size_t k = 0; k < m2; ++k)
        {
            spec[k] = y[k];
            if (k > 0 && k * 2 != newLen)
                spec[newLen - k] = std::conj (y[k]);
        }
        fft::fftInverse (spec);

        Vector out (newLen);
        for (std::size_t i = 0; i < newLen; ++i)
            out[i] = spec[i].real();
        return out;
    }

    // Fixed resample pairs memoize the matrix, then matvec per frame.
    inline const Matrix& resampleFftMatrix (std::size_t oldLen, std::size_t newLen)
    {
        using Key = std::pair<std::size_t, std::size_t>;
        static std::mutex mutex;
        static std::map<Key, Matrix> cache;
        const std::lock_guard<std::mutex> lock (mutex);
        const Key key { oldLen, newLen };
        const auto it = cache.find (key);
        if (it != cache.end())
            return it->second;
        Matrix mat (newLen, Vector (oldLen, 0.0));
        for (std::size_t j = 0; j < oldLen; ++j)
        {
            Vector basis (oldLen, 0.0);
            basis[j] = 1.0;
            const Vector col = resampleFft (basis, newLen);
            for (std::size_t i = 0; i < newLen; ++i)
                mat[i][j] = col[i];
        }
        return cache.emplace (key, std::move (mat)).first->second;
    }

    inline Vector resampleFftCached (const Vector& input, std::size_t newLen)
    {
        const std::size_t oldLen = input.size();
        if (oldLen == newLen)
            return input;
        if (input.empty() || newLen == 0)
            return Vector (newLen, 0.0);
        const Matrix& mat = resampleFftMatrix (oldLen, newLen);
        Vector out (newLen, 0.0);
        for (std::size_t i = 0; i < newLen; ++i)
        {
            double s = 0.0;
            const Vector& row = mat[i];
            for (std::size_t j = 0; j < oldLen; ++j)
                s += row[j] * input[j];
            out[i] = s;
        }
        return out;
    }

    inline std::mt19937_64& sharedRng()
    {
        thread_local std::mt19937_64 rng { std::random_device {}() };
        return rng;
    }

    inline double uniformPhase()
    {
        std::uniform_real_distribution<double> dist (0.0, 2.0 * constPi());
        return dist (sharedRng());
    }
} // namespace morphex::dsp::util
