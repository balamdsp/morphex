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
#include <memory>

#include "../Helpers/InterfaceDefines.h"
#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/SMTConstants.h"

namespace Morphex { class Slider; }

class Morphex::Slider : public juce::Slider
{
public:

    Slider (AudioProcessorValueTreeState& state_to_control,
            const Morphex::Parameters parameter_num,
            const Slider::SliderStyle slider_style = Slider::SliderStyle::RotaryHorizontalVerticalDrag)
    :   juce::Slider (),
        mParameterNum (parameter_num)
    {
        Morphex::Parameter<float> morphex_parameter = Morphex::PARAMETERS<float>[parameter_num];

        setName (morphex_parameter.label);

        setSliderStyle (slider_style);

        applyTextBoxStyle();

        setRange (morphex_parameter.min_value, morphex_parameter.max_value, 0.001f);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxHighlightColourId, GUI::Color::AccentDim);

        mAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>
                      (state_to_control, morphex_parameter.ID, *this);

        updateText();
    }

    ~Slider() override {}

    void resized() override
    {
        juce::Slider::resized();
        if (! juce::approximatelyEqual (MorphexZoom::uiScale, lastTextBoxScale))
            applyTextBoxStyle();
    }

    void lookAndFeelChanged() override
    {
        juce::Slider::lookAndFeelChanged();
        for (auto* child : getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setFont (CustomLookAndFeel::makeFont (valueBoxFontSize));
    }

    void setValueBoxFontSize (float s) noexcept
    {
        valueBoxFontSize = s;
        lookAndFeelChanged();
    }
    
    String getTextFromValue (double value) override
    {
        switch (mParameterNum)
        {
            case Morphex::Parameters::OutputGain:
                if (std::abs (value - std::round (value)) < 0.001)
                    return String (roundToInt (value)) + " dB";
                
                return String (value, 1) + " dB";
                
            case Morphex::Parameters::asdr_attack:
            case Morphex::Parameters::asdr_decay:
            case Morphex::Parameters::asdr_release:
                return String (value, 2) + " s";
                
            case Morphex::Parameters::asdr_sustain:
                return String (value, 2);
                
            case Morphex::Parameters::time_scrub_rate:
                return String (roundToInt (value)) + "%";
                
            case Morphex::Parameters::pitch_bend_range:
                return String (roundToInt (value)) + " st";

            case Morphex::Parameters::pad_smoothing_ms:
                return String (roundToInt (value)) + " ms";

            case Morphex::Parameters::transient_preserve:
                return String (roundToInt (value)) + "%";

            case Morphex::Parameters::stocs_gain:
                return String (value, 1) + " dB";

            case Morphex::Parameters::morph_freq_trim:
            case Morphex::Parameters::morph_amp_trim:
                return String (roundToInt (value * 100.0)) + "%";
                
            default:
                return juce::Slider::getTextFromValue (value);
        }
    }
    
    double getValueFromText (const String& text) override
    {
        if (mParameterNum == Morphex::Parameters::OutputGain)
            return text.retainCharacters ("-+.0123456789").getDoubleValue();

        if (mParameterNum == Morphex::Parameters::morph_freq_trim
            || mParameterNum == Morphex::Parameters::morph_amp_trim)
        {
            // Textbox shows percent; accept both 0.25 and 25%.
            const double v = text.retainCharacters ("-+.0123456789").getDoubleValue();
            return std::abs (v) > 1.0 ? v / 100.0 : v;
        }

        return juce::Slider::getValueFromText (text);
    }
    
private:

    void applyTextBoxStyle()
    {
        const float s = MorphexZoom::uiScale;
        if (getSliderStyle() == Slider::SliderStyle::LinearHorizontal)
            setTextBoxStyle (Slider::TextEntryBoxPosition::TextBoxRight, false,
                             juce::roundToInt (54.0f * s), juce::roundToInt (20.0f * s));
        else
            setTextBoxStyle (Slider::TextEntryBoxPosition::TextBoxBelow, false, 0, 0);
        lastTextBoxScale = s;
    }

    const Morphex::Parameters mParameterNum;

    float valueBoxFontSize = 20.0f;
    float lastTextBoxScale = 0.0f;

    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> mAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Slider);
};
