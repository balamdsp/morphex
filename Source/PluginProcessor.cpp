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

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Helpers/SMTConstants.h"
#include "Helpers/SMTUtils.h"


//==============================================================================
SpectralMorphingToolAudioProcessor::SpectralMorphingToolAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
: AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                  .withInput  ("Input",  AudioChannelSet::stereo(), true)
#endif
                  .withOutput ("Output", AudioChannelSet::stereo(), true)
#endif
                  ),
parameters(*this,                   /** reference to processor */
           nullptr,                 /** null pointer to undoManager (optional) */
           juce::Identifier ("SMT"), /** valueTree identifier */
           createParameterLayout()) /** initialize parameters */
#endif
{
    // Initialize the preset manager
    mPresetManager = std::make_unique<PresetManager> (this, &mMorphexSynth);

    // Slot playhead rail source for the loader column.
    mMorphexSynth.instrument.slotPhaseDisplay = slotPhaseDisplay;

    // Restore persisted session settings (CRT toggle/strength + voices/
    // legato/pitchlock survive restarts via settings.xml).
    crtEnabled.store (mPresetManager->getCrtEnabled());
    crtStrength.store (mPresetManager->getCrtStrength());

    voicesValue = mPresetManager->getVoicesCount();
    legatoValue = mPresetManager->getLegatoEnabled();
    pitchLockValue = mPresetManager->getPitchLockEnabled();
    voicesValue.addListener (this);
    legatoValue.addListener (this);
    pitchLockValue.addListener (this);
    tooltipDelayMsValue.addListener (this);
    tooltipsEnabledValue.addListener (this);
    confirmDestructiveValue.addListener (this);
    valueChanged (voicesValue);
    valueChanged (legatoValue);
    valueChanged (pitchLockValue);

    // Create an initial new preset
    mPresetManager->createNewPreset();

    uiModeValue = juce::String ("performance");
    centerSectionValue = 0;
    tooltipDelayMsValue = mPresetManager->getTooltipDelayMs();
    tooltipsEnabledValue = mPresetManager->getTooltipsEnabled();
    confirmDestructiveValue = mPresetManager->getConfirmDestructive();
    analyzerGeneration.store (0);
    analyzerIntervalStart = 0.0;
    analyzerIntervalEnd = 0.0;

    analyzeQueue.configure (
        [this] (int slot, const juce::String& errorMessage)
        {
            // Batch file failure: count it done so the batch can finish.
            if (slot == -2 && batchActive.load())
            {
                ++batchDone;
                if (batchFirstError.isEmpty())
                    batchFirstError = errorMessage;
                finishBatchIfDone();
            }
        },
        [this] (int slot, bool ok, const juce::String& errorMessage)
        {
            if (slot == -2 && batchActive.load())
            {
                ++batchDone;
                if (! ok && batchFirstError.isEmpty())
                    batchFirstError = errorMessage;
                finishBatchIfDone();
                return;
            }
            juce::ignoreUnused (slot, ok, errorMessage);
        },
        [this] (int slot, bool ok, MorphexAnalyzeQueue::ResultPtr result,
                const juce::String& errorMessage)
        {
            // Slot jobs feed the .had into the async slot loader; scratch
            // jobs publish partials + resynth for the views.
            static const MorphLocation slotMap[4] = { MorphLocation::LeftHigh,
                                                      MorphLocation::RightHigh,
                                                      MorphLocation::LeftLow,
                                                      MorphLocation::RightLow };
            if (slot >= 0 && slot < 4)
            {
                // Stale guard: cancelAnalyzerJobs resets kind to -1, so a
                // late delivery reports Cancelled instead of loading.
                if (analyzerJobKind.load() < 0)
                {
                    soundLoadListeners.call ([&] (SoundLoadListener& l)
                    {
                        l.soundLoadFinished (slotMap[slot], false, "Cancelled");
                    });
                    return;
                }
                analyzerJobKind.store (-1);
                if (ok && result != nullptr && ! result->sdifPath.empty())
                {
                    loadSoundAsync (result->sdifPath, slotMap[slot]);
                }
                else
                {
                    const juce::String err = errorMessage.isEmpty() ? "Analysis failed"
                                                                    : errorMessage;
                    soundLoadListeners.call ([&] (SoundLoadListener& l)
                    {
                        l.soundLoadFinished (slotMap[slot], false, err.toStdString());
                    });
                }
                return;
            }

            juce::ignoreUnused (slot, errorMessage);
            // Stale guard: OPEN/CLEAR while a job was in flight resets kind
            // to -1, so late deliveries drop instead of resurrecting data.
            if (analyzerJobKind.load() < 0)
                return;
            if (ok && result != nullptr)
            {
                const std::lock_guard<std::mutex> lock (analyzerMutex);
                analyzerPartials = result->partials;
                analyzerResynthBuf =
                    std::make_shared<const std::vector<float>> (result->renderedSamples);
                if (! result->hpsMatrices.hfreq.empty())
                {
                    analyzerLastHps = result->hpsMatrices;
                    analyzerStocEnvBuf = std::make_shared<const StocMatrix> (
                        result->hpsMatrices.stocEnv);
                }
            }
            analyzerJobKind.store (-1);
            analyzerGeneration.fetch_add (1);
        });
}

