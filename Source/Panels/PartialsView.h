#pragma once

#include <JuceHeader.h>

#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/InterfaceDefines.h"
#include "../Helpers/SMTConstants.h"
#include "../Analysis/MorphexAnalyzerTypes.h"
#include "../PluginProcessor.h"

// PARTIALS map: time-X, log-freq-Y, tube thickness = amp.
class PartialsView : public Component, public Timer
{
public:
    explicit PartialsView (SpectralMorphingToolAudioProcessor* inProcessor)
        : mProcessor (inProcessor)
    {
        startTimer (125);
    }

    void setData (const morphex::analyzer::PartialFrameData& d)
    {
        data = d;
        tubesDirty = true;
        repaint();
    }

    void setStocEnv (std::shared_ptr<const SpectralMorphingToolAudioProcessor::StocMatrix> env,
                     double sampleRate)
    {
        stocEnv = std::move (env);
        stocRate = sampleRate;
        stocDirty = true;
        tubesDirty = true;
        repaint();
    }

    void setFundamentalHz (double hz)
    {
        if (! juce::approximatelyEqual (fundamentalHz, hz))
        {
            fundamentalHz = hz;
            repaint();
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (GUI::Color::CardDark);
        auto bounds = getLocalBounds().toFloat();

        g.setColour (GUI::Color::Logo.withAlpha (0.70f));
        g.setFont (CustomLookAndFeel::makeFont (16.0f));
        auto header = bounds.removeFromTop (18.0f).reduced (6.0f, 0.0f);
        g.drawText (">> PARTIALS", header, Justification::topLeft, false);

        // Info line: frame/partial counts once analyzed (nothing when empty).
        if (data.numFrames > 0)
        {
            const juce::String info = juce::String (data.numFrames) + " frames x "
                                      + juce::String (data.numPartials) + " partials";
            g.setColour (GUI::Color::Logo.withAlpha (0.45f));
            g.setFont (CustomLookAndFeel::makeFont (15.0f));
            g.drawText (info, header.withTrimmedLeft (120.0f), Justification::topLeft, false);
        }

        auto mapBounds = getLocalBounds().toFloat().withTrimmedTop (20.0f).reduced (6.0f);
        g.setColour (GUI::Color::Background);
        g.fillRoundedRectangle (mapBounds, 3.0f * MorphexZoom::uiScale);
        GUI::Paint::drawCardOutline (g, mapBounds, 3.0f * MorphexZoom::uiScale, 0.25f);

        // Cached tubes layer: spines + dots + wash bake
        // once per data/size change; f0 line and marker stay live.
        rebuildTubesLayer (mapBounds);
        if (tubesLayer.isValid())
            g.drawImageAt (tubesLayer, (int) mapBounds.getX(), (int) mapBounds.getY());

        if (data.numFrames <= 0 || data.numPartials <= 0 || data.freqs.empty())
        {
            g.setColour (GUI::Color::Logo.withAlpha (0.35f));
            g.setFont (CustomLookAndFeel::makeFont (19.0f));
            g.drawText ("> ANALYZE TO INSPECT <", mapBounds, Justification::centred, false);
            return;
        }

        // Log-frequency helpers shared with the baked layer.
        const float fMin = 30.0f, fMax = 12000.0f;
        const float logMin = std::log10 (fMin), logMax = std::log10 (fMax);
        auto yForFreq = [&] (float fr)
        {
            return mapBounds.getBottom() - (std::log10 (fr) - logMin) / (logMax - logMin) * mapBounds.getHeight();
        };
        auto xForFrame = [&] (int f)
        {
            return mapBounds.getX() + (float) f / (float) juce::jmax (1, data.numFrames - 1) * mapBounds.getWidth();
        };

        // f0 reference line (0 = hidden).
        if (fundamentalHz > 0.0 && fundamentalHz >= fMin && fundamentalHz <= fMax)
        {
            const float fy = yForFreq ((float) fundamentalHz);
            g.setColour (GUI::Color::KeyDown.withAlpha (0.80f));
            g.drawLine (mapBounds.getX(), fy, mapBounds.getRight(), fy, 1.0f);
        }

        // Max-active marker (triangle glyph).
        if (cachedMaxActive > 0)
        {
            const float px = xForFrame (cachedMaxActiveFrame);
            juce::Path tri;
            tri.addTriangle (px, mapBounds.getY() + 9.0f,
                             px + 5.0f, mapBounds.getY() + 2.0f,
                             px - 5.0f, mapBounds.getY() + 2.0f);
            g.setColour (GUI::Color::KeyDown.withAlpha (0.9f));
            g.fillPath (tri);
        }
    }

    void timerCallback() override
    {
        const int gen = mProcessor->getAnalyzerSourceGeneration();
        if (gen != lastGeneration)
        {
            lastGeneration = gen;
            if (mProcessor->hasAnalyzerPartials())
                setData (mProcessor->getAnalyzerPartials());
            else
                setData (morphex::analyzer::PartialFrameData());
            setStocEnv (mProcessor->getAnalyzerStocEnv(),
                        mProcessor->getAnalyzerSampleRate());
        }
    }

private:
    // Baked tubes layer (paint blits it).
    void rebuildTubesLayer (juce::Rectangle<float> mapBounds)
    {
        const int W = juce::jmax (1, (int) mapBounds.getWidth());
        const int H = juce::jmax (1, (int) mapBounds.getHeight());
        if (! tubesDirty && tubesLayer.isValid()
            && tubesLayer.getWidth() == W && tubesLayer.getHeight() == H)
            return;

        tubesDirty = false;
        tubesLayer = juce::Image();
        cachedMaxActive = 0;
        cachedMaxActiveFrame = 0;
        if (data.numFrames <= 0 || data.numPartials <= 0 || data.freqs.empty())
            return;

        rebuildStocLayer (mapBounds);

        juce::Image img (juce::Image::ARGB, W, H, true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::translation (-mapBounds.getX(), -mapBounds.getY()));
        if (stocLayer.isValid())
            g.drawImageAt (stocLayer, mapBounds.getX(), mapBounds.getY());

        static constexpr float fMin = 30.0f, fMax = 12000.0f;
        const float logMin = std::log10 (fMin), logMax = std::log10 (fMax);
        auto yForFreq = [&] (float fr)
        {
            return mapBounds.getBottom() - (std::log10 (fr) - logMin) / (logMax - logMin) * mapBounds.getHeight();
        };
        auto xForFrame = [&] (int f)
        {
            return mapBounds.getX() + (float) f / (float) juce::jmax (1, data.numFrames - 1) * mapBounds.getWidth();
        };

        // Spine polylines: same partial index connected across frames.
        for (int p = 0; p < data.numPartials; ++p)
        {
            juce::Path spine;
            bool started = false;
            for (int f = 0; f < data.numFrames; ++f)
            {
                const size_t i = (size_t) f * (size_t) data.numPartials + (size_t) p;
                if (i >= data.freqs.size() || i >= data.amps.size())
                    break;
                const float fr = data.freqs[i];
                if (fr < fMin || fr > fMax || data.amps[i] < -120.0f)
                {
                    started = false;
                    continue;
                }
                const float x = xForFrame (f);
                const float y = yForFreq (fr);
                if (! started)
                {
                    spine.startNewSubPath (x, y);
                    started = true;
                }
                else
                {
                    spine.lineTo (x, y);
                }
            }
            g.setColour (juce::Colours::white.withAlpha (0.40f));
            g.strokePath (spine, juce::PathStrokeType (1.0f));
        }

        int maxActive = 0, maxActiveFrame = 0;
        for (int f = 0; f < data.numFrames; ++f)
        {
            const float x = xForFrame (f);
            int active = 0;
            for (int p = 0; p < data.numPartials; ++p)
            {
                const size_t i = (size_t) f * (size_t) data.numPartials + (size_t) p;
                if (i >= data.freqs.size() || i >= data.amps.size())
                    break;
                const float fr = data.freqs[i];
                if (fr < fMin || fr > fMax)
                    continue;
                const float y = yForFreq (fr);
                const float thick = juce::jlimit (1.0f, 4.0f, 1.0f + data.amps[i] * 3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.60f));
                g.fillEllipse (x - thick * 0.5f, y - thick * 0.5f, thick, thick);
                if (data.amps[i] > -120.0f)
                    ++active;
            }
            if (active > maxActive)
            {
                maxActive = active;
                maxActiveFrame = f;
            }
        }
        cachedMaxActive = maxActive;
        cachedMaxActiveFrame = maxActiveFrame;
        tubesLayer = std::move (img);
    }

