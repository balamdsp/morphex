/* Copyright (C) 2020 Marc Sanchez Martinez
 *
 * https://github.com/MarcSM/morphex
 *
 * Morphex is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Morphex is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Morphex. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "Entities/MorphexSynth.h"
#include "Managers/PresetManager.h"

#include "Analysis/MorphexAnalyzeQueue.h"
#include "Helpers/SMTParameters.h"

//==============================================================================
/**
 */
class SpectralMorphingToolAudioProcessor
:   public AudioProcessor,
    private juce::Value::Listener
{
public:
    //==============================================================================
    SpectralMorphingToolAudioProcessor();
    ~SpectralMorphingToolAudioProcessor();

    // --- Sound load listener (async background loads) ---
    struct SoundLoadListener
    {
        virtual ~SoundLoadListener() = default;
        virtual void soundLoadFinished (MorphLocation location, bool success,
                                         const std::string& errorMessage) = 0;
    };
    void addSoundLoadListener (SoundLoadListener* l) { soundLoadListeners.add (l); }
    void removeSoundLoadListener (SoundLoadListener* l) { soundLoadListeners.remove (l); }
    // Fan-out for slot-cleared refresh: same channel as load completion
    // (receivers filter by location and refresh; no failure UI implied).
    void broadcastSlotRefresh (MorphLocation location)
    {
        soundLoadListeners.call ([&] (SoundLoadListener& l)
        {
            l.soundLoadFinished (location, true, {});
        });
    }

    void loadSoundAsync (const std::string& filePath, MorphLocation morphLocation);
    
    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif
    
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;
    
    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    
    //==============================================================================
    const String getName() const override;
    
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    
    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;
    
    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    //==============================================================================
    AudioProcessorValueTreeState parameters;
    
    PresetManager* getPresetManager()
    {
        return mPresetManager.get();
    }
    
    MidiKeyboardState& getMidiState()
    {
        return midiState;
    }
    
    MorphexSynth mMorphexSynth { &parameters };

    // CRT render toggle (session-only -- not persisted with plugin state).
    void setCrtEnabled (bool b)
    {
        crtEnabled.store (b);
        if (mPresetManager != nullptr)
            mPresetManager->setCrtEnabled (b);
    }
    bool isCrtEnabled() const noexcept { return crtEnabled.load(); }
    const std::atomic<bool>& getCrtEnabledFlag() const noexcept { return crtEnabled; }

    void setCrtStrength (int s)
    {
        crtStrength.store (juce::jlimit (0, 2, s));
        if (mPresetManager != nullptr)
            mPresetManager->setCrtStrength (s);
    }
    int getCrtStrength() const noexcept { return crtStrength.load(); }
    const std::atomic<int>& getCrtStrengthFlag() const noexcept { return crtStrength; }

    // PERFORMANCE/ANALYZE mode (session-only Value, persisted via guiState).
    juce::Value uiModeValue;
    juce::String getUiMode() const { return uiModeValue.getValue().toString(); }
    void setUiMode (const juce::String& m) { uiModeValue = m.isEmpty() ? juce::String ("performance") : m; }
    // Center section tab (0 CORE, 1 SOUNDS, 2 SETTINGS). A Value so Init can
    // drive the tab from anywhere, like uiModeValue.
    juce::Value centerSectionValue;
    int getCenterSection() const { return juce::jlimit (0, 2, (int) centerSectionValue.getValue()); }
    void setCenterSection (int s) { centerSectionValue.setValue (juce::jlimit (0, 2, s)); }

    juce::Value tooltipDelayMsValue;
    juce::Value tooltipsEnabledValue;
    juce::Value confirmDestructiveValue;
    int getTooltipDelayMs() const { return (int) tooltipDelayMsValue.getValue(); }
    bool getTooltipsEnabled() const { return static_cast<bool> (tooltipsEnabledValue.getValue()); }
    bool getConfirmDestructive() const { return static_cast<bool> (confirmDestructiveValue.getValue()); }
    void resetAnalyzerDefaults();

    // Analyzer session stubs (Phase 2: full HPS backend; now buffer/interval only).
    bool hasAnalyzerSource() const;
    juce::String getAnalyzerSourceName() const;
    double getAnalyzerSourceDurationSec() const;
    double getAnalyzerSampleRate() const;
    int getAnalyzerSourceGeneration() const noexcept;
    std::shared_ptr<const std::vector<float>> getAnalyzerSourceBuffer() const;
    void analyzerSetInterval (double startSec, double endSec);
    double getAnalyzerIntervalStart() const;
    double getAnalyzerIntervalEnd() const;
    juce::String openAnalyzerSource (const juce::File& audioFile);
    void clearAnalyzerSession();
    void analyzeScratch();
    void analyzeScratch (const morphex::analyzer::Settings& settings);
    void synthesizeScratch();

    // In-plugin analysis for one slot: WAV/AIFF -> .had -> async slot load.
    // Completion (or failure) arrives via the existing SoundLoadListener.
    void analyzeAndLoadSlot (int slotIndex, const juce::File& audioFile);

    bool hasAnalyzerPartials() const;
    morphex::analyzer::PartialFrameData getAnalyzerPartials() const;
    std::shared_ptr<const std::vector<float>> getAnalyzerResynthBuffer() const;
    bool hasAnalyzerResynth() const;
    // Stochastic envelope backdrop for the partials view (frames x bins,
    // dB; null until analyzed). Shared snapshot, lock-free for the UI.
    using StocMatrix = std::vector<std::vector<double>>;
    std::shared_ptr<const StocMatrix> getAnalyzerStocEnv() const;
    // Analyzer job state for the info line (kind: -1 idle, 0 analyze, 1 synth).
    int getAnalyzerJobKind() const noexcept;
    float getAnalyzerJobProgress() const;
    juce::String getAnalyzerJobStage() const;
    // Drop queued jobs; the in-flight job runs out and its delivery is
    // guarded (scratch drops silently, slots report "Cancelled").
    void cancelAnalyzerJobs();
    // Per-slot analysis progress for the SoundPanel LOADING line
    // (0..1 while that slot's job runs, -1 when idle).
    float getAnalyzeProgressForSlot (int slotIndex) const;
    // Batch: analyze files, writing .had next to each source.
    void analyzeBatchFiles (const std::vector<juce::File>& files,
                            const morphex::analyzer::Settings& settings);
    bool isBatchActive() const noexcept;
    int getBatchDone() const noexcept;
    int getBatchTotal() const noexcept;
    juce::String getBatchCurrentName() const;
    juce::String getBatchFinalMessage() const;
    // Export scratch analysis: writes <stem>.had + <stem>_resynth.wav into dir.
    bool exportScratchToFolder (const juce::File& dir, juce::String& errOut);

    // Voice session Values: UI writes, engine follows,
    // settings.xml persists.
    juce::Value voicesValue;
    juce::Value legatoValue;
    juce::Value pitchLockValue;

    int getCurrentVoices() const { return juce::jlimit (1, MAX_VOICES, (int) voicesValue.getValue()); }
    bool getLegatoEnabled() const { return static_cast<bool> (legatoValue.getValue()); }
    bool getPitchLockEnabled() const { return static_cast<bool> (pitchLockValue.getValue()); }
    void setLegatoEnabled (bool b) { legatoValue.setValue (b); }
    void setPitchLockEnabled (bool b) { pitchLockValue.setValue (b); }

    void valueChanged (juce::Value& value) override;

    // Preview player: audio-private cursor + display mirror; source
    // normalized at OPEN; -40 dB vol floor.
    double getPreviewCursorSec() const noexcept { return previewCursorSec.load(); }
    double getPreviewCursorMirrorSec() const noexcept { return previewCursorMirrorSec.load(); }
    static constexpr float previewVolFloorDb = -40.0f;

    // One-shot preview transport (0 stopped, 1 source, 2 resynth).
    void playPreviewSource();
    void playPreviewResynth();
    void stopPreview();
    bool isPreviewPlaying() const noexcept { return previewPlayState.load() != 0; }
    bool isPreviewingResynth() const noexcept { return previewPlayState.load() == 2; }
    void setPreviewVol (bool resynth, float linearGain);
    void setPreviewVolDb (bool resynth, float db);
    // Transport aliases used by the analyzer window.
    bool previewPlaying() const noexcept { return isPreviewPlaying(); }
    void previewStop() { stopPreview(); }

    // Slot playhead positions 0..1 for the loader rail (-1 = no data yet).
    // Published lock-free by voices (last writer wins), polled by UI timers.
    float getSlotPhaseDisplay (int slot) const noexcept
    {
        if (slot < 0 || slot >= 4)
            return -1.0f;
        return slotPhaseDisplay[slot].load();
    }
    
private:
    
    AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
//        std::vector<std::unique_ptr<AudioParameterFloat>> params;
        std::vector<std::unique_ptr<RangedAudioParameter>> params;

        for(int i = 0; i < Morphex::Parameters::TotalNumParameters; i++) {

            Morphex::Parameter<float> smt_parameter = Morphex::PARAMETERS<float>[i];

            params.push_back
                (std::make_unique<AudioParameterFloat>
                    (juce::ParameterID { smt_parameter.ID, 1 }, smt_parameter.label,
                     NormalisableRange<float> (smt_parameter.min_value, smt_parameter.max_value),
                     smt_parameter.default_value,
                     juce::AudioParameterFloatAttributes().withLabel (String()))
                 );
        }

        juce::StringArray scaleChoices;
        for (int i = 0; i < Morphex::Zoom::ZOOM_COUNT; ++i)
            scaleChoices.add (juce::String (Morphex::Zoom::ZOOM_PERCENTS[i], 0) + "%");

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { Morphex::Zoom::UI_SCALE_ID, 1 },
            "UI Scale", scaleChoices, Morphex::Zoom::UI_SCALE_DEFAULT));

        return { params.begin(), params.end() };
    }