SpectralMorphingToolAudioProcessor::~SpectralMorphingToolAudioProcessor()
{
    voicesValue.removeListener (this);
    legatoValue.removeListener (this);
    pitchLockValue.removeListener (this);
    tooltipDelayMsValue.removeListener (this);
    tooltipsEnabledValue.removeListener (this);
    confirmDestructiveValue.removeListener (this);
    loadAlive->store (false);
}

void SpectralMorphingToolAudioProcessor::valueChanged (juce::Value& value)
{
    if (value.refersToSameSourceAs (voicesValue))
    {
        const int n = getCurrentVoices();
        mMorphexSynth.setMaxVoices (n);
        if (mPresetManager != nullptr)
            mPresetManager->setVoicesCount (n);
    }
    else if (value.refersToSameSourceAs (legatoValue))
    {
        const bool b = getLegatoEnabled();
        mMorphexSynth.setLegatoEnabled (b);
        if (mPresetManager != nullptr)
            mPresetManager->setLegatoEnabled (b);
    }
    else if (value.refersToSameSourceAs (pitchLockValue))
    {
        const bool b = getPitchLockEnabled();
        mMorphexSynth.instrument.pitchLocked = b;
        if (mPresetManager != nullptr)
            mPresetManager->setPitchLockEnabled (b);
    }
    else if (value.refersToSameSourceAs (tooltipDelayMsValue))
    {
        if (mPresetManager != nullptr)
            mPresetManager->setTooltipDelayMs (getTooltipDelayMs());
    }
    else if (value.refersToSameSourceAs (tooltipsEnabledValue))
    {
        if (mPresetManager != nullptr)
            mPresetManager->setTooltipsEnabled (getTooltipsEnabled());
    }
    else if (value.refersToSameSourceAs (confirmDestructiveValue))
    {
        if (mPresetManager != nullptr)
            mPresetManager->setConfirmDestructive (getConfirmDestructive());
    }
}

//==============================================================================
const String SpectralMorphingToolAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpectralMorphingToolAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool SpectralMorphingToolAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool SpectralMorphingToolAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double SpectralMorphingToolAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SpectralMorphingToolAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int SpectralMorphingToolAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SpectralMorphingToolAudioProcessor::setCurrentProgram (int index)
{
}

const String SpectralMorphingToolAudioProcessor::getProgramName (int index)
{
    return {};
}

void SpectralMorphingToolAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void SpectralMorphingToolAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    ignoreUnused (samplesPerBlock);
//    lastSampleRate = sampleRate
    
    // Pre-playback initializations
    midiCollector.reset (sampleRate);
    mMorphexSynth.setCurrentPlaybackSampleRate (sampleRate);
    deviceSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    stopPreview();
}

void SpectralMorphingToolAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SpectralMorphingToolAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;
    
    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    
    return true;
#endif
}
#endif

void SpectralMorphingToolAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    buffer.clear();
    
    // Get next midi events
    midiState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
    
    // TODO - CHECK (buffer, midiMessages, -> 0 <- , buffer.getNumSamples());
    
    // Get the synth to process the midi events and generate its output
    mMorphexSynth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

    // One-shot analyzer preview, mixed post-synth.
    const int previewKind = previewPlayState.load();
    if (previewKind != 0)
    {
        auto playBuf = previewPlayBuffer;
        const float vol = (previewKind == 1 ? srcPreviewVol : rsnPreviewVol).load();
        const double sr = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;

        if (playBuf != nullptr && ! playBuf->empty() && vol >= 0.01f)
        {
            double cursor = previewCursorSec.load();
            const int n = buffer.getNumSamples();
            const int chans = buffer.getNumChannels();

            for (int i = 0; i < n; ++i)
            {
                const std::size_t idx = (std::size_t) (cursor * sr);
                if (idx >= playBuf->size())
                {
                    cursor = (double) playBuf->size() / sr;
                    previewPlayState.store (0);
                    break;
                }
                const float s = (*playBuf)[idx] * vol;
                for (int c = 0; c < chans; ++c)
                    buffer.addSample (c, i, s);
                cursor += 1.0 / sr;
            }

            previewCursorSec.store (cursor);
            previewCursorMirrorSec.store (cursor);
        }
        else
        {
            previewPlayState.store (0);
        }
    }
}

//==============================================================================
bool SpectralMorphingToolAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* SpectralMorphingToolAudioProcessor::createEditor()
{
    return new SpectralMorphingToolAudioProcessorEditor (*this);
}

//==============================================================================
void SpectralMorphingToolAudioProcessor::loadSoundAsync (const std::string& filePath, MorphLocation morphLocation)
{
    auto alive = loadAlive;
    juce::Thread::launch ([alive, this, filePath, morphLocation]
    {
        if (! alive->load())
            return;

        std::shared_ptr<Sound> decoded;
        std::string errorMessage;
        bool ok = false;
        try
        {
            if (! alive->load())
                return;

            decoded = Core::Instrument::decodeSound (filePath);
            ok = decoded != nullptr && decoded->loaded;
            if (! ok)
                errorMessage = "Could not parse sound file";
        }
        catch (const std::exception& e)
        {
            errorMessage = e.what();
        }
        catch (...)
        {
            errorMessage = "Unknown load error";
        }

        juce::MessageManager::callAsync ([alive, this, morphLocation, decoded, ok, errorMessage]
        {
            if (! alive->load())
                return;

            // Install on the message thread: atomic shared_ptr swap, so live
            // voices snapshot safely and held notes keep the old sound.
            if (ok && decoded != nullptr)
                mMorphexSynth.instrument.installSound (decoded, morphLocation);

            soundLoadListeners.call ([&] (SoundLoadListener& l)
            {
                l.soundLoadFinished (morphLocation, ok, errorMessage);
            });
        });
    });
}

//==============================================================================
void SpectralMorphingToolAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    ValueTree presetTree = mPresetManager->getStateTree();
    
    MemoryOutputStream stream;
    presetTree.writeToStream (stream);
    
    destData.setSize (stream.getDataSize());
    destData.copyFrom (stream.getData(), 0, stream.getDataSize());
}

void SpectralMorphingToolAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    MemoryInputStream stream (data, sizeInBytes, false);
    ValueTree presetTree = ValueTree::readFromStream (stream);
    
    if (presetTree.isValid())
    {
        mPresetManager->loadStateFromTree (presetTree);
    }
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralMorphingToolAudioProcessor();
}

