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

#include "MainPanel.h"
#include "TopPanel.h"
#include "AnalyzerWindow.h"

#include "../Components/CRTScreen.h"
#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/InterfaceDefines.h"
#include "../Helpers/SMTConstants.h"

struct AnchoredTooltipWindow : public juce::TooltipWindow
{
    void setTipsEnabled (bool enabled) noexcept
    {
        if (tipsEnabled != enabled)
        {
            tipsEnabled = enabled;
            if (! tipsEnabled)
                hideTip();
            else
                repaint();
        }
    }

    juce::String getTipFor (juce::Component& c) override
    {
        if (! tipsEnabled)
            return {};
        if (auto* parent = getParentComponent())
        {
            const auto r = parent->getLocalArea (&c, c.getLocalBounds());
            CustomLookAndFeel::TooltipAnchor::pos = r.getCentre();
            CustomLookAndFeel::TooltipAnchor::valid = true;
        }
        return juce::TooltipWindow::getTipFor (c);
    }

private:
    bool tipsEnabled = true;
};

class MorphexPanel : public Component,
                     public DragAndDropContainer
{
public:

    MorphexPanel (SpectralMorphingToolAudioProcessor* inProcessor)
        : mProcessor (inProcessor),
          screen (inProcessor),
          crtOverlay (&screen, &inProcessor->getCrtEnabledFlag())
    {
        static CustomLookAndFeel customLookAndFeel;
        setLookAndFeel (&customLookAndFeel);
        juce::LookAndFeel::setDefaultLookAndFeel (&customLookAndFeel);

        addAndMakeVisible (screen);
        addAndMakeVisible (crtOverlay);
        crtOverlay.setStrengthSource (&inProcessor->getCrtStrengthFlag());
        crtOverlay.setCrtStrength (inProcessor->getCrtStrength());
        crtOverlay.toBehind (&screen);
        crtOverlay.toFront (false);

        setSize (juce::roundToInt ((float) MORPHEX_PANEL_WIDTH * MorphexZoom::uiScale),
                 juce::roundToInt ((float) MORPHEX_PANEL_HEIGHT * MorphexZoom::uiScale));
    }

    ~MorphexPanel() override
    {
    }

    void paint (Graphics& g) override
    {
        g.fillAll (GUI::Color::Background);
    }

    void resized() override
    {

        crtOverlay.setBounds (getLocalBounds());
        screen.setBounds (getLocalBounds());

        const float pad = CRTScreen::getFrameSize();
        const float scale = 1.0f / (1.0f + 2.0f * pad);
        const float tx = getWidth()  * 0.5f * (1.0f - scale);
        const float ty = getHeight() * 0.5f * (1.0f - scale);

        screen.setTransform (mProcessor != nullptr && mProcessor->isCrtEnabled()
                                 ? juce::AffineTransform::scale (scale, scale).translated (tx, ty)
                                 : juce::AffineTransform());
    }

private:

    struct Screen : public Component, private juce::Value::Listener
    {
        Screen (SpectralMorphingToolAudioProcessor* inProcessor)
            : topPanel (inProcessor),
              mainPanel (inProcessor),
              analyzerWindow (inProcessor),
              mProcessor (inProcessor)
        {
            addAndMakeVisible (topPanel);
            addAndMakeVisible (mainPanel);
            addAndMakeVisible (analyzerWindow);
            analyzerWindow.setVisible (false);
            addChildComponent (tooltipWindow);

            mProcessor->uiModeValue.addListener (this);
            mProcessor->tooltipDelayMsValue.addListener (this);
            mProcessor->tooltipsEnabledValue.addListener (this);
            applyUiMode();
            applyTooltipSettings();
        }

        ~Screen() override
        {
            if (mProcessor != nullptr)
            {
                mProcessor->uiModeValue.removeListener (this);
                mProcessor->tooltipDelayMsValue.removeListener (this);
                mProcessor->tooltipsEnabledValue.removeListener (this);
            }
        }

        void paint (Graphics& g) override {}

        void resized() override
        {
            FlexBox fb;
            fb.flexDirection = FlexBox::Direction::column;

            float topPanelHeight = getHeight() / 8.0f;

            FlexItem top (static_cast<float> (getWidth()), topPanelHeight, topPanel);

            auto contentBounds = getLocalBounds().withTrimmedTop ((int) topPanelHeight);
            mainPanel.setBounds (contentBounds);
            analyzerWindow.setBounds (contentBounds);

            fb.items.add (top);
            fb.performLayout (getLocalBounds().toFloat());
        }

        void valueChanged (juce::Value& value) override
        {
            if (value.refersToSameSourceAs (mProcessor->uiModeValue))
                applyUiMode();
            else
                applyTooltipSettings();
        }

    private:
        void applyUiMode()
        {
            const bool analyze = mProcessor != nullptr && mProcessor->getUiMode() == "analyze";
            mainPanel.setVisible (! analyze);
            analyzerWindow.setVisible (analyze);
        }

        void applyTooltipSettings()
        {
            if (mProcessor == nullptr)
                return;
            tooltipWindow.setMillisecondsBeforeTipAppears (
                juce::jmax (0, mProcessor->getTooltipDelayMs()));
            tooltipWindow.setTipsEnabled (mProcessor->getTooltipsEnabled());
        }

    public:
        TopPanel topPanel;
        MainPanel mainPanel;
        AnalyzerWindow analyzerWindow;

    private:
        SpectralMorphingToolAudioProcessor* mProcessor = nullptr;
        AnchoredTooltipWindow tooltipWindow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Screen)
    };

    SpectralMorphingToolAudioProcessor* mProcessor = nullptr;
    Screen screen;
    CRTScreen crtOverlay { &screen, nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphexPanel)
};
