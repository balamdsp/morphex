#pragma once

#include <JuceHeader.h>

#include "MorphexAnalyzerTypes.h"
#include "MorphexDsp/MorphexHad.h"
#include "MorphexDsp/MorphexHpsModel.h"
#include "MorphexDsp/MorphexSineModel.h"
#include "MorphexDsp/MorphexWav.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

// Worker HPS backend (monolithic jobs).
class MorphexAnalyzeQueue : private juce::Thread
{
public:
    using FailureNotify = std::function<void (int slotIndex, const juce::String& errorMessage)>;
    using WriteDoneNotify = std::function<void (int slotIndex, bool ok, const juce::String& errorMessage)>;
    using ResultPtr = std::shared_ptr<const morphex::analyzer::Result>;
    using ResultNotify = std::function<void (int slotIndex, bool ok, ResultPtr result, const juce::String& errorMessage)>;

    enum class Kind
    {
        Analyze,
        Synthesize
    };

    struct Request
    {
        // slotIndex: >=0 slot job, -1 scratch (analyzer window), -2 batch.
        int slotIndex = -1;
        Kind kind = Kind::Analyze;
        juce::File sourceAudio;
        juce::File outputBase;
        morphex::analyzer::Settings settings;
        bool deliverResult = false;
        morphex::analyzer::PartialFrameData synthInput;
        double synthSampleRate = 44100.0;
        // Region-scoped analysis: when true, analyze these samples instead of
        // reading sourceAudio (bracket interval sliced by the processor).
        bool useMemorySource = false;
        std::vector<float> memorySamples;
        double memoryRate = 44100.0;
    };

    MorphexAnalyzeQueue()
        : juce::Thread ("Morphex Analyzer")
    {
    }

    ~MorphexAnalyzeQueue() override
    {
        {
            const std::lock_guard<std::mutex> lock (queueMutex);
            pending.clear();
        }
        signalThreadShouldExit();
        wakeup.signal();
        stopThread (-1);
    }

    void configure (FailureNotify onFailure, WriteDoneNotify onWriteDone, ResultNotify onResult)
    {
        failureNotify = std::move (onFailure);
        writeDoneNotify = std::move (onWriteDone);
        resultNotify = std::move (onResult);

        if (! isThreadRunning())
            startThread (juce::Thread::Priority::normal);
    }

    void enqueue (const Request& request)
    {
        {
            const std::lock_guard<std::mutex> lock (queueMutex);
            if (request.slotIndex >= 0)
                pending.erase (std::remove_if (pending.begin(), pending.end(),
                                               [&] (const Request& r)
                                               { return r.slotIndex == request.slotIndex; }),
                               pending.end());
            pending.push_back (request);
        }
        wakeup.signal();
    }

    bool isBusy() const noexcept { return jobActive.load(); }
    int busySlot() const noexcept { return jobActive.load() ? activeSlot.load() : -99; }
    float overallProgress() const noexcept { return isBusy() ? progress01.load() : 0.0f; }
    int queuedCount() const noexcept
    {
        const std::lock_guard<std::mutex> lock (queueMutex);
        return (int) pending.size();
    }

