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

#include "Instrument.h"
#include "Codec.h"
#include "Tools.h"

#include <algorithm>
#include <cmath>

namespace Core
{
    // Grid Sounds live in the Note/Velocity storage for the process lifetime
    // (~Instrument is empty), so aliasing them with a no-op deleter is safe.
    static std::shared_ptr<Sound> aliasGridSound (Sound* raw)
    {
        return std::shared_ptr<Sound> (raw, [] (Sound*) noexcept {});
    }

    void Instrument::applyFormantShift (std::vector<float>& mags, const std::vector<float>& freqs,
                                        float shiftSt)
    {
        if (shiftSt == 0.0f || mags.empty() || freqs.empty())
            return;

        struct Pt
        {
            float logf;
            float mag;
        };
        std::vector<Pt> env;
        const std::size_t n = std::min (mags.size(), freqs.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            if (freqs[i] > 0.0f && mags[i] > DEFAULT_DB + 1.0f)
                env.push_back ({ std::log (freqs[i]), mags[i] });
        }
        if (env.size() < 2)
            return;
        std::sort (env.begin(), env.end(),
                   [] (const Pt& a, const Pt& b) { return a.logf < b.logf; });

        const double logR = std::log (std::pow (2.0, (double) shiftSt / 12.0));
        for (std::size_t i = 0; i < n; ++i)
        {
            if (freqs[i] <= 0.0f || mags[i] <= DEFAULT_DB + 1.0f)
                continue;
            const float target = std::log (freqs[i]) - (float) logR;

            float envMag;
            if (target <= env.front().logf)
                envMag = env.front().mag;
            else if (target >= env.back().logf)
                envMag = env.back().mag;
            else
            {
                const auto it = std::upper_bound (
                    env.begin(), env.end(), target,
                    [] (float value, const Pt& pt) { return value < pt.logf; });
                const Pt& b = *it;
                const Pt& a = *(it - 1);
                const float t = (target - a.logf) / (b.logf - a.logf);
                envMag = a.mag + t * (b.mag - a.mag);
            }

            const float correction = std::max (-24.0f, std::min (24.0f, envMag - mags[i]));
            mags[i] += correction;
        }
    }

    float Instrument::cornerX (int cornerIdx) noexcept
    {
        return (cornerIdx == MorphLocation::RightLow || cornerIdx == MorphLocation::RightHigh)
            ? 1.0f : 0.0f;
    }

    // Bilinear pad weight of one slot at (x, y).
    float Instrument::cornerWeight (int slot, float x, float y) noexcept
    {
        const float wx = (slot == MorphLocation::RightLow || slot == MorphLocation::RightHigh) ? x : 1.0f - x;
        const float wy = (slot == MorphLocation::LeftHigh || slot == MorphLocation::RightHigh) ? y : 1.0f - y;
        return wx * wy;
    }
} // namespace Core

namespace Core
{
    Instrument::Instrument()
    {
        // Notes
        this->note = std::vector<Note*> (NUM_MIDI_NOTES);
        
        for (int i = 0; i < this->note.size(); i++)
        {
            this->note[i] = new Note (i);
        }
        
        this->init();
    };
    
    Instrument::~Instrument() {}
    
    void Instrument::init()
    {
        // Data
        this->name = "New Instrument";
        this->samples_dirpath = "";
        
        // Morph Notes
        for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
        {
            this->setMorphNote (this->note[i], (MorphLocation) i);
        }
        
        // Mode
        if (this->mode == Mode::FullRange) this->interpolation_mode = Interpolation::None;
    }
    
    void Instrument::reset()
    {
        this->init();
        
        // Reset Notes
        for (int i = 0; i < this->note.size(); i++)
        {
            this->note[i]->reset();
        }
    }
    
    std::shared_ptr<Sound> Instrument::decodeSound (const std::string& file_path)
    {
        return std::make_shared<Sound> (file_path);
    }

    void Instrument::installSound (std::shared_ptr<Sound> decoded, MorphLocation morph_location)
    {
        if (decoded == nullptr || ! decoded->loaded)
            return;

        if (morph_location < NUM_MORPH_LOCATIONS)
        {
            // Free velocity slot per location so same-pitch sounds coexist.
            int velocity = decoded->velocity;

            for (int attempt = 0; attempt < NUM_MIDI_VELOCITIES; attempt++)
            {
                bool slot_in_use = false;

                for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
                {
                    if (i != morph_location &&
                        morph_sounds[i] != nullptr &&
                        morph_sounds[i]->note == decoded->note &&
                        morph_sounds[i]->velocity == velocity)
                    {
                        slot_in_use = true;
                        break;
                    }
                }

                if (! slot_in_use) break;

                velocity++;
                if (velocity > MAX_MIDI_VELOCITY) velocity = 0;
            }

            decoded->velocity = velocity;
        }

        // Bookkeeping copy in the Note grid (velocity-slot record, FullRange).
        this->note[decoded->note]->velocity[decoded->velocity]->sound = *decoded;
        this->note[decoded->note]->velocity[decoded->velocity]->loaded = true;

        if (morph_location < NUM_MORPH_LOCATIONS)
        {
            this->morph_notes[morph_location] = this->note[decoded->note];
            // Snapshot isolation: the morph slot owns its own heap copy so
            // held notes survive later installs and slot clears.
            this->morph_sounds[morph_location] = decoded;
        }
    }

