#pragma once

#include <JuceHeader.h>

#include <vector>

// Analyzer pipeline types (PartialFrameData shape).
namespace morphex::analyzer
{
    enum class Stage
    {
        Reading = 0,
        DetectingOnsets,
        TrackingPartials,
        Labeling,
        Writing
    };

    struct Settings
    {
        double sampleRate = 44100.0;
        int windowSize = 1001;
        int fftSize = 1024;
        float ampFloorDb = -100.0f;
        float onsetSensitivity = 0.5f;
        float fundamental = 440.0f;
        // HPS core params (CLI/Rust defaults; mapped into hps::AnalysisParams).
        std::string windowType = "blackman";
        double minSineDur = 0.05;
        double minF0 = 100.0;
        double maxF0 = 1000.0;
        double maxF0Error = 50.0;
        double harmDevSlope = 0.01;
        int maxHarm = 200;
        double stocFact = 0.1;
    };

    struct PartialFrameData
    {
        int numFrames = 0;
        int numPartials = 0;
        double sampleRate = 44100.0;
        // Row-major [frame * numPartials + partial]; empty until backend lands.
        std::vector<float> freqs;
        std::vector<float> amps;
    };

    struct Result
    {
        PartialFrameData partials;
        std::vector<float> renderedSamples;
        double renderedSampleRate = 0.0;
        std::string sdifPath;
        // Full HPS matrices for send-to-slot .had export (empty for Synthesize).
        struct HpsMatrices
        {
            std::vector<std::vector<double>> hfreq, hmag, sfreq, smag, stocEnv;
            std::string windowType = "blackman";
            int windowSize = 1001, fftSize = 1024;
            double magnitudeThreshold = -100.0, minSineDur = 0.05;
            double minF0 = 100.0, maxF0 = 1000.0, maxF0Error = 50.0;
            double harmDevSlope = 0.01;
            int maxHarm = 200;
            double stocFact = 0.1;
            double fs = 44100.0;
            bool empty() const { return hfreq.empty(); }
        };
        HpsMatrices hpsMatrices;
    };
}