private:
    
    MidiKeyboardState midiState;
    MidiMessageCollector midiCollector;
    
    std::unique_ptr<PresetManager> mPresetManager;

    // CRT toggle flag (session-only, never serialized).
    std::atomic<bool> crtEnabled { true };
    std::atomic<int> crtStrength { 2 };

    // Async sound loading
    juce::ListenerList<SoundLoadListener> soundLoadListeners;
    std::mutex soundLoadMutex;
    std::shared_ptr<std::atomic<bool>> loadAlive { std::make_shared<std::atomic<bool>> (true) };

    // Preview-player audio state (private: only the audio thread writes the
    // cursor; the UI reads the mirror via the public getters above).
    std::atomic<double> previewCursorSec { 0.0 };
    std::atomic<double> previewCursorMirrorSec { 0.0 };
    std::atomic<int> previewPlayState { 0 };
    std::atomic<float> srcPreviewVol { 1.0f };
    std::atomic<float> rsnPreviewVol { 1.0f };
    std::shared_ptr<const std::vector<float>> previewPlayBuffer;
    double deviceSampleRate = 44100.0;
    void playPreviewBuffer (bool resynth);
    void finishBatchIfDone();

    std::atomic<float> slotPhaseDisplay[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

    // Analyzer scratch session (decoded source, interval, published results).
    mutable std::mutex analyzerMutex;
    std::vector<float> analyzerSourceSamples;
    double analyzerSampleRate = 0.0;
    juce::String analyzerSourceName;
    juce::File analyzerSourceFile;
    double analyzerIntervalStart = 0.0;
    double analyzerIntervalEnd = 0.0;
    std::atomic<int> analyzerGeneration { 0 };
    std::shared_ptr<const std::vector<float>> analyzerSourceBuf;
    morphex::analyzer::PartialFrameData analyzerPartials;
    morphex::analyzer::Result::HpsMatrices analyzerLastHps;
    std::shared_ptr<const std::vector<float>> analyzerResynthBuf;
    std::shared_ptr<const StocMatrix> analyzerStocEnvBuf;
    MorphexAnalyzeQueue analyzeQueue;
    std::atomic<int> analyzerJobKind { -1 };
    // Batch state (message thread owns the file list; worker reports via
    // the queue write-done callback).
    std::atomic<bool> batchActive { false };
    std::atomic<int> batchDone { 0 };
    std::atomic<int> batchTotal { 0 };
    std::vector<juce::File> batchFiles;
    juce::String batchFirstError;
    juce::String batchFinalMessage;
    std::atomic<double> batchFinalMs { 0.0 };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralMorphingToolAudioProcessor)
};