//==============================================================================
// Analyzer session: buffers, interval, preview transport, queue jobs.

void SpectralMorphingToolAudioProcessor::resetAnalyzerDefaults()
{
    tooltipDelayMsValue = 500;
    tooltipsEnabledValue = true;
    if (getUiMode().isEmpty())
        setUiMode ("performance");
}

bool SpectralMorphingToolAudioProcessor::hasAnalyzerSource() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerSampleRate > 0.0 && ! analyzerSourceSamples.empty();
}

juce::String SpectralMorphingToolAudioProcessor::getAnalyzerSourceName() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerSourceName;
}

double SpectralMorphingToolAudioProcessor::getAnalyzerSourceDurationSec() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    if (analyzerSampleRate <= 0.0 || analyzerSourceSamples.empty())
        return 0.0;
    return (double) analyzerSourceSamples.size() / analyzerSampleRate;
}

double SpectralMorphingToolAudioProcessor::getAnalyzerSampleRate() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerSampleRate;
}

int SpectralMorphingToolAudioProcessor::getAnalyzerSourceGeneration() const noexcept
{
    return analyzerGeneration.load();
}

std::shared_ptr<const std::vector<float>> SpectralMorphingToolAudioProcessor::getAnalyzerSourceBuffer() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerSourceBuf;
}

void SpectralMorphingToolAudioProcessor::analyzerSetInterval (double startSec, double endSec)
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    analyzerIntervalStart = juce::jmax (0.0, startSec);
    analyzerIntervalEnd = juce::jmax (analyzerIntervalStart, endSec);
}

double SpectralMorphingToolAudioProcessor::getAnalyzerIntervalStart() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerIntervalStart;
}

double SpectralMorphingToolAudioProcessor::getAnalyzerIntervalEnd() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerIntervalEnd;
}

juce::String SpectralMorphingToolAudioProcessor::openAnalyzerSource (const juce::File& audioFile)
{
    stopPreview();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (audioFile));
    if (reader == nullptr)
        return "Unsupported audio file";

    if (reader->lengthInSamples > (juce::int64) reader->sampleRate * 60)
        return "File longer than 60 s cap";

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);

    std::vector<float> mono ((size_t) buf.getNumSamples(), 0.0f);
    for (int n = 0; n < buf.getNumSamples(); ++n)
    {
        double s = 0.0;
        for (int c = 0; c < buf.getNumChannels(); ++c)
            s += buf.getSample (c, n);
        mono[(size_t) n] = (float) (s / juce::jmax (1, buf.getNumChannels()));
    }

    float peak = 0.0f;
    for (auto v : mono)
        peak = juce::jmax (peak, std::abs (v));
    if (peak > 0.0f)
    {
        const float g = 0.99f / peak;
        for (auto& v : mono)
            v *= g;
    }

    {
        const std::lock_guard<std::mutex> lock (analyzerMutex);
        analyzerSourceSamples = std::move (mono);
        analyzerSampleRate = reader->sampleRate;
        analyzerSourceName = audioFile.getFileName();
        analyzerSourceFile = audioFile;
        analyzerSourceBuf = std::make_shared<const std::vector<float>> (analyzerSourceSamples);
        const double dur = (double) analyzerSourceSamples.size() / analyzerSampleRate;
        analyzerIntervalStart = 0.0;
        analyzerIntervalEnd = dur;
        // New source invalidates previous analysis (and guards any
        // in-flight job: its delivery sees kind < 0 and drops).
        analyzerPartials = morphex::analyzer::PartialFrameData();
        analyzerLastHps = morphex::analyzer::Result::HpsMatrices();
        analyzerResynthBuf.reset();
        analyzerStocEnvBuf.reset();
    }
    analyzerJobKind.store (-1);
    analyzerGeneration.fetch_add (1);
    previewCursorSec.store (0.0);
    previewCursorMirrorSec.store (0.0);
    return {};
}

