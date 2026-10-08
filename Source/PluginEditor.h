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
#include <cstdint>
#include "PluginProcessor.h"

#include "Helpers/InterfaceDefines.h"
#include "Panels/MorphexPanel.h"

extern "C" int morphexGetFrameExtents (std::uintptr_t windowH,
                                       int* outFrameW, int* outFrameH);

static juce::Point<int> getNativeFrameSize (juce::Component* topLevelWindow)
{
    if (topLevelWindow != nullptr)
        if (auto* peer = topLevelWindow->getPeer())
            if (peer->getNativeHandle() != nullptr)
            {
                int frameW = 0, frameH = 0;

                if (morphexGetFrameExtents (reinterpret_cast<std::uintptr_t> (peer->getNativeHandle()),
                                            &frameW, &frameH) != 0)
                    return { frameW, frameH };
            }

    return {};
}

//==============================================================================
/**
*/
class SpectralMorphingToolAudioProcessorEditor  : public AudioProcessorEditor,
                                                 public juce::AudioProcessorValueTreeState::Listener
{
public:
    SpectralMorphingToolAudioProcessorEditor (SpectralMorphingToolAudioProcessor&);
    ~SpectralMorphingToolAudioProcessorEditor() override;

    //==============================================================================
    void paint (Graphics&) override;
    void resized() override;

    void parameterChanged (const juce::String& parameterID, float) override
    {
        if (parameterID != Morphex::Zoom::UI_SCALE_ID)
            return;
        applyZoom (readZoomScale());
    }

    void applyZoom (float scale)
    {
        scale = juce::jlimit (Morphex::Zoom::ZOOM_MIN, Morphex::Zoom::ZOOM_MAX, scale);
        uiScale = scale;
        MorphexZoom::uiScale = scale;
        updateZoomLimits();

        const int pixW = juce::roundToInt ((float) MORPHEX_PANEL_WIDTH * uiScale);
        const int pixH = juce::roundToInt ((float) MORPHEX_PANEL_HEIGHT * uiScale);

        setSize (pixW, pixH);
        resized();
        repaint();
    }

    void updateZoomLimits()
    {
        setResizeLimits (juce::roundToInt ((float) MORPHEX_PANEL_WIDTH * Morphex::Zoom::ZOOM_MIN),
                         juce::roundToInt ((float) MORPHEX_PANEL_HEIGHT * Morphex::Zoom::ZOOM_MIN),
                         juce::roundToInt ((float) MORPHEX_PANEL_WIDTH * Morphex::Zoom::ZOOM_MAX),
                         juce::roundToInt ((float) MORPHEX_PANEL_HEIGHT * Morphex::Zoom::ZOOM_MAX));
        setResizable (false, false);
    }

    void parentHierarchyChanged() override
    {
        if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
            return;

        if (topLevelIsWindow)
            return;

        if (auto* rw = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        {
            topLevelIsWindow = true;
            // Synchronous calls (see docs/ERRATA.md): deferring to callAsync
            // reintroduces the black overhang.
            rw->setColour (juce::ResizableWindow::backgroundColourId, GUI::Color::Background);
            rw->setUsingNativeTitleBar (true);

           #if JUCE_WINDOWS
            if (auto* peer = getPeer())
                peer->setCustomPlatformScaleFactor (1.0f);
           #endif
        }
    }

private:
    float readZoomScale() const
    {
        if (auto* param = processor.parameters.getParameter (Morphex::Zoom::UI_SCALE_ID))
        {
            const int idx = juce::jlimit (0, Morphex::Zoom::ZOOM_COUNT - 1,
                                          juce::roundToInt (param->getValue()
                                              * (float) (Morphex::Zoom::ZOOM_COUNT - 1)));
            return Morphex::Zoom::ZOOM_PERCENTS[idx] / 100.0f;
        }
        return 1.0f;
    }

    SpectralMorphingToolAudioProcessor& processor;

    std::unique_ptr<MorphexPanel> morphexPanel;
    float uiScale = 1.0f;

    bool topLevelIsWindow = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralMorphingToolAudioProcessorEditor)
};
