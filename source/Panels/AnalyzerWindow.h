#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <memory>

#include "WaveformView.h"
#include "PartialsView.h"
#include "../Components/SquareFader.h"
#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/InterfaceDefines.h"
#include "../Helpers/SMTConstants.h"
#include "../PluginProcessor.h"

// Analyzer window; HPS backend, no onset layer.
class AnalyzerWindow : public Component,
                       public juce::FileDragAndDropTarget,
                       public juce::DragAndDropTarget,
                       private juce::MultiTimer
{
public:
    explicit AnalyzerWindow (SpectralMorphingToolAudioProcessor* inProcessor)
        : mProcessor (inProcessor),
          sourceView (inProcessor, false),
          partialsView (inProcessor),
          resynthView (inProcessor, true),
          resolutionVal (1001.0), windowVal (1024.0), ampFloorVal (-100.0),
          onsetSensVal (0.5), fundamentalVal (440.0),
          f0LoVal (100.0), f0HiVal (1000.0),
          srcVolDbVal (0.0), rsnVolDbVal (0.0)
    {
        addAndMakeVisible (sourceView);
        addAndMakeVisible (partialsView);
        addAndMakeVisible (resynthView);
        addAndMakeVisible (infoLabel);

        // Restore persisted analyzer session settings (dials + preview vols
        // survive restarts via settings.xml, like voices/legato).
        if (auto* pm = mProcessor->getPresetManager())
        {
            resolutionVal = juce::jlimit (64.0, 8192.0, pm->getAnalyzerResolution());
            windowVal = (double) snapWin (juce::jlimit (64.0, 8192.0, pm->getAnalyzerWindow()));
            ampFloorVal = juce::jlimit (-180.0, 0.0, pm->getAnalyzerAmpFloor());
            onsetSensVal = juce::jlimit (0.25, 1.25, pm->getAnalyzerOnsetSens());
            fundamentalVal = juce::jlimit (0.0, 2000.0, pm->getAnalyzerFundamental());
            f0LoVal = juce::jlimit (20.0, 1000.0, pm->getAnalyzerF0Lo());
            f0HiVal = juce::jlimit (50.0, 5000.0, pm->getAnalyzerF0Hi());
            // Restored pairs predate blocking: enforce the guard band once.
            if ((double) f0LoVal.getValue() > (double) f0HiVal.getValue() - 10.0)
                f0LoVal = (double) f0HiVal.getValue() - 10.0;
            srcVolDbVal = juce::jlimit (-40.0, 6.0, pm->getAnalyzerSrcDb());
            rsnVolDbVal = juce::jlimit (-40.0, 6.0, pm->getAnalyzerRsnDb());
        }

        // Empty SOURCE goes straight to the file browser, like OPEN AUDIO.
        sourceView.onEmptyClick = [this] { openBrowser(); };
        resynthView.onEmptyClick = [this] { mProcessor->synthesizeScratch(); };

        // Size rows like VOLUME; free resolution, power-of-two FFT.
        resFader = std::make_unique<SquareFader> (resolutionVal, "RESOLUTION",
                                                  64.0, 8192.0, 1.0, "", 0.35, 0);
        resFader->setTooltip ("HPS window size -- set freely, clamped to the FFT at analyze time");
        resFader->setVerticalValue (true);
        resFader->setTrackInsetU (4.0f);
        resFader->setCaptionScale (0.7f);
        resFader->onValueChange = [this]
        {
            // Block above the FFT instead of leaving an unanalyzable pair.
            const double fft = (double) windowVal.getValue();
            if ((double) resolutionVal.getValue() > fft)
                resFader->setValue (fft, NotificationType::sendNotification);
            markAnalyzerSettingsDirty();
        };
        addAndMakeVisible (resFader.get());

        winFader = std::make_unique<SquareFader> (windowVal, "WINDOW",
                                                  256.0, 8192.0, 1.0, "", 0.35, 0);
        winFader->setTooltip ("FFT size -- snaps to powers of two, always >= resolution");
        winFader->setVerticalValue (true);
        winFader->setTrackInsetU (4.0f);
        winFader->setCaptionScale (0.7f);
        winFader->onValueChange = [this]
        {
            // Snap to the stepped sizes, then pull a too-large window down.
            const int snapped = snapWin (winFader->getValue());
            if ((double) snapped != winFader->getValue())
                winFader->setValue ((double) snapped, NotificationType::sendNotification);
            else
                windowVal.setValue ((double) snapped);
            const double fft = (double) windowVal.getValue();
            if ((double) resolutionVal.getValue() > fft)
            {
                resolutionVal.setValue (fft);
                resFader->refresh();
            }
            markAnalyzerSettingsDirty();
        };
        addAndMakeVisible (winFader.get());

        auto addParamFader = [&] (std::unique_ptr<SquareFader>& slot,
                                  const char* name, juce::Value bound,
                                  double lo, double hi, double step,
                                  const char* unit, const char* tip,
                                  double skew = 1.0, int decimals = 1)
        {
            slot = std::make_unique<SquareFader> (bound, name, lo, hi, step, unit, skew, decimals);
            if (tip != nullptr)
                slot->setTooltip (tip);
            slot->setVerticalValue (true);
            slot->setTrackInsetU (4.0f);
            slot->setCaptionScale (0.45f);
            slot->setCaptionBelow (true);
            slot->onValueChange = [this] { markAnalyzerSettingsDirty(); };
            addAndMakeVisible (slot.get());
        };

        addParamFader (ampFloorFader, "AMP FLOOR", ampFloorVal, -180.0, 0.0, 0.5, " dB",
                       "Quietest partial kept -- anything below this level is discarded");
        addParamFader (onsetFader, "ONSET SENS", onsetSensVal, 0.25, 1.25, 0.01, " x",
                       "Track-pruning strictness -- higher keeps shorter partials (applies on the next analysis)",
                       1.0, 2);
        addParamFader (fundFader, "FUNDAMENTAL", fundamentalVal, 0.0, 2000.0, 1.0, " Hz",
                       "Reference pitch for the partials-view f0 line (0 = hidden)", 0.35, 0);
        addParamFader (f0LoFader, "F0 LO", f0LoVal, 20.0, 1000.0, 1.0, " Hz",
                       "Lowest fundamental the tracker considers -- lower hears bass (E1 = 41 Hz)",
                       0.35, 0);
        addParamFader (f0HiFader, "F0 HI", f0HiVal, 50.0, 5000.0, 1.0, " Hz",
                       "Highest fundamental the tracker considers -- higher hears bright sources",
                       0.35, 0);

        // Mutual F0 blocking: dragged fader stops, never overlaps.
        f0LoFader->onValueChange = [this]
        {
            const double hi = (double) f0HiVal.getValue();
            if ((double) f0LoVal.getValue() > hi - 10.0)
            {
                f0LoVal.setValue (hi - 10.0);
                f0LoFader->refresh();
            }
            markAnalyzerSettingsDirty();
        };
        f0HiFader->onValueChange = [this]
        {
            const double lo = (double) f0LoVal.getValue();
            if ((double) f0HiVal.getValue() < lo + 10.0)
            {
                f0HiVal.setValue (lo + 10.0);
                f0HiFader->refresh();
            }
            markAnalyzerSettingsDirty();
        };

        fundFader->onValueChange = [this]
        {
            partialsView.setFundamentalHz ((double) fundamentalVal.getValue());
            markAnalyzerSettingsDirty();
        };
        partialsView.setFundamentalHz ((double) fundamentalVal.getValue());

        srcVolFader = std::make_unique<SquareFader> (srcVolDbVal, "SOURCE", -40.0, 6.0, 0.5, " dB");
        srcVolFader->setTooltip ("Monitor volume for source preview");
        srcVolFader->setVerticalValue (true);
        srcVolFader->setTrackInsetU (4.0f);
        srcVolFader->setCaptionScale (0.7f);
        srcVolFader->onValueChange = [this]
        {
            pushVolToProcessor (false);
            markAnalyzerSettingsDirty();
        };
        addAndMakeVisible (srcVolFader.get());

        rsnVolFader = std::make_unique<SquareFader> (rsnVolDbVal, "RESYNTHESIS", -40.0, 6.0, 0.5, " dB");
        rsnVolFader->setTooltip ("Monitor volume for resynthesis preview");
        rsnVolFader->setVerticalValue (true);
        rsnVolFader->setTrackInsetU (4.0f);
        rsnVolFader->setCaptionScale (0.7f);
        rsnVolFader->onValueChange = [this]
        {
            pushVolToProcessor (true);
            markAnalyzerSettingsDirty();
        };
        addAndMakeVisible (rsnVolFader.get());
        // Restored vols must reach the engine (fader creation alone doesn't).
        pushVolToProcessor (false);
        pushVolToProcessor (true);

        clearButton.setTooltip ("Remove the source audio, partials and resynthesis");
        clearButton.onClick = [this]
        {
            auto doClear = [this] { mProcessor->clearAnalyzerSession(); };
            if (! mProcessor->getConfirmDestructive())
            {
                doClear();
                return;
            }
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Clear analyzer?",
                "Remove the source audio, partials and resynthesis?",
                "Clear", "Cancel", this,
                juce::ModalCallbackFunction::create ([doClear] (int result)
                {
                    if (result != 0)
                        doClear();
                }));
        };
        addAndMakeVisible (clearButton);

        vizExpandButton.setTooltip ("Expand visualization -- hide the parameters section so waveforms and partials fill the window");
        vizExpandButton.onClick = [this] { setViewMode (ViewMode::VizFull); };
        addAndMakeVisible (vizExpandButton);

        paramsExpandButton.setTooltip ("Expand parameters -- hide the visualization section so parameters fill the window");
        paramsExpandButton.onClick = [this] { setViewMode (ViewMode::ParamsFull); };
        addAndMakeVisible (paramsExpandButton);

        auto wireTextButton = [this] (juce::TextButton& b, const char* name, const char* tip, std::function<void()> fn)
        {
            b.setButtonText (name);
            b.setTooltip (tip);
            b.setLookAndFeel (&smallButtonLnf);
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        };

        wireTextButton (openButton, "OPEN AUDIO", "Browse audio (multi-select runs a batch)",
                        [this] { openBrowser(); });
        wireTextButton (analyzeButton, "ANALYZE", "Analyze the loaded source into partials (uses the bracket interval)",
                        [this]
                        {
                            if (mProcessor->getAnalyzerJobKind() >= 0 || mProcessor->isBatchActive())
                                mProcessor->cancelAnalyzerJobs();
                            else
                                doAnalyze();
                        });
        playSrcButton.onClick = [this] { togglePreview (false); };
        wireTextButton (playSrcButton, "PLAY SOURCE", "Play or stop the source audio", playSrcButton.onClick);
        playRsnButton.onClick = [this] { togglePreview (true); };
        wireTextButton (playRsnButton, "PLAY RESYNTH", "Play or stop the resynthesis", playRsnButton.onClick);
        wireTextButton (exportButton, "EXPORT", "Export the analysis (.had + resynthesis WAV) to a folder",
                        [this] { chooseExport(); });

        applyActionAvailability();
        syncViewMode();

        infoLabel.setJustificationType (juce::Justification::centredLeft);
        infoLabel.setFont (CustomLookAndFeel::makeFont (14.0f));
        infoLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.75f));
        infoLabel.setText ("> CLICK SOURCE OR PRESS OPEN", juce::NotificationType::dontSendNotification);
        addAndMakeVisible (infoLabel);

        startTimer (timerFast, 33);
        startTimer (timerSlow, 150);
    }

    ~AnalyzerWindow() override
    {
        stopTimer (timerFast);
        stopTimer (timerSlow);
        for (auto* b : { &openButton, &analyzeButton,
                         &playSrcButton, &playRsnButton, &exportButton })
            b->setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        drawCard (g, card1Rect, ">> VISUALIZATION");
        drawCard (g, card2Rect, viewMode == ViewMode::ParamsFull ? ">> ANALYSIS" : ">> PARAMETERS");
        drawInnerTitle (g, paramsRect, viewMode == ViewMode::ParamsFull ? ">> PARAMETERS" : ">> ANALYSIS");
        drawInnerTitle (g, volumeRect, ">> VOLUME");
        drawInnerTitle (g, actionsRect, ">> ACTIONS");

        if (isDragOver)
        {
            g.setColour (GUI::Color::Accent.withAlpha (0.60f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f, 2.0f);
        }
    }

    void resized() override
    {
        const float zs = MorphexZoom::uiScale;
        const int pad = juce::roundToInt (8.0f * zs);
        const int gapC = juce::roundToInt (8.0f * zs);
        const int titleH = juce::roundToInt (22.0f * zs);
        auto area = getLocalBounds().reduced ((int) (GUI::Layout::MainMargin * zs));

        infoLabel.setBounds (area.removeFromBottom (juce::roundToInt (16.0f * zs)));
        area.removeFromBottom (gapC);

        auto card1 = area;
        auto card2 = area;
        if (viewMode == ViewMode::VizFull)
        {
            card2 = juce::Rectangle<int>();
        }
        else if (viewMode == ViewMode::ParamsFull)
        {
            card1 = juce::Rectangle<int>();
        }
        else
        {
            const int card2H = (int) (area.getHeight() * 0.38f);
            card2 = area.removeFromBottom (card2H);
            area.removeFromBottom (gapC);
            card1 = area;
        }

        card1Rect = card1;
        card2Rect = card2;

        const auto headerCard = (viewMode == ViewMode::ParamsFull) ? card2 : card1;
        const int btn = juce::roundToInt (18.0f * zs);
        const int btnGap = juce::roundToInt (6.0f * zs);
        const int rightPad = juce::roundToInt (10.0f * zs);
        const int hdrY = headerCard.getY() + pad;
        int bx = headerCard.getRight() - rightPad - btn;
        clearButton.setBounds (bx, hdrY, btn, btn);
        bx -= btn + btnGap;
        paramsExpandButton.setBounds (bx, hdrY, btn, btn);
        bx -= btn + btnGap;
        vizExpandButton.setBounds (bx, hdrY, btn, btn);

        if (viewMode != ViewMode::ParamsFull)
        {
            auto v = card1.reduced (pad);
            v.removeFromTop (titleH);
            sourceView.setBounds (v.removeFromTop (juce::roundToInt (64.0f * zs)));
            v.removeFromTop (gapC);
            partialsView.setBounds (v.removeFromTop (juce::jmax (0, v.getHeight() - juce::roundToInt (64.0f * zs) - gapC)));
            v.removeFromTop (gapC);
            resynthView.setBounds (v);
        }

        if (viewMode != ViewMode::VizFull)
        {
            auto c2 = card2.reduced (pad);
            c2.removeFromTop (titleH);
            if (viewMode == ViewMode::ParamsFull)
            {
                const int specMinH = juce::roundToInt (64.0f * zs);
                const int paramsH = juce::jmax (0, juce::jmin (juce::roundToInt (230.0f * zs),
                                                               c2.getHeight() - gapC - specMinH));
                auto paramsArea = c2.removeFromBottom (paramsH);
                c2.removeFromBottom (juce::roundToInt (4.0f * zs));
                partialsView.setBounds (c2);
                c2 = paramsArea;
            }
            else if (c2.getHeight() > juce::roundToInt (230.0f * zs))
            {
                c2 = c2.withSizeKeepingCentre (c2.getWidth(), juce::roundToInt (230.0f * zs));
            }
            auto actions = c2.removeFromRight ((int) (c2.getWidth() * 0.30f));
            c2.removeFromRight (gapC);
            const int volW = juce::jmin (juce::roundToInt (140.0f * zs), c2.getWidth() / 2);
            auto volume = c2.removeFromRight (volW);
            c2.removeFromRight (gapC);
            auto analysis = c2;

            paramsRect = analysis;
            volumeRect = volume;
            actionsRect = actions;

            auto ab = analysis;
            ab.removeFromLeft (juce::roundToInt (12.0f * zs));
            ab.removeFromRight (juce::roundToInt (12.0f * zs));
            ab.removeFromTop (viewMode == ViewMode::ParamsFull ? juce::roundToInt (4.0f * zs)
                                                              : juce::roundToInt (8.0f * zs));
            ab.removeFromBottom (juce::roundToInt (12.0f * zs));
            ab.removeFromTop (juce::roundToInt (17.0f * zs));
            layoutDialBank (ab);

            auto vb = volume;
            vb.removeFromLeft (juce::roundToInt (12.0f * zs));
            vb.removeFromRight (juce::roundToInt (12.0f * zs));
            vb.removeFromTop (juce::roundToInt (8.0f * zs));
            vb.removeFromBottom (juce::roundToInt (12.0f * zs));
            vb.removeFromTop (juce::roundToInt (17.0f * zs));
            const int faderW = vb.getWidth() / 2;
            srcVolFader->setBounds (vb.removeFromLeft (faderW));
            rsnVolFader->setBounds (vb);

            auto ac = actions;
            ac.removeFromLeft (juce::roundToInt (12.0f * zs));
            ac.removeFromRight (juce::roundToInt (12.0f * zs));
            ac.removeFromTop (juce::roundToInt (8.0f * zs));
            ac.removeFromBottom (juce::roundToInt (12.0f * zs));
            ac.removeFromTop (juce::roundToInt (17.0f * zs));
            layoutButtonGrid (ac);
        }
        else
        {
            paramsRect = juce::Rectangle<int>();
            volumeRect = juce::Rectangle<int>();
            actionsRect = juce::Rectangle<int>();
        }
    }

    void drawCard (juce::Graphics& g, const juce::Rectangle<int>& card, const juce::String& title)
    {
        if (card.getWidth() <= 0 || card.getHeight() <= 0)
            return;

        const float zs = MorphexZoom::uiScale;
        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (GUI::Paint::insetCardBounds (card.toFloat()), 4.0f * zs);
        GUI::Paint::drawCardOutline (g, card.toFloat(), 4.0f * zs);

        g.setColour (GUI::Color::KeyDown);
        g.setFont (CustomLookAndFeel::makeFont (18.0f));
        g.drawText (title, card.reduced (juce::roundToInt (12.0f * zs), 0)
                                .withTrimmedTop (juce::roundToInt (4.0f * zs))
                                .withHeight (juce::roundToInt (24.0f * zs)),
                    juce::Justification::topLeft, false);
    }

    void drawInnerTitle (juce::Graphics& g, const juce::Rectangle<int>& inner, const juce::String& title)
    {
        if (inner.getWidth() <= 0 || inner.getHeight() <= 0)
            return;

        const float zs = MorphexZoom::uiScale;
        GUI::Paint::drawCardOutline (g, inner.toFloat(), GUI::Layout::InnerCardCorner);
        g.setColour (GUI::Color::KeyDown);
        const float titleSize = (viewMode == ViewMode::ParamsFull) ? 16.0f : 13.0f;
        g.setFont (CustomLookAndFeel::makeFont (titleSize));
        g.drawText (title, inner.reduced (juce::roundToInt (8.0f * zs), 0)
                                .withTrimmedTop (juce::roundToInt (4.0f * zs))
                                .withHeight (juce::roundToInt (titleSize * zs)),
                    juce::Justification::topLeft, false);
    }

    static bool isSupportedDropFile (const juce::File& f)
    {
        const juce::String ext = f.getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff"
            || ext == ".mp3" || ext == ".flac" || ext == ".ogg";
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        if (files.isEmpty())
            return true;
        for (auto& f : files)
            if (isSupportedDropFile (juce::File (f)))
                return true;
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        isDragOver = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        isDragOver = false;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        isDragOver = false;
        for (auto& f : files)
        {
            if (isSupportedDropFile (juce::File (f)))
            {
                handleDroppedAudioFile (juce::File (f));
                break;
            }
        }
        repaint();
    }

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const juce::String path = details.description.toString();
        return path != "directory" && isSupportedDropFile (juce::File (path));
    }

    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails&) override
    {
        isDragOver = true;
        repaint();
    }

    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        isDragOver = false;
        repaint();
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override
    {
        isDragOver = false;
        const juce::String path = dragSourceDetails.description.toString();
        if (path != "directory" && isSupportedDropFile (juce::File (path)))
            handleDroppedAudioFile (juce::File (path));
        repaint();
    }

    void handleDroppedAudioFile (const juce::File& f)
    {
        if (f.getFileExtension().equalsIgnoreCase (".had"))
        {
            infoLabel.setText ("> .had files load into slots, not the analyzer -- open audio instead",
                               juce::NotificationType::dontSendNotification);
            return;
        }

        const juce::String err = mProcessor->openAnalyzerSource (f);
        if (err.isNotEmpty())
            infoLabel.setText ("> " + err, juce::NotificationType::dontSendNotification);
        else
            refreshSessionViews();
    }

