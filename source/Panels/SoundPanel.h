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

#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/SMTConstants.h"
#include "../Helpers/SMTParameters.h"
#include "../Components/SquareKnob.h"
#include "../Components/TransportIconButton.h"
#include "../PluginProcessor.h"

class ViewFlipButton : public TextButton
{
public:
    using TextButton::TextButton;

    void setCursorView (bool inCursorView) noexcept
    {
        if (cursorView != inCursorView)
        {
            cursorView = inCursorView;
            repaint();
        }
    }

    void paint (Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOver(), isDown());

        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float u = MorphexZoom::uiScale;

        g.setColour (isMouseOver() ? MorphexColors::textPrimary : MorphexColors::textMid);

        if (cursorView)
        {
            const float lineW = 2.0f * u;
            const float s = 2.6f * u;
            const float top = cy - 5.0f * u + s * 0.5f;
            const float bottom = cy + 5.0f * u + s * 0.5f;
            g.fillRect (cx - lineW * 0.5f, top, lineW, bottom - top);
            juce::Path tri;
            tri.addTriangle (cx - s, top - s, cx + s, top - s, cx, top + s * 0.4f);
            g.fillPath (tri);
        }
        else
        {
            const float lineH = 1.5f * u;
            const float halfW = 7.0f * u;
            const float knob = 3.0f * u;
            const float rows[3] = { cy - 4.5f * u, cy, cy + 4.5f * u };
            const float knobX[3] = { cx - 3.0f * u, cx + 2.0f * u, cx - 0.5f * u };
            for (int i = 0; i < 3; ++i)
            {
                g.fillRect (cx - halfW, rows[i] - lineH * 0.5f, halfW * 2.0f, lineH);
                g.fillRect (knobX[i] - knob * 0.5f, rows[i] - knob * 0.5f, knob, knob);
            }
        }
    }

private:
    bool cursorView = false;
};

class ReverseToggleButton : public TextButton
{
public:
    using TextButton::TextButton;

    void paint (Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOver(), isDown());

        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float u = MorphexZoom::uiScale;

        g.setColour (getToggleState() || isMouseOver() ? MorphexColors::textPrimary : MorphexColors::textMid);

        const float halfH = 5.0f * u;
        const float barW = 2.0f * u;
        const float barX = cx - 5.5f * u;
        g.fillRect (barX, cy - halfH, barW, halfH * 2.0f);

        juce::Path tri;
        tri.addTriangle (cx - 2.5f * u, cy,
                         cx + 4.5f * u, cy - halfH,
                         cx + 4.5f * u, cy + halfH);
        g.fillPath (tri);
    }
};

