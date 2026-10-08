#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <memory>

#include "../Components/Slider.h"
#include "../Components/SquareFader.h"
#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/InterfaceDefines.h"
#include "../Helpers/SMTConstants.h"
#include "../Helpers/SMTParameters.h"
#include "../PluginProcessor.h"

// ENGINE card: cross-mode selectors (APVTS-bound) + per-slot cursor note.
// TRANSIENTS card: note-on residual-sine emphasis (APVTS-bound).
class SettingsPanel : public Component,
                      private juce::AudioProcessorValueTreeState::Listener,
                      public SpectralMorphingToolAudioProcessor::SoundLoadListener
{
public:
    explicit SettingsPanel (SpectralMorphingToolAudioProcessor* inProcessor)
        : mProcessor (inProcessor)
    {
        // --- INTERFACE: delay fader + behavior toggles.
        // Zoom and CRT live in the hamburger menu.
        tipDelayFader = std::make_unique<SquareFader> (mProcessor->tooltipDelayMsValue,
                                                       "DELAY", 0.0, 2000.0, 50.0, " ms", 1.0, 0);
        tipDelayFader->setTooltip ("Tooltip delay in milliseconds -- 0 shows tips instantly");
        tipDelayFader->setFontScale (1.8f);
        tipDelayFader->setCaptionScale (0.78f);
        tipDelayFader->setSquareTrack (true);
        tipDelayFader->setTrackInsetU (4.0f);
        addAndMakeVisible (tipDelayFader.get());

        tooltipsToggle.setButtonText ("TOOLTIPS");
        tooltipsToggle.setClickingTogglesState (true);
        tooltipsToggle.setTooltip ("Show helper tips when hovering any control");
        tooltipsToggle.getToggleStateValue().referTo (mProcessor->tooltipsEnabledValue);
        tooltipsToggle.setLookAndFeel (&smallToggleLnf);
        addAndMakeVisible (tooltipsToggle);

        confirmToggle.setButtonText ("CONFIRM ON REMOVE");
        confirmToggle.setClickingTogglesState (true);
        confirmToggle.setTooltip ("Confirm before removing slot sounds");
        confirmToggle.getToggleStateValue().referTo (mProcessor->confirmDestructiveValue);
        confirmToggle.setLookAndFeel (&smallToggleLnf);
        addAndMakeVisible (confirmToggle);

        // --- ENGINE: cross mode (MORPH card pattern: XROSS toggle +
        // FREQ/AMP/FORM numbered toggles, SmallToggleLook).
        crossModeButton.setButtonText ("XROSS");
        crossModeButton.setClickingTogglesState (true);
        crossModeButton.setTooltip ("Cross mode -- pitch follows the FREQ slot, loudness the AMP slot (off = classic pad morph)");
        crossModeButton.setLookAndFeel (&smallToggleLnf);
        crossModeButton.onClick = [this]
        {
            if (auto* p = mProcessor->parameters.getParameter (
                    Morphex::PARAMETERS<float>[Morphex::Parameters::cross_mode].ID))
                p->setValueNotifyingHost (crossModeButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (crossModeButton);

        auto wireCornerButton = [this] (juce::ToggleButton& b, Morphex::Parameters param,
                                        const char* tag, const char* tip)
        {
            b.setTooltip (tip);
            b.setLookAndFeel (&smallToggleLnf);
            b.onClick = [this, param]
            {
                if (auto* p = mProcessor->parameters.getParameter (
                        Morphex::PARAMETERS<float>[param].ID))
                {
                    const int cur = juce::jlimit (0, 3, juce::roundToInt (p->getValue() * 3.0f));
                    p->setValueNotifyingHost ((float) ((cur + 1) % 4) / 3.0f);
                }
            };
            addAndMakeVisible (b);
            refreshCornerButton (b, param, tag);
        };

        wireCornerButton (crossFreqButton, Morphex::Parameters::cross_freq_corner, "FREQ",
                          "Cross frequency slot -- pitch comes from this slot in Cross mode (1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right)");
        wireCornerButton (crossAmpButton, Morphex::Parameters::cross_amp_corner, "AMP",
                          "Cross amplitude slot -- loudness comes from this slot in Cross mode (1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right)");
        wireCornerButton (crossFormButton, Morphex::Parameters::cross_form_corner, "FORM",
                          "Cross formant slot -- resonance shift follows this slot in Cross mode (1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right; audible only with per-slot FORMANT set)");
        refreshCrossModeButton();
        refreshCornerButton (crossFreqButton, Morphex::Parameters::cross_freq_corner, "FREQ");
        refreshCornerButton (crossAmpButton, Morphex::Parameters::cross_amp_corner, "AMP");
        refreshCornerButton (crossFormButton, Morphex::Parameters::cross_form_corner, "FORM");

        // --- ENGINE trims: Delay-style vertical faders with side captions.
        auto trimPercent = [] (double v)
        {
            return juce::String (juce::roundToInt (v * 100.0)) + "%";
        };
        freqTrimFader = std::make_unique<SquareFader> (
            mProcessor->parameters, Morphex::Parameters::morph_freq_trim,
            "FREQ", juce::String(), 1.0, 0, 0.001, trimPercent);
        freqTrimFader->setTooltip ("Morph freq trim -- shifts the resting morph X after glide, so the pad position stays put (morph mode; cross corners are absolute)");
        freqTrimFader->setFontScale (1.8f);
        freqTrimFader->setCaptionScale (0.78f);
        freqTrimFader->setSquareTrack (true);
        freqTrimFader->setTrackInsetU (4.0f);
        addAndMakeVisible (freqTrimFader.get());

        ampTrimFader = std::make_unique<SquareFader> (
            mProcessor->parameters, Morphex::Parameters::morph_amp_trim,
            "AMP", juce::String(), 1.0, 0, 0.001, trimPercent);
        ampTrimFader->setTooltip ("Morph amp trim -- shifts the resting morph Y after glide, so the pad position stays put (morph mode; cross corners are absolute)");
        ampTrimFader->setFontScale (1.8f);
        ampTrimFader->setCaptionScale (0.78f);
        ampTrimFader->setSquareTrack (true);
        ampTrimFader->setTrackInsetU (4.0f);
        addAndMakeVisible (ampTrimFader.get());
        mProcessor->parameters.addParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_mode].ID, this);
        mProcessor->parameters.addParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_freq_corner].ID, this);
        mProcessor->parameters.addParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_amp_corner].ID, this);
        mProcessor->parameters.addParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_form_corner].ID, this);
        mProcessor->parameters.addParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::transient_preserve].ID, this);
        // Corner toggle glyphs follow slot loads/removes as well as params.
        mProcessor->addSoundLoadListener (this);

        // --- SYNTHESIS: component generation toggles (moved from CorePanel
        // COMPONENTS card so CorePanel matches the pad/mid/voice shape).
        harmonicButton.setClickingTogglesState (true);
        sinusoidalButton.setClickingTogglesState (true);
        stochasticButton.setClickingTogglesState (true);
        attackButton.setClickingTogglesState (true);
        residualButton.setClickingTogglesState (true);

        auto& gen = mProcessor->mMorphexSynth.instrument.generate;
        harmonicButton.setToggleState (gen.harmonic, NotificationType::dontSendNotification);
        sinusoidalButton.setToggleState (gen.sinusoidal, NotificationType::dontSendNotification);
        stochasticButton.setToggleState (gen.stochastic, NotificationType::dontSendNotification);
        attackButton.setToggleState (gen.attack, NotificationType::dontSendNotification);
        residualButton.setToggleState (gen.residual, NotificationType::dontSendNotification);

        harmonicButton.onClick = [this] { updateGenerateFlag (harmonicButton.getToggleState(), 0); };
        sinusoidalButton.onClick = [this] { updateGenerateFlag (sinusoidalButton.getToggleState(), 1); };
        stochasticButton.onClick = [this] { updateGenerateFlag (stochasticButton.getToggleState(), 2); };
        attackButton.onClick = [this] { updateGenerateFlag (attackButton.getToggleState(), 3); };
        residualButton.onClick = [this] { updateGenerateFlag (residualButton.getToggleState(), 4); };

        harmonicButton.setTooltip ("Harmonic component -- pitched partials tracked from the analysis");
        sinusoidalButton.setTooltip ("Sinusoidal component -- residual tonal sines left after harmonic subtraction");
        stochasticButton.setTooltip ("Stochastic component -- filtered-noise residual from the analysis envelope");
        attackButton.setTooltip ("Attack component -- raw transient slices layered under the morph");
        residualButton.setTooltip ("Residual component -- raw leftover slices layered under the morph");

        addAndMakeVisible (harmonicButton);
        addAndMakeVisible (sinusoidalButton);
        addAndMakeVisible (stochasticButton);
        addAndMakeVisible (attackButton);
        addAndMakeVisible (residualButton);

        // --- TRANSIENTS: note-on residual-sine emphasis ---
        transPreserveSlider = std::make_unique<Morphex::Slider> (
            mProcessor->parameters, Morphex::Parameters::transient_preserve,
            juce::Slider::LinearHorizontal);
        transPreserveSlider->setTooltip ("Transient preserve -- boosts residual attack sines at note-on, decaying over ~80 ms (0% = off)");
        addAndMakeVisible (transPreserveSlider.get());

        transPreserveLabel.setText ("TRANS PRESERVE", NotificationType::dontSendNotification);
        transPreserveLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.5f));
        transPreserveLabel.setFont (CustomLookAndFeel::makeFont (19.0f));
        transPreserveLabel.setJustificationType (Justification::centredLeft);
        addAndMakeVisible (transPreserveLabel);

        stocGainSlider = std::make_unique<Morphex::Slider> (
            mProcessor->parameters, Morphex::Parameters::stocs_gain,
            juce::Slider::LinearHorizontal);
        stocGainSlider->setTooltip ("Stochastic gain -- residual-noise presence trim, -24 to +6 dB (0 = unity)");
        addAndMakeVisible (stocGainSlider.get());

        stocGainLabel.setText ("STOC GAIN", NotificationType::dontSendNotification);
        stocGainLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.5f));
        stocGainLabel.setFont (CustomLookAndFeel::makeFont (19.0f));
        stocGainLabel.setJustificationType (Justification::centredLeft);
        addAndMakeVisible (stocGainLabel);
    }

    ~SettingsPanel() override
    {
        mProcessor->removeSoundLoadListener (this);
        tooltipsToggle.setLookAndFeel (nullptr);
        confirmToggle.setLookAndFeel (nullptr);
        crossModeButton.setLookAndFeel (nullptr);
        crossFreqButton.setLookAndFeel (nullptr);
        crossAmpButton.setLookAndFeel (nullptr);
        crossFormButton.setLookAndFeel (nullptr);
        mProcessor->parameters.removeParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_mode].ID, this);
        mProcessor->parameters.removeParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_freq_corner].ID, this);
        mProcessor->parameters.removeParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_amp_corner].ID, this);
        mProcessor->parameters.removeParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::cross_form_corner].ID, this);
        mProcessor->parameters.removeParameterListener (
            Morphex::PARAMETERS<float>[Morphex::Parameters::transient_preserve].ID, this);
    }

    void parameterChanged (const String& parameterID, float) override
    {
        // Host automation can land off-thread; hop to the message thread.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SettingsPanel> (this),
                                          parameterID]
        {
            if (safe == nullptr)
                return;
            if (parameterID == Morphex::PARAMETERS<float>[Morphex::Parameters::cross_mode].ID)
                safe->refreshCrossModeButton();
            else if (parameterID == Morphex::PARAMETERS<float>[Morphex::Parameters::cross_freq_corner].ID)
                safe->refreshCornerButton (safe->crossFreqButton, Morphex::Parameters::cross_freq_corner, "FREQ");
            else if (parameterID == Morphex::PARAMETERS<float>[Morphex::Parameters::cross_amp_corner].ID)
                safe->refreshCornerButton (safe->crossAmpButton, Morphex::Parameters::cross_amp_corner, "AMP");
            else if (parameterID == Morphex::PARAMETERS<float>[Morphex::Parameters::cross_form_corner].ID)
                safe->refreshCornerButton (safe->crossFormButton, Morphex::Parameters::cross_form_corner, "FORM");
        });
    }

    void refreshCrossModeButton()
    {
        const bool xOn = readCrossParam (Morphex::Parameters::cross_mode, 0.0f) >= 0.5f;
        crossModeButton.setToggleState (xOn, NotificationType::dontSendNotification);
        for (auto* b : { &crossFreqButton, &crossAmpButton, &crossFormButton })
        {
            // Clicks stay gated on XROSS, but the buttons keep readable text
            // instead of fading out (0.6, not the usual 0.35 dim).
            b->setEnabled (xOn);
            b->setAlpha (xOn ? 1.0f : 0.6f);
        }
        repaint();
    }

    void refreshCornerButton (juce::ToggleButton& b, Morphex::Parameters param, const char* tag)
    {
        const int idx = juce::jlimit (0, 3, juce::roundToInt (readCrossParam (param, 0.0f)));
        b.setButtonText (juce::String (tag) + " " + juce::String (idx + 1));
        // Corner glyphs: active brackets while the slot holds a sound.
        const auto sound = mProcessor->mMorphexSynth.instrument.getMorphSound (
            (MorphLocation) idx);
        b.setToggleState (sound != nullptr && sound->loaded,
                          juce::NotificationType::dontSendNotification);
    }

    void soundLoadFinished (MorphLocation, bool, const std::string&) override
    {
        refreshCornerButton (crossFreqButton, Morphex::Parameters::cross_freq_corner, "FREQ");
        refreshCornerButton (crossAmpButton, Morphex::Parameters::cross_amp_corner, "AMP");
        refreshCornerButton (crossFormButton, Morphex::Parameters::cross_form_corner, "FORM");
    }

    void paint (Graphics& g) override
    {
        drawCard (g, interfaceArea, "INTERFACE");
        drawCard (g, engineArea, "ENGINE");
        drawCard (g, synthesisArea, "SYNTHESIS");
        drawCard (g, transientArea, "TRANSIENTS");
    }

    void resized() override
    {
        const float zs = MorphexZoom::uiScale;
        const int inset = juce::roundToInt (GUI::Layout::CardInset * zs);
        const int gap = juce::roundToInt (GUI::Layout::CardGap * zs);

        // Shared card insets (INTERFACE metrics); mid row takes the rest
        // so SYNTHESIS fills with no bottom gap or clipping.
        const int rowH = juce::roundToInt (28.0f * zs);
        const int pad = juce::roundToInt (12.0f * zs);
        const int synGap = juce::roundToInt (6.0f * zs);
        const int cardTop = juce::roundToInt (8.0f * zs);
        const int cardBottom = juce::roundToInt (12.0f * zs);
        const int cardTitle = juce::roundToInt (20.0f * zs);
        const int transH = juce::roundToInt (104.0f * zs);
        const int intH = juce::roundToInt (120.0f * zs);
        const int engineMin = cardTop + cardTitle + 4 * rowH + 4 * synGap
                              + juce::roundToInt (48.0f * zs) + cardBottom;
        auto area = getLocalBounds().reduced (inset);
        const int midRowH = juce::jmax (engineMin, area.getHeight() - gap
                                        - transH - gap - intH);
        auto midRow = area.removeFromTop (midRowH);
        area.removeFromTop (gap);
        transientArea = area.removeFromTop (transH);
        area.removeFromTop (gap);
        interfaceArea = area.removeFromTop (intH);

        {
            const int engineW = (midRow.getWidth() - gap) * 45 / 100;
            engineArea = midRow.removeFromLeft (engineW);
            midRow.removeFromLeft (gap);
            synthesisArea = midRow;
        }

        // INTERFACE (bottom card): DELAY fader left, two toggles stacked
        // right -- layout and metrics.
        {
            const int side = juce::roundToInt (12.0f * zs);
            const int top = juce::roundToInt (8.0f * zs);
            const int bottom = juce::roundToInt (12.0f * zs);
            const int title_h = juce::roundToInt (20.0f * zs);
            auto ui = interfaceArea;
            ui.removeFromLeft (side);
            ui.removeFromRight (side);
            ui.removeFromTop (top);
            ui.removeFromBottom (bottom);
            ui.removeFromTop (title_h);

            const int col_gap = juce::roundToInt (10.0f * zs);
            const int fader_w = juce::jmin (juce::roundToInt (110.0f * zs), ui.getWidth() / 2);
            tipDelayFader->setBounds (ui.removeFromLeft (fader_w));
            ui.removeFromLeft (col_gap);

            const int toggle_gap = juce::roundToInt (8.0f * zs);
            const int toggle_h = (ui.getHeight() - toggle_gap) / 2;
            tooltipsToggle.setBounds (ui.removeFromTop (toggle_h));
            ui.removeFromTop (toggle_gap);
            confirmToggle.setBounds (ui);
        }

        auto insetCard = [&] (Rectangle<int> card)
        {
            card.removeFromLeft (pad);
            card.removeFromRight (pad);
            card.removeFromTop (cardTop);
            card.removeFromBottom (cardBottom);
            card.removeFromTop (cardTitle);
            return card;
        };

        // ENGINE buttons spread evenly like SYNTHESIS; the trim faders
        // below span the same full inset width (center gap only).
        auto e = insetCard (engineArea);
        const int trimHA = juce::roundToInt (84.0f * zs);
        const int engBtnH = (e.getHeight() - trimHA - 4 * synGap) / 4;
        crossModeButton.setBounds (e.removeFromTop (engBtnH));
        e.removeFromTop (synGap);
        crossFreqButton.setBounds (e.removeFromTop (engBtnH));
        e.removeFromTop (synGap);
        crossAmpButton.setBounds (e.removeFromTop (engBtnH));
        e.removeFromTop (synGap);
        crossFormButton.setBounds (e.removeFromTop (engBtnH));
        e.removeFromTop (synGap);

        // Morph trims: Delay-style vertical faders with side captions.
        {
            auto erow = e;
            const int colW = (erow.getWidth() - synGap) / 2;
            freqTrimFader->setBounds (erow.removeFromLeft (colW));
            erow.removeFromLeft (synGap);
            ampTrimFader->setBounds (erow);
        }

        // Synthesis buttons divide the leftover evenly: five rows, four
        // gaps, no dead space at the bottom.
        {
            auto sy = insetCard (synthesisArea);
            const int synRowH = (sy.getHeight() - 4 * synGap) / 5;
            harmonicButton.setBounds (sy.removeFromTop (synRowH));
            sy.removeFromTop (synGap);
            sinusoidalButton.setBounds (sy.removeFromTop (synRowH));
            sy.removeFromTop (synGap);
            stochasticButton.setBounds (sy.removeFromTop (synRowH));
            sy.removeFromTop (synGap);
            attackButton.setBounds (sy.removeFromTop (synRowH));
            sy.removeFromTop (synGap);
            residualButton.setBounds (sy.removeFromTop (synRowH));
        }

        {
            auto t = insetCard (transientArea);
            const int transLabelW = juce::roundToInt (150.0f * zs);
            auto trow1 = t.removeFromTop (rowH);
            transPreserveLabel.setBounds (trow1.removeFromLeft (transLabelW));
            trow1.removeFromLeft (juce::roundToInt (8.0f * zs));
            transPreserveSlider->setBounds (trow1);
            t.removeFromTop (juce::roundToInt (8.0f * zs));
            auto trow2 = t.removeFromTop (rowH);
            stocGainLabel.setBounds (trow2.removeFromLeft (transLabelW));
            trow2.removeFromLeft (juce::roundToInt (8.0f * zs));
            stocGainSlider->setBounds (trow2);
        }
    }

