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

#include "../Components/PadXY.h"
#include "../Components/Slider.h"
#include "../Components/SquareFader.h"
#include "../Components/SquareKnob.h"
#include "../Components/BipolarFader.h"
#include "../Components/TransportIconButton.h"
#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/SMTConstants.h"

//==============================================================================
/*
*/
class CorePanel    : public Component,
                     public Timer,
                     public juce::Value::Listener
{
public:
    CorePanel(SpectralMorphingToolAudioProcessor* inProcessor)
    :   instrument (&inProcessor->mMorphexSynth.instrument),
        mProcessor (inProcessor)
    {
        voicesFader = std::make_unique<SquareKnob> (mProcessor->voicesValue,
                                                     "VOICES", 1.0, (double) MAX_VOICES, 1.0);
        voicesFader->setTooltip ("Polyphony -- how many notes sound at once (1 = mono with legato glide)");
        voicesFader->setFontScale (1.0f);
        addAndMakeVisible (voicesFader.get());
        mProcessor->voicesValue.addListener (this);

        // Pad XY
        Morphex::Parameter<float> freqs_interp_factor_parameter = Morphex::PARAMETERS<float>[Morphex::Parameters::freqs_interp_factor];
        Morphex::Parameter<float> mags_interp_factor_parameter = Morphex::PARAMETERS<float>[Morphex::Parameters::mags_interp_factor];

        mPadXY = std::make_unique<PadXY>(inProcessor, inProcessor->parameters,
                           freqs_interp_factor_parameter,
                           mags_interp_factor_parameter);

        addAndMakeVisible(mPadXY.get());

        // PLAYBACK: rate + transport switches
        rateCaption.setText ("RATE", NotificationType::dontSendNotification);
        rateCaption.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.75f));
        rateCaption.setFont (CustomLookAndFeel::makeFont (18.0f));
        rateCaption.setJustificationType (Justification::centred);
        addAndMakeVisible (rateCaption);

        rateFader = std::make_unique<BipolarFader> (inProcessor->parameters,
                                                    Morphex::Parameters::time_scrub_rate, 1.0);
        rateFader->setFontScale (1.4f);
        rateFader->setTooltip ("Global playback rate -- scales every slot (100% natural, 0% frozen, negative runs backward); needs the Rate switch on");
        addAndMakeVisible (rateFader.get());

        scrubButton.setTooltip ("Rate switch -- off forces 100% natural speed, ignoring the Rate fader");
        scrubButton.setToggleState (instrument->time_scrub_enabled, NotificationType::dontSendNotification);
        scrubButton.onClick = [this] { instrument->time_scrub_enabled = scrubButton.getToggleState(); };
        addAndMakeVisible (scrubButton);

        loopButton.setTooltip ("Loop switch -- off plays each note once through the sound, then releases");
        loopButton.setToggleState (instrument->sound_looping, NotificationType::dontSendNotification);
        loopButton.onClick = [this] { instrument->sound_looping = loopButton.getToggleState(); };
        addAndMakeVisible (loopButton);

        forwardButton.setTooltip ("Forward-only -- looping always runs forward instead of ping-pong");
        forwardButton.onClick = [this]
        {
            if (auto* p = mProcessor->parameters.getParameter (
                    Morphex::PARAMETERS<float>[Morphex::Parameters::forward_only].ID))
                p->setValueNotifyingHost (forwardButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (forwardButton);

        padKnob = std::make_unique<SquareKnob> (inProcessor->parameters,
                                                Morphex::Parameters::pad_smoothing_ms,
                                                "SMOOTHING",
                                                [] (double v)
                                                { return juce::String (juce::roundToInt (v)) + " ms"; });
        padKnob->setTooltip ("Pad smoothing -- the morph position glides toward the puck over this time, so quick pad moves morph gradually instead of jumping (0 = off)");
        padKnob->setFontScale (1.2f);
        addAndMakeVisible (padKnob.get());

        // VOICING: tuning knobs with value-inside + caption-below (all live).
        transposeFader = std::make_unique<SquareKnob> (inProcessor->parameters,
                                                       Morphex::Parameters::transpose_st,
                                                       "TRANSPOSE",
                                                       [] (double v)
                                                       { return juce::String (juce::roundToInt (v)) + " st"; });
        transposeFader->setTooltip ("Transpose -- shifts every note by semitones (12 = one octave up)");
        fineFader = std::make_unique<SquareKnob> (inProcessor->parameters,
                                                  Morphex::Parameters::fine_tune_cents,
                                                  "FINE",
                                                  [] (double v)
                                                  { return juce::String (juce::roundToInt (v)) + " ct"; });
        fineFader->setTooltip ("Fine tune -- nudges every note by cents (100 = one semitone, for matching other instruments)");
        bendFader = std::make_unique<SquareKnob> (inProcessor->parameters,
                                                  Morphex::Parameters::pitch_bend_range,
                                                  "BEND",
                                                  [] (double v)
                                                  { return juce::String (juce::roundToInt (v)) + " st"; });
        bendFader->setTooltip ("Pitch bend range -- how far the pitch wheel bends notes, in semitones (wider = deeper dives)");
        glideFader = std::make_unique<SquareKnob> (inProcessor->parameters,
                                                   Morphex::Parameters::glide_time_ms,
                                                   "GLIDE",
                                                   [] (double v)
                                                   { return juce::String (juce::roundToInt (v)) + " ms"; });
        glideFader->setTooltip ("Glide -- portamento time toward pressed notes and pitch-wheel moves (0 = off)");

        for (auto* f : { transposeFader.get(), fineFader.get(), bendFader.get(), glideFader.get() })
        {
            f->setFontScale (1.0f);
            addAndMakeVisible (f);
        }

        legatoButton.setButtonText ("LEGATO");
        legatoButton.setClickingTogglesState (true);
        legatoButton.setTooltip ("Legato -- in mono, new notes glide from the current pitch instead of retriggering the envelope");
        legatoButton.setToggleState (mProcessor->getLegatoEnabled(), NotificationType::dontSendNotification);
        legatoButton.onClick = [this] { mProcessor->setLegatoEnabled (legatoButton.getToggleState()); };
        legatoButton.setLookAndFeel (&voiceToggleLnf);
        mProcessor->legatoValue.addListener (this);
        addAndMakeVisible (legatoButton);
        applyLegatoAvailability();

        pitchLockButton.setButtonText ("PITCH LOCK");
        pitchLockButton.setClickingTogglesState (true);
        pitchLockButton.setTooltip ("Pitch lock -- keeps every instrument at its original tuning instead of following the played key (transpose / fine still apply)");
        pitchLockButton.setToggleState (mProcessor->getPitchLockEnabled(), NotificationType::dontSendNotification);
        pitchLockButton.onClick = [this] { mProcessor->setPitchLockEnabled (pitchLockButton.getToggleState()); };
        pitchLockButton.setLookAndFeel (&voiceToggleLnf);
        mProcessor->pitchLockValue.addListener (this);
        addAndMakeVisible (pitchLockButton);

        // AMP ENVELOPE: vertical square faders
        const auto secText = [] (double v)
        { return juce::String (v, 2) + " s"; };
        const auto pct01Text = [] (double v)
        { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };

        attackFader  = std::make_unique<SquareFader> (inProcessor->parameters, Morphex::Parameters::asdr_attack,
                                                      "ATTACK", " s", 1.0, 2, 0.001, secText);
        decayFader   = std::make_unique<SquareFader> (inProcessor->parameters, Morphex::Parameters::asdr_decay,
                                                      "DECAY", " s", 1.0, 2, 0.001, secText);
        sustainFader = std::make_unique<SquareFader> (inProcessor->parameters, Morphex::Parameters::asdr_sustain,
                                                      "SUSTAIN", "%", 1.0, 0, 0.001, pct01Text);
        releaseFader = std::make_unique<SquareFader> (inProcessor->parameters, Morphex::Parameters::asdr_release,
                                                      "RELEASE", " s", 1.0, 2, 0.001, secText);

        attackFader->setTooltip ("Amp attack -- fade-in time from note start to full volume");
        decayFader->setTooltip ("Amp decay -- fall time from full volume down to the sustain level");
        sustainFader->setTooltip ("Amp sustain -- held volume while the note is down (0% = silence)");
        releaseFader->setTooltip ("Amp release -- fade-out time after note-off");

        for (auto* f : { attackFader.get(), decayFader.get(),
                         sustainFader.get(), releaseFader.get() })
        {
            f->setFontScale (2.0f);
            f->setCaptionScale (0.7f);
            f->setSquareTrack (false);
            f->setVerticalValue (true);
            f->setTrackInsetU (4.0f);
            addAndMakeVisible (f);
        }

        startTimer (150);
    }

    ~CorePanel() override
    {
        legatoButton.setLookAndFeel (nullptr);
        pitchLockButton.setLookAndFeel (nullptr);
        mProcessor->voicesValue.removeListener (this);
        mProcessor->legatoValue.removeListener (this);
        mProcessor->pitchLockValue.removeListener (this);
    }

    void valueChanged (juce::Value& value) override
    {
        if (value.refersToSameSourceAs (mProcessor->voicesValue))
        {
            if (voicesFader != nullptr)
                voicesFader->refresh();
            applyLegatoAvailability();
        }
        else if (value.refersToSameSourceAs (mProcessor->legatoValue))
            legatoButton.setToggleState (mProcessor->getLegatoEnabled(), NotificationType::dontSendNotification);
        else if (value.refersToSameSourceAs (mProcessor->pitchLockValue))
            pitchLockButton.setToggleState (mProcessor->getPitchLockEnabled(), NotificationType::dontSendNotification);
    }

    void applyLegatoAvailability()
    {
        const bool mono = mProcessor->getCurrentVoices() == 1;
        legatoButton.setEnabled (mono);
        legatoButton.setAlpha (mono ? 1.0f : 0.35f);
    }

    void timerCallback() override
    {
        // Keep transport switches in sync with preset/host changes.
        scrubButton.setToggleState (instrument->time_scrub_enabled,
                                    NotificationType::dontSendNotification);
        const bool loopOn = instrument->sound_looping;
        loopButton.setToggleState (loopOn,
                                   NotificationType::dontSendNotification);
        // Forward-only needs the loop running;
        // losing the loop also switches forward off.
        forwardButton.setEnabled (loopOn);
        forwardButton.setAlpha (loopOn ? 1.0f : 0.35f);
        if (auto* p = mProcessor->parameters.getParameter (
                Morphex::PARAMETERS<float>[Morphex::Parameters::forward_only].ID))
        {
            if (! loopOn && p->getValue() > 0.5f)
                p->setValueNotifyingHost (0.0f);
            forwardButton.setToggleState (p->getValue() > 0.5f,
                                          NotificationType::dontSendNotification);
        }
    }

    void paint (Graphics& g) override
    {
        const float zs = MorphexZoom::uiScale;
        const float innerCornerSize = GUI::Layout::InnerCardCorner * zs;

        auto drawSection = [&] (const Rectangle<int>& area, const String& label)
        {
            if (area.getWidth() <= 0)
                return;

            const auto card = GUI::Paint::insetCardBounds (area.toFloat());
            g.setColour (GUI::Color::Background);
            g.fillRoundedRectangle (card, innerCornerSize);
            GUI::Paint::drawCardOutline (g, area.toFloat(), innerCornerSize);

            if (label.isNotEmpty())
            {
                g.setColour (GUI::Color::KeyDown);
                g.setFont (CustomLookAndFeel::makeFont (18.0f));
                g.drawText (">> " + label, area.reduced (juce::roundToInt (6.0f * zs), 0)
                                .withTrimmedTop (juce::roundToInt (2.0f * zs))
                                .withHeight (juce::roundToInt (20.0f * zs)),
                            Justification::topLeft, false);
            }
        };

        drawSection (padArea, {});
        drawSection (adsrArea, "AMP ENVELOPE");
        drawSection (playbackArea, "PLAYBACK");
        drawSection (voiceArea, "VOICING");
    }

    void resized() override
    {
        const float zs = MorphexZoom::uiScale;
        const float inset = GUI::Layout::CardInset * zs;
        const float gap = GUI::Layout::CardGap * zs;

        const int mid_height = juce::roundToInt (190.0f * zs);
        const int voice_height = juce::roundToInt (110.0f * zs);

        const int section_x = (int) inset;
        const int section_width = getWidth() - (int) inset * 2;

        const int voice_y = getHeight() - (int) inset - voice_height;
        const int mid_y = voice_y - (int) gap - mid_height;
        const int pad_y = (int) inset;
        const int pad_height = jmax (80, mid_y - (int) gap - pad_y);

        voiceArea.setBounds (section_x, voice_y, section_width, voice_height);
        padArea.setBounds (section_x, pad_y, section_width, pad_height);
        mPadXY->setBounds (padArea);

        {
            auto mid = Rectangle<int> (section_x, mid_y, section_width, mid_height);
            const int mid_gap = juce::roundToInt (10.0f * zs);
            const int env_w = (section_width - mid_gap) * 3 / 5;
            adsrArea = mid.removeFromLeft (env_w);
            mid.removeFromLeft (mid_gap);
            playbackArea = mid;
        }

        const int title_h = juce::roundToInt (20.0f * zs);

        // AMP ENVELOPE: four vertical faders
        {
            const int env_side = juce::roundToInt (12.0f * zs);
            const int env_top = juce::roundToInt (8.0f * zs);
            const int env_bottom = juce::roundToInt (12.0f * zs);
            auto env = adsrArea;
            env.removeFromLeft (env_side);
            env.removeFromRight (env_side);
            env.removeFromTop (env_top);
            env.removeFromBottom (env_bottom);
            env.removeFromTop (title_h);

            auto faders = env;
            const int env_gap = juce::roundToInt (8.0f * zs);
            const int fw = (faders.getWidth() - env_gap * 3) / 4;
            attackFader->setBounds (faders.removeFromLeft (fw));
            faders.removeFromLeft (env_gap);
            decayFader->setBounds (faders.removeFromLeft (fw));
            faders.removeFromLeft (env_gap);
            sustainFader->setBounds (faders.removeFromLeft (fw));
            faders.removeFromLeft (env_gap);
            releaseFader->setBounds (faders);
        }

        // PLAYBACK: rate fader, transport icons, smoothing knob
        {
            const int gen_side = juce::roundToInt (12.0f * zs);
            const int gen_top = juce::roundToInt (8.0f * zs);
            const int gen_bottom = juce::roundToInt (12.0f * zs);
            auto gen = playbackArea;
            gen.removeFromLeft (gen_side);
            gen.removeFromRight (gen_side);
            gen.removeFromTop (gen_top);
            gen.removeFromBottom (gen_bottom);
            gen.removeFromTop (title_h);

            auto rateCap = gen.removeFromTop (juce::roundToInt (16.0f * zs));
            rateCaption.setBounds (rateCap.translated (0, juce::roundToInt (-2.0f * zs)));
            rateFader->setBounds (gen.removeFromTop (juce::roundToInt (32.0f * zs)));
            gen.removeFromTop (juce::roundToInt (8.0f * zs));

            const int gen_gap = juce::roundToInt (10.0f * zs);
            const int knob_w = juce::roundToInt (80.0f * zs);
            padKnob->setBounds (gen.removeFromRight (knob_w));
            gen.removeFromRight (gen_gap);

            const int icon_h = (gen.getHeight() - gen_gap * 2) / 3;
            scrubButton.setBounds (gen.removeFromTop (icon_h));
            gen.removeFromTop (gen_gap);
            loopButton.setBounds (gen.removeFromTop (icon_h));
            gen.removeFromTop (gen_gap);
            forwardButton.setBounds (gen);
        }

        // VOICING: five square knobs plus two toggles. Knobs carry their
        // value inside the square with the caption below.
        {
            const int strip_pad = juce::roundToInt (12.0f * zs);
            const int strip_gap = juce::roundToInt (8.0f * zs);
            auto strip = voiceArea;
            strip.removeFromLeft (strip_pad);
            strip.removeFromRight (strip_pad);
            strip.removeFromTop (juce::roundToInt (4.0f * zs));
            strip.removeFromBottom (juce::roundToInt (12.0f * zs));
            strip.removeFromTop (juce::roundToInt (20.0f * zs));

            const int toggle_w = juce::roundToInt (110.0f * zs);
            auto toggles = strip.removeFromRight (toggle_w);
            strip.removeFromRight (strip_gap);

            const int toggle_gap = juce::roundToInt (10.0f * zs);
            const int toggle_h = (toggles.getHeight() - toggle_gap) / 2;
            legatoButton.setBounds (toggles.removeFromTop (toggle_h));
            toggles.removeFromTop (toggle_gap);
            pitchLockButton.setBounds (toggles);

            const int knob_gap = juce::roundToInt (8.0f * zs);
            const int knob_w = (strip.getWidth() - knob_gap * 4) / 5;
            voicesFader->setBounds (strip.removeFromLeft (knob_w));
            strip.removeFromLeft (knob_gap);
            bendFader->setBounds (strip.removeFromLeft (knob_w));
            strip.removeFromLeft (knob_gap);
            glideFader->setBounds (strip.removeFromLeft (knob_w));
            strip.removeFromLeft (knob_gap);
            transposeFader->setBounds (strip.removeFromLeft (knob_w));
            strip.removeFromLeft (knob_gap);
            fineFader->setBounds (strip);
        }
    }

    struct VoiceToggleLook : public CustomLookAndFeel
    {
        juce::Font getTextButtonFont (juce::TextButton&, int) override
        {
            return CustomLookAndFeel::makeFont (20.0f);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool shouldDrawButtonAsHighlighted, bool isButtonDown) override
        {
            drawButtonBackground (g, button, findColour (juce::TextButton::buttonColourId),
                                  shouldDrawButtonAsHighlighted, isButtonDown);

            using namespace MorphexColors;
            const bool isOn = button.getToggleState();
            const auto area = button.getLocalBounds();
            g.setFont (CustomLookAndFeel::makeFont (20.0f));
            g.setColour (isOn ? GUI::Color::KeyDown : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));

            const juce::String text = isOn ? ("> " + button.getButtonText() + " <")
                                           : ("[ " + button.getButtonText() + " ]");

            g.drawText (text, area.translated (0, juce::roundToInt (-2.0f * MorphexZoom::uiScale)),
                        juce::Justification::centred, true);
        }
    };