    // Drop all queued (not yet started) jobs. The in-flight job is
    // monolithic and runs out; its delivery is guarded processor-side.
    void cancelPending()
    {
        const std::lock_guard<std::mutex> lock (queueMutex);
        pending.clear();
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Request job;
            bool haveJob = false;
            {
                const std::lock_guard<std::mutex> lock (queueMutex);
                if (! pending.empty())
                {
                    job = pending.front();
                    pending.pop_front();
                    haveJob = true;
                }
            }

            if (! haveJob)
            {
                wakeup.wait();
                continue;
            }

            runJob (job);
        }
    }

    void runJob (const Request& job)
    {
        activeSlot.store (job.slotIndex);
        jobActive.store (true);
        progress01.store (0.05f);

        auto result = std::make_shared<morphex::analyzer::Result>();
        bool ok = false;
        juce::String err;

        if (threadShouldExit())
        {
            activeSlot.store (-1);
            jobActive.store (false);
            return;
        }

        try
        {
            if (job.kind == Kind::Synthesize)
            {
                // Harmonics-only resynth from view partials (no stocEnv in
                // PartialFrameData, so no stochastic layer here).
                const auto& in = job.synthInput;
                morphex::dsp::Matrix tfreq (in.numFrames,
                                            morphex::dsp::Vector ((std::size_t) in.numPartials));
                morphex::dsp::Matrix tmag (in.numFrames,
                                           morphex::dsp::Vector ((std::size_t) in.numPartials));
                for (int f = 0; f < in.numFrames; ++f)
                {
                    for (int p = 0; p < in.numPartials; ++p)
                    {
                        const std::size_t i = (std::size_t) f * (std::size_t) in.numPartials
                                            + (std::size_t) p;
                        if (i < in.freqs.size())
                            tfreq[(std::size_t) f][(std::size_t) p] = (double) in.freqs[i];
                        if (i < in.amps.size())
                            tmag[(std::size_t) f][(std::size_t) p] = (double) in.amps[i];
                    }
                }
                const morphex::dsp::Matrix empty;
                const auto y = morphex::dsp::sine::sineModelSynth (
                    tfreq, tmag, empty, morphex::dsp::hps::synthesisFftSize,
                    morphex::dsp::hps::hopSize, job.synthSampleRate);
                result->renderedSamples.reserve (y.size());
                for (auto v : y)
                    result->renderedSamples.push_back ((float) v);
                result->renderedSampleRate = job.synthSampleRate;
                result->partials = in;
                ok = true;
            }
            else
            {
                morphex::dsp::hps::AnalysisParams p;
                p.windowType = job.settings.windowType;
                p.windowSize = (std::size_t) job.settings.windowSize;
                p.fftSize = (std::size_t) job.settings.fftSize;
                p.magnitudeThreshold = (double) job.settings.ampFloorDb;
                p.minSineDur = job.settings.minSineDur;
                p.minF0 = job.settings.minF0;
                p.maxF0 = job.settings.maxF0;
                p.maxF0Error = job.settings.maxF0Error;
                p.harmDevSlope = job.settings.harmDevSlope;
                p.maxHarm = (std::size_t) job.settings.maxHarm;
                p.stocFact = job.settings.stocFact;

                std::vector<double> samplesDouble;
                double sampleRate = 44100.0;
                if (job.useMemorySource && ! job.memorySamples.empty() && job.memoryRate > 0.0)
                {
                    samplesDouble.reserve (job.memorySamples.size());
                    for (auto v : job.memorySamples)
                        samplesDouble.push_back ((double) v);
                    sampleRate = job.memoryRate;
                }
                else
                {
                    auto wav = morphex::wav::readWav (
                        job.sourceAudio.getFullPathName().toStdString());
                    progress01.store (0.15f);
                    samplesDouble = std::move (wav.samples);
                    sampleRate = (double) wav.sampleRate;
                }

                auto r = morphex::dsp::hps::hpsModelAnalysis (samplesDouble, sampleRate, p);
                progress01.store (0.9f);

                const int frames = (int) r.hfreq.size();
                int partials = 0;
                for (const auto& f : r.hfreq)
                    partials = std::max (partials, (int) f.size());
                result->partials.numFrames = frames;
                result->partials.numPartials = partials;
                result->partials.sampleRate = sampleRate;
                result->partials.freqs.reserve ((std::size_t) frames * (std::size_t) partials);
                result->partials.amps.reserve ((std::size_t) frames * (std::size_t) partials);
                for (int f = 0; f < frames; ++f)
                {
                    for (int k = 0; k < partials; ++k)
                    {
                        result->partials.freqs.push_back (
                            k < (int) r.hfreq[(std::size_t) f].size()
                                ? (float) r.hfreq[(std::size_t) f][(std::size_t) k] : 0.0f);
                        result->partials.amps.push_back (
                            k < (int) r.hmag[(std::size_t) f].size()
                                ? (float) r.hmag[(std::size_t) f][(std::size_t) k] : -200.0f);
                    }
                }
                result->renderedSamples.reserve (r.y.size());
                for (auto v : r.y)
                    result->renderedSamples.push_back ((float) v);
                result->renderedSampleRate = sampleRate;

                result->hpsMatrices.hfreq = r.hfreq;
                result->hpsMatrices.hmag = r.hmag;
                result->hpsMatrices.sfreq = r.sfreq;
                result->hpsMatrices.smag = r.smag;
                result->hpsMatrices.stocEnv = r.stocEnv;
                result->hpsMatrices.windowType = p.windowType;
                result->hpsMatrices.windowSize = (int) p.windowSize;
                result->hpsMatrices.fftSize = (int) p.fftSize;
                result->hpsMatrices.magnitudeThreshold = p.magnitudeThreshold;
                result->hpsMatrices.minSineDur = p.minSineDur;
                result->hpsMatrices.minF0 = p.minF0;
                result->hpsMatrices.maxF0 = p.maxF0;
                result->hpsMatrices.maxF0Error = p.maxF0Error;
                result->hpsMatrices.harmDevSlope = p.harmDevSlope;
                result->hpsMatrices.maxHarm = (int) p.maxHarm;
                result->hpsMatrices.stocFact = p.stocFact;
                result->hpsMatrices.fs = sampleRate;

                if (job.outputBase.getFullPathName().isNotEmpty())
                {
                    const auto doc = morphex::had::buildHad (
                        p, (std::uint32_t) sampleRate, r.hfreq, r.hmag, r.sfreq, r.smag, r.stocEnv);
                    const std::string xml = morphex::had::generateHadXml (doc);
                    juce::File hadFile (job.outputBase.withFileExtension (".had"));
                    hadFile.getParentDirectory().createDirectory();
                    if (! hadFile.replaceWithText (juce::String (xml)))
                        throw std::runtime_error ("cannot write .had file");
                    result->sdifPath = hadFile.getFullPathName().toStdString();
                }
                ok = true;
            }
        }
        catch (const std::exception& e)
        {
            err = juce::String (e.what());
        }
        catch (...)
        {
            err = "Unknown analysis error";
        }

        activeSlot.store (-1);
        jobActive.store (false);
        progress01.store (0.0f);

        if (threadShouldExit())
            return;

        if (job.deliverResult)
        {
            auto onResultCopy = resultNotify;
            juce::MessageManager::callAsync ([onResultCopy, slot = job.slotIndex, result, ok, err]
            {
                if (onResultCopy)
                    onResultCopy (slot, ok, ok ? result : nullptr, err);
            });
            return;
        }

        auto onFailureCopy = failureNotify;
        auto onWriteDoneCopy = writeDoneNotify;
        juce::MessageManager::callAsync ([onFailureCopy, onWriteDoneCopy, slot = job.slotIndex,
                                          ok, err]
        {
            if (ok)
            {
                if (onWriteDoneCopy)
                    onWriteDoneCopy (slot, true, {});
            }
            else
            {
                const juce::String text = err.isEmpty() ? "Analysis failed" : err;
                if (onFailureCopy)
                    onFailureCopy (slot, text);
                else if (onWriteDoneCopy)
                    onWriteDoneCopy (slot, false, text);
            }
        });
    }

    FailureNotify failureNotify;
    WriteDoneNotify writeDoneNotify;
    ResultNotify resultNotify;

    mutable std::mutex queueMutex;
    std::deque<Request> pending;
    juce::WaitableEvent wakeup;

    std::atomic<int> activeSlot { -1 };
    std::atomic<bool> jobActive { false };
    std::atomic<float> progress01 { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphexAnalyzeQueue)
};
