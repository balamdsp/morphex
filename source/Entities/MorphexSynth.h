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

#include "JuceHeader.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "../Core/Instrument.h"
#include "../Core/Sound.h"
#include "../Core/Voice.h"

#include "../DSP/Gain.h"

using namespace Core;

class MorphexSynth
:   public Synthesiser
{
public:
    
    MorphexSynth (AudioProcessorValueTreeState* parameters);
    ~MorphexSynth();
    
    void setCurrentPlaybackSampleRate (double sampleRate) override;
    void renderNextBlock (AudioBuffer<float>& outputAudio,
                          const MidiBuffer& inputMidi,
                          int startSample, int numSamples);

    // Voice cap + mono legato (no retrigger; stack falls back on note-off).
    void setMaxVoices (int n);
    int getMaxVoices() const noexcept { return maxVoices.load(); }
    void setLegatoEnabled (bool b);
    bool isLegatoEnabled() const noexcept { return legatoEnabled.load(); }

    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    void allNotesOff (int midiChannel, bool allowTailOff) override;

    SynthesiserVoice* findFreeVoice (SynthesiserSound* soundToPlay,
                                     int midiChannel, int midiNoteNumber,
                                     bool stealIfNoneAvailable) const override;

    void reset();
    void initializeDSP();

    Instrument instrument;
    
protected:
    
    OwnedArray<Voice> voices;
    
private:

    AudioProcessorValueTreeState* mParameters;

    std::unique_ptr<DSP::Gain> mOutputGain[NUM_CHANNELS];

    double currentSampleRate;

    std::atomic<int> maxVoices { MAX_VOICES };
    std::atomic<bool> legatoEnabled { true };

    // Mono legato note stack, newest last (synth lock guards audio use).
    std::vector<std::pair<int, float>> monoNoteStack;
};

