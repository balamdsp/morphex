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

// TODO - Put everything in a namespace called "Morphex::Parameters"

namespace Morphex
{
    template<typename T>
    struct Parameter
    {
        String ID;
        String label;
        T min_value;
        T max_value;
        T default_value;
    };

    // Rename them and call them like: SMTParameter::FreqsInterpFactor
    enum Parameters
    {
        OutputGain = 0,
        freqs_interp_factor,
        mags_interp_factor,
        stocs_interp_factor,
        stocs_gain,
        asdr_attack,
        asdr_decay,
        asdr_sustain,
        asdr_release,
        time_scrub_rate,
        pitch_bend_range,
        slot1_rate,
        slot1_offset,
        slot1_loopstart,
        slot1_loopend,
        slot1_reverse,
        slot2_rate,
        slot2_offset,
        slot2_loopstart,
        slot2_loopend,
        slot2_reverse,
        slot3_rate,
        slot3_offset,
        slot3_loopstart,
        slot3_loopend,
        slot3_reverse,
        slot4_rate,
        slot4_offset,
        slot4_loopstart,
        slot4_loopend,
        slot4_reverse,
        slot1_formant,
        slot2_formant,
        slot3_formant,
        slot4_formant,
        slot1_loopmode,
        slot2_loopmode,
        slot3_loopmode,
        slot4_loopmode,
        pad_smoothing_ms,
        transpose_st,
        fine_tune_cents,
        glide_time_ms,
        forward_only,
        transient_preserve,
        cross_mode,
        cross_freq_corner,
        cross_amp_corner,
        cross_form_corner,
        morph_freq_trim,
        morph_amp_trim,
        TotalNumParameters
    };

    //typedef std::map<int, Parameter> IndexedParameters;ç
    
//    params.push_back(std::make_unique<AudioParameterFloat>(things));
//    params.push_back(std::make_unique<AudioParameterBool>(things));

    template <typename T>
    using IndexedParameters = std::map<int, Parameter<T>>;

