#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <array>

// G-Force–inspired visualizer.
// Smooth, flowing waveform ribbons that morph and pulse with audio.
// Multiple layered ribbons with color cycling and trail effects.
class GForceComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr int WAVE_SIZE = 512;
    static constexpr int NUM_RIBBONS = 5;

    GForceComponent()
    {
        waveBuffer.fill(0.0f);
        startTimerHz(30);
    }

    ~GForceComponent() override { stopTimer(); }

    // ── Public controls ──
    void setRibbonCount(int n) { ribbonCount = juce::jlimit(1, 8, n); }
    int getRibbonCount() const { return ribbonCount; }
    void moreRibbons() { setRibbonCount(ribbonCount + 1); }
    void fewerRibbons() { setRibbonCount(ribbonCount - 1); }

    void setSpeed(float s) { speedMult = juce::jlimit(0.25f, 4.0f, s); }
    float getSpeed() const { return speedMult; }

    void setColorSpeed(float s) { colorSpeedMult = juce::jlimit(0.0f, 4.0f, s); }
    float getColorSpeed() const { return colorSpeedMult; }

    void setTrailIntensity(float t) { trailAlpha = juce::jlimit(0x02, 0x30, static_cast<int>(t * 48.0f)); }
    float getTrailIntensity() const { return static_cast<float>(trailAlpha) / 48.0f; }

    void cycleTrail()
    {
        // Cycle between light, medium, heavy trails
        if (trailAlpha <= 0x06) trailAlpha = 0x0D;
        else if (trailAlpha <= 0x10) trailAlpha = 0x20;
        else trailAlpha = 0x04;
    }

    void pushSamples(const float* data, int numSamples)
    {
        // Copy waveform into ring buffer
        for (int i = 0; i < numSamples; ++i)
        {
            waveBuffer[writePos] = data[i];
            writePos = (writePos + 1) % WAVE_SIZE;
        }

        // Compute RMS and peak
        float sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sum += data[i] * data[i];
        float rms = std::sqrt(sum / static_cast<float>(juce::jmax(1, numSamples)));
        smoothedRms.store(smoothedRms.load() * 0.8f + rms * 0.2f);
    }

    void timerCallback() override
    {
        double spd = static_cast<double>(speedMult);
        phase += 0.015 * spd;
        colorPhase += 0.003 * static_cast<double>(colorSpeedMult);
        morphPhase += 0.008 * spd;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth();
        float h = bounds.getHeight();

        // Trail effect: semi-transparent dark overlay instead of full clear
        g.setColour(juce::Colour(static_cast<uint32_t>((trailAlpha << 24) | 0x000008)));
        g.fillRect(bounds);

        // If this is the first paint or size changed, do full clear
        if (w != lastW || h != lastH)
        {
            g.fillAll(juce::Colour(0xFF050508));
            lastW = w;
            lastH = h;
        }

        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);
        float cx = w * 0.5f;
        float cy = h * 0.5f;

        // Snapshot the waveform
        std::array<float, WAVE_SIZE> wave;
        int rp = writePos;
        for (int i = 0; i < WAVE_SIZE; ++i)
            wave[i] = waveBuffer[(rp + i) % WAVE_SIZE];

        // Draw multiple morphing ribbons
        for (int r = 0; r < ribbonCount; ++r)
        {
            float ribbonPhase = static_cast<float>(phase) + r * 1.257f;
            float morphOffset = static_cast<float>(morphPhase) + r * 0.7f;
            float ribbonEnergy = energy * (0.6f + 0.4f * std::sin(ribbonPhase * 0.3f));

            // Color cycling per ribbon
            float hue = std::fmod(static_cast<float>(colorPhase) + r * 0.2f, 1.0f);
            auto ribbonColor = juce::Colour::fromHSV(hue, 0.7f + energy * 0.3f, 0.8f + energy * 0.2f, 1.0f);

            // Morph parameters — control the shape transformation
            float scaleX = 0.7f + 0.3f * std::sin(morphOffset * 1.1f);
            float scaleY = 0.5f + 0.5f * std::sin(morphOffset * 0.8f + 1.0f);
            float rotation = static_cast<float>(ribbonPhase * 0.2);
            float spiralFactor = 0.3f + 0.7f * (0.5f + 0.5f * std::sin(morphOffset * 0.5f));

            juce::Path ribbon;
            bool started = false;

            int step = 2;
            for (int i = 0; i < WAVE_SIZE; i += step)
            {
                float t = static_cast<float>(i) / WAVE_SIZE;
                float sample = wave[i] * (1.0f + ribbonEnergy * 3.0f);

                // Parametric position — mix between different curve types
                float angle = t * juce::MathConstants<float>::twoPi * (1.0f + spiralFactor);
                float radiusBase = juce::jmin(w, h) * 0.25f * scaleX;
                float radiusMod = sample * juce::jmin(w, h) * 0.15f * scaleY;

                // Lissajous-inspired curve with morphing parameters
                float freqX = 1.0f + std::floor(std::sin(morphOffset * 0.3f) * 2.0f + 2.5f);
                float freqY = 1.0f + std::floor(std::cos(morphOffset * 0.4f) * 2.0f + 2.5f);

                float px = std::sin(angle * freqX + rotation) * (radiusBase + radiusMod);
                float py = std::cos(angle * freqY + rotation * 0.7f) * (radiusBase * 0.8f + radiusMod);

                float x = cx + px;
                float y = cy + py;

                if (!started)
                {
                    ribbon.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    ribbon.lineTo(x, y);
                }
            }

            // Draw with glow effect — wider dim stroke + thinner bright stroke
            float alpha = 0.15f + ribbonEnergy * 0.25f;
            g.setColour(ribbonColor.withAlpha(alpha * 0.4f));
            g.strokePath(ribbon, juce::PathStrokeType(4.0f + energy * 3.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(ribbonColor.withAlpha(alpha));
            g.strokePath(ribbon, juce::PathStrokeType(1.5f + energy * 1.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Center glow orb — pulses with energy
        float orbRadius = 20.0f + energy * 40.0f;
        float orbHue = std::fmod(static_cast<float>(colorPhase * 0.5), 1.0f);
        auto orbColor = juce::Colour::fromHSV(orbHue, 0.5f, 1.0f, 1.0f);

        for (int i = 4; i >= 0; --i)
        {
            float r = orbRadius * (1.0f + i * 0.5f);
            float a = (0.03f + energy * 0.05f) / (1.0f + i * 0.8f);
            g.setColour(orbColor.withAlpha(a));
            g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
        }
    }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

private:
    std::array<float, WAVE_SIZE> waveBuffer;
    int writePos = 0;
    double phase = 0.0;
    double colorPhase = 0.0;
    double morphPhase = 0.0;
    std::atomic<float> smoothedRms { 0.0f };
    float lastW = 0, lastH = 0;

    // Controllable parameters
    int ribbonCount = NUM_RIBBONS;   // 1-8
    float speedMult = 1.0f;          // 0.25-4.0
    float colorSpeedMult = 1.0f;     // 0.0-4.0
    int trailAlpha = 0x0D;           // trail overlay opacity

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GForceComponent)
};
