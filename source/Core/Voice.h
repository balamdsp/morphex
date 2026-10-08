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

#include "Instrument.h"
#include "Synthesis.h"

#include "Codec.h"
#include "../Entities/MorphSound.h"
#include "../Helpers/SMTAudioHelpers.h"
#include "../Helpers/SMTHelperFunctions.h"
#include "../Helpers/SMTParameters.h"

using namespace Core;

// Transient emphasis decay (note-on s-boost envelope tau). Placeholder until
// a Hit Window control lands; 80 ms suits percussive and tonal alike.
constexpr double transientTauSec = 0.08;

struct Voice
:   public SynthesiserVoice
{
    Voice (Instrument* instrument, AudioProcessorValueTreeState* parameters)
    :   mParameters (parameters),
        instrument (instrument),
        synthesis (instrument)
    {
        this->reset();
    }
    
    void reset()
    {
        // Note playback
        this->playing_note = false;
        this->loop_mode = true;
        this->hold_note = false;
        this->track_velocity = false; // NOTE - High CPU usage if true
        this->allow_pitch_wheel = true;
        
        // Default values
        this->f_pressed_midi_note = 0;
        this->f_current_midi_note = 0;
        this->f_current_velocity = 0;
        this->f_last_midi_note = 0;
        
        // Sounds
        this->max_loop_start = 0;
        this->min_loop_end = 0;
        this->min_note_end = 0;

        // Independent cursors restart; transient emphasis re-arms
        for (int i = 0; i < 4; i++)
        {
            this->slotCursor[i] = 0.0f;
            this->slotDir[i] = 1;
            this->slotDone[i] = false;
        }
        this->transientBoost = 1.0;
        this->smoothPadX = 0.5;
        this->smoothPadY = 0.5;
    }
    
    void setCurrentPlaybackSampleRate (double newRate) override
    {
        SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        this->sampleRate = newRate;
    }
    
    bool canPlaySound (SynthesiserSound* synthSound) override
    {
        return dynamic_cast<MorphSound*> (synthSound) != nullptr;
    }
    
    void startNote (int midiNoteNumber, float velocity, SynthesiserSound*, int currentPitchWheelPosition) override
    {
        // Restart the envelope and load the current ADSR parameter values
        this->adsrEnvelope.reset();
        this->adsrEnvelope.noteOn();
        this->updateAdsrParams (
            Morphex::getParameterValueSafe<float>(mParameters, Morphex::PARAMETERS<float>[Morphex::Parameters::asdr_attack].ID, 0.1f),
            Morphex::getParameterValueSafe<float>(mParameters, Morphex::PARAMETERS<float>[Morphex::Parameters::asdr_decay].ID, 0.8f),
            Morphex::getParameterValueSafe<float>(mParameters, Morphex::PARAMETERS<float>[Morphex::Parameters::asdr_sustain].ID, 0.8f),
            Morphex::getParameterValueSafe<float>(mParameters, Morphex::PARAMETERS<float>[Morphex::Parameters::asdr_release].ID, 0.1f));
        
        this->playing_note = true;
        
        this->f_pressed_midi_note = (float) midiNoteNumber;
        this->f_current_midi_note = (float) midiNoteNumber;
        this->f_last_midi_note = (float) midiNoteNumber;
        this->f_current_velocity = velocity;
        
        this->updateMorphSounds (this->f_pressed_midi_note, this->f_current_velocity);

        this->initSlotCursors();
        this->transientBoost = 1.0;

        // Snap pad smoothing to the live pad so notes start exactly where
        // the puck sits (no sweep-in from a stale value).
        this->smoothPadX = (double) Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::freqs_interp_factor].ID, 0.5f);
        this->smoothPadY = (double) Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::mags_interp_factor].ID, 0.5f);
        
        this->updateMidiNoteWithPitchWheel (currentPitchWheelPosition, true);
        
        // Velocity affects the gain
        this->level = velocity * 0.15;
    }
    
    void stopNote (float velocity, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            this->adsrEnvelope.noteOff();
        }
        else
        {
            clearAndResetCurrentNote();
            this->playing_note = false;
        }
    }

    // Mono legato entry: retarget pitch (glide pulls it over) and level
    // without retriggering the envelope, cursors, or transients.
    void legatoTo (float midiNote, float velocity)
    {
        this->f_pressed_midi_note = midiNote;
        this->f_last_midi_note = midiNote;
        this->f_current_velocity = velocity;
        this->level = velocity * 0.15;
        this->updateMorphSounds (midiNote, velocity);
    }
    
    void pitchWheelMoved (int newValue) override
    {
        this->updateMidiNoteWithPitchWheel (newValue);
    }
    
    void updateMidiNoteWithPitchWheel (int newValue, bool set_current = false)
    {
        // Map range 0-16383 to -range:range semitones (range comes from the
        // "Pitch Bend" parameter, a DAW-style scaler that defaults to +/-2)
        float semitones_range = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::pitch_bend_range].ID,
            this->f_pitch_wheel_range_semitones);
        
        if (this->allow_pitch_wheel)
        {
            float f_new_midi_pitch_wheel = jmap ((float) newValue, 0.0f, 16383.0f,
                                                 -semitones_range,
                                                 semitones_range);
            
            if (set_current)
            {
                this->f_current_midi_pitch_wheel.setCurrentAndTargetValue (f_new_midi_pitch_wheel);
            }
            else
            {
                this->f_current_midi_pitch_wheel.setTargetValue (f_new_midi_pitch_wheel);
            }
        }
    }
    
    void controllerMoved (int /*controllerNumber*/, int /*newValue*/) override {}
    
    // TODO
    void setADSRSampleRate (double sampleRate)
    {
        this->adsrEnvelope.setSampleRate (sampleRate);
        this->sampleRate = sampleRate;
    }
    
    // TODO
    void updateAdsrParams (float attack, float decay, float sustain, float release)
    {
        this->adsrParams.attack  = attack;
        this->adsrParams.decay   = decay;
        this->adsrParams.sustain = sustain;
        this->adsrParams.release = release;
        
        // Update ADSR parameters
        this->adsrEnvelope.setParameters (this->adsrParams);
    }
    
    void clearAndResetCurrentNote()
    {
        this->clearCurrentNote();
        this->synthesis.reset();
        this->adsrEnvelope.reset();
        this->reset();
    }

    // Slot cursor APVTS mapping: stride-5 cursor block (rate/offset/loopstart/
    // loopend/reverse) per slot, matching SMTParameters slot1_rate + slot*5.
    static Morphex::Parameters slotCursorParam (int slot, int field)
    {
        return (Morphex::Parameters) ((int) Morphex::Parameters::slot1_rate + slot * 5 + field);
    }

    static Morphex::Parameters slotFormantParam (int slot)
    {
        return (Morphex::Parameters) ((int) Morphex::Parameters::slot1_formant + slot);
    }

    static Morphex::Parameters slotLoopParam (int slot)
    {
        return (Morphex::Parameters) ((int) Morphex::Parameters::slot1_loopmode + slot);
    }

    float readSlotParam (int slot, int field, float defaultValue) const
    {
        return Morphex::getParameterValueSafe<float> (
            mParameters, Morphex::PARAMETERS<float>[slotCursorParam (slot, field)].ID,
            defaultValue);
    }

    // Per-slot loop windows in frames (degenerate -> full range) + longest
    // effective contributor for the note-end rule (T23: short slots freeze).
    void updateSlotWindows()
    {
        this->noteEndFrames = 0;

        for (int s = 0; s < 4; s++)
        {
            this->slotMaxFrames[s] = 0;
            this->slotLoopStartF[s] = 0;
            this->slotLoopEndF[s] = 0;

            if (s >= (int) this->morph_sounds.size() || this->morph_sounds[s] == nullptr
                || ! this->morph_sounds[s]->loaded)
                continue;

            const int maxF = this->morph_sounds[s]->max_frames;
            this->slotMaxFrames[s] = maxF;

            int wStart = (int) (readSlotParam (s, 2, 0.0f) / 100.0f * (float) maxF);
            int wEnd = (int) (readSlotParam (s, 3, 100.0f) / 100.0f * (float) maxF);
            if (wEnd <= wStart)
            {
                wStart = 0;
                wEnd = maxF;
            }
            this->slotLoopStartF[s] = wStart;
            this->slotLoopEndF[s] = wEnd;

            const int effEnd = std::min (wEnd, std::max (1, maxF) - 1);
            this->noteEndFrames = std::max (this->noteEndFrames, effEnd);
        }
    }

    // Note-on cursor placement with raw reverse (counts in forward mode
    // too); slotDir is the pendulum rising flag.
    void initSlotCursors()
    {
        const bool scrubNeg = this->instrument->time_scrub_enabled
            && Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::time_scrub_rate].ID, 0.0f) < 0.0f;

        for (int s = 0; s < 4; s++)
        {
            const int wStart = this->slotLoopStartF[s];
            const int wEnd = this->slotLoopEndF[s];
            const float offset01 = readSlotParam (s, 1, 0.0f) / 100.0f;
            const bool reverse = readSlotParam (s, 4, 0.0f) > 0.5f;
            const bool backward = scrubNeg != reverse;

            if (wEnd > wStart)
            {
                this->slotCursor[s] = backward ? (float) wEnd - offset01 * (float) (wEnd - wStart)
                                               : (float) wStart + offset01 * (float) (wEnd - wStart);
                this->slotDir[s] = (backward == scrubNeg) ? 1 : -1;
            }
            else
            {
                this->slotCursor[s] = 0.0f;
                this->slotDir[s] = 1;
            }
            this->slotDone[s] = false;
        }
    }

    // Cross-mode formant weights: corner-pure gets the full static shift.
    void computeFormantShifts (float xForm, float y, bool cross, float outSt[4]) const
    {
        float raw[4];
        for (int s = 0; s < 4; s++)
        {
            raw[s] = Morphex::getParameterValueSafe<float> (
                mParameters, Morphex::PARAMETERS<float>[slotFormantParam (s)].ID, 0.0f);
            outSt[s] = raw[s];
        }
        if (! cross)
            return;

        float wSum = 0.0f;
        float w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int s = 0; s < 4; s++)
        {
            if (s < (int) this->morph_sounds.size() && this->morph_sounds[s] != nullptr
                && this->morph_sounds[s]->loaded)
            {
                w[s] = Instrument::cornerWeight (s, xForm, y);
                wSum += w[s];
            }
        }
        if (wSum <= 0.0f)
            return;
        for (int s = 0; s < 4; s++)
            outSt[s] = raw[s] * w[s] / wSum;
    }
    
    void updateMorphSounds (float f_note, float f_velocity)
    {
        // Instrument::Mode::Morphing
        if (this->instrument->mode == Instrument::Mode::Morphing)
        {
            this->morph_sounds = this->instrument->getMorphSounds();
        }
        // Instrument::Mode::FullRange
        else
        {
            this->morph_sounds = this->instrument->getCloserSounds (f_note, f_velocity);
        }

        updateSlotWindows();
        
        bool first_iter = true;
        
        for (int i = 0; i < this->morph_sounds.size(); i++)
        {
            if (this->morph_sounds[i] != nullptr && this->morph_sounds[i]->loaded)
            {
                if (first_iter)
                {
                    this->max_loop_start = this->morph_sounds[i]->loop.start;
                    this->min_loop_end = this->morph_sounds[i]->loop.end;
                    this->min_note_end = this->morph_sounds[i]->max_frames;
                    
                    first_iter = false;
                }
                else
                {
                    // Compute common looping regions
                    this->max_loop_start = std::max (this->max_loop_start, this->morph_sounds[i]->loop.start);
                    this->min_loop_end = std::min (this->min_loop_end, this->morph_sounds[i]->loop.end);
                    
                    // Shortest end stays for the legacy FullRange rule.
                    this->min_note_end = std::min (this->min_note_end, this->morph_sounds[i]->max_frames);
                }
            }
        }
        
        this->f_last_midi_note = f_note;
    }
    
    void renderNextBlock (AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (this->playing_note)
        {
            // Glide (VOICING card): exponential portamento toward the pressed
            // note + wheel at the glide-time rate; 0 ms snaps like before.
            const float glideMs = Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::glide_time_ms].ID, 0.0f);
            const float wheelNow = this->allow_pitch_wheel
                ? this->f_current_midi_pitch_wheel.getNextValue() : 0.0f;
            const float glideTarget = this->f_pressed_midi_note + wheelNow;
            if (glideMs > 0.0f && this->sampleRate > 0.0)
            {
                const double blockSec = (double) numSamples / this->sampleRate;
                const double alpha = 1.0 - std::exp (-blockSec / ((double) glideMs / 1000.0 / 3.0));
                this->f_current_midi_note += (float) ((double) (glideTarget - this->f_current_midi_note) * alpha);
            }
            else
            {
                this->f_current_midi_note = glideTarget;
            }
            
            std::vector<float> frame = getNextFrame (this->f_current_midi_note, this->f_current_velocity, numSamples);
            
            for (int i_sample = 0; i_sample < numSamples; i_sample++)
            {
                const float envelope = this->adsrEnvelope.getNextSample();
                auto currentSample = (float) (frame[i_sample] * this->level * envelope);
//                auto currentSample = (float) (std::sin (this->currentAngle) * this->level * this->tailOff);
                
                for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                {
                    outputBuffer.addSample (i, startSample, currentSample);
                }
                
                ++startSample;
                
                // When the release phase has finished, end note playback
                if (this->adsrEnvelope.isActive() == false)
                {
                    this->playing_note = false;
                }
            }
            
            if (!this->playing_note)
            {
                clearAndResetCurrentNote();
            }
        }
    }
    
    std::vector<float> getNextFrame (float f_note, float f_velocity, int i_frame_length, float f_interpolation_factor = -1)
    {
        // Refresh loop windows every block so START/END apply live like the
        // other playhead controls (cursor positions still init per note).
        updateSlotWindows();

        // Synthesis parameters shortcuts
        const int i_hop_size = this->synthesis.parameters.hop_size;
        int* i_current_frame = &this->synthesis.live_values.i_current_frame;
        
        // Rate switch scales every slot's travel (signed); off is natural.
        const float time_scrub_pct = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::time_scrub_rate].ID, 0.0f);
        const bool scrub_active = this->instrument->time_scrub_enabled;
        
        // If "track_velocity" is enabled
        if (this->track_velocity)
        {
            // If current note has changed
            if (this->f_current_midi_note != this->f_last_midi_note)
            {
                this->updateMorphSounds (this->f_current_midi_note, this->f_current_velocity);
            }
        }
        
        Sound::Frame sound_frame;

        // Cross-mode blend positions (D1): frequency rides the FREQ corner,
        // loudness the AMP corner; y stays shared. Morph mode: both follow X.
        const bool cross_mode = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_mode].ID, 0.0f) > 0.5f;
        float pad_x = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::freqs_interp_factor].ID, 0.5f);
        float pad_y = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::mags_interp_factor].ID, 0.5f);

        // Pad smoothing: one-pole glide toward the puck (0 ms = off).
        {
            const float padSmoothMs = Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::pad_smoothing_ms].ID, 0.0f);
            double padAlpha = 1.0;
            if (padSmoothMs > 0.0f && this->sampleRate > 0.0)
            {
                const double tau = (double) padSmoothMs / 1000.0;
                const double blockSec = (double) i_frame_length / this->sampleRate;
                padAlpha = 1.0 - std::exp (-blockSec / tau);
            }
            this->smoothPadX += padAlpha * ((double) pad_x - this->smoothPadX);
            this->smoothPadY += padAlpha * ((double) pad_y - this->smoothPadY);
            pad_x = (float) this->smoothPadX;
            pad_y = (float) this->smoothPadY;
        }

        // Morph trims: post-glide pad offsets (morph mode only).
        {
            const float freqTrim = Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::morph_freq_trim].ID, 0.0f);
            const float ampTrim = Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::morph_amp_trim].ID, 0.0f);
            if (freqTrim != 0.0f)
                pad_x = juce::jlimit (0.0f, 1.0f, pad_x + freqTrim);
            if (ampTrim != 0.0f)
                pad_y = juce::jlimit (0.0f, 1.0f, pad_y + ampTrim);
        }
        float x_freq = pad_x;
        float x_amp = pad_x;
        float x_form = pad_x;
        if (cross_mode)
        {
            const int freqCorner = (int) std::round (Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::cross_freq_corner].ID, 0.0f));
            const int ampCorner = (int) std::round (Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::cross_amp_corner].ID, 3.0f));
            const int formCorner = (int) std::round (Morphex::getParameterValueSafe<float>(mParameters,
                Morphex::PARAMETERS<float>[Morphex::Parameters::cross_form_corner].ID, 3.0f));
            x_freq = Instrument::cornerX (std::min (3, std::max (0, freqCorner)));
            x_amp = Instrument::cornerX (std::min (3, std::max (0, ampCorner)));
            x_form = Instrument::cornerX (std::min (3, std::max (0, formCorner)));
        }

        float formantSt[4];
        computeFormantShifts (x_form, pad_y, cross_mode, formantSt);

        // Tuning shifts the render target; tracking follows the played key.
        // Pitch lock instead renders every slot at its own original note, so
        // only the transpose/fine offset keeps riding on top of it.
        const float transposeSt = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::transpose_st].ID, 0.0f);
        const float fineCents = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::fine_tune_cents].ID, 0.0f);
        const bool forwardOnly = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::forward_only].ID, 0.0f) > 0.5f;
        const float noteOffsetSt = transposeSt + fineCents / 100.0f;
        const float tunedNote = f_current_midi_note + noteOffsetSt;

        const float transient_preserve = Morphex::getParameterValueSafe<float>(mParameters,
            Morphex::PARAMETERS<float>[Morphex::Parameters::transient_preserve].ID, 0.0f);

        // Sustain while any counted slot (loaded, non-frozen) still has
        // material or loops; frozen-only notes sustain until key release.
        const bool looping = this->instrument->sound_looping && this->loop_mode;

        while (this->synthesis.live_values.i_samples_ready < i_frame_length)
        {
            // If we are on the last frame of the longest note
            if (*i_current_frame >= this->noteEndFrames - 1 && !scrub_active
                && this->noteEndFrames > 0 && !looping)
            {
                // End note playback
                this->synthesis.live_values.last_frame = true;
            }

            // Instrument::Mode::Morphing
            if (this->instrument->mode == Instrument::Mode::Morphing)
            {
                // Resolve int fetch indices from the live fractional cursors.
                Core::SlotCursor cursors[4];
                for (int s = 0; s < 4; s++)
                {
                    const int maxF = (s < (int) this->morph_sounds.size()
                                      && this->morph_sounds[s] != nullptr
                                      && this->morph_sounds[s]->loaded)
                        ? this->slotMaxFrames[s] : 0;
                    cursors[s].frame = maxF > 0
                        ? std::min (std::max (0, (int) std::floor (this->slotCursor[s])), maxF - 1)
                        : 0;
                    cursors[s].formantSt = formantSt[s];
                }

                sound_frame = this->instrument->morphSoundFrames (tunedNote,
                                                                  morph_sounds, cursors,
                                                                  i_hop_size, x_freq, x_amp, pad_y,
                                                                  noteOffsetSt);

                // Note-on transient emphasis (D4 v1): boost the residual
                // sine mix while the envelope is hot; 0% = off (bit-identical).
                if (transient_preserve > 0.0f && this->transientBoost > 0.001
                    && sound_frame.hasSinusoidal())
                {
                    const float boostDb = transient_preserve / 100.0f * 12.0f
                                          * (float) this->transientBoost;
                    for (auto& m : sound_frame.sinusoidal.mags)
                    {
                        if (m > DEFAULT_DB + 1.0f)
                            m += boostDb;
                    }
                }
                this->transientBoost *= std::exp (-(double) i_hop_size / this->sampleRate
                                                  / transientTauSec);

                // Stochastic presence trim, dB-native (-24..+6, default 0 =
                // unity, so untouched sessions render bit-identically).
                const float stocs_gain = Morphex::getParameterValueSafe<float>(mParameters,
                    Morphex::PARAMETERS<float>[Morphex::Parameters::stocs_gain].ID, 0.0f);
                if (stocs_gain != 0.0f && sound_frame.hasStochastic())
                {
                    for (auto& m : sound_frame.stochastic)
                    {
                        if (m > DEFAULT_DB + 1.0f)
                            m += stocs_gain;
                    }
                }
            }
            // Instrument::Mode::FullRange
            else
            {
                if (this->instrument->interpolation_mode == Instrument::Interpolation::None or
                    this->morph_sounds[MorphLocation::Left] == this->morph_sounds[MorphLocation::Right])
                {
                    std::shared_ptr<Sound> selected_sound;
                    
                    if (this->instrument->interpolation_mode == Instrument::Interpolation::None)
                    {
                        int left_note_distance = std::abs (this->f_pressed_midi_note - this->morph_sounds[MorphLocation::Left]->note);
                        int right_note_distance = std::abs (this->f_pressed_midi_note - this->morph_sounds[MorphLocation::Right]->note);

                        if (left_note_distance < right_note_distance)
                        {
                            selected_sound = morph_sounds[MorphLocation::Left];
                        }
                        else
                        {
                            selected_sound = morph_sounds[MorphLocation::Right];
                        }
                    }
                    else
                    {
                        selected_sound = morph_sounds[MorphLocation::Left];
                    }   
                    
                    sound_frame = selected_sound->getFrame (*i_current_frame, i_hop_size);

                    // Get target frequency (tuned: transpose + fine). Pitch
                    // lock keeps the sound at its own original note instead.
                    float f_note_frequency = Tools::Midi::toFreq (selected_sound->note);
                    float f_target_frequency = this->instrument->pitchLocked
                        ? Tools::Midi::toFreq (selected_sound->note + noteOffsetSt)
                        : Tools::Midi::toFreq (tunedNote);

                    const bool rescale = f_target_frequency != f_note_frequency;

                    if (rescale && sound_frame.hasHarmonic())
                    {
                        // Recalculate the harmonics for the current midi note
                        for (int i=0; i<sound_frame.harmonic.freqs.size(); i++)
                        {
                            sound_frame.harmonic.freqs[i] = (sound_frame.harmonic.freqs[i] / f_note_frequency) * f_target_frequency;
                        }
                    }
                    
                    if (rescale && sound_frame.hasSinusoidal())
                    {
                        // Recalculate the harmonics for the current midi note
                        for (int i=0; i<sound_frame.sinusoidal.freqs.size(); i++)
                        {
                            sound_frame.sinusoidal.freqs[i] = (sound_frame.sinusoidal.freqs[i] / f_note_frequency) * f_target_frequency;
                        }
                    }
                }
                else
                {
                    // TODO - Apply fade out if *i_current_frame > this->min_note_end - 4 (4 = fade_out_frames)
                    Core::SlotCursor sharedCursor[4];
                    for (int s = 0; s < 4; s++)
                        sharedCursor[s].frame = *i_current_frame;
                    sound_frame = this->instrument->morphSoundFrames (tunedNote,
                                                                      morph_sounds, sharedCursor,
                                                                      i_hop_size, pad_x, pad_x, pad_y,
                                                                      noteOffsetSt);
                }
            }

            // NOTE - "frame" will have "i_hop_size" more samples ready to be played after each call
            // TODO - This function needs to be optimized
            this->synthesis.generateSoundFrame (sound_frame, i_frame_length);
            
            // Global signed rate multiplier; slots stay independent.
            const float rateMult = scrub_active ? time_scrub_pct / 100.0f : 1.0f;

            // Shared conductor (display, FullRange).
            *i_current_frame += 1;

            // Per-slot cursors: ping-pong when looping, clamp when not.
            bool anyCounted = false;
            bool anyActive = false;

            for (int s = 0; s < 4; s++)
            {
                if (s >= (int) this->morph_sounds.size()
                    || this->morph_sounds[s] == nullptr
                    || ! this->morph_sounds[s]->loaded)
                    continue;

                const float rate = readSlotParam (s, 0, 100.0f) / 100.0f;
                if (rate <= 0.0f)
                    continue; // frozen: holds its frame, neutral for duration

                anyCounted = true;

                const int wStart = this->slotLoopStartF[s];
                const int wEnd = this->slotLoopEndF[s];
                const int maxF = std::max (1, this->slotMaxFrames[s]);
                const bool slotLoop = Morphex::getParameterValueSafe<float> (
                    mParameters, Morphex::PARAMETERS<float>[slotLoopParam (s)].ID, 1.0f) > 0.5f;

                // Pendulum model: slotDir is the rising flag; reflection
                // flips it, forward path never touches it.
                const bool reverse = readSlotParam (s, 4, 0.0f) > 0.5f;
                const float revSign = reverse ? -1.0f : 1.0f;
                const float riseSign = this->slotDir[s] >= 0 ? 1.0f : -1.0f;
                const float wEndF = (float) wEnd;
                const float wStartF = (float) wStart;

                if (looping && slotLoop && wEnd > wStart)
                {
                    if (forwardOnly)
                    {
                        const float fwdStep = revSign * rate * rateMult;
                        this->slotCursor[s] += fwdStep;
                        if (fwdStep >= 0.0f)
                        {
                            while (this->slotCursor[s] > wEndF)
                                this->slotCursor[s] -= (wEndF - wStartF);
                            if (this->slotCursor[s] < wStartF)
                                this->slotCursor[s] = wStartF;
                        }
                        else
                        {
                            while (this->slotCursor[s] < wStartF)
                                this->slotCursor[s] += (wEndF - wStartF);
                            if (this->slotCursor[s] > wEndF)
                                this->slotCursor[s] = wEndF;
                        }
                    }
                    else
                    {
                        this->slotCursor[s] += riseSign * rate * rateMult;
                        if (this->slotCursor[s] >= wEndF
                            || this->slotCursor[s] <= wStartF)
                        {
                            this->slotCursor[s] = (this->slotCursor[s] >= wEndF)
                                ? 2.0f * wEndF - this->slotCursor[s]
                                : 2.0f * wStartF - this->slotCursor[s];
                            this->slotDir[s] = -this->slotDir[s];
                        }
                    }
                    anyActive = true;
                }
                else
                {
                    const float lo = (float) wStart;
                    const float hi = (float) std::min (wEnd, maxF - 1);
                    const float travel = revSign * rate * rateMult;
                    this->slotCursor[s] += travel;
                    if (travel >= 0.0f)
                    {
                        if (this->slotCursor[s] >= hi)
                        {
                            this->slotCursor[s] = hi;
                            this->slotDone[s] = true;
                        }
                        else
                        {
                            anyActive = true;
                        }
                    }
                    else
                    {
                        if (this->slotCursor[s] <= lo)
                        {
                            this->slotCursor[s] = lo;
                            this->slotDone[s] = true;
                        }
                        else
                        {
                            anyActive = true;
                        }
                    }
                }

                // Legacy shared-loop sustain region (kept for FullRange);
                // the per-slot rule above governs Morphing notes.
                if (this->instrument->sound_looping && this->loop_mode
                    && this->max_loop_start < this->min_loop_end
                    && (*i_current_frame * i_hop_size) >= this->min_loop_end)
                {
                    *i_current_frame = int (this->max_loop_start / i_hop_size);
                }
                else if (anyCounted && !anyActive && this->instrument->mode == Instrument::Mode::Morphing)
                {
                    // Longest contributor finished: end note playback
                    this->playing_note = false;
                }
                else if (*i_current_frame >= this->min_note_end
                         && this->instrument->mode != Instrument::Mode::Morphing)
                {
                    // FullRange one-shot end (legacy shortest rule kept)
                    this->playing_note = false;
                }
            }

            // Publish slot playheads for the loader rail (lock-free, UI polls).
            if (auto* display = this->instrument->slotPhaseDisplay)
            {
                for (int s = 0; s < 4; s++)
                {
                    if (s >= (int) this->morph_sounds.size()
                        || this->morph_sounds[s] == nullptr
                        || ! this->morph_sounds[s]->loaded
                        || this->slotMaxFrames[s] <= 0)
                    {
                        display[s].store (-1.0f);
                        continue;
                    }
                    display[s].store (juce::jlimit (0.0f, 1.0f,
                        this->slotCursor[s] / (float) this->slotMaxFrames[s]));
                }
            }
        }
        
        // Selecting the processed samples
        std::vector<float> frame = this->synthesis.getBuffer (Synthesis::BufferSection::Play, Channel::Mono, i_frame_length);
        
        // Update play pointer position
        this->synthesis.updatePlayPointer (i_frame_length);

        // Update samples ready to be played
        this->synthesis.live_values.i_samples_ready -= i_frame_length;
        
        return frame;
    }
    
