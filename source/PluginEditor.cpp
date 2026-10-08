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

//==============================================================================
SpectralMorphingToolAudioProcessorEditor::SpectralMorphingToolAudioProcessorEditor (SpectralMorphingToolAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    morphexPanel = std::make_unique<MorphexPanel>(&p);
    addAndMakeVisible (morphexPanel.get());

#if JUCE_ANDROID || JUCE_IOS
    setFullScreen (true);
#else
    setResizable (false, false);

    uiScale = readZoomScale();
    MorphexZoom::uiScale = uiScale;
    updateZoomLimits();
    applyZoom (uiScale);

    processor.parameters.addParameterListener (Morphex::Zoom::UI_SCALE_ID, this);
#endif
}

SpectralMorphingToolAudioProcessorEditor::~SpectralMorphingToolAudioProcessorEditor()
{
#if ! (JUCE_ANDROID || JUCE_IOS)
    processor.parameters.removeParameterListener (Morphex::Zoom::UI_SCALE_ID, this);
#endif
}

//==============================================================================
void SpectralMorphingToolAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (GUI::Color::Background);
}

void SpectralMorphingToolAudioProcessorEditor::resized()
{
    morphexPanel->setBounds (0, 0, getWidth(), getHeight());
}