class SoundPanel :  public Component,
                    public DragAndDropTarget,
                    public FileDragAndDropTarget,
                    public Timer,
                    public SpectralMorphingToolAudioProcessor::SoundLoadListener
{
public:

    SoundPanel (SpectralMorphingToolAudioProcessor* inProcessor, int i_sound_num)
    :   i_sound_num (i_sound_num),
        mProcessor (inProcessor),
        slot_index (i_sound_num - 1),
        instrument (&inProcessor->mMorphexSynth.instrument),
        soundNumberPanel (i_sound_num),
        soundNamePanel (this)
    {
        // 2x2 loader grid matching the pad quadrants.
        if (i_sound_num == 1) morph_location = MorphLocation::LeftHigh;
        else if (i_sound_num == 2) morph_location = MorphLocation::RightHigh;
        else if (i_sound_num == 3) morph_location = MorphLocation::LeftLow;
        else if (i_sound_num == 4) morph_location = MorphLocation::RightLow;
        else morph_location = MorphLocation::NUM_MORPH_LOCATIONS;

        if (morph_location != MorphLocation::NUM_MORPH_LOCATIONS) this->updateCurrentSound();
        else this->sound = std::make_shared<Sound>();

        addAndMakeVisible (soundNumberPanel);
        addAndMakeVisible (soundNamePanel);

        removeButton.setTooltip ("Remove this sound");
        removeButton.onClick = [this, soundNum = i_sound_num]
        {
            auto doClear = [this]
            {
                if (morph_location != MorphLocation::NUM_MORPH_LOCATIONS)
                {
                    instrument->clearSound (morph_location);
                    isLoading = false;
                    loadFailed = false;
                    loadErrorMessage.clear();
                    updateCurrentSound();
                    repaint();
                    mProcessor->broadcastSlotRefresh (morph_location);
                }
            };
            if (! mProcessor->getConfirmDestructive())
            {
                doClear();
                return;
            }
            juce::Component::SafePointer<SoundPanel> safePanel (this);
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Remove sound?",
                "Remove the sound from slot " + juce::String (soundNum) + "?",
                "Remove", "Cancel", this,
                juce::ModalCallbackFunction::create (
                    [safePanel, doClear = std::move (doClear)] (int result)
                    {
                        if (result != 0 && safePanel != nullptr)
                            doClear();
                    }));
        };
        addAndMakeVisible (removeButton);

        const auto percentText = [] (double v)
        { return juce::String (juce::roundToInt (v)) + "%"; };
        cursorRateKnob = std::make_unique<SquareKnob> (
            inProcessor->parameters, slotParam (engineSlot(), 0), "RATE", percentText);
        cursorOffsetKnob = std::make_unique<SquareKnob> (
            inProcessor->parameters, slotParam (engineSlot(), 1), "OFFSET", percentText);
        cursorStartKnob = std::make_unique<SquareKnob> (
            inProcessor->parameters, slotParam (engineSlot(), 2), "START", percentText);
        cursorEndKnob = std::make_unique<SquareKnob> (
            inProcessor->parameters, slotParam (engineSlot(), 3), "END", percentText);
        cursorFormantKnob = std::make_unique<SquareKnob> (
            inProcessor->parameters, formantParam (engineSlot()), "FORMANT",
            [] (double v) { return juce::String (v, 1) + " st"; });
        for (auto* k : { cursorRateKnob.get(), cursorOffsetKnob.get(),
                         cursorStartKnob.get(), cursorEndKnob.get(),
                         cursorFormantKnob.get() })
        {
            k->setSquareYOffsetU (2.0f);
            addChildComponent (k);
        }

        const juce::String slotTag = "Slot " + juce::String (i_sound_num) + " ";
        cursorRateKnob->setTooltip (slotTag + "playhead speed -- 100% plays naturally, 0% freezes the cursor");
        cursorOffsetKnob->setTooltip (slotTag + "start position inside its own sound");
        cursorStartKnob->setTooltip (slotTag + "loop window start -- must sit below End, or the full sound plays");
        cursorEndKnob->setTooltip (slotTag + "loop window end -- must sit above Start, or the full sound plays");
        cursorFormantKnob->setTooltip (slotTag + "formant shift in semitones -- resonances move, pitch does not");

        cursorRevButton.setClickingTogglesState (true);
        cursorRevButton.setTooltip ("Reverse this slot's playhead");
        cursorRevButton.onClick = [this]
        {
            if (auto* p = mProcessor->parameters.getParameter (
                    Morphex::PARAMETERS<float>[slotParam (engineSlot(), 4)].ID))
                p->setValueNotifyingHost (cursorRevButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (cursorRevButton);

        slotLoopButton.setTooltip ("Loop this slot -- off plays once through, then holds the last frame");
        slotLoopButton.onClick = [this]
        {
            if (auto* p = mProcessor->parameters.getParameter (
                    Morphex::PARAMETERS<float>[slotLoopParam (engineSlot())].ID))
                p->setValueNotifyingHost (slotLoopButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (slotLoopButton);

        cursorViewButton.setTooltip ("Show playhead controls");
        cursorViewButton.onClick = [this] { setCursorView (! showCursors); };
        addAndMakeVisible (cursorViewButton);

        setCursorView (showCursors);

        mProcessor->addSoundLoadListener (this);

        startTimer (GUI_REFRESH_TIMER_CALLBACK_SOUND);
    }

    ~SoundPanel() override
    {
        mProcessor->removeSoundLoadListener (this);
    }

    void setCursorView (bool show)
    {
        showCursors = show;
        soundNumberPanel.setVisible (! show);
        soundNamePanel.setVisible (! show);
        cursorRateKnob->setVisible (show);
        cursorOffsetKnob->setVisible (show);
        cursorStartKnob->setVisible (show);
        cursorEndKnob->setVisible (show);
        cursorFormantKnob->setVisible (show);
        applyIconAvailability();
        cursorViewButton.setCursorView (show);
        cursorViewButton.setTooltip (show ? "Show sound" : "Show playhead controls");
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        const float zs = MorphexZoom::uiScale;
        const float innerCornerSize = GUI::Layout::InnerCardCorner * zs;

        const Rectangle<float> outerCard = getLocalBounds().toFloat();
        const Rectangle<float> innerCard = GUI::Paint::insetCardBounds (outerCard);

        g.setColour (GUI::Color::Background);
        g.fillRoundedRectangle (innerCard, innerCornerSize);
        GUI::Paint::drawCardOutline (g, outerCard, innerCornerSize);

        if (showCursors)
            return;

        const bool loaded = (sound != nullptr && sound->loaded);
        const float tagDim = loaded ? 1.0f : 0.45f;

        {
            Justification tag_just (Justification::centred);
            if (i_sound_num == 1)      tag_just = Justification::topLeft;
            else if (i_sound_num == 2) tag_just = Justification::topRight;
            else if (i_sound_num == 3) tag_just = Justification::bottomLeft;
            else                       tag_just = Justification::bottomRight;

            auto tagBounds = getLocalBounds().reduced (juce::roundToInt (24.0f * zs),
                                                             juce::roundToInt (16.0f * zs));

            if (i_sound_num == 1 || i_sound_num == 2)
            {
                // Lift LH/RH to mirror LL/RL padding: top-anchored caps carry
                // ascent whitespace that bottom-anchored text does not.
                tagBounds.translate (0, -juce::roundToInt (6.0f * zs));
            }

            g.setFont (CustomLookAndFeel::makeFont (54.0f));
            g.setColour (GUI::Color::Logo.withAlpha (0.30f * tagDim));
            g.drawText (getPadPositionLabel (i_sound_num),
                        tagBounds, tag_just, false);

            g.setFont (CustomLookAndFeel::makeFont (48.0f));
            g.setColour (GUI::Color::KeyDown.withAlpha (tagDim));
            g.drawText (getPadPositionLabel (i_sound_num),
                        tagBounds, tag_just, false);
        }

        const int tag_inset = juce::roundToInt (17.0f * zs);
        const int tag_height = juce::roundToInt (18.0f * zs);
        const int tag_top = tag_inset;
        const int tag_bottom = getHeight() - tag_inset - tag_height;

        g.setColour (GUI::Color::Logo.withAlpha (0.50f * tagDim));
        g.setFont (CustomLookAndFeel::makeFont (18.0f));

        Rectangle<int> tag_rect (getLocalBounds().getX() + tag_inset, tag_top,
                                 getWidth() - tag_inset * 2, tag_height);

        Justification tag_justification (Justification::centred);

        if (i_sound_num == 1) // LH top-left  -> SOUND bottom-right
        {
            tag_rect.setY (tag_bottom);
            tag_justification = Justification::bottomRight;
        }
        else if (i_sound_num == 2) // RH top-right -> SOUND bottom-left
        {
            tag_rect.setY (tag_bottom);
            tag_justification = Justification::bottomLeft;
        }
        else if (i_sound_num == 3) // LL bottom-left -> SOUND top-right
        {
            tag_rect.setY (tag_top);
            tag_justification = Justification::topRight;
        }
        else // RL bottom-right -> SOUND top-left
        {
            tag_rect.setY (tag_top);
            tag_justification = Justification::topLeft;
        }

        g.drawText ("[ SOUND " + String (i_sound_num) + " ]", tag_rect, tag_justification, false);
    }

    void resized() override
    {
        const float zs = MorphexZoom::uiScale;
        const int removeSize = juce::roundToInt (20.0f * zs);
        const int removeY = getHeight() - removeSize - juce::roundToInt (14.0f * zs);
        const int trioGap = juce::roundToInt (8.0f * zs);
        const int trioW = removeSize * 4 + trioGap * 3;
        const int trioX = getWidth() / 2 - trioW / 2;
        slotLoopButton.setBounds (trioX, removeY, removeSize, removeSize);
        cursorRevButton.setBounds (trioX + (removeSize + trioGap), removeY, removeSize, removeSize);
        cursorViewButton.setBounds (trioX + (removeSize + trioGap) * 2, removeY,
                                    removeSize, removeSize);
        removeButton.setBounds (trioX + (removeSize + trioGap) * 3, removeY,
                                removeSize, removeSize);

        if (showCursors)
        {
            const int pad = juce::roundToInt (10.0f * zs);
            juce::Component* knobs[5] = { cursorRateKnob.get(), cursorOffsetKnob.get(),
                                          cursorStartKnob.get(), cursorEndKnob.get(),
                                          cursorFormantKnob.get() };
            const int cellW = (getWidth() - pad * 2) / 5;
            const int knobH = juce::jmax (1, removeY - pad * 2);
            const int knobY = pad;
            for (int i = 0; i < 5; ++i)
                knobs[i]->setBounds (pad + i * cellW, knobY, cellW, knobH);
            return;
        }

        Grid grid;

        const float grid_margin = GUI::Layout::ContentInset * zs;

        grid.columnGap = Grid::Px (grid_margin);

        using Track = Grid::TrackInfo;

        grid.templateRows = {Track (1_fr)};

        grid.templateColumns = {Track (1_fr), Track (1_fr), Track (1_fr)};

        grid.justifyContent = Grid::JustifyContent::spaceBetween;
        grid.justifyItems = Grid::JustifyItems::stretch;
        grid.alignContent = Grid::AlignContent::center;

        if (i_sound_num == 1 or i_sound_num == 3)
        {
            grid.items.addArray
            ({
                GridItem (soundNumberPanel),
                GridItem (soundNamePanel).withArea (1, 2, 1, 4),
            });
        }
        else
        {
            grid.items.addArray
            ({
                GridItem (soundNamePanel).withArea (1, 1, 1, 3),
                GridItem (soundNumberPanel),
            });
        }

        Rectangle<int> grid_bounds (juce::roundToInt (grid_margin), juce::roundToInt (grid_margin),
                                    juce::roundToInt ((float) getWidth() - (grid_margin * 2.0f)),
                                    juce::roundToInt ((float) getHeight() - (grid_margin * 2.0f)));

        grid.performLayout (grid_bounds);
    }

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }

    static bool isSupportedDropFile (const File& f)
    {
        const String ext = f.getFileExtension().toLowerCase();
        return ext == ".had" || ext == ".wav" || ext == ".wave"
            || ext == ".aif" || ext == ".aiff";
    }

    bool isInterestedInFileDrag (const StringArray& files) override
    {
        if (files.isEmpty())
            return true;
        for (auto& f : files)
            if (isSupportedDropFile (File (f)))
                return true;
        return false;
    }

    void fileDragEnter (const StringArray&, int, int) override
    {
        isDragOver = true;
        repaint();
    }

    void fileDragExit (const StringArray&) override
    {
        isDragOver = false;
        repaint();
    }

    void filesDropped (const StringArray& files, int, int) override
    {
        isDragOver = false;
        for (auto& f : files)
        {
            const File file (f);
            if (! isSupportedDropFile (file))
                continue;
            handleDroppedFile (file);
            break;
        }
        repaint();
    }

    void handleDroppedFile (const File& f)
    {
        if (morph_location == MorphLocation::NUM_MORPH_LOCATIONS)
            return;

        setCursorView (false);

        isLoading = true;
        loadFailed = false;
        loadErrorMessage.clear();
        applyIconAvailability();

        if (f.getFileExtension().equalsIgnoreCase (".had"))
            mProcessor->loadSoundAsync (f.getFullPathName().toStdString(), morph_location);
        else
            mProcessor->analyzeAndLoadSlot (slot_index, f);

        repaint();
    }

    void itemDragEnter (const SourceDetails&) override
    {
        isDragOver = true;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        isDragOver = false;
        repaint();
    }

    void itemDropped (const SourceDetails& dragSourceDetails) override
    {
        std::string sound_file_path = dragSourceDetails.description.toString().toStdString();

        if (sound_file_path != "directory")
        {
            const File dropped (sound_file_path);
            if (isSupportedDropFile (dropped))
                handleDroppedFile (dropped);
        }

        isDragOver = false;
        repaint();
    }

    void paintOverChildren (Graphics& g) override
    {
        if (isDragOver)
        {
            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.drawRect (getLocalBounds(), 2);
        }
    }

    // SpectralMorphingToolAudioProcessor::SoundLoadListener (message thread).
    void soundLoadFinished (MorphLocation location, bool success, const std::string& errorMessage) override
    {
        if (location != this->morph_location)
            return;

        isLoading = false;
        loadFailed = ! success;
        loadErrorMessage = juce::String (errorMessage);
        updateCurrentSound();
        repaint();
    }

private:

    // Engine slot for this card (corner order LL/RL/LH/RH, not card order).
    int engineSlot() const noexcept
    {
        return juce::jlimit (0, 3, (int) morph_location);
    }

    static Morphex::Parameters slotParam (int engineSlot, int field)
    {
        // field: 0 rate, 1 offset, 2 loopstart, 3 loopend, 4 reverse
        const int base = (int) Morphex::Parameters::slot1_rate + engineSlot * 5;
        return (Morphex::Parameters) (base + field);
    }

    static Morphex::Parameters formantParam (int engineSlot)
    {
        return (Morphex::Parameters) ((int) Morphex::Parameters::slot1_formant + engineSlot);
    }

    static Morphex::Parameters slotLoopParam (int engineSlot)
    {
        return (Morphex::Parameters) ((int) Morphex::Parameters::slot1_loopmode + engineSlot);
    }

    static String getPadPositionLabel (int i_sound_num)
    {
        switch (i_sound_num)
        {
            case 1: return "LH";
            case 2: return "RH";
            case 3: return "LL";
            default: return "RL";
        }
    }

    struct SoundNumberPanel : public Component
    {
        SoundNumberPanel (int i_sound_num) : i_sound_num (i_sound_num) {}

        void paint (Graphics&) override
        {
            // All tag text renders in SoundPanel::paint() (full card space).
        }

        int i_sound_num;
    };

    struct SoundNamePanel : public Component
    {
        SoundNamePanel (SoundPanel* sound_panel) : sound_panel (sound_panel) {}

        void paint (Graphics& g) override
        {
            const float zs = MorphexZoom::uiScale;
            auto bounds = getLocalBounds().reduced (juce::roundToInt (4.0f * zs), 0);
            const int line_height = juce::roundToInt (23.0f * zs);
            const int line_gap = juce::roundToInt (2.0f * zs);
            const Colour errorColour (0xFFE0E0E0);

            // Load in progress
            if (sound_panel->isLoading)
            {
                g.setFont (CustomLookAndFeel::makeFont (21.0f));
                g.setColour (GUI::Color::Logo.withAlpha (0.55f));
                // In-file analysis shows live queue progress; .had loads are
                // fast and stay on the plain line.
                const float p = sound_panel->mProcessor->getAnalyzeProgressForSlot (
                    sound_panel->slot_index);
                if (p >= 0.0f)
                    g.drawText ("> ANALYZING " + juce::String (juce::roundToInt (p * 100.0f)) + "%...",
                                bounds, Justification::centred, false);
                else
                    g.drawText ("> LOADING...", bounds, Justification::centred, false);
                return;
            }

            // Failed import
            if (sound_panel->loadFailed)
            {
                const int block_y = bounds.getCentreY() - line_height - line_gap / 2;

                g.setFont (CustomLookAndFeel::makeFont (21.0f));
                g.setColour (errorColour);
                g.drawText ("> LOAD FAILED", bounds.withY (block_y).withHeight (line_height),
                            Justification::centred, false);

                if (sound_panel->loadErrorMessage.isNotEmpty())
                {
                    g.setFont (CustomLookAndFeel::makeFont (15.0f));
                    g.setColour (errorColour.withAlpha (0.7f));
                    g.drawFittedText (sound_panel->loadErrorMessage,
                                      bounds.withY (block_y + line_height + line_gap).withHeight (line_height * 2),
                                      Justification::centred, 2);
                }
                return;
            }

            if (sound_panel->sound == nullptr || ! sound_panel->sound->loaded)
            {
                // Empty slot: a subtle drop hint.
                g.setFont (CustomLookAndFeel::makeFont (19.0f));
                g.setColour (GUI::Color::Logo.withAlpha (0.28f));
                g.drawText ("> DROP SOUND HERE <", bounds, Justification::centred, false);
                return;
            }

            // File name (all caps) and extension, stacked in a compact block
            // centered in the loader.
            const int block_y = bounds.getCentreY() - (line_height * 2 + line_gap) / 2;

            g.setFont (CustomLookAndFeel::makeFont (21.0f));
            g.setColour (GUI::Color::Logo);
            g.drawFittedText ("> " + juce::String (sound_panel->sound->name).toUpperCase(),
                              bounds.withY (block_y).withHeight (line_height),
                              Justification::centred, 1);

            // Sub-line: extension plus attack/residual presence (spectral
            // components this slot actually carries: +A attack, +R residual).
            juce::String subLine = ":: " + juce::String (sound_panel->sound->extension).toUpperCase();
            if (sound_panel->sound->model != nullptr)
            {
                if (! sound_panel->sound->model->values.attack.empty())
                    subLine += " +A";
                if (! sound_panel->sound->model->values.residual.empty())
                    subLine += " +R";
            }

            // Sub-line sits 3px above the natural slot.
            const int subLineY = block_y + line_height + line_gap
                                 - juce::roundToInt (3.0f * zs);
            g.setColour (GUI::Color::Logo.withAlpha (0.45f));
            g.setFont (CustomLookAndFeel::makeFont (16.0f));
            g.drawText (subLine,
                        bounds.withY (subLineY).withHeight (line_height),
                        Justification::centred, false);
        }

        SoundPanel* sound_panel;
    };

    void timerCallback() override
    {
        if (morph_location < MorphLocation::NUM_MORPH_LOCATIONS)
        {
            const std::shared_ptr<Sound> s = instrument->getMorphSound (morph_location);
            bool path_changed = (s != nullptr && this->current_sound_path != s->path);
            bool cleared = (s == nullptr && ! this->current_sound_path.empty());

            if (path_changed || cleared)
            {
                this->updateCurrentSound();
                isLoading = false;
                loadFailed = false;
                loadErrorMessage.clear();
                repaint();
            }

            // Live percent while an in-file analysis runs for this slot.
            if (isLoading)
                repaint();

            syncToggleButtons();
        }
    }

    void syncToggleButtons()
    {
        if (auto* p = mProcessor->parameters.getParameter (
                Morphex::PARAMETERS<float>[slotParam (engineSlot(), 4)].ID))
            cursorRevButton.setToggleState (p->getValue() > 0.5f,
                                            NotificationType::dontSendNotification);
        if (auto* p = mProcessor->parameters.getParameter (
                Morphex::PARAMETERS<float>[slotLoopParam (engineSlot())].ID))
            slotLoopButton.setToggleState (p->getValue() > 0.5f,
                                           NotificationType::dontSendNotification);
    }

    void updateCurrentSound()
    {
        this->sound = instrument->getMorphSound (morph_location);
        if (this->sound != nullptr)
        {
            this->current_sound_path = this->sound->path;
            removeButton.setVisible (sound->loaded);
        }
        else
        {
            this->current_sound_path.clear();
            removeButton.setVisible (false);
        }
        applyIconAvailability();
    }

    void applyIconAvailability()
    {
        const bool loaded = (sound != nullptr && sound->loaded && ! isLoading);
        cursorRevButton.setEnabled (loaded);
        cursorRevButton.setAlpha (loaded ? 1.0f : 0.35f);
        slotLoopButton.setEnabled (loaded);
        slotLoopButton.setAlpha (loaded ? 1.0f : 0.35f);

        const bool xOn = loaded && ! showCursors;
        removeButton.setEnabled (xOn);
        removeButton.setAlpha (xOn ? 1.0f : 0.35f);
    }

    std::string current_sound_path;

    int i_sound_num;
    int slot_index;
    MorphLocation morph_location;

    SpectralMorphingToolAudioProcessor* mProcessor;
    Instrument* instrument;
    std::shared_ptr<Sound> sound;

    SoundNumberPanel soundNumberPanel;
    SoundNamePanel soundNamePanel;

    juce::TextButton removeButton {"X"};
    ViewFlipButton cursorViewButton;
    ReverseToggleButton cursorRevButton;
    TransportIconButton slotLoopButton { TransportIconButton::Glyph::Loop };

    std::unique_ptr<SquareKnob> cursorRateKnob;
    std::unique_ptr<SquareKnob> cursorOffsetKnob;
    std::unique_ptr<SquareKnob> cursorStartKnob;
    std::unique_ptr<SquareKnob> cursorEndKnob;
    std::unique_ptr<SquareKnob> cursorFormantKnob;
    bool showCursors = false;

    bool isDragOver = false;
    bool isLoading = false;
    bool loadFailed = false;
    juce::String loadErrorMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundPanel)
};
