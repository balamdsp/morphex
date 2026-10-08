#pragma once

#include <JuceHeader.h>

#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/InterfaceDefines.h"
#include "../Helpers/SMTConstants.h"
#include "../PluginProcessor.h"

// SOURCE waveform: draggable interval brackets + play cursor.
class WaveformView : public Component, public Timer
{
public:
    WaveformView (SpectralMorphingToolAudioProcessor* inProcessor, bool isResynthView = false)
        : mProcessor (inProcessor), resynth (isResynthView)
    {
        startTimer (100);
    }

    std::shared_ptr<const std::vector<float>> viewBuffer() const
    {
        if (resynth)
            return mProcessor->getAnalyzerResynthBuffer();
        return mProcessor->getAnalyzerSourceBuffer();
    }

    std::function<void()> onEmptyClick;

    double viewSampleRate() const
    {
        const double sr = mProcessor->getAnalyzerSampleRate();
        return sr > 0.0 ? sr : 44100.0;
    }

    void paint (Graphics& g) override
    {
        g.fillAll (GUI::Color::CardDark);
        auto bounds = getLocalBounds().toFloat();

        g.setColour (GUI::Color::Logo.withAlpha (0.70f));
        g.setFont (CustomLookAndFeel::makeFont (16.0f));
        juce::String title = resynth ? ">> RESYNTH" : ">> SOURCE";
        if (zoomToInterval && ! resynth)
            title += "  >< ZOOMED (double-click to fit)";
        g.drawText (title,
                    bounds.removeFromTop (18.0f).reduced (6.0f, 0.0f),
                    Justification::topLeft, false);

        auto waveBounds = getLocalBounds().toFloat().withTrimmedTop (20.0f).reduced (6.0f);
        g.setColour (GUI::Color::Background);
        g.fillRoundedRectangle (waveBounds, 3.0f * MorphexZoom::uiScale);
        GUI::Paint::drawCardOutline (g, waveBounds, 3.0f * MorphexZoom::uiScale, 0.25f);

        auto buf = viewBuffer();
        if (buf == nullptr || buf->empty())
        {
            g.setColour (GUI::Color::Logo.withAlpha (0.35f));
            g.setFont (CustomLookAndFeel::makeFont (19.0f));
            g.drawText (resynth ? "> SYNTHESIZE TO INSPECT <" : "> DROP AUDIO HERE <",
                        waveBounds, Justification::centred, false);
            return;
        }

        const double sr = viewSampleRate();
        const double dur = (double) buf->size() / sr;
        if (sr <= 0.0 || dur <= 0.0)
            return;

        // View window: full file, or the bracket interval when magnified
        // (source view double-click toggle).
        double win0 = 0.0, win1 = dur;
        if (zoomToInterval && ! resynth)
        {
            const double s0 = mProcessor->getAnalyzerIntervalStart();
            const double s1 = mProcessor->getAnalyzerIntervalEnd();
            if (s1 > s0)
            {
                win0 = juce::jlimit (0.0, dur, s0);
                win1 = juce::jlimit (0.0, dur, s1);
            }
        }
        const double winLen = juce::jmax (1.0 / sr, win1 - win0);

        // Decimated peak trace.
        const int W = juce::jmax (1, (int) waveBounds.getWidth());
        g.setColour (GUI::Color::KeyDown.withAlpha (0.85f));
        juce::Path trace;
        const size_t N = buf->size();
        const size_t n0 = (size_t) (win0 / dur * (double) N);
        const size_t n1 = juce::jmax (n0 + 1, (size_t) (win1 / dur * (double) N));
        for (int x = 0; x < W; ++x)
        {
            const size_t i0 = n0 + (size_t) ((double) x / (double) W * (double) (n1 - n0));
            const size_t i1 = juce::jmax (i0 + 1, n0 + (size_t) ((double) (x + 1) / (double) W * (double) (n1 - n0)));
            float peak = 0.0f;
            for (size_t i = i0; i < i1 && i < N; ++i)
                peak = juce::jmax (peak, std::abs ((*buf)[i]));
            const float y = waveBounds.getCentreY() - peak * waveBounds.getHeight() * 0.48f;
            const float y2 = waveBounds.getCentreY() + peak * waveBounds.getHeight() * 0.48f;
            const float px = waveBounds.getX() + (float) x;
            if (x == 0)
                trace.startNewSubPath (px, y);
            trace.lineTo (px, y);
            trace.lineTo (px, y2);
        }
        g.strokePath (trace, juce::PathStrokeType (1.0f));

        // Interval brackets (source view only). Magnified: they sit at the
        // window edges to show the zoom is active.
        if (! resynth)
        {
            const double s0 = mProcessor->getAnalyzerIntervalStart();
            const double s1 = mProcessor->getAnalyzerIntervalEnd();
            const bool zoomed = zoomToInterval && win1 > win0 && (win0 > 0.0 || win1 < dur);
            const float x0 = zoomed ? waveBounds.getX()
                                    : waveBounds.getX() + (float) (s0 / dur * waveBounds.getWidth());
            const float x1 = zoomed ? waveBounds.getRight()
                                    : waveBounds.getX() + (float) (s1 / dur * waveBounds.getWidth());
            if (! zoomed)
            {
                g.setColour (GUI::Color::Background.withAlpha (0.72f));
                g.fillRect (juce::Rectangle<float> (waveBounds.getX(), waveBounds.getY(), x0 - waveBounds.getX(), waveBounds.getHeight()));
                g.fillRect (juce::Rectangle<float> (x1, waveBounds.getY(), waveBounds.getRight() - x1, waveBounds.getHeight()));
            }
            g.setColour (GUI::Color::KeyDown);
            g.drawLine (x0, waveBounds.getY(), x0, waveBounds.getBottom(), 1.5f);
            g.drawLine (x1, waveBounds.getY(), x1, waveBounds.getBottom(), 1.5f);
        }

        // Play cursor: only while the one-shot transport runs; the playhead
        // supplier only shows while previewPlaying().
        if (mProcessor->isPreviewPlaying())
        {
            const double cur = mProcessor->getPreviewCursorMirrorSec();
            const double rel = (cur - win0) / winLen;
            if (rel >= 0.0 && rel <= 1.0)
            {
                const float cx = waveBounds.getX() + (float) (rel * waveBounds.getWidth());
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawLine (cx, waveBounds.getY(), cx, waveBounds.getBottom(), 1.0f);
            }
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        auto buf = viewBuffer();
        if (buf == nullptr || buf->empty())
        {
            if (onEmptyClick)
                onEmptyClick();
            return;
        }
        if (resynth)
            return;
        dragStart = e.getPosition();
        dragStartInterval = { mProcessor->getAnalyzerIntervalStart(), mProcessor->getAnalyzerIntervalEnd() };
    }

    void mouseDoubleClick (const MouseEvent&) override
    {
        // Magnifier: zoom the trace to the bracket interval and back.
        if (resynth)
            return;
        if (viewBuffer() == nullptr)
            return;
        const double s0 = mProcessor->getAnalyzerIntervalStart();
        const double s1 = mProcessor->getAnalyzerIntervalEnd();
        const double dur = mProcessor->getAnalyzerSourceDurationSec();
        if (dur <= 0.0)
            return;
        if (zoomToInterval)
        {
            zoomToInterval = false;
        }
        else if (s1 > s0 && (s0 > dur * 0.001 || s1 < dur * 0.999))
        {
            zoomToInterval = true;
        }
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (resynth)
            return;
        const double dur = mProcessor->getAnalyzerSourceDurationSec();
        if (dur <= 0.0)
            return;
        auto waveBounds = getLocalBounds().toFloat().withTrimmedTop (20.0f).reduced (6.0f);
        double spanSec = dur;
        if (zoomToInterval)
        {
            const double s0 = mProcessor->getAnalyzerIntervalStart();
            const double s1 = mProcessor->getAnalyzerIntervalEnd();
            if (s1 > s0)
                spanSec = juce::jmax (dur / juce::jmax (1.0f, waveBounds.getWidth()), s1 - s0);
        }
        const double secPerPx = spanSec / juce::jmax (1.0f, waveBounds.getWidth());
        const double dx = (e.getPosition().x - dragStart.x) * secPerPx;
        // Drag moves the whole interval; shift-drag stretches the end.
        if (e.mods.isShiftDown())
            mProcessor->analyzerSetInterval (dragStartInterval.first,
                                             juce::jlimit (0.0, dur, dragStartInterval.second + dx));
        else
        {
            const double len = dragStartInterval.second - dragStartInterval.first;
            double ns = juce::jlimit (0.0, juce::jmax (0.0, dur - len), dragStartInterval.first + dx);
            mProcessor->analyzerSetInterval (ns, ns + len);
        }
        repaint();
    }

    void timerCallback() override
    {
        // Generation changes only (no always-on repaint).
        // The play cursor refreshes via the parent's playback repaints.
        const int gen = mProcessor->getAnalyzerSourceGeneration();
        if (gen != lastGeneration)
        {
            lastGeneration = gen;
            zoomToInterval = false;
            repaint();
        }
    }

private:
    SpectralMorphingToolAudioProcessor* mProcessor = nullptr;
    bool resynth = false;
    bool zoomToInterval = false;
    int lastGeneration = -1;
    juce::Point<int> dragStart;
    std::pair<double, double> dragStartInterval { 0.0, 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