    void Instrument::loadSound (std::string file_path, MorphLocation morph_location)
    {
        installSound (decodeSound (file_path), morph_location);
    }
    
    std::vector<Note*> Instrument::getLoadedNotes()
    {
        // Output
        std::vector<Note*> loaded_notes;
        
        for (int i = 0; i < this->note.size(); i++)
        {
            Note* note = this->note[i];
            
            if ( note->hasAnyVelocity() )
            {
                loaded_notes.push_back (note);
            }
        }
        
        return loaded_notes;
    }
    
    MorphNotes Instrument::getCloserNotes (float f_target_note)
    {
        int l_i_target_note = round (f_target_note);
        int h_i_target_note = ceil (f_target_note);
        
        // Output (use "{}" to ensure "nullptr" initialization)
        MorphNotes closer_notes {};
        
        std::vector<Note*> loaded_notes = getLoadedNotes();

        // Instrument::Mode::Morphing
        if (this->mode == Instrument::Mode::Morphing)
        {
            int i_notes_to_load = std::min ((int) loaded_notes.size(), (int) MorphLocation::NUM_MORPH_LOCATIONS);

            for (int i = 0; i < i_notes_to_load; i++)
            {
                closer_notes[i] = loaded_notes[i];
            }
        }
        // Instrument::Mode::FullRange
        else
        {
            // For loaded_notes sorted from min to max
            for (int i = 0; i < loaded_notes.size(); i++)
            {
                Note* note = loaded_notes[i];

                if (note->value < l_i_target_note)
                {
                    closer_notes[MorphLocation::Left] = note;
                }
                else if (h_i_target_note < note->value)
                {
                    closer_notes[MorphLocation::Right] = note;
                    break;
                }
                else
                {
                    closer_notes[MorphLocation::Left] = note;
                    closer_notes[MorphLocation::Right] = note;
                    if (h_i_target_note <= note->value) break;
                }
            }
        }
        
        if (closer_notes[MorphLocation::Left] == nullptr) closer_notes[MorphLocation::Left] = closer_notes[MorphLocation::Right];
        if (closer_notes[MorphLocation::Right] == nullptr) closer_notes[MorphLocation::Right] = closer_notes[MorphLocation::Left];

        return closer_notes;
    }
    
    MorphSounds Instrument::getCloserSounds (float f_target_note, float f_velocity)
    {
        MorphNotes closer_notes = getCloserNotes (f_target_note);
        
        // Output
        MorphSounds closer_sounds {};
        
        // Transform velocity range from 0-1 to 0-127
        float f_velocity_midi_range = jmap (f_velocity, 0.0f, 1.0f, 1.0f, 127.0f);
        
        // Getting closer velocities
        for (int i = 0; i < closer_notes.size(); i++)
        {
            if (closer_notes[i] != nullptr)
            {
                std::vector<Velocity*> loaded_velocities = closer_notes[i]->getLoadedVelocities();
                
                int i_closer_velocity = 0;
                float f_shortest_velocity_distance = MAX_MIDI_VELOCITY;
                
                for (int j = 0; j < loaded_velocities.size(); j++)
                {
                    float f_velocity_distance = std::abs (loaded_velocities[j]->value - f_velocity_midi_range);
                    
                    // Priority to lower velocities ("<=" for upper velocities)
                    if (f_velocity_distance < f_shortest_velocity_distance)
                    {
                        i_closer_velocity = j;
                        f_shortest_velocity_distance = f_velocity_distance;
                    }
                }
                
                closer_sounds[i] = aliasGridSound (&loaded_velocities[i_closer_velocity]->sound);
            }
        }
        
        return closer_sounds;
    }
    
    MorphNotes Instrument::getMorphNotes()
    {
        return morph_notes;
    }
    
    void Instrument::setMorphNote (Note* note, MorphLocation morph_location, int midi_velocity)
    {
        morph_notes[morph_location] = note;

        this->setMorphSound (aliasGridSound (&morph_notes[morph_location]->velocity[midi_velocity]->sound),
                             morph_location);
    }
    
    MorphSounds Instrument::getMorphSounds()
    {
        return morph_sounds;
    }
    
    std::shared_ptr<Sound> Instrument::getMorphSound (MorphLocation morph_location)
    {
        return morph_sounds[morph_location];
    }
    