void SpectralMorphingToolAudioProcessor::clearAnalyzerSession()
{
    {
        const std::lock_guard<std::mutex> lock (analyzerMutex);
        analyzerSourceSamples.clear();
        analyzerSourceBuf.reset();
        analyzerSampleRate = 0.0;
        analyzerSourceName.clear();
        analyzerSourceFile = juce::File();
        analyzerIntervalStart = 0.0;
        analyzerIntervalEnd = 0.0;
        analyzerPartials = morphex::analyzer::PartialFrameData();
        analyzerLastHps = morphex::analyzer::Result::HpsMatrices();
        analyzerResynthBuf.reset();
        analyzerStocEnvBuf.reset();
    }
    analyzerJobKind.store (-1);
    analyzerGeneration.fetch_add (1);
    stopPreview();
    previewCursorSec.store (0.0);
    previewCursorMirrorSec.store (0.0);
}

void SpectralMorphingToolAudioProcessor::stopPreview()
{
    previewPlayState.store (0);
}

void SpectralMorphingToolAudioProcessor::setPreviewVol (bool resynth, float linearGain)
{
    if (resynth)
        rsnPreviewVol.store (juce::jlimit (0.0f, 1.0f, linearGain));
    else
        srcPreviewVol.store (juce::jlimit (0.0f, 1.0f, linearGain));
}

void SpectralMorphingToolAudioProcessor::setPreviewVolDb (bool resynth, float db)
{
    const float clamped = juce::jlimit (-40.0f, 6.0f, db);
    const float gain = std::pow (10.0f, clamped / 20.0f);
    setPreviewVol (resynth, juce::jlimit (0.0f, 2.0f, gain));
}

void SpectralMorphingToolAudioProcessor::playPreviewBuffer (bool resynth)
{
    stopPreview();

    std::shared_ptr<const std::vector<float>> src =
        resynth ? getAnalyzerResynthBuffer() : getAnalyzerSourceBuffer();
    if (src == nullptr || src->empty())
        return;

    const double sr = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;
    const double srcRate = getAnalyzerSampleRate();

    auto toDeviceRate = [&] (const std::vector<float>& in)
    {
        auto out = std::make_shared<std::vector<float>>();
        if (srcRate <= 0.0 || std::abs (srcRate - sr) < 1.0)
        {
            *out = in;
        }
        else
        {
            // Linear resample to device rate (message thread; audio only reads).
            const std::size_t outLen = (std::size_t) ((double) in.size() * sr / srcRate);
            out->resize (outLen);
            for (std::size_t i = 0; i < outLen; ++i)
            {
                const double pos = (double) i * srcRate / sr;
                const std::size_t idx = (std::size_t) pos;
                const double frac = pos - (double) idx;
                const float a = in[std::min (idx, in.size() - 1)];
                const float b = in[std::min (idx + 1, in.size() - 1)];
                (*out)[i] = (float) (a * (1.0 - frac) + b * frac);
            }
        }
        return out;
    };

    previewPlayBuffer = toDeviceRate (*src);

    previewCursorSec.store (0.0);
    previewCursorMirrorSec.store (0.0);
    previewPlayState.store (resynth ? 2 : 1);
}

void SpectralMorphingToolAudioProcessor::playPreviewSource()
{
    playPreviewBuffer (false);
}

void SpectralMorphingToolAudioProcessor::playPreviewResynth()
{
    playPreviewBuffer (true);
}

void SpectralMorphingToolAudioProcessor::analyzeScratch()
{
    morphex::analyzer::Settings defaults;
    analyzeScratch (defaults);
}