    //static std::map<int, Parameter> smt_parameters =
    template <typename T>
    static IndexedParameters<T> PARAMETERS =
    {
    //  index                   ID                      label                       min_value   max_value   default_value
        {OutputGain,            {"OutputGain",          "Master",                   -60.0f,     12.0f,      0.0f}   },
        {freqs_interp_factor,   {"FreqsInterpFactor",   "Morph X Position",         0.0f,       1.0f,       0.5f}   },
        {mags_interp_factor,    {"MagsInterpFactor",    "Morph Y Position",         0.0f,       1.0f,       0.5f}   },
        {stocs_interp_factor,   {"StocsInterpFactor",   "Stochastic Component",     0.0f,       1.0f,       0.5f}   },
        {stocs_gain,            {"StocsGain",           "Stochastic Gain",          -24.0f,     6.0f,       0.0f}   },
        {asdr_attack,           {"ADSRAttack",          "Attack",                   0.01f,      5.0f,       0.1f}   },
        {asdr_decay,            {"ADSRDecay",           "Decay",                    0.01f,      2.0f,       0.8f}   },
        {asdr_sustain,          {"ADSRSustain",         "Sustain",                  0.0f,       1.0f,       0.8f}   },
        {asdr_release,          {"ADSRRelease",         "Release",                  0.01f,      5.0f,       0.1f}   },
        {time_scrub_rate,       {"TimeScrubRate",       "Time Scrub",               -400.0f,    400.0f,     100.0f}   },
        {pitch_bend_range,      {"PitchBendRange",      "Pitch Bend",               1.0f,       24.0f,      2.0f}   },
        {slot1_rate,            {"Slot1Rate",           "Slot 1 Rate",              0.0f,       200.0f,     100.0f} },
        {slot1_offset,          {"Slot1Offset",         "Slot 1 Offset",            0.0f,       100.0f,     0.0f}   },
        {slot1_loopstart,       {"Slot1LoopStart",      "Slot 1 Loop Start",        0.0f,       100.0f,     0.0f}   },
        {slot1_loopend,         {"Slot1LoopEnd",        "Slot 1 Loop End",          0.0f,       100.0f,     100.0f} },
        {slot1_reverse,         {"Slot1Reverse",        "Slot 1 Reverse",           0.0f,       1.0f,       0.0f}   },
        {slot2_rate,            {"Slot2Rate",           "Slot 2 Rate",              0.0f,       200.0f,     100.0f} },
        {slot2_offset,          {"Slot2Offset",         "Slot 2 Offset",            0.0f,       100.0f,     0.0f}   },
        {slot2_loopstart,       {"Slot2LoopStart",      "Slot 2 Loop Start",        0.0f,       100.0f,     0.0f}   },
        {slot2_loopend,         {"Slot2LoopEnd",        "Slot 2 Loop End",          0.0f,       100.0f,     100.0f} },
        {slot2_reverse,         {"Slot2Reverse",        "Slot 2 Reverse",           0.0f,       1.0f,       0.0f}   },
        {slot3_rate,            {"Slot3Rate",           "Slot 3 Rate",              0.0f,       200.0f,     100.0f} },
        {slot3_offset,          {"Slot3Offset",         "Slot 3 Offset",            0.0f,       100.0f,     0.0f}   },
        {slot3_loopstart,       {"Slot3LoopStart",      "Slot 3 Loop Start",        0.0f,       100.0f,     0.0f}   },
        {slot3_loopend,         {"Slot3LoopEnd",        "Slot 3 Loop End",          0.0f,       100.0f,     100.0f} },
        {slot3_reverse,         {"Slot3Reverse",        "Slot 3 Reverse",           0.0f,       1.0f,       0.0f}   },
        {slot4_rate,            {"Slot4Rate",           "Slot 4 Rate",              0.0f,       200.0f,     100.0f} },
        {slot4_offset,          {"Slot4Offset",         "Slot 4 Offset",            0.0f,       100.0f,     0.0f}   },
        {slot4_loopstart,       {"Slot4LoopStart",      "Slot 4 Loop Start",        0.0f,       100.0f,     0.0f}   },
        {slot4_loopend,         {"Slot4LoopEnd",        "Slot 4 Loop End",          0.0f,       100.0f,     100.0f} },
        {slot4_reverse,         {"Slot4Reverse",        "Slot 4 Reverse",           0.0f,       1.0f,       0.0f}   },
        {slot1_formant,         {"Slot1Formant",        "Slot 1 Formant",           -12.0f,    12.0f,      0.0f}   },
        {slot2_formant,         {"Slot2Formant",        "Slot 2 Formant",           -12.0f,    12.0f,      0.0f}   },
        {slot3_formant,         {"Slot3Formant",        "Slot 3 Formant",           -12.0f,    12.0f,      0.0f}   },
        {slot4_formant,         {"Slot4Formant",        "Slot 4 Formant",           -12.0f,    12.0f,      0.0f}   },
        {slot1_loopmode,        {"Slot1LoopMode",       "Slot 1 Loop",              0.0f,       1.0f,       1.0f}   },
        {slot2_loopmode,        {"Slot2LoopMode",       "Slot 2 Loop",              0.0f,       1.0f,       1.0f}   },
        {slot3_loopmode,        {"Slot3LoopMode",       "Slot 3 Loop",              0.0f,       1.0f,       1.0f}   },
        {slot4_loopmode,        {"Slot4LoopMode",       "Slot 4 Loop",              0.0f,       1.0f,       1.0f}   },
        {pad_smoothing_ms,      {"PadSmoothingTime",    "Pad Smoothing",            0.0f,       1000.0f,    0.0f}   },
        {transpose_st,          {"TransposeSt",         "Transpose",                -12.0f,     12.0f,      0.0f}   },
        {fine_tune_cents,       {"FineTuneCents",       "Fine Tune",                -100.0f,    100.0f,     0.0f}   },
        {glide_time_ms,         {"GlideTime",           "Glide",                    0.0f,       1000.0f,    0.0f}   },
        {forward_only,          {"ForwardOnly",         "Forward Only",             0.0f,       1.0f,       0.0f}   },
        {transient_preserve,    {"TransientPreserve",   "Transient Preserve",       0.0f,       100.0f,     0.0f}   },
        {cross_mode,            {"CrossMode",           "Cross Mode",               0.0f,       1.0f,       0.0f}   },
        {cross_freq_corner,     {"CrossFreqCorner",     "Cross Freq Corner",        0.0f,       3.0f,       0.0f}   },
        {cross_amp_corner,      {"CrossAmpCorner",      "Cross Amp Corner",         0.0f,       3.0f,       3.0f}   },
        {cross_form_corner,     {"CrossFormCorner",     "Cross Form Corner",        0.0f,       3.0f,       3.0f}   },
        {morph_freq_trim,       {"MorphFreqTrim",       "Morph Freq Trim",          -0.5f,      0.5f,       0.0f}   },
        {morph_amp_trim,        {"MorphAmpTrim",        "Morph Amp Trim",           -0.5f,      0.5f,       0.0f}   }
    };

    template<typename T>
    inline Parameter<T> getParameterByID(String parameter_ID)
    {
        Parameter<T> found_parameter;
        
        auto result = std::find_if( PARAMETERS<T>.begin(), PARAMETERS<T>.end(), [parameter_ID](const auto& mo)
        {
//            return mo.second.parameter_ID == parameter_ID;
            return mo.second.ID == parameter_ID;
        });
        
        // Return variable if found
        if(result != PARAMETERS<T>.end())
            found_parameter = result->second;
        
        return found_parameter;
    }
    
    /**
     * Safely get parameter value from AudioProcessorValueTreeState
     * Returns defaultValue if parameter is not found (prevents crashes)
     */
    template<typename T>
    inline T getParameterValueSafe(AudioProcessorValueTreeState* params, const String& paramID, T defaultValue)
    {
        auto param = params->getParameter(paramID);
        if (param != nullptr)
            return (T)param->convertFrom0to1(param->getValue());
        return defaultValue;
    }

    namespace Zoom
    {
        inline constexpr const char* UI_SCALE_ID = "ui_scale";
        inline constexpr float ZOOM_PERCENTS[5] = { 100.0f, 125.0f, 150.0f, 200.0f, 300.0f };
        inline constexpr int ZOOM_COUNT = 5;
        inline constexpr int UI_SCALE_DEFAULT = 0;
        inline constexpr float ZOOM_MIN = 1.0f;
        inline constexpr float ZOOM_MAX = 3.0f;
    }
} // namespace Morphex
