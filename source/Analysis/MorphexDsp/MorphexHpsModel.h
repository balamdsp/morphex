#pragma once

// MorphexHpsModel — full HPS analysis/synthesis orchestration.
// Port of morphex_extra/.../dsp/hps.rs (hpsModel.py).

#include "MorphexHarmonicModel.h"
#include "MorphexSineModel.h"
#include "MorphexStochasticModel.h"

namespace morphex::dsp::hps
{
    struct AnalysisResult
    {
        Matrix hfreq, hmag, hphase;
        Matrix sfreq, smag, sphase;
        Matrix stocEnv;
        Vector y, yh, yst;
    };

    struct AnalysisParams
    {
        std::string windowType = "blackman";
        std::size_t windowSize = 1001;
        std::size_t fftSize = 1024;
        double magnitudeThreshold = -100.0;
        double minSineDur = 0.05;
        double minF0 = 100.0;
        double maxF0 = 1000.0;
        double maxF0Error = 50.0;
        double harmDevSlope = 0.01;
        std::size_t maxHarm = 200;
        double stocFact = 0.1;
    };

    constexpr std::size_t synthesisFftSize = 512;
    constexpr std::size_t hopSize = 128;

    inline std::tuple<Vector, Vector, Vector> hpsModelSynth (const Matrix& hfreq,
                                                             const Matrix& hmag,
                                                             const Matrix& hphase,
                                                             const Matrix& stocEnv,
                                                             std::size_t ns, std::size_t h,
                                                             double fs)
    {
        (void) hphase; // empty phases: phase propagation is used instead
        const Matrix emptyPhases;
        const Vector yh = sine::sineModelSynth (hfreq, hmag, emptyPhases, ns, h, fs);
        const Vector yst = stochastic::stochasticModelSynth (stocEnv, h, h * 2);

        const std::size_t minLen = std::min (yh.size(), yst.size());
        Vector y (minLen, 0.0);
        for (std::size_t i = 0; i < minLen; ++i)
            y[i] = yh[i] + yst[i];
        return { y, yh, yst };
    }

    inline AnalysisResult hpsModelAnalysis (const Vector& x, double fs, const AnalysisParams& p)
    {
        const Vector w = windowing::getWindow (p.windowType, p.windowSize);

        auto [hfreq, hmag, hphase] = harmonic::harmonicModelAnalysis (
            x, fs, w, p.fftSize, hopSize, p.magnitudeThreshold, p.maxHarm,
            p.minF0, p.maxF0, p.maxF0Error, p.harmDevSlope, p.minSineDur);

        const Vector xr = util::sineSubtraction (x, synthesisFftSize, hopSize,
                                                 hfreq, hmag, hphase, fs);

        auto [sfreq, smag, sphase] = sine::sineModelAnalysis (
            xr, fs, w, p.fftSize, hopSize, p.magnitudeThreshold,
            p.maxHarm, p.minSineDur, 20.0, 0.01);

        const Matrix stocEnv = stochastic::stochasticModelAnalysis (xr, hopSize, hopSize * 2,
                                                                    p.stocFact);

        auto [y, yh, yst] = hpsModelSynth (hfreq, hmag, hphase, stocEnv,
                                           synthesisFftSize, hopSize, fs);

        return { hfreq, hmag, hphase, sfreq, smag, sphase, stocEnv, y, yh, yst };
    }
} // namespace morphex::dsp::hps