    void Instrument::setMorphSound (std::shared_ptr<Sound> sound, MorphLocation morph_location)
    {
        morph_sounds[morph_location] = sound;
    }
    
    void Instrument::clearSound (MorphLocation morph_location)
    {
        if (morph_location < NUM_MORPH_LOCATIONS)
        {
            morph_sounds[morph_location] = nullptr;
        }
    }
    
    Sound::Frame Instrument::getSoundFrame (float f_note, float f_velocity, int i_current_frame, int i_frame_length, float f_freqs_interp_factor, float f_mags_interp_factor)
    {
        MorphSounds morph_sounds = getCloserSounds (f_note, f_velocity);
        
        if (morph_sounds[MorphLocation::Left] == morph_sounds[MorphLocation::Right])
        {
            return morph_sounds[MorphLocation::Left]->getFrame (i_current_frame, i_frame_length);
        }
        else
        {
            SlotCursor cursors[4];
            for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
                cursors[i].frame = i_current_frame;
            // Legacy path: single x drives both blends (morph, never cross).
            return morphSoundFrames (f_note, morph_sounds, cursors, i_frame_length,
                                     f_freqs_interp_factor, f_freqs_interp_factor,
                                     f_mags_interp_factor);
        }
    }
    
    // Per-note loops run on their own points (unaligned by design).
    Sound::Frame Instrument::morphSoundFrames (float f_target_note, const MorphSounds& morph_sounds,
                                              const SlotCursor cursors[4], int i_frame_length,
                                              float x_freq_factor, float x_amp_factor, float y_factor,
                                              float f_note_offset_st)
    {
        // Output
        Sound::Frame morphed_sound_frame;

        // Cross mode: split x positions; morph passes xFreq == xAmp.
        const bool cross_mode = (x_freq_factor != x_amp_factor);

        // Get target frequency
        float f_target_frequency = Tools::Midi::toFreq (f_target_note);
//        float f_target_frequency = MidiMessage::getMidiNoteInHertz(int i_note);

        // Pitch lock: every slot keeps its own original note (plus the global
        // transpose/fine offset) instead of being shifted to the target note,
        // which still drives the FrequencyBased morph position.
        const bool transposeToTarget = ! this->pitchLocked || f_note_offset_st != 0.0f;
        const auto targetFrequencyFor = [this, f_target_frequency, f_note_offset_st]
                                        (const std::shared_ptr<Sound>& sound)
        {
            if (! this->pitchLocked || sound == nullptr)
                return f_target_frequency;
            return Tools::Midi::toFreq ((float) sound->note + f_note_offset_st);
        };
        
        // Interpolation factor is calculated taking into account
        // how far is each note from the target frequency (normalized)
        if (this->interpolation_mode == Interpolation::FrequencyBased)
        {
            if (morph_sounds[MorphLocation::Left]->note == morph_sounds[MorphLocation::Right]->note)
            {
                x_freq_factor = 0.0;
                x_amp_factor = 0.0;
            }
            else
            {
                x_freq_factor =
                (f_target_frequency - Tools::Midi::toFreq (morph_sounds[MorphLocation::Left]->note)) /
                (Tools::Midi::toFreq (morph_sounds[MorphLocation::Right]->note) - Tools::Midi::toFreq (morph_sounds[MorphLocation::Left]->note));
            }

            y_factor = x_freq_factor;
        }

        float f_stocs_interp_factor = x_amp_factor;
        float f_attack_interp_factor = x_amp_factor;
        float f_residual_interp_factor = x_amp_factor;

        // 4-voice morphing is only available when all 4 locations have loaded sounds
        bool has_all_four_sounds = true;
        for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
        {
            if (morph_sounds[i] == nullptr || !morph_sounds[i]->loaded)
            {
                has_all_four_sounds = false;
                break;
            }
        }

        // Corner fast path: pad on a corner returns that frame directly
        // (off in Cross mode, which has no single corner).
        if (! cross_mode)
        {
            const float x = x_freq_factor;
            const float y = y_factor;

            MorphLocation corner = MorphLocation::NUM_MORPH_LOCATIONS;

            if (x <= 0.001f && y <= 0.001f)
                corner = MorphLocation::LeftLow;
            else if (x >= 0.999f && y <= 0.001f && has_all_four_sounds)
                corner = MorphLocation::RightLow;
            else if (x <= 0.001f && y >= 0.999f && has_all_four_sounds)
                corner = MorphLocation::LeftHigh;
            else if (x >= 0.999f && y >= 0.999f)
                corner = has_all_four_sounds ? MorphLocation::RightHigh : MorphLocation::RightLow;

            if (corner != MorphLocation::NUM_MORPH_LOCATIONS && morph_sounds[corner] != nullptr)
            {
                Sound::Frame corner_frame = morph_sounds[corner]->getFrame (cursors[corner].frame,
                                                                            i_frame_length);

                // Transpose the corner sound's frequencies to the target note,
                // matching what the mid-pad interpolation would produce
                if (corner_frame.hasHarmonic())
                {
                    if (transposeToTarget)
                    {
                        Tools::Calculate::divideByScalar (corner_frame.harmonic.freqs,
                                                          Tools::Midi::toFreq (morph_sounds[corner]->note));
                        Tools::Calculate::multiplyByScalar (corner_frame.harmonic.freqs,
                                                            targetFrequencyFor (morph_sounds[corner]));
                    }
                    applyFormantShift (corner_frame.harmonic.mags, corner_frame.harmonic.freqs,
                                       cursors[corner].formantSt);
                }

                if (corner_frame.hasSinusoidal())
                {
                    if (transposeToTarget)
                    {
                        Tools::Calculate::divideByScalar (corner_frame.sinusoidal.freqs,
                                                          Tools::Midi::toFreq (morph_sounds[corner]->note));
                        Tools::Calculate::multiplyByScalar (corner_frame.sinusoidal.freqs,
                                                            targetFrequencyFor (morph_sounds[corner]));
                    }
                    applyFormantShift (corner_frame.sinusoidal.mags, corner_frame.sinusoidal.freqs,
                                       cursors[corner].formantSt);
                }

                return corner_frame;
            }
        }

        MorphSoundFrames morph_sound_frames;
        
        // Select each slot's own frame (independent cursors: rate/offset/loop
        // window/reverse resolved by the Voice into cursors[i].frame).
        for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
        {
            if (morph_sounds[i] != nullptr)
            {
                morph_sound_frames[i] = morph_sounds[i]->getFrame (cursors[i].frame, i_frame_length);
            }
        }
        
        // Get the maximum number of harmonics and sound length
        int i_max_harmonics = 0;
        for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
        {
            i_max_harmonics = std::max (i_max_harmonics, morph_sound_frames[i].getMaxHarmonics());
        }
        
        // The stochastic spectrum covers the whole analysis FFT range,
        // so it needs its own (larger) interpolation length
        int i_max_stochastic_length = 0;
        for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
        {
            i_max_stochastic_length = std::max (i_max_stochastic_length, (int) morph_sound_frames[i].stochastic.size());
        }
        
        int i_max_sinusoids = 0;
        for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
        {
            i_max_sinusoids = std::max (i_max_sinusoids, morph_sound_frames[i].getMaxSinusoids());
        }
        
        // Fallback frame: heaviest available corner when data is missing.
        
        if (morph_sound_frames[MorphLocation::LeftLow].hasHarmonic() or
            morph_sound_frames[MorphLocation::RightLow].hasHarmonic() or
            morph_sound_frames[MorphLocation::LeftHigh].hasHarmonic() or
            morph_sound_frames[MorphLocation::RightHigh].hasHarmonic())
        {
            // TODO - Check parameter morph_sounds[i]->parameters.transpose.harmonic
            // if false, do not transpose the frame of this note
            
            // Transpose every note's frequencies to the target frequency
            for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
            {
                if (morph_sounds[i] != nullptr)
                {
                    if (transposeToTarget)
                    {
                        Tools::Calculate::divideByScalar (morph_sound_frames[i].harmonic.freqs,
                                                          Tools::Midi::toFreq(morph_sounds[i]->note));
                        Tools::Calculate::multiplyByScalar (morph_sound_frames[i].harmonic.freqs,
                                                            targetFrequencyFor (morph_sounds[i]));
                    }
                    // Formant shift rides the slot's own envelope (pitch path
                    // untouched by construction); shift 0 skips the multiply.
                    applyFormantShift (morph_sound_frames[i].harmonic.mags,
                                       morph_sound_frames[i].harmonic.freqs,
                                       cursors[i].formantSt);
                }
            }
            
            if (has_all_four_sounds)
            {
                // Bilinear interpolation of the harmonic frequencies (X/Y pad).
                // Cross mode: frequencies blend at the FREQ corner position.
                morphed_sound_frame.harmonic.freqs =
                bilinearInterpolateFrames (FrameType::Frequencies,
                                           x_freq_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].harmonic.freqs,
                                           morph_sound_frames[MorphLocation::RightLow].harmonic.freqs,
                                           morph_sound_frames[MorphLocation::LeftHigh].harmonic.freqs,
                                           morph_sound_frames[MorphLocation::RightHigh].harmonic.freqs,
                                           i_max_harmonics);

                // Bilinear interpolation of the harmonic magnitudes (X/Y pad).
                // Cross mode: loudness blends at the AMP corner position.
                morphed_sound_frame.harmonic.mags =
                bilinearInterpolateFrames (FrameType::Magnitudes,
                                           x_amp_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].harmonic.mags,
                                           morph_sound_frames[MorphLocation::RightLow].harmonic.mags,
                                           morph_sound_frames[MorphLocation::LeftHigh].harmonic.mags,
                                           morph_sound_frames[MorphLocation::RightHigh].harmonic.mags,
                                           i_max_harmonics);
            }
            else
            {
                // 2-sound (backward compatible) linear interpolation
                morphed_sound_frame.harmonic.freqs =
                interpolateFrames (FrameType::Frequencies,
                                   x_freq_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].harmonic.freqs,
                                   morph_sound_frames[MorphLocation::RightLow].harmonic.freqs,
                                   i_max_harmonics);