private:

    SpectralMorphingToolAudioProcessor* mProcessor = nullptr;
    Instrument* instrument;

    Rectangle<int> adsrArea;
    Rectangle<int> padArea;
    Rectangle<int> playbackArea;
    Rectangle<int> voiceArea;

    std::unique_ptr<PadXY> mPadXY;

    std::unique_ptr<BipolarFader> rateFader;
    TransportIconButton scrubButton { TransportIconButton::Glyph::Scrub };
    TransportIconButton loopButton { TransportIconButton::Glyph::Loop };
    TransportIconButton forwardButton { TransportIconButton::Glyph::Forward };
    std::unique_ptr<SquareKnob> padKnob;

    juce::Label rateCaption;

    std::unique_ptr<SquareKnob> transposeFader;
    std::unique_ptr<SquareKnob> fineFader;
    std::unique_ptr<SquareKnob> bendFader;
    std::unique_ptr<SquareKnob> glideFader;
    std::unique_ptr<SquareKnob> voicesFader;
    juce::ToggleButton legatoButton;
    juce::ToggleButton pitchLockButton;

    VoiceToggleLook voiceToggleLnf;

    std::unique_ptr<SquareFader> attackFader;
    std::unique_ptr<SquareFader> decayFader;
    std::unique_ptr<SquareFader> sustainFader;
    std::unique_ptr<SquareFader> releaseFader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorePanel)
};