    // Cached spectrogram: rebuilt on data/size change, blitted per paint.
    void rebuildStocLayer (juce::Rectangle<float> mapBounds)
    {
        const int W = juce::jmax (1, (int) mapBounds.getWidth());
        const int H = juce::jmax (1, (int) mapBounds.getHeight());
        if (! stocDirty && stocLayer.isValid()
            && stocLayer.getWidth() == W && stocLayer.getHeight() == H)
            return;

        stocDirty = false;
        stocLayer = juce::Image();
        if (stocEnv == nullptr || stocEnv->empty() || (*stocEnv)[0].empty()
            || stocRate <= 0.0 || W < 4 || H < 4)
            return;

        const int F = (int) stocEnv->size();
        const int B = (int) (*stocEnv)[0].size();
        const double nyquist = stocRate * 0.5;
        if (F < 2 || B < 2 || nyquist <= 0.0)
            return;

        static constexpr float fMin = 30.0f, fMax = 12000.0f;
        const float logMin = std::log10 (fMin), logMax = std::log10 (fMax);

        // Auto-contrast per analysis; -60 dB stays pitch black.
        std::vector<double> all;
        all.reserve ((size_t) F * (size_t) B);
        for (const auto& row : *stocEnv)
            for (auto v : row)
                if (std::isfinite (v))
                    all.push_back (v);
        if (all.empty())
            return;
        std::sort (all.begin(), all.end());
        const double floorDb = std::max (-60.0, all[(all.size() - 1) / 100]);
        const double ceilDb = all[(all.size() - 1) * 90 / 100];
        const double span = std::max (1e-6, ceilDb - floorDb);

        juce::Image img (juce::Image::ARGB, W, H, true);
        for (int y = 0; y < H; ++y)
        {
            // Inverse of the tube log mapping (y grows downward).
            const float fr = std::pow (10.0f, logMax - ((float) y + 0.5f) / (float) H
                                                 * (logMax - logMin));
            const double bPos = juce::jlimit (0.0, (double) B - 1.0,
                                              (double) fr / nyquist * (double) (B - 1));
            const int b0 = (int) bPos;
            const int b1 = juce::jmin (B - 1, b0 + 1);
            const double bFrac = bPos - (double) b0;
            for (int x = 0; x < W; ++x)
            {
                const double fPos = (double) x / (double) juce::jmax (1, W - 1) * (double) (F - 1);
                const int f0 = juce::jmin (F - 1, (int) fPos);
                const int f1 = juce::jmin (F - 1, f0 + 1);
                const double fFrac = fPos - (double) f0;
                const auto& r0 = (*stocEnv)[(size_t) f0];
                const auto& r1 = (*stocEnv)[(size_t) f1];
                if (b0 >= (int) r0.size() || b1 >= (int) r0.size()
                    || b0 >= (int) r1.size() || b1 >= (int) r1.size())
                    continue;
                double v = r0[(size_t) b0] * (1.0 - bFrac) * (1.0 - fFrac)
                         + r0[(size_t) b1] * bFrac * (1.0 - fFrac)
                         + r1[(size_t) b0] * (1.0 - bFrac) * fFrac
                         + r1[(size_t) b1] * bFrac * fFrac;
                if (! std::isfinite (v))
                    continue;
                // Raised floor, high-energy bias; tubes keep dominance.
                const float t = juce::jlimit (0.0f, 1.0f, (float) ((v - floorDb) / span));
                const float biased = std::pow (t, 0.6f);
                const float alpha = 0.30f * biased;
                img.setPixelAt (x, y, juce::Colour (0xffd8d8d8).withAlpha (alpha));
            }
        }
        stocLayer = std::move (img);
    }

    SpectralMorphingToolAudioProcessor* mProcessor = nullptr;
    morphex::analyzer::PartialFrameData data;
    double fundamentalHz = 440.0;
    int lastGeneration = -1;
    std::shared_ptr<const SpectralMorphingToolAudioProcessor::StocMatrix> stocEnv;
    double stocRate = 0.0;
    juce::Image stocLayer;
    bool stocDirty = true;
    juce::Image tubesLayer;
    bool tubesDirty = true;
    int cachedMaxActive = 0;
    int cachedMaxActiveFrame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartialsView)
};
