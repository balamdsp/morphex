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

#include "MorphexSynth.h"

#include "../Helpers/SMTConstants.h"

MorphexSynth::MorphexSynth (AudioProcessorValueTreeState* parameters)
:   mParameters (parameters)
{
    this->initializeDSP();
    
    // Initialize the instrument
    this->instrument = Instrument();
    
    this->instrument.mode = Instrument::Mode::Morphing;
    this->instrument.interpolation_mode = Instrument::Interpolation::Manual;
//    this->instrument.mode = Instrument::Mode::FullRange;
//    this->instrument.interpolation_mode = Instrument::Interpolation::None;
    
    this->instrument.generate.harmonic = true;
    this->instrument.generate.sinusoidal = false;
    this->instrument.generate.stochastic = false;
    this->instrument.generate.attack = false;
    this->instrument.generate.residual = false;
    
    // For testing teh full range instrument case
    bool load_default_sounds = false;
    
    if (load_default_sounds)
    {
        std::string full_path = getDefaultCollectionsDirectory().toStdString() + directorySeparator.toStdString() + "Factory";
        
        if (this->instrument.mode == Instrument::Mode::FullRange)
        {
            //    std::string instrument_folder = "Suitcase Dry 20200615";
            std::string instrument_folder = "Suitcase Dry Test";
            //    std::string instrument_folder = "Suitcase Dry Full";
            //    std::string instrument_folder = "Morphing Test Optimized";
            full_path = getDefaultPluginDataDirectory().toStdString() + directorySeparator.toStdString() + "Instruments" + directorySeparator.toStdString() + instrument_folder;
        }
        
        DirectoryIterator iter (File (full_path), true, "*.had");
        //    DirectoryIterator iter (File (full_path), true, "*.had");
        //    static const String PLUGIN_DATA_DIRECTORY = (File::getSpecialLocation(File::userDesktopDirectory)).getFullPathName() + directorySeparator + PLUGIN_NAME;
        
        while (iter.next())
        {
            File sound_file (iter.getFile());
            std::string sound_file_path = sound_file.getFullPathName().toStdString();
            
            MorphLocation morph_location = MorphLocation::NUM_MORPH_LOCATIONS;
            
            if (this->instrument.mode == Instrument::Mode::Morphing)
            {
                if (this->instrument.num_samples_loaded < MorphLocation::NUM_MORPH_LOCATIONS)
                {
                    morph_location = (MorphLocation) this->instrument.num_samples_loaded;
                    this->instrument.loadSound (sound_file_path, morph_location);
                    this->instrument.num_samples_loaded++;
                }
            }
            else
            {
                this->instrument.loadSound (sound_file_path, morph_location);
                this->instrument.num_samples_loaded++;
            }
        }
        
        DBG("Sound files loaded: " + String (this->instrument.num_samples_loaded));
    }
    
    // Add some voices to our synth, to play the sounds..
    for (int i = 0; i < MAX_VOICES; i++)
    {
        this->addVoice (new Voice (&this->instrument, parameters));
    }
    
    // TODO: release has no way back; stop jumps all sounds to release.
    
    // TODO: morph in MorphSound for output decorrelation/spread.
    
    // Add a sound for them to play
    this->clearSounds();
    this->addSound (new MorphSound());
    this->setNoteStealingEnabled (true);
}

MorphexSynth::~MorphexSynth() {}

void MorphexSynth::setMaxVoices (int n)
{
    maxVoices.store (juce::jlimit (1, MAX_VOICES, n));
    if (getMaxVoices() != 1)
        monoNoteStack.clear();
}

void MorphexSynth::setLegatoEnabled (bool b)
{
    legatoEnabled.store (b);
    if (! b)
        monoNoteStack.clear();
}

static Voice* activeMorphexVoice (MorphexSynth* synth)
{
    for (int i = 0; i < synth->getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<Voice*> (synth->getVoice (i)))
        {
            if (v->isVoiceActive())
                return v;
        }
    }
    return nullptr;
}