void SpectralMorphingToolAudioProcessor::analyzeScratch (
    const morphex::analyzer::Settings& settings)
{
    stopPreview();

    juce::File source;
    std::vector<float> regionSamples;
    double regionRate = 44100.0;
    bool useRegion = false;
    {
        const std::lock_guard<std::mutex> lock (analyzerMutex);
        source = analyzerSourceFile;
        const double dur = (analyzerSampleRate > 0.0 && ! analyzerSourceSamples.empty())
            ? (double) analyzerSourceSamples.size() / analyzerSampleRate : 0.0;
        double s0 = juce::jlimit (0.0, dur, analyzerIntervalStart);
        double s1 = juce::jlimit (0.0, dur, analyzerIntervalEnd);
        if (s1 < s0)
            std::swap (s0, s1);
        // Bracket interval is real: slice with short edge fades unless it
        // spans (nearly) the whole file.
        if (dur > 0.0 && (s0 > dur * 0.001 || s1 < dur * 0.999))
        {
            const std::size_t i0 = (std::size_t) (s0 * analyzerSampleRate);
            const std::size_t i1 = (std::size_t) juce::jmin (s1 * analyzerSampleRate,
                                                             (double) analyzerSourceSamples.size());
            if (i1 > i0)
            {
                regionSamples.assign (analyzerSourceSamples.begin() + (std::ptrdiff_t) i0,
                                      analyzerSourceSamples.begin() + (std::ptrdiff_t) i1);
                // 256-sample raised-cosine edge fades to avoid click frames.
                const std::size_t fade = std::min<std::size_t> (256, regionSamples.size() / 2);
                for (std::size_t i = 0; i < fade; ++i)
                {
                    const float w = 0.5f * (1.0f - std::cos ((float) juce::MathConstants<double>::pi
                                                             * (float) i / (float) fade));
                    regionSamples[i] *= w;
                    regionSamples[regionSamples.size() - 1 - i] *= w;
                }
                regionRate = analyzerSampleRate;
                useRegion = true;
            }
        }
    }
    if (! source.existsAsFile())
        return;

    juce::File outBase = juce::File (getDefaultAnalyzerDirectory())
                             .getChildFile (source.getFileNameWithoutExtension());
    MorphexAnalyzeQueue::Request req;
    req.slotIndex = -1;
    req.kind = MorphexAnalyzeQueue::Kind::Analyze;
    req.sourceAudio = source;
    req.outputBase = outBase;
    req.settings = settings;
    req.deliverResult = true;
    if (useRegion)
    {
        req.useMemorySource = true;
        req.memorySamples = std::move (regionSamples);
        req.memoryRate = regionRate;
    }
    analyzerJobKind.store (0);
    analyzeQueue.enqueue (req);
}

void SpectralMorphingToolAudioProcessor::synthesizeScratch()
{
    stopPreview();

    morphex::analyzer::PartialFrameData partials;
    double sr = 44100.0;
    {
        const std::lock_guard<std::mutex> lock (analyzerMutex);
        partials = analyzerPartials;
        sr = analyzerSampleRate > 0.0 ? analyzerSampleRate : 44100.0;
    }
    if (partials.numFrames <= 0)
        return;

    MorphexAnalyzeQueue::Request req;
    req.slotIndex = -1;
    req.kind = MorphexAnalyzeQueue::Kind::Synthesize;
    req.synthInput = partials;
    req.synthSampleRate = sr;
    req.deliverResult = true;
    analyzerJobKind.store (1);
    analyzeQueue.enqueue (req);
}

void SpectralMorphingToolAudioProcessor::analyzeAndLoadSlot (int slotIndex,
                                                             const juce::File& audioFile)
{
    if (slotIndex < 0 || slotIndex > 3 || ! audioFile.existsAsFile())
        return;

    morphex::analyzer::Settings defaults;
    juce::File outBase = juce::File (getDefaultAnalyzerDirectory())
                             .getChildFile (audioFile.getFileNameWithoutExtension());
    MorphexAnalyzeQueue::Request req;
    req.slotIndex = slotIndex;
    req.kind = MorphexAnalyzeQueue::Kind::Analyze;
    req.sourceAudio = audioFile;
    req.outputBase = outBase;
    req.settings = defaults;
    req.deliverResult = true;
    analyzerJobKind.store (0);
    analyzeQueue.enqueue (req);
}