private:
    static constexpr int timerFast = 1;
    static constexpr int timerSlow = 2;

    struct SmallButtonLookAndFeel : public CustomLookAndFeel
    {
        juce::Font getTextButtonFont (juce::TextButton&, int) override
        {
            return CustomLookAndFeel::makeFont (17.0f);
        }
    };

    struct HeaderIconButton : public juce::ToggleButton
    {
        enum class Kind { Monitor, Faders, Close };

        explicit HeaderIconButton (Kind k) : kind (k) {}

        void paint (juce::Graphics& g) override
        {
            getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId),
                                                   isMouseOver(), isDown());

            const auto bounds = getLocalBounds().toFloat();
            const float cx = bounds.getCentreX();
            const float cy = bounds.getCentreY();
            const float u = MorphexZoom::uiScale;
            const bool toggledOn = getToggleState();

            g.setColour (toggledOn || isMouseOver() ? MorphexColors::textPrimary : MorphexColors::textMid);

            if (kind == Kind::Monitor)
            {
                g.drawRoundedRectangle (cx - 6.2f * u, cy - 5.6f * u, 12.4f * u, 7.9f * u,
                                        1.0f * u, 1.2f * u);
                g.drawLine (cx, cy + 2.3f * u, cx, cy + 4.7f * u, 1.3f * u);
                g.drawLine (cx - 3.1f * u, cy + 4.7f * u, cx + 3.1f * u, cy + 4.7f * u, 1.3f * u);
            }
            else if (kind == Kind::Faders)
            {
                const float lineH = 1.2f * u;
                const float halfW = 5.8f * u;
                const float knob = 2.5f * u;
                const float rows[3] = { cy - 3.6f * u, cy, cy + 3.6f * u };
                const float knobX[3] = { cx - 2.2f * u, cx + 1.8f * u, cx - 0.4f * u };
                for (int i = 0; i < 3; ++i)
                {
                    g.fillRect (cx - halfW, rows[i] - lineH * 0.5f, halfW * 2.0f, lineH);
                    g.fillRect (knobX[i] - knob * 0.5f, rows[i] - knob * 0.5f, knob, knob);
                }
            }
            else
            {
                const float arm = 4.0f * u;
                g.drawLine (cx - arm, cy - arm, cx + arm, cy + arm, 1.7f * u);
                g.drawLine (cx + arm, cy - arm, cx - arm, cy + arm, 1.7f * u);
            }
        }

    private:
        Kind kind;
    };

    static int snapWin (double v) noexcept
    {
        int best = 1024;
        for (int c : { 256, 512, 1024, 2048, 4096, 8192 })
            if (std::abs ((double) c - v) < std::abs ((double) best - v))
                best = c;
        return best;
    }

    void layoutDialBank (juce::Rectangle<int> area)
    {
        // Size column (RESOLUTION knob over WINDOW combo) plus the five
        // square knobs on the side, mirroring the actions-card columns.
        const float zs = MorphexZoom::uiScale;
        const int colW = area.getWidth() / 6;
        const int inset = juce::roundToInt (2.0f * zs);
        const int sizeH = area.getHeight() / 2;
        resFader->setBounds (juce::Rectangle<int> (
            area.getX(), area.getY(), colW, sizeH).reduced (inset));
        winFader->setBounds (juce::Rectangle<int> (
            area.getX(), area.getY() + sizeH, colW, area.getHeight() - sizeH)
                             .reduced (inset));
        auto place = [&] (juce::Component* cell, int c)
        {
            if (cell != nullptr)
                cell->setBounds (juce::Rectangle<int> (
                    area.getX() + (c + 1) * colW, area.getY(),
                    colW, area.getHeight()).reduced (inset));
        };
        juce::Component* faders[] = {
            ampFloorFader.get(), onsetFader.get(), fundFader.get(),
            f0LoFader.get(), f0HiFader.get(),
        };
        for (int c = 0; c < 5; ++c)
            place (faders[(size_t) c], c);
    }

    void layoutButtonGrid (juce::Rectangle<int> area)
    {
        juce::TextButton* left[2] =
        {
            &openButton, &analyzeButton,
        };
        juce::TextButton* right[3] =
        {
            &playSrcButton, &playRsnButton, &exportButton,
        };

        const float zs = MorphexZoom::uiScale;
        const int gapG = juce::roundToInt (8.0f * zs);
        const int colW = (area.getWidth() - gapG) / 2;

        auto layoutColumn = [&] (juce::TextButton** buttons, int count, int x)
        {
            const int total = area.getHeight();
            for (int i = 0; i < count; ++i)
            {
                const int top = area.getY() + (i * (total + gapG)) / count;
                const int bottom = (i + 1 == count) ? area.getBottom()
                    : area.getY() + (((i + 1) * (total + gapG)) / count) - gapG;
                buttons[i]->setBounds (x, top, colW, juce::jmax (1, bottom - top));
            }
        };

        layoutColumn (left, 2, area.getX());
        layoutColumn (right, 3, area.getX() + colW + gapG);
    }

    void applyActionAvailability()
    {
        const bool hasSource = mProcessor->hasAnalyzerSource();
        const bool hasFrames = mProcessor->hasAnalyzerPartials();
        const bool hasResynth = mProcessor->hasAnalyzerResynth();
        const int kind = mProcessor->getAnalyzerJobKind();
        const bool idle = kind < 0 && ! mProcessor->isBatchActive();

        setActionAvailable (openButton, true);
        // A running job turns ANALYZE into CANCEL (covers batch and the
        // empty-resynth synthesize path too).
        if (kind >= 0)
        {
            setActionAvailable (analyzeButton, true);
            setActionText (analyzeButton, "CANCEL");
            analyzeButton.setTooltip ("Cancel the running job");
        }
        else
        {
            setActionText (analyzeButton, "ANALYZE");
            analyzeButton.setTooltip ("Analyze the loaded source into partials (uses the bracket interval)");
            setActionAvailable (analyzeButton, hasSource && idle);
        }
        setActionAvailable (playSrcButton, hasSource && previewFreeFor (false));
        setActionAvailable (playRsnButton, hasResynth && previewFreeFor (true));
        setActionAvailable (exportButton, hasFrames && idle);
    }

    // Mutually exclusive preview: while one side plays, the other button
    // dims -- stop first, then play the other side.
    bool previewFreeFor (bool resynth) const
    {
        return ! mProcessor->previewPlaying()
               || mProcessor->isPreviewingResynth() == resynth;
    }

    static void setActionAvailable (juce::TextButton& b, bool available)
    {
        if (b.isEnabled() != available)
        {
            b.setEnabled (available);
            b.setAlpha (available ? 1.0f : 0.35f);
        }
    }

    static void setActionText (juce::TextButton& b, const char* text)
    {
        if (b.getButtonText() != juce::String (text))
            b.setButtonText (text);
    }

    void pushVolToProcessor (bool resynth)
    {
        const float db = (float) (double) (resynth ? rsnVolDbVal.getValue() : srcVolDbVal.getValue());
        mProcessor->setPreviewVolDb (resynth, db);
    }

    void markAnalyzerSettingsDirty() { analyzerSettingsDirty = true; }

    void saveAnalyzerSettings()
    {
        if (auto* pm = mProcessor->getPresetManager())
        {
            pm->setAnalyzerSettings ((double) resolutionVal.getValue(),
                                     (double) windowVal.getValue(),
                                     (double) ampFloorVal.getValue(),
                                     (double) onsetSensVal.getValue(),
                                     (double) fundamentalVal.getValue(),
                                     (double) f0LoVal.getValue(),
                                     (double) f0HiVal.getValue(),
                                     (double) srcVolDbVal.getValue(),
                                     (double) rsnVolDbVal.getValue());
        }
    }

    void togglePreview (bool resynth)
    {
        juce::TextButton& button = resynth ? playRsnButton : playSrcButton;
        const char* playText = resynth ? "PLAY RESYNTH" : "PLAY SOURCE";

        if (mProcessor->previewPlaying()
            && mProcessor->isPreviewingResynth() == resynth)
        {
            mProcessor->previewStop();
            button.setButtonText (playText);
            infoLabel.setText ("> playback stopped", juce::NotificationType::dontSendNotification);
            applyActionAvailability();
            return;
        }

        mProcessor->previewStop();

        if (resynth)
        {
            if (! mProcessor->hasAnalyzerResynth())
            {
                infoLabel.setText ("> nothing synthesized yet -- press ANALYZE first",
                                   juce::NotificationType::dontSendNotification);
                applyActionAvailability();
                return;
            }
            mProcessor->playPreviewResynth();
        }
        else
        {
            if (! mProcessor->hasAnalyzerSource())
            {
                infoLabel.setText ("> no source loaded", juce::NotificationType::dontSendNotification);
                applyActionAvailability();
                return;
            }
            mProcessor->playPreviewSource();
        }
        button.setButtonText ("STOP");
        applyActionAvailability();

        const double volDb = (double) (resynth ? rsnVolDbVal.getValue() : srcVolDbVal.getValue());
        infoLabel.setText (juce::String ("> playing ")
                               + (resynth ? "RESYNTHESIS" : "SOURCE")
                               + "..."
                               + (volDb < -30.0
                                      ? "  -- note: volume dial is at " + juce::String (volDb, 1) + " dB"
                                      : juce::String()),
                           juce::NotificationType::dontSendNotification);
    }

    void doAnalyze()
    {
        mProcessor->analyzeScratch (buildDialSettings());
    }

    morphex::analyzer::Settings buildDialSettings() const
    {
        morphex::analyzer::Settings settings;
        // Resolution runs free, so clamp it to the FFT here (host
        // automation writes the Values directly, bypassing the UI).
        settings.fftSize = juce::jmax (256, snapWin ((double) windowVal.getValue()));
        settings.windowSize = juce::jmin (juce::jmax (64, (int) std::round ((double) resolutionVal.getValue())),
                                           settings.fftSize);
        settings.ampFloorDb = (float) (double) ampFloorVal.getValue();
        // ONSET SENS dial: the only honest HPS mapping is
        // track-pruning strictness (higher keeps shorter).
        const double sens = juce::jlimit (0.25, 1.25, (double) onsetSensVal.getValue());
        settings.minSineDur = 0.10 / sens;
        settings.fundamental = (float) (double) fundamentalVal.getValue();
        // F0 range, sanitized even if a host automated the Values past each
        // other (the dials also limit each other live, 10 Hz guard band).
        const double hi = juce::jlimit (50.0, 5000.0, (double) f0HiVal.getValue());
        const double lo = juce::jmin (juce::jlimit (20.0, 1000.0, (double) f0LoVal.getValue()),
                                      hi - 10.0);
        settings.minF0 = lo;
        settings.maxF0 = juce::jmax (hi, lo + 10.0);
        return settings;
    }

    void openBrowser()
    {
        openChooser = std::make_unique<juce::FileChooser> (
            "Open audio for analysis (multi-select runs a batch)",
            juce::File (getDefaultCollectionsDirectory()),
            "*.wav;*.wave;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
        openChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto files = fc.getResults();
                                      if (files.isEmpty())
                                          return;
                                      if (files.size() == 1)
                                      {
                                          const juce::String err =
                                              mProcessor->openAnalyzerSource (files[0]);
                                          if (err.isNotEmpty())
                                              infoLabel.setText ("> " + err,
                                                                  juce::NotificationType::dontSendNotification);
                                          else
                                              refreshSessionViews();
                                          return;
                                      }
                                      std::vector<juce::File> v;
                                      for (auto& f : files)
                                          v.push_back (f);
                                      mProcessor->analyzeBatchFiles (v, buildDialSettings());
                                  });
    }

    void chooseExport()
    {
        juce::File initial = juce::File (getDefaultAnalyzerDirectory());
        exportChooser = std::make_unique<juce::FileChooser> ("Export analysis (.had + resynthesis WAV) to folder",
                                                             initial);
        exportChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                    [this] (const juce::FileChooser& fc)
                                    {
                                        const auto dir = fc.getResult();
                                        if (! dir.isDirectory())
                                            return;
                                        juce::String err;
                                        if (mProcessor->exportScratchToFolder (dir, err))
                                            infoLabel.setText ("> exported to " + dir.getFileName(),
                                                                juce::NotificationType::dontSendNotification);
                                        else
                                            infoLabel.setText ("> " + err, juce::NotificationType::dontSendNotification);
                                    });
    }

    void timerCallback (int id) override
    {
        if (id == timerFast)
        {
            const bool playing = mProcessor->previewPlaying();
            if (playing)
            {
                sourceView.repaint();
                resynthView.repaint();
            }

            if (wasPlaying && ! playing)
            {
                infoLabel.setText ("> playback finished", juce::NotificationType::dontSendNotification);
                playSrcButton.setButtonText ("PLAY SOURCE");
                playRsnButton.setButtonText ("PLAY RESYNTH");
                applyActionAvailability();
            }
            wasPlaying = playing;

            if (mProcessor->isBatchActive())
            {
                const int done = mProcessor->getBatchDone();
                const int total = juce::jmax (1, mProcessor->getBatchTotal());
                const int pct = (int) std::round (100.0f * mProcessor->getAnalyzerJobProgress());
                infoLabel.setText ("> BATCH " + juce::String (juce::jmin (done + 1, total))
                                       + "/" + juce::String (total)
                                       + " " + mProcessor->getBatchCurrentName()
                                       + "... " + juce::String (pct) + "%",
                                   juce::NotificationType::dontSendNotification);
                return;
            }

            const int kind = mProcessor->getAnalyzerJobKind();
            if (kind >= 0)
            {
                const int pct = (int) std::round (100.0f * mProcessor->getAnalyzerJobProgress());
                const juce::String what = kind == 0 ? "ANALYZING" : "SYNTHESIZING";
                infoLabel.setText ("> " + what + "... "
                                       + juce::String (pct) + "% "
                                       + mProcessor->getAnalyzerJobStage().toUpperCase(),
                                   juce::NotificationType::dontSendNotification);
                return;
            }
            if (playing)
            {
                const double pos = mProcessor->getPreviewCursorMirrorSec();
                const bool rsn = mProcessor->isPreviewingResynth();
                double dur = 0.0;
                if (rsn)
                {
                    auto rb = mProcessor->getAnalyzerResynthBuffer();
                    if (rb != nullptr)
                        dur = (double) rb->size() / juce::jmax (1.0, mProcessor->getAnalyzerSampleRate());
                }
                else
                {
                    dur = mProcessor->getAnalyzerSourceDurationSec();
                }

                infoLabel.setText (juce::String ("> playing ")
                                       + (rsn ? "RESYNTH" : "SOURCE")
                                       + "... " + juce::String (juce::jlimit (0.0, dur > 0.0 ? dur : pos, pos), 2) + "s / "
                                       + juce::String (dur, 2) + "s",
                                   juce::NotificationType::dontSendNotification);
                return;
            }
            composeIdleInfoLine();
            return;
        }

        refreshSessionViews();

        // Bounded persistence write for any dial/fader turns since last tick.
        if (analyzerSettingsDirty)
        {
            analyzerSettingsDirty = false;
            saveAnalyzerSettings();
        }

        clearButton.setVisible (mProcessor->hasAnalyzerSource());
        applyActionAvailability();

        if (! mProcessor->previewPlaying())
        {
            if (playSrcButton.getButtonText() == "STOP") playSrcButton.setButtonText ("PLAY SOURCE");
            if (playRsnButton.getButtonText() == "STOP") playRsnButton.setButtonText ("PLAY RESYNTH");
        }
    }

    void refreshSessionViews()
    {
        const int srcGen = mProcessor->getAnalyzerSourceGeneration();
        if (srcGen != lastSourceGen)
        {
            lastSourceGen = srcGen;
            lastFramesCount = -1;
            lastResynthSize = -1;
            repaint();
        }

        auto rb = mProcessor->getAnalyzerResynthBuffer();
        const int rsSize = rb != nullptr ? (int) rb->size() : -1;
        if (rsSize != lastResynthSize)
        {
            lastResynthSize = rsSize;
        }

        // Partials copy is heavy (full freq/amp vectors): only refresh when
        // the generation moved since the last partials cache.
        if (srcGen == lastPartialGen && lastFramesCount >= 0)
            return;
        lastPartialGen = srcGen;

        const auto partials = mProcessor->getAnalyzerPartials();
        const int frameCount = partials.numFrames;
        lastFramesCount = frameCount;

        // Cache max-freq / max-active for the idle info line.
        cachedMaxFreq = 0.0f;
        cachedMaxActive = 0;
        for (int f = 0; f < partials.numFrames; ++f)
        {
            int active = 0;
            for (int p = 0; p < partials.numPartials; ++p)
            {
                const size_t i = (size_t) f * (size_t) partials.numPartials + (size_t) p;
                if (i >= partials.freqs.size() || i >= partials.amps.size())
                    break;
                if (partials.freqs[i] >= 30.0f && partials.freqs[i] <= 12000.0f)
                {
                    cachedMaxFreq = juce::jmax (cachedMaxFreq, partials.freqs[i]);
                    if (partials.amps[i] > -120.0f)
                        ++active;
                }
            }
            cachedMaxActive = juce::jmax (cachedMaxActive, active);
        }
    }

    void composeIdleInfoLine()
    {
        const juce::String batchFinal = mProcessor->getBatchFinalMessage();
        if (batchFinal.isNotEmpty())
        {
            infoLabel.setText ("> " + batchFinal, juce::NotificationType::dontSendNotification);
            return;
        }

        if (! mProcessor->hasAnalyzerSource())
        {
            infoLabel.setText ("> CLICK SOURCE OR PRESS OPEN", juce::NotificationType::dontSendNotification);
            return;
        }

        const juce::String name = mProcessor->getAnalyzerSourceName();
        const double dur = mProcessor->getAnalyzerSourceDurationSec();
        const bool hasFrames = mProcessor->hasAnalyzerPartials();

        juce::String line = name + "  [" + juce::String (dur, 2) + "s @ "
                            + juce::String ((int) mProcessor->getAnalyzerSampleRate()) + " Hz]";
        if (hasFrames)
        {
            line << "   partials: " << lastFramesCount
                 << "   max freq: " << (int) cachedMaxFreq << " Hz"
                 << "   max active: " << cachedMaxActive;
        }
        else
        {
            line << "   (not analyzed yet)";
        }
        line << "   [" << juce::String (mProcessor->getAnalyzerIntervalStart(), 2)
             << " - " << juce::String (mProcessor->getAnalyzerIntervalEnd(), 2) << "]";
        infoLabel.setText ("> " + line, juce::NotificationType::dontSendNotification);
    }

    enum class ViewMode { Split, VizFull, ParamsFull };

    void setViewMode (ViewMode m)
    {
        if (m == viewMode)
            m = ViewMode::Split;
        viewMode = m;
        syncViewMode();
        resized();
    }

    void syncViewMode()
    {
        vizExpandButton.setToggleState (viewMode == ViewMode::VizFull,
                                        juce::NotificationType::dontSendNotification);
        paramsExpandButton.setToggleState (viewMode == ViewMode::ParamsFull,
                                           juce::NotificationType::dontSendNotification);
        setKnobFontScale (viewMode == ViewMode::ParamsFull ? 1.25f : 1.0f);
        applyViewVisibility();
    }

    void setKnobFontScale (float s)
    {
        for (auto* k : { ampFloorFader.get(), onsetFader.get(), fundFader.get(),
                         f0LoFader.get(), f0HiFader.get() })
            if (k != nullptr)
                k->setFontScale (2.0f);
        if (srcVolFader != nullptr)
            srcVolFader->setFontScale (2.0f);
        if (rsnVolFader != nullptr)
            rsnVolFader->setFontScale (2.0f);
        if (resFader != nullptr)
            resFader->setFontScale (2.0f);
        if (winFader != nullptr)
            winFader->setFontScale (2.0f);
    }

    void applyViewVisibility()
    {
        const bool showViz = viewMode != ViewMode::ParamsFull;
        const bool showParams = viewMode != ViewMode::VizFull;
        sourceView.setVisible (showViz);
        partialsView.setVisible (true);
        resynthView.setVisible (showViz);

        for (auto* k : { ampFloorFader.get(), onsetFader.get(), fundFader.get(),
                         f0LoFader.get(), f0HiFader.get() })
            if (k != nullptr)
                k->setVisible (showParams);
        if (resFader != nullptr)
            resFader->setVisible (showParams);
        if (winFader != nullptr)
            winFader->setVisible (showParams);
        if (srcVolFader != nullptr)
            srcVolFader->setVisible (showParams);
        if (rsnVolFader != nullptr)
            rsnVolFader->setVisible (showParams);
        for (auto* b : { &openButton, &analyzeButton,
                         &playSrcButton, &playRsnButton, &exportButton })
            b->setVisible (showParams);
    }

    SpectralMorphingToolAudioProcessor* mProcessor;

    bool isDragOver = false;
    ViewMode viewMode = ViewMode::ParamsFull;

    juce::Rectangle<int> card1Rect, card2Rect, paramsRect, volumeRect, actionsRect;

    WaveformView sourceView;
    PartialsView partialsView;
    WaveformView resynthView;
    juce::Label infoLabel;

    int lastSourceGen = -1;
    int lastPartialGen = -1;
    bool analyzerSettingsDirty = false;
    int lastFramesCount = -1;
    int lastResynthSize = -1;
    bool wasPlaying = false;
    float cachedMaxFreq = 0.0f;
    int cachedMaxActive = 0;

    juce::Value resolutionVal, windowVal, ampFloorVal, onsetSensVal, fundamentalVal;
    juce::Value f0LoVal, f0HiVal;
    juce::Value srcVolDbVal, rsnVolDbVal;
    std::unique_ptr<SquareFader> ampFloorFader, onsetFader, fundFader;
    std::unique_ptr<SquareFader> f0LoFader, f0HiFader;
    std::unique_ptr<SquareFader> resFader, winFader;
    std::unique_ptr<SquareFader> srcVolFader, rsnVolFader;

    SmallButtonLookAndFeel smallButtonLnf;

    juce::TextButton openButton, analyzeButton;
    juce::TextButton playSrcButton, playRsnButton, exportButton;
    HeaderIconButton clearButton { HeaderIconButton::Kind::Close };
    HeaderIconButton vizExpandButton { HeaderIconButton::Kind::Monitor };
    HeaderIconButton paramsExpandButton { HeaderIconButton::Kind::Faders };

    std::unique_ptr<juce::FileChooser> openChooser;
    std::unique_ptr<juce::FileChooser> exportChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerWindow)
};
