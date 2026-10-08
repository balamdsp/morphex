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

#include "Note.h"

#include <atomic>
#include <memory>
#include <vector>

namespace Core
{
    class Instrument;
    
    enum MorphLocation
    {
        // X/Y Pad quadrant positions (4-voice morphing)
        LeftLow = 0,      // Bottom-left quadrant (X=0, Y=0)
        RightLow,         // Bottom-right quadrant (X=1, Y=0)
        LeftHigh,         // Top-left quadrant (X=0, Y=1)
        RightHigh,        // Top-right quadrant (X=1, Y=1)
        NUM_MORPH_LOCATIONS  // = 4 total morph locations
        
        // Backward compatibility aliases
        , Left = LeftLow   // Map old Left to LeftLow
        , Right = RightLow // Map old Right to RightLow
    };
    
    typedef std::array<Note*, MorphLocation::NUM_MORPH_LOCATIONS> MorphNotes;
    // Voices snapshot sounds at note-on (survive preset switches); grid
    // Sounds use a no-op deleter.
    typedef std::array<std::shared_ptr<Sound>, MorphLocation::NUM_MORPH_LOCATIONS> MorphSounds;
    typedef std::array<Sound::Frame, MorphLocation::NUM_MORPH_LOCATIONS> MorphSoundFrames;

    // Per-slot playback state passed from Voice to morphSoundFrames.
    struct SlotCursor
    {
        int frame = 0;          // resolved frame index (loop-windowed/clamped)
        float formantSt = 0.0f; // slot formant shift, semitones (D8-scaled for Cross)
    };
    
    const static int NUM_MIDI_NOTES = 128;
}

class Core::Instrument
{
public:
    
    enum class Mode
    {
        Morphing = 0,
        FullRange
    };
    
    enum class Interpolation
    {
        None = 0,
        Manual,
        FrequencyBased
    };
    
    enum FrameType
    {
        Frequencies = 0,    // Interpolate Frequencies
        Magnitudes,         // Interpolate Magnitudes
        Stochastic,         // Interpolate Stochastic
        Waveform,           // Interpolate Waveforms
        Sinusoids,          // Interpolate Sinusoid magnitudes
    };
    
    struct Generate
    {
        bool harmonic = true;
        bool sinusoidal = true;
        bool stochastic = true;
        bool attack = true;
        bool residual = false;
    };
    Generate generate;
    
    // Notes
    std::vector<Note*> note;
    
    // Data
    std::string name;
    std::string samples_dirpath;
    int num_samples_loaded = 0;

    // Mode
    Mode mode = Mode::Morphing;

    // Interpolation
    Interpolation interpolation_mode = Interpolation::Manual;

    // Time-scrub: bypassed (normal playback) when disabled, driven by the
    // time_scrub_rate parameter when enabled.
    bool time_scrub_enabled = false;
    
    // Note looping: when disabled, the note plays through the shared material
    // once and then stops (no sustain-region loop).
    bool sound_looping = true;

    // Slot playhead display (loader rail): array of 4 atomics owned by the
    // processor, published lock-free by voices. Null in headless use.
    std::atomic<float>* slotPhaseDisplay = nullptr;

    // Pitch lock renders each slot at its own original note (transpose/fine
    // still apply as an offset on top of the original tuning).
    bool pitchLocked = false;
    
    Instrument();
    ~Instrument();
    
    void init();
    void reset();
    
    void loadSound (std::string file_path, MorphLocation morph_location = MorphLocation::NUM_MORPH_LOCATIONS);

    // Split for async loading: decodeSound is pure/worker-safe, installSound
    // runs on the message thread (atomic shared_ptr swap, no audio race).
    static std::shared_ptr<Sound> decodeSound (const std::string& file_path);
    void installSound (std::shared_ptr<Sound> decoded, MorphLocation morph_location);
    
    std::vector<Note*> getLoadedNotes();
    
    MorphNotes getCloserNotes (float f_target_note);
    MorphSounds getCloserSounds (float f_target_note, float f_velocity);
    
    MorphNotes getMorphNotes();
    void setMorphNote (Note* note, MorphLocation morph_location, int midi_velocity = MAX_MIDI_VELOCITY);
    MorphSounds getMorphSounds();
    std::shared_ptr<Sound> getMorphSound (MorphLocation morph_location);
    void setMorphSound (std::shared_ptr<Sound> sound, MorphLocation morph_location);
    void clearSound (MorphLocation morph_location);

    Sound* getSound (float f_note, float f_velocity);
    Sound::Frame getSoundFrame (float f_note, float f_velocity, int i_current_frame, int i_frame_length, float f_freqs_interp_factor, float f_mags_interp_factor);

    // Per-slot cursors (Voice-resolved); xFreq/xAmp blends, y shared,
    // formant shifts ride along. f_note_offset_st is the transpose/fine
    // offset applied on top of a pitch-locked slot's original note.
    Sound::Frame morphSoundFrames (float f_target_note, const MorphSounds& morph_sounds,
                                   const SlotCursor cursors[4], int i_frame_length,
                                   float x_freq_factor, float x_amp_factor, float y_factor,
                                   float f_note_offset_st = 0.0f);

    // Per-frame magnitude-envelope formant shift (resonances move, pitch does
    // not): mag(f) = env(f * 2^(-st/12)), corrections clamped to +-24 dB.
    static void applyFormantShift (std::vector<float>& mags, const std::vector<float>& freqs,
                                   float shiftSt);

    // Corner index (LL/RL/LH/RH) to pad-x; one slot's bilinear weight.
    static float cornerX (int cornerIdx) noexcept;
    static float cornerWeight (int slot, float x, float y) noexcept;
    
    std::vector<float> getNextFrame (float f_note, float f_velocity, int i_frame_length,
                                     float f_freqs_interp_factor, float f_mags_interp_factor);
    
    std::vector<float> interpolateFrames (FrameType frame_type, float interp_factor,
                                          const std::vector<float>& frame_1, const std::vector<float>& frame_2,
                                          int i_frame_length);
    
    // Bilinear interpolation for 4-voice morphing
    std::vector<float> bilinearInterpolateFrames (FrameType frame_type, 
                                                   float x_factor, float y_factor,
                                                   const std::vector<float>& frame_00, const std::vector<float>& frame_10,
                                                   const std::vector<float>& frame_01, const std::vector<float>& frame_11,
                                                   int i_frame_length);
private:
    
    MorphNotes morph_notes;
    MorphSounds morph_sounds {};
};