                morphed_sound_frame.harmonic.mags =
                interpolateFrames (FrameType::Magnitudes,
                                   x_amp_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].harmonic.mags,
                                   morph_sound_frames[MorphLocation::RightLow].harmonic.mags,
                                   i_max_harmonics);
            }
        }
        
        // Sinusoidal component
        if (morph_sound_frames[MorphLocation::LeftLow].hasSinusoidal() or
            morph_sound_frames[MorphLocation::RightLow].hasSinusoidal() or
            morph_sound_frames[MorphLocation::LeftHigh].hasSinusoidal() or
            morph_sound_frames[MorphLocation::RightHigh].hasSinusoidal())
        {
            // Transpose every note's sinusoidal frequencies to the target frequency
            for (int i = 0; i < MorphLocation::NUM_MORPH_LOCATIONS; i++)
            {
                if (morph_sounds[i] != nullptr)
                {
                    if (transposeToTarget)
                    {
                        Tools::Calculate::divideByScalar (morph_sound_frames[i].sinusoidal.freqs,
                                                          Tools::Midi::toFreq (morph_sounds[i]->note));
                        Tools::Calculate::multiplyByScalar (morph_sound_frames[i].sinusoidal.freqs,
                                                            targetFrequencyFor (morph_sounds[i]));
                    }
                    applyFormantShift (morph_sound_frames[i].sinusoidal.mags,
                                       morph_sound_frames[i].sinusoidal.freqs,
                                       cursors[i].formantSt);
                }
            }
            
            if (has_all_four_sounds)
            {
                // Bilinear interpolation of the sinusoidal components (X/Y pad)
                morphed_sound_frame.sinusoidal.freqs =
                bilinearInterpolateFrames (FrameType::Frequencies,
                                           x_freq_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].sinusoidal.freqs,
                                           morph_sound_frames[MorphLocation::RightLow].sinusoidal.freqs,
                                           morph_sound_frames[MorphLocation::LeftHigh].sinusoidal.freqs,
                                           morph_sound_frames[MorphLocation::RightHigh].sinusoidal.freqs,
                                           i_max_sinusoids);
                
                morphed_sound_frame.sinusoidal.mags =
                bilinearInterpolateFrames (FrameType::Sinusoids,
                                           x_amp_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].sinusoidal.mags,
                                           morph_sound_frames[MorphLocation::RightLow].sinusoidal.mags,
                                           morph_sound_frames[MorphLocation::LeftHigh].sinusoidal.mags,
                                           morph_sound_frames[MorphLocation::RightHigh].sinusoidal.mags,
                                           i_max_sinusoids);
                
                morphed_sound_frame.sinusoidal.phases =
                bilinearInterpolateFrames (FrameType::Frequencies,
                                           x_freq_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].sinusoidal.phases,
                                           morph_sound_frames[MorphLocation::RightLow].sinusoidal.phases,
                                           morph_sound_frames[MorphLocation::LeftHigh].sinusoidal.phases,
                                           morph_sound_frames[MorphLocation::RightHigh].sinusoidal.phases,
                                           i_max_sinusoids);
            }
            else
            {
                // 2-sound (backward compatible) linear interpolation
                morphed_sound_frame.sinusoidal.freqs =
                interpolateFrames (FrameType::Frequencies,
                                   x_freq_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].sinusoidal.freqs,
                                   morph_sound_frames[MorphLocation::RightLow].sinusoidal.freqs,
                                   i_max_sinusoids);
                
                morphed_sound_frame.sinusoidal.mags =
                interpolateFrames (FrameType::Sinusoids,
                                   y_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].sinusoidal.mags,
                                   morph_sound_frames[MorphLocation::RightLow].sinusoidal.mags,
                                   i_max_sinusoids);
                
                morphed_sound_frame.sinusoidal.phases =
                interpolateFrames (FrameType::Frequencies,
                                   y_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].sinusoidal.phases,
                                   morph_sound_frames[MorphLocation::RightLow].sinusoidal.phases,
                                   i_max_sinusoids);
            }
        }
        
        if (morph_sound_frames[MorphLocation::LeftLow].hasStochastic() or
            morph_sound_frames[MorphLocation::RightLow].hasStochastic() or
            morph_sound_frames[MorphLocation::LeftHigh].hasStochastic() or
            morph_sound_frames[MorphLocation::RightHigh].hasStochastic())
        {
            if (has_all_four_sounds)
            {
                // Bilinear interpolation of the stochastic components (X/Y pad)
                morphed_sound_frame.stochastic =
                bilinearInterpolateFrames (FrameType::Stochastic,
                                           x_amp_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].stochastic,
                                           morph_sound_frames[MorphLocation::RightLow].stochastic,
                                           morph_sound_frames[MorphLocation::LeftHigh].stochastic,
                                           morph_sound_frames[MorphLocation::RightHigh].stochastic,
                                           i_max_stochastic_length);
            }
            else
            {
                // 2-sound (backward compatible) linear interpolation
                morphed_sound_frame.stochastic =
                interpolateFrames (FrameType::Stochastic,
                                   f_stocs_interp_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].stochastic,
                                   morph_sound_frames[MorphLocation::RightLow].stochastic,
                                   i_max_stochastic_length);
            }
        }
        
        if (morph_sound_frames[MorphLocation::LeftLow].hasAttack() or
            morph_sound_frames[MorphLocation::RightLow].hasAttack() or
            morph_sound_frames[MorphLocation::LeftHigh].hasAttack() or
            morph_sound_frames[MorphLocation::RightHigh].hasAttack())
        {
            // TODO - Check parameter morph_sounds[i]->parameters.transpose.attack
            // if false, do not transpose the frame of this note
            
            if (has_all_four_sounds)
            {
                // Bilinear interpolation of the attack components (X/Y pad)
                morphed_sound_frame.attack =
                bilinearInterpolateFrames (FrameType::Waveform,
                                           x_amp_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].attack,
                                           morph_sound_frames[MorphLocation::RightLow].attack,
                                           morph_sound_frames[MorphLocation::LeftHigh].attack,
                                           morph_sound_frames[MorphLocation::RightHigh].attack,
                                           i_frame_length);
            }
            else
            {
                // 2-sound (backward compatible) linear interpolation
                morphed_sound_frame.attack =
                interpolateFrames (FrameType::Waveform,
                                   f_attack_interp_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].attack,
                                   morph_sound_frames[MorphLocation::RightLow].attack,
                                   i_frame_length);
            }
        }
        
        if (morph_sound_frames[MorphLocation::LeftLow].hasResidual() or
            morph_sound_frames[MorphLocation::RightLow].hasResidual() or
            morph_sound_frames[MorphLocation::LeftHigh].hasResidual() or
            morph_sound_frames[MorphLocation::RightHigh].hasResidual())
        {
            // TODO - Check parameter morph_sounds[i]->parameters.transpose.residual
            // if false, do not transpose the frame of this note
            
            if (has_all_four_sounds)
            {
                // Bilinear interpolation of the residual components (X/Y pad)
                morphed_sound_frame.residual =
                bilinearInterpolateFrames (FrameType::Waveform,
                                           x_amp_factor, y_factor,
                                           morph_sound_frames[MorphLocation::LeftLow].residual,
                                           morph_sound_frames[MorphLocation::RightLow].residual,
                                           morph_sound_frames[MorphLocation::LeftHigh].residual,
                                           morph_sound_frames[MorphLocation::RightHigh].residual,
                                           i_frame_length);
            }
            else
            {
                // 2-sound (backward compatible) linear interpolation
                morphed_sound_frame.residual =
                interpolateFrames (FrameType::Waveform,
                                   f_residual_interp_factor,
                                   morph_sound_frames[MorphLocation::LeftLow].residual,
                                   morph_sound_frames[MorphLocation::RightLow].residual,
                                   i_frame_length);
            }
        }
        
        return morphed_sound_frame;
    }
    
    std::vector<float> Instrument::interpolateFrames (FrameType frame_type,
                                                      float interp_factor,
                                                      const std::vector<float>& frame_1,
                                                      const std::vector<float>& frame_2,
                                                      int i_frame_length)
    {
        // TODO - Get rid of the for loops, use matrix multiplications instead
        
        // Stop synthesis when shortest sound has finished
        bool stop_at_shortest = true;
        
        // Stochastic magnitudes are stored in dB (like Magnitudes), so missing
        // data must default to DEFAULT_DB (silence) and not 0.0 (full scale)
        const bool is_magnitude_based = (frame_type == FrameType::Magnitudes or
                                         frame_type == FrameType::Stochastic or
                                         frame_type == FrameType::Sinusoids);
        
        float DEFAULT_VALUE = is_magnitude_based ? DEFAULT_DB : DEFAULT_HZ;
        
        // Output
        std::vector<float> interpolated_frame (i_frame_length, DEFAULT_VALUE);
        
        if (i_frame_length != 0)
        {
            // TODO - Check if all elements in the magnitude frame are 0.0
            // do this when loading the sound
            
            
            // When only one corner has frequency data, inherit its values
            // (the amplitudes are crossfaded separately via the magnitudes)
            if (frame_type == FrameType::Frequencies and
                (frame_1.size() == 0) != (frame_2.size() == 0))
            {
                const std::vector<float>& present = frame_1.size() == 0 ? frame_2 : frame_1;
                for (int i = 0; i < i_frame_length; i++)
                {
                    interpolated_frame[i] = (i < present.size()) ? present[i] : DEFAULT_VALUE;
                }
                return interpolated_frame;
            }
            
            // Aux values
            float aux_value_1 = DEFAULT_VALUE;
            float aux_value_2 = DEFAULT_VALUE;
            
            for (int i = 0; i < i_frame_length; i++)
            {
                // Stochastic magnitudes are stored in dB; crossfade in linear
                // amplitude so missing data fades to/from silence smoothly
                if (frame_type == FrameType::Stochastic or frame_type == FrameType::Sinusoids)
                {
                    const float amp_1 = (i < frame_1.size() and abs (frame_1[i]) != 0.0)
                                        ? std::pow (10.0f, frame_1[i] / 20.0f) : 0.0f;
                    const float amp_2 = (i < frame_2.size() and abs (frame_2[i]) != 0.0)
                                        ? std::pow (10.0f, frame_2[i] / 20.0f) : 0.0f;
                    const float amp = amp_1 * (1.0f - interp_factor) + amp_2 * interp_factor;
                    interpolated_frame[i] = (amp > 0.0f) ? 20.0f * std::log10 (amp) : DEFAULT_DB;
                }
                else if (frame_type == FrameType::Waveform)
                {
                    // Waveform data crossfades against silence (0.0) when a
                    // sound has no attack/residual data for this frame
                    const float v_1 = (i < frame_1.size()) ? frame_1[i] : 0.0f;
                    const float v_2 = (i < frame_2.size()) ? frame_2[i] : 0.0f;
                    interpolated_frame[i] = interp_factor * v_2 + (1.0f - interp_factor) * v_1;
                }
                else if (stop_at_shortest and (frame_1.size() == 0 or frame_2.size() == 0))
                {
                    interpolated_frame[i] = DEFAULT_VALUE;
                }
                else
                {
                    if (is_magnitude_based)
                    {
                        if (i < frame_1.size() and abs (frame_1[i]) != 0.0) aux_value_1 = frame_1[i];
                        else aux_value_1 = DEFAULT_VALUE;
                        
                        if (i < frame_2.size() and abs (frame_2[i]) != 0.0) aux_value_2 = frame_2[i];
                        else aux_value_2 = DEFAULT_VALUE;
                    }
                    else
                    {
                        if (i < frame_1.size()) aux_value_1 = frame_1[i];
                        else aux_value_1 = DEFAULT_VALUE;
                        
                        if (i < frame_2.size()) aux_value_2 = frame_2[i];
                        else aux_value_2 = DEFAULT_VALUE;
                    }
                    
                    interpolated_frame[i] = interp_factor * aux_value_2 + (1-interp_factor) * aux_value_1;
                }
            }
        }
        
        return interpolated_frame;
    }

    // Bilinear interpolation for 4-voice morphing
    std::vector<float> Instrument::bilinearInterpolateFrames (FrameType frame_type, 
                                                               float x_factor, float y_factor,
                                                               const std::vector<float>& frame_00, // LeftLow
                                                               const std::vector<float>& frame_10, // RightLow
                                                               const std::vector<float>& frame_01, // LeftHigh
                                                               const std::vector<float>& frame_11, // RightHigh
                                                               int i_frame_length)
    {
        // Stochastic magnitudes are stored in dB (like Magnitudes), so missing
        // data must default to DEFAULT_DB (silence) and not 0.0 (full scale)
        const bool is_magnitude_based = (frame_type == FrameType::Magnitudes or
                                         frame_type == FrameType::Stochastic or
                                         frame_type == FrameType::Sinusoids);
        
        float DEFAULT_VALUE = is_magnitude_based ? DEFAULT_DB : DEFAULT_HZ;
        
        // Output
        std::vector<float> interpolated_frame (i_frame_length, DEFAULT_VALUE);
        
        if (i_frame_length == 0) return interpolated_frame;
        
        // Precomputed bilinear weights (hoisted out of the per-index loops)
        const float w00 = (1.0f - x_factor) * (1.0f - y_factor);
        const float w10 = x_factor * (1.0f - y_factor);
        const float w01 = (1.0f - x_factor) * y_factor;
        const float w11 = x_factor * y_factor;
        
        // Stochastic magnitudes are stored in dB; crossfade in linear amplitude
        // so missing corners fade to/from silence smoothly
        if (frame_type == FrameType::Stochastic or frame_type == FrameType::Sinusoids)
        {
            for (int i = 0; i < i_frame_length; i++)
            {
                const float amp00 = (i < frame_00.size() && abs (frame_00[i]) != 0.0)
                                    ? std::pow (10.0f, frame_00[i] / 20.0f) : 0.0f;
                const float amp10 = (i < frame_10.size() && abs (frame_10[i]) != 0.0)
                                    ? std::pow (10.0f, frame_10[i] / 20.0f) : 0.0f;
                const float amp01 = (i < frame_01.size() && abs (frame_01[i]) != 0.0)
                                    ? std::pow (10.0f, frame_01[i] / 20.0f) : 0.0f;
                const float amp11 = (i < frame_11.size() && abs (frame_11[i]) != 0.0)
                                    ? std::pow (10.0f, frame_11[i] / 20.0f) : 0.0f;
                
                const float amp = amp00 * w00 + amp10 * w10 + amp01 * w01 + amp11 * w11;
                
                interpolated_frame[i] = (amp > 0.0f) ? 20.0f * std::log10 (amp) : DEFAULT_DB;
            }
            
            return interpolated_frame;
        }
        
        // Waveform data crossfades against silence (0.0) when a corner has no
        // attack/residual data for this frame
        if (frame_type == FrameType::Waveform)
        {
            for (int i = 0; i < i_frame_length; i++)
            {
                const float v00 = (i < frame_00.size()) ? frame_00[i] : 0.0f;
                const float v10 = (i < frame_10.size()) ? frame_10[i] : 0.0f;
                const float v01 = (i < frame_01.size()) ? frame_01[i] : 0.0f;
                const float v11 = (i < frame_11.size()) ? frame_11[i] : 0.0f;
                
                interpolated_frame[i] = v00 * w00 + v10 * w10 + v01 * w01 + v11 * w11;
            }
            
            return interpolated_frame;
        }
        
        // Ensure all frames have data
        bool has_all_frames = (!frame_00.empty() && !frame_10.empty() && 
                               !frame_01.empty() && !frame_11.empty());
        
        // Non-magnitude data with missing corners inherits the heaviest
        // non-empty corner (matches the 2-sound fallback).
        const std::vector<float>* fallback_frame = nullptr;
        if (!has_all_frames && !is_magnitude_based)
        {
            float max_weight = 0.0f;
            if (!frame_00.empty() && w00 >= max_weight) { max_weight = w00; fallback_frame = &frame_00; }
            if (!frame_10.empty() && w10 >= max_weight) { max_weight = w10; fallback_frame = &frame_10; }
            if (!frame_01.empty() && w01 >= max_weight) { max_weight = w01; fallback_frame = &frame_01; }
            if (!frame_11.empty() && w11 >= max_weight) { max_weight = w11; fallback_frame = &frame_11; }
        }
        
        for (int i = 0; i < i_frame_length; i++)
        {
            float v00 = DEFAULT_VALUE;
            float v10 = DEFAULT_VALUE;
            float v01 = DEFAULT_VALUE;
            float v11 = DEFAULT_VALUE;
            
            if (is_magnitude_based)
            {
                if (i < frame_00.size() && abs (frame_00[i]) != 0.0) v00 = frame_00[i];
                if (i < frame_10.size() && abs (frame_10[i]) != 0.0) v10 = frame_10[i];
                if (i < frame_01.size() && abs (frame_01[i]) != 0.0) v01 = frame_01[i];
                if (i < frame_11.size() && abs (frame_11[i]) != 0.0) v11 = frame_11[i];
            }
            else
            {
                if (i < frame_00.size())
                    v00 = frame_00[i];
                else if (fallback_frame && i < fallback_frame->size())
                    v00 = (*fallback_frame)[i];
                
                if (i < frame_10.size())
                    v10 = frame_10[i];
                else if (fallback_frame && i < fallback_frame->size())
                    v10 = (*fallback_frame)[i];
                
                if (i < frame_01.size())
                    v01 = frame_01[i];
                else if (fallback_frame && i < fallback_frame->size())
                    v01 = (*fallback_frame)[i];
                
                if (i < frame_11.size())
                    v11 = frame_11[i];
                else if (fallback_frame && i < fallback_frame->size())
                    v11 = (*fallback_frame)[i];
            }
            
            // Bilinear interpolation formula:
            // v = v00*(1-x)*(1-y) + v10*x*(1-y) + v01*(1-x)*y + v11*x*y
            interpolated_frame[i] = v00 * w00 + v10 * w10 + v01 * w01 + v11 * w11;
        }
        
        return interpolated_frame;
    }
} // namespace Core
