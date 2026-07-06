#pragma once

#include <JuceHeader.h>
#include <cmath>

// Aurora Borealis / Northern Lights visualizer.
// Renders undulating curtains of color driven by audio energy.
class AuroraComponent : public juce::Component, public juce::Timer
{
public:
    AuroraComponent() { startTimerHz(30); }
    ~AuroraComponent() override { stopTimer(); }

    void pushSamples(const float* data, int numSamples)
    {
        float sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sum += data[i] * data[i];
        float rms = std::sqrt(sum / static_cast<float>(juce::jmax(1, numSamples)));
        smoothedRms.store(smoothedRms.load() * 0.85f + rms * 0.15f);
    }

    void timerCallback() override
    {
        phase += 0.012;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth();
        float h = bounds.getHeight();

        // Dark sky background
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xFF050510), 0, 0,
            juce::Colour(0xFF0a0a20), 0, h, false));
        g.fillRect(bounds);

        // Stars
        juce::Random rng(42);
        for (int i = 0; i < 60; ++i)
        {
            float sx = rng.nextFloat() * w;
            float sy = rng.nextFloat() * h * 0.7f;
            float brightness = 0.2f + 0.3f * rng.nextFloat();
            float twinkle = 0.5f + 0.5f * std::sin(static_cast<float>(phase * (1.0 + rng.nextFloat() * 2.0) + rng.nextFloat() * 6.28));
            g.setColour(juce::Colours::white.withAlpha(brightness * twinkle));
            g.fillEllipse(sx, sy, 1.5f, 1.5f);
        }

        float energy = juce::jmin(1.0f, smoothedRms.load() * 6.0f);

        // Draw multiple aurora curtain layers
        for (int layer = 0; layer < 4; ++layer)
        {
            float layerPhase = static_cast<float>(phase) * (0.3f + layer * 0.15f);
            float baseY = h * (0.15f + layer * 0.12f);
            float amplitude = (30.0f + energy * 60.0f) * (1.0f - layer * 0.15f);
            float curtainAlpha = (0.12f + energy * 0.15f) * (1.0f - layer * 0.12f);

            // Color per layer
            juce::Colour col;
            switch (layer)
            {
                case 0: col = juce::Colour(0xFF00FF80); break; // green
                case 1: col = juce::Colour(0xFF00C8FF); break; // cyan
                case 2: col = juce::Colour(0xFF8040FF); break; // purple
                case 3: col = juce::Colour(0xFFFF2080); break; // pink
            }

            juce::Path curtain;
            curtain.startNewSubPath(0, h);

            int steps = static_cast<int>(w / 3.0f);
            for (int i = 0; i <= steps; ++i)
            {
                float x = (static_cast<float>(i) / steps) * w;
                float nx = x / w;

                // Layered noise for organic movement
                float n1 = std::sin(nx * 3.0f + layerPhase * 1.3f) * 0.5f;
                float n2 = std::sin(nx * 7.0f - layerPhase * 0.7f + layer) * 0.3f;
                float n3 = std::sin(nx * 13.0f + layerPhase * 2.1f + layer * 2.0f) * 0.15f;
                float noise = n1 + n2 + n3;

                float y = baseY + noise * amplitude;
                curtain.lineTo(x, y);
            }

            curtain.lineTo(w, h);
            curtain.closeSubPath();

            // Gradient fill: bright at top of curtain, fading down
            float topY = baseY - amplitude;
            g.setGradientFill(juce::ColourGradient(
                col.withAlpha(curtainAlpha * 1.5f), 0, topY,
                col.withAlpha(0.0f), 0, h * 0.85f, false));
            g.fillPath(curtain);

            // Bright edge at the top of curtain
            juce::Path edge;
            edge.startNewSubPath(0, h);
            for (int i = 0; i <= steps; ++i)
            {
                float x = (static_cast<float>(i) / steps) * w;
                float nx = x / w;
                float n1 = std::sin(nx * 3.0f + layerPhase * 1.3f) * 0.5f;
                float n2 = std::sin(nx * 7.0f - layerPhase * 0.7f + layer) * 0.3f;
                float n3 = std::sin(nx * 13.0f + layerPhase * 2.1f + layer * 2.0f) * 0.15f;
                float noise = n1 + n2 + n3;
                float y = baseY + noise * amplitude;
                edge.lineTo(x, y);
            }

            g.setColour(col.withAlpha(curtainAlpha * 2.5f));
            g.strokePath(edge, juce::PathStrokeType(2.0f));
        }

        // Soft glow at horizon
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xFF00FF80).withAlpha(0.04f + energy * 0.06f), w * 0.5f, h * 0.6f,
            juce::Colours::transparentBlack, w * 0.5f, h * 0.2f, true));
        g.fillRect(bounds);
    }

private:
    double phase = 0.0;
    std::atomic<float> smoothedRms { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuroraComponent)
};
