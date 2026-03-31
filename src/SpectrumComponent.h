#pragma once

#include <JuceHeader.h>
#include "DawLookAndFeel.h"

// Real-time FFT spectrum analyzer.
// Feed audio samples via pushSamples() from the audio thread,
// then the component draws frequency bars on the UI thread timer.
class SpectrumComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr int fftOrder = 10;              // 1024-point FFT
    static constexpr int fftSize = 1 << fftOrder;    // 1024
    static constexpr int numBars = 32;               // display bands

    SpectrumComponent()
    {
        startTimerHz(30);
    }

    ~SpectrumComponent() override { stopTimer(); }

    // ── Public controls ──
    void setDecaySpeed(float d) { decaySpeed = juce::jlimit(0.5f, 0.99f, d); }
    float getDecaySpeed() const { return decaySpeed; }
    void cycleDecay()
    {
        // Cycle: fast → medium → slow → very slow
        if (decaySpeed < 0.75f) decaySpeed = 0.85f;
        else if (decaySpeed < 0.90f) decaySpeed = 0.93f;
        else if (decaySpeed < 0.96f) decaySpeed = 0.98f;
        else decaySpeed = 0.65f;
    }

    void setSensitivity(float s) { sensitivity = juce::jlimit(0.1f, 2.0f, s); }
    float getSensitivity() const { return sensitivity; }
    void sensitivityUp()   { setSensitivity(sensitivity + 0.2f); }
    void sensitivityDown() { setSensitivity(sensitivity - 0.2f); }

    // Called from audio thread — push mono samples into ring buffer
    void pushSamples(const float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            fifo[fifoIndex] = data[i];
            if (++fifoIndex >= fftSize)
            {
                std::copy(fifo, fifo + fftSize, fftData);
                fftDataReady.store(true);
                fifoIndex = 0;
            }
        }
    }

    void timerCallback() override
    {
        if (fftDataReady.exchange(false))
        {
            // Window function (Hann)
            for (int i = 0; i < fftSize; ++i)
            {
                float window = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi
                                * static_cast<float>(i) / static_cast<float>(fftSize)));
                fftData[i] *= window;
            }

            // Zero-pad upper half
            std::fill(fftData + fftSize, fftData + fftSize * 2, 0.0f);

            fft.performFrequencyOnlyForwardTransform(fftData);

            // Bin into bars (logarithmic frequency mapping)
            float maxFreq = static_cast<float>(fftSize);
            for (int bar = 0; bar < numBars; ++bar)
            {
                // Log scale: each bar covers an exponentially increasing range
                float startFrac = std::pow(static_cast<float>(bar) / numBars, 2.0f);
                float endFrac   = std::pow(static_cast<float>(bar + 1) / numBars, 2.0f);
                int startBin = static_cast<int>(startFrac * maxFreq * 0.5f);
                int endBin   = juce::jmax(startBin + 1, static_cast<int>(endFrac * maxFreq * 0.5f));
                endBin = juce::jmin(endBin, fftSize / 2);

                float sum = 0.0f;
                for (int b = startBin; b < endBin; ++b)
                    sum = juce::jmax(sum, fftData[b]);

                // Convert to dB-ish scale
                float level = juce::jlimit(0.0f, 1.0f,
                    (std::log10(1.0f + sum * 10.0f * sensitivity)) * 0.5f);

                // Smooth falloff
                barLevels[bar] = juce::jmax(level, barLevels[bar] * decaySpeed);
            }

            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        // Get colors from theme — use lcdAmber for spectrum bars (matches OLED display color)
        uint32_t barColor = 0xffc8e4ff;  // ice-blue default
        uint32_t bgColor  = 0xff0a0e14;
        if (auto* lnf = dynamic_cast<DawLookAndFeel*>(&getLookAndFeel()))
        {
            barColor = lnf->getTheme().lcdAmber;
            bgColor  = lnf->getTheme().bodyDark;
        }

        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(bgColor));
        g.fillRoundedRectangle(bounds, 3.0f);

        float barW = bounds.getWidth() / static_cast<float>(numBars);
        float maxH = bounds.getHeight() - 2.0f;

        for (int i = 0; i < numBars; ++i)
        {
            float h = barLevels[i] * maxH;
            float x = bounds.getX() + static_cast<float>(i) * barW;
            float y = bounds.getBottom() - 1.0f - h;

            // Gradient: brighter at top
            float brightness = barLevels[i];
            auto color = juce::Colour(barColor).withMultipliedBrightness(0.4f + brightness * 0.6f);

            g.setColour(color);
            g.fillRect(x + 0.5f, y, barW - 1.0f, h);
        }
    }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

private:
    juce::dsp::FFT fft { fftOrder };

    float fifo[fftSize] = {};
    float fftData[fftSize * 2] = {};
    int fifoIndex = 0;
    std::atomic<bool> fftDataReady { false };

    float barLevels[numBars] = {};

    // Controllable parameters
    float decaySpeed = 0.85f;     // 0.5-0.99 bar falloff rate
    float sensitivity = 1.0f;    // 0.1-2.0 input gain

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumComponent)
};