private:
    
    AudioProcessorValueTreeState* mParameters;
    
    Instrument* instrument;
    Synthesis synthesis;
        
    // Midi
    float f_pressed_midi_note;
    float f_current_midi_note;
    SmoothedValue<float, ValueSmoothingTypes::Linear> f_current_midi_pitch_wheel;
    float f_current_velocity;
    float f_last_midi_note;
    float f_pitch_wheel_range_semitones = 2.0f;
    
    // Note playback
    bool playing_note;
    bool loop_mode;
    bool hold_note;
    bool track_velocity;
    bool allow_pitch_wheel;

    // Sounds
    Core::MorphSounds morph_sounds;
    int max_loop_start;
    int min_loop_end;
    int min_note_end;

    // Per-slot independent cursors (rate/offset/loop window/reverse). Fractional
    // accumulators advanced per generated frame; resolved to int fetch indices.
    float slotCursor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int slotDir[4] = { 1, 1, 1, 1 };
    int slotLoopStartF[4] = { 0, 0, 0, 0 };
    int slotLoopEndF[4] = { 0, 0, 0, 0 };
    int slotMaxFrames[4] = { 0, 0, 0, 0 };
    bool slotDone[4] = { false, false, false, false };
    int noteEndFrames = 0;

    // Pad smoothing state (one-pole lowpass, per-voice here since
    // Morphex has no control thread; snapped to the pad at note-on).
    double smoothPadX = 0.5;
    double smoothPadY = 0.5;

    // Note-on transient emphasis envelope state (1 at note-on, decays to 0).
    double transientBoost = 1.0;
    
    double level = 0.0;
    
    // Time-scrub LFO (triangle ramp in [0,1], see getNextFrame)
    double sampleRate = 44100.0;
    
    // ADSR envelope
    juce::ADSR adsrEnvelope;
    juce::ADSR::Parameters adsrParams;
};