bool SpectralMorphingToolAudioProcessor::hasAnalyzerPartials() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerPartials.numFrames > 0;
}

morphex::analyzer::PartialFrameData SpectralMorphingToolAudioProcessor::getAnalyzerPartials() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerPartials;
}

std::shared_ptr<const std::vector<float>> SpectralMorphingToolAudioProcessor::getAnalyzerResynthBuffer() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerResynthBuf;
}

std::shared_ptr<const SpectralMorphingToolAudioProcessor::StocMatrix>
SpectralMorphingToolAudioProcessor::getAnalyzerStocEnv() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerStocEnvBuf;
}

bool SpectralMorphingToolAudioProcessor::hasAnalyzerResynth() const
{
    const std::lock_guard<std::mutex> lock (analyzerMutex);
    return analyzerResynthBuf != nullptr && ! analyzerResynthBuf->empty();
}

int SpectralMorphingToolAudioProcessor::getAnalyzerJobKind() const noexcept
{
    return analyzerJobKind.load();
}

float SpectralMorphingToolAudioProcessor::getAnalyzerJobProgress() const
{
    return analyzeQueue.overallProgress();
}

juce::String SpectralMorphingToolAudioProcessor::getAnalyzerJobStage() const
{
    const float p = analyzeQueue.overallProgress();
    if (analyzerJobKind.load() < 0)
        return {};
    if (p <= 0.06f)
        return "queued";
    if (p < 0.2f)
        return "reading";
    if (p < 0.9f)
        return "analyzing";
    return "writing";
}

bool SpectralMorphingToolAudioProcessor::exportScratchToFolder (const juce::File& dir,
                                                                juce::String& errOut)
{
    morphex::analyzer::Result::HpsMatrices hps;
    std::shared_ptr<const std::vector<float>> resynth;
    juce::String stem;
    {
        const std::lock_guard<std::mutex> lock (analyzerMutex);
        hps = analyzerLastHps;
        resynth = analyzerResynthBuf;
        stem = analyzerSourceName.isEmpty() ? juce::String ("scratch")
                                            : juce::File (analyzerSourceName).getFileNameWithoutExtension();
    }
    if (hps.empty())
    {
        errOut = "Nothing analyzed yet -- press ANALYZE first";
        return false;
    }
    if (! dir.isDirectory() && ! dir.createDirectory())
    {
        errOut = "Cannot create export folder";
        return false;
    }

    morphex::dsp::hps::AnalysisParams p;
    p.windowType = hps.windowType;
    p.windowSize = (std::size_t) hps.windowSize;
    p.fftSize = (std::size_t) hps.fftSize;
    p.magnitudeThreshold = hps.magnitudeThreshold;
    p.minSineDur = hps.minSineDur;
    p.minF0 = hps.minF0;
    p.maxF0 = hps.maxF0;
    p.maxF0Error = hps.maxF0Error;
    p.harmDevSlope = hps.harmDevSlope;
    p.maxHarm = (std::size_t) hps.maxHarm;
    p.stocFact = hps.stocFact;

    const auto doc = morphex::had::buildHad (p, (std::uint32_t) hps.fs, hps.hfreq, hps.hmag,
                                             hps.sfreq, hps.smag, hps.stocEnv);
    juce::File hadFile = dir.getChildFile (stem + ".had");
    if (! hadFile.replaceWithText (juce::String (morphex::had::generateHadXml (doc))))
    {
        errOut = "Cannot write .had file";
        return false;
    }

    // Resynth WAV alongside (same stem, the write-next-to-source policy
    // adapted to a chosen folder).
    if (resynth != nullptr && ! resynth->empty())
    {
        juce::File wavFile = dir.getChildFile (stem + "_resynth.wav");
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (new juce::FileOutputStream (wavFile),
                                       hps.fs > 0.0 ? hps.fs : 44100.0, 1, 16, {}, 0));
        if (writer != nullptr)
        {
            juce::AudioBuffer<float> buf (1, (int) resynth->size());
            for (int i = 0; i < (int) resynth->size(); ++i)
                buf.setSample (0, i, (*resynth)[(std::size_t) i]);
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
    }
    return true;
}