void MorphexSynth::noteOn (int midiChannel, int midiNoteNumber, float velocity)
{
    // Mono legato: overlapping notes glide on the playing voice instead of
    // retriggering (ADSR and cursors keep running).
    if (getMaxVoices() == 1 && isLegatoEnabled())
    {
        if (Voice* playing = activeMorphexVoice (this))
        {
            monoNoteStack.push_back ({ midiNoteNumber, velocity });
            playing->legatoTo ((float) midiNoteNumber, velocity);
            return;
        }
        monoNoteStack.push_back ({ midiNoteNumber, velocity });
    }

    Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
}

void MorphexSynth::noteOff (int midiChannel, int midiNoteNumber, float velocity,
                             bool allowTailOff)
{
    if (getMaxVoices() == 1 && isLegatoEnabled())
    {
        monoNoteStack.erase (std::remove_if (monoNoteStack.begin(), monoNoteStack.end(),
                                             [&] (const std::pair<int, float>& e)
                                             { return e.first == midiNoteNumber; }),
                             monoNoteStack.end());
        if (! monoNoteStack.empty())
        {
            // Fall back down to the most recent still-held note.
            if (Voice* playing = activeMorphexVoice (this))
                playing->legatoTo ((float) monoNoteStack.back().first,
                                   monoNoteStack.back().second);
            return;
        }
    }

    Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
}

void MorphexSynth::allNotesOff (int midiChannel, bool allowTailOff)
{
    monoNoteStack.clear();
    Synthesiser::allNotesOff (midiChannel, allowTailOff);
}

SynthesiserVoice* MorphexSynth::findFreeVoice (SynthesiserSound* soundToPlay,
                                               int midiChannel, int midiNoteNumber,
                                               bool stealIfNoneAvailable) const
{
    // Base JUCE logic restricted to the first maxVoices voices. (Called under
    // the synth lock via noteOn, like the base implementation.)
    const int n = juce::jlimit (1, getNumVoices(), maxVoices.load());

    for (int i = 0; i < n; ++i)
    {
        if (auto* voice = getVoice (i))
        {
            if ((! voice->isVoiceActive()) && voice->canPlaySound (soundToPlay))
                return voice;
        }
    }

    if (stealIfNoneAvailable)
    {
        SynthesiserVoice* oldest = nullptr;
        for (int i = 0; i < n; ++i)
        {
            if (auto* voice = getVoice (i))
            {
                if (voice->canPlaySound (soundToPlay)
                    && (oldest == nullptr || voice->wasStartedBefore (*oldest)))
                    oldest = voice;
            }
        }
        return oldest;
    }

    juce::ignoreUnused (midiChannel, midiNoteNumber);
    return nullptr;
}

void MorphexSynth::setCurrentPlaybackSampleRate (double sampleRate)
{
    currentSampleRate = sampleRate;
    
    Voice* morph_voice;
    
    // Set new sample rate to ADSR for each voice instance
    for (int i=0; i < this->getNumVoices();i++)
    {
        if ((morph_voice = dynamic_cast<Voice*> (this->getVoice(i))))
        {
            morph_voice->setADSRSampleRate (sampleRate);
        }
    }
    
    // Call base class method
    Synthesiser::setCurrentPlaybackSampleRate (sampleRate);
}

void MorphexSynth::renderNextBlock (AudioBuffer<float>& outputAudio,
                                    const MidiBuffer& inputMidi,
                                    int startSample, int numSamples)
{
    // Call base class method
    Synthesiser::renderNextBlock (outputAudio, inputMidi, startSample, numSamples);
    
    // Output gain (dB) - use safe parameter access to prevent crashes
    const float output_gain_db = Morphex::getParameterValueSafe<float>(mParameters, 
        Morphex::PARAMETERS<float>[Morphex::Parameters::OutputGain].ID, 0.0f);
    const float output_gain = juce::Decibels::decibelsToGain (output_gain_db);
    
    for (int channel = 0; channel < outputAudio.getNumChannels(); ++channel)
    {
        auto* buffer = outputAudio.getWritePointer (channel);
        mOutputGain[channel]->process (buffer, output_gain, outputAudio.getNumSamples());
    }
}

void MorphexSynth::reset()
{
    // Reset the instrument
    this->instrument.reset();
}

void MorphexSynth::initializeDSP()
{
    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        mOutputGain[i] = std::make_unique <DSP::Gain>();
    }
}