private:
    void updateGenerateFlag (bool newValue, int component)
    {
        auto& gen = mProcessor->mMorphexSynth.instrument.generate;
        switch (component)
        {
            case 0: gen.harmonic = newValue; break;
            case 1: gen.sinusoidal = newValue; break;
            case 2: gen.stochastic = newValue; break;
            case 3: gen.attack = newValue; break;
            default: gen.residual = newValue; break;
        }
    }
    float readCrossParam (Morphex::Parameters param, float fallback) const
    {
        return Morphex::getParameterValueSafe<float> (
            &mProcessor->parameters, Morphex::PARAMETERS<float>[param].ID, fallback);
    }

    void drawCard (Graphics& g, const Rectangle<int>& area, const String& title)
    {
        if (area.getWidth() <= 0 || area.getHeight() <= 0)
            return;
        const float zs = MorphexZoom::uiScale;
        const float corner = GUI::Layout::InnerCardCorner * zs;
        const auto card = GUI::Paint::insetCardBounds (area.toFloat());
        g.setColour (GUI::Color::Background);
        g.fillRoundedRectangle (card, corner);
        GUI::Paint::drawCardOutline (g, area.toFloat(), corner);
        if (title.isNotEmpty())
        {
            g.setColour (GUI::Color::KeyDown);
            g.setFont (CustomLookAndFeel::makeFont (18.0f));
            g.drawText (">> " + title,
                        area.reduced (juce::roundToInt (6.0f * zs), 0)
                            .withTrimmedTop (juce::roundToInt (2.0f * zs))
                            .withHeight (juce::roundToInt (20.0f * zs)),
                        Justification::topLeft, false);
        }
    }

    struct SmallToggleLook : public CustomLookAndFeel
    {
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool shouldDrawButtonAsHighlighted, bool isButtonDown) override
        {
            drawButtonBackground (g, button, findColour (juce::TextButton::buttonColourId),
                                  shouldDrawButtonAsHighlighted, isButtonDown);

            using namespace MorphexColors;
            const bool isOn = button.getToggleState();
            const auto area = button.getLocalBounds();
            g.setFont (CustomLookAndFeel::makeFont (20.0f));
            g.setColour (isOn ? textPrimary : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));

            const juce::String text = isOn ? ("> " + button.getButtonText() + " <")
                                           : ("[ " + button.getButtonText() + " ]");

            g.drawText (text, area.translated (0, juce::roundToInt (-2.0f * MorphexZoom::uiScale)),
                        juce::Justification::centred, true);
        }
    };

    SpectralMorphingToolAudioProcessor* mProcessor = nullptr;

    Rectangle<int> interfaceArea, engineArea, synthesisArea, transientArea;

    juce::Label transPreserveLabel, stocGainLabel;
    juce::ToggleButton crossModeButton, crossFreqButton, crossAmpButton, crossFormButton;
    juce::ToggleButton tooltipsToggle, confirmToggle;
    std::unique_ptr<SquareFader> tipDelayFader;
    std::unique_ptr<Morphex::Slider> transPreserveSlider;
    std::unique_ptr<Morphex::Slider> stocGainSlider;
    std::unique_ptr<SquareFader> freqTrimFader;
    std::unique_ptr<SquareFader> ampTrimFader;

    juce::TextButton harmonicButton { "Harmonic" }, sinusoidalButton { "Sinusoidal" },
                     stochasticButton { "Stochastic" }, attackButton { "Attack" },
                     residualButton { "Residual" };

    SmallToggleLook smallToggleLnf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};