void SpectralMorphingToolAudioProcessor::cancelAnalyzerJobs()
{
    analyzeQueue.cancelPending();
    batchActive.store (false);
    batchDone.store (0);
    batchTotal.store (0);
    analyzerJobKind.store (-1);
    analyzerGeneration.fetch_add (1);
}

float SpectralMorphingToolAudioProcessor::getAnalyzeProgressForSlot (int slotIndex) const
{
    if (analyzeQueue.busySlot() == slotIndex)
        return analyzeQueue.overallProgress();
    return -1.0f;
}

void SpectralMorphingToolAudioProcessor::analyzeBatchFiles (const std::vector<juce::File>& files,
                                                              const morphex::analyzer::Settings& settings)
{
    // Keep only supported audio that exists; .had files load into slots.
    std::vector<juce::File> audio;
    for (const auto& f : files)
    {
        if (! f.existsAsFile())
            continue;
        const juce::String ext = f.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff"
            || ext == ".mp3" || ext == ".flac" || ext == ".ogg")
            audio.push_back (f);
    }
    if (audio.empty())
        return;

    stopPreview();
    batchFiles = audio;
    batchDone.store (0);
    batchTotal.store ((int) audio.size());
    batchFirstError.clear();
    batchFinalMessage.clear();
    batchActive.store (true);

    morphex::analyzer::Settings jobSettings = settings;
    for (const auto& f : audio)
    {
        MorphexAnalyzeQueue::Request req;
        req.slotIndex = -2;
        req.kind = MorphexAnalyzeQueue::Kind::Analyze;
        req.sourceAudio = f;
        // Write-next-to-source policy.
        req.outputBase = f.getSiblingFile (f.getFileNameWithoutExtension());
        req.settings = jobSettings;
        req.deliverResult = false;
        analyzeQueue.enqueue (req);
    }
    analyzerJobKind.store (0);
}

bool SpectralMorphingToolAudioProcessor::isBatchActive() const noexcept
{
    return batchActive.load();
}

int SpectralMorphingToolAudioProcessor::getBatchDone() const noexcept
{
    return batchDone.load();
}

int SpectralMorphingToolAudioProcessor::getBatchTotal() const noexcept
{
    return batchTotal.load();
}

juce::String SpectralMorphingToolAudioProcessor::getBatchCurrentName() const
{
    const int done = batchDone.load();
    if (done < 0 || done >= (int) batchFiles.size())
        return {};
    return batchFiles[(std::size_t) done].getFileName();
}

juce::String SpectralMorphingToolAudioProcessor::getBatchFinalMessage() const
{
    // Sticky for ~6 s so the completion line survives past the last tick.
    const double ageMs = (double) juce::Time::getMillisecondCounter() - batchFinalMs.load();
    if (batchFinalMessage.isEmpty() || ageMs > 6000.0)
        return {};
    return batchFinalMessage;
}

void SpectralMorphingToolAudioProcessor::finishBatchIfDone()
{
    if (! batchActive.load())
        return;
    if (batchDone.load() < batchTotal.load())
        return;
    batchActive.store (false);
    analyzerJobKind.store (-1);
    const int total = batchTotal.load();
    batchFinalMessage = batchFirstError.isEmpty()
        ? "BATCH done: " + juce::String (total) + " file" + (total == 1 ? "" : "s")
        : "BATCH done with errors (" + batchFirstError + ")";
    batchFinalMs.store ((double) juce::Time::getMillisecondCounter());
    analyzerGeneration.fetch_add (1);
}
