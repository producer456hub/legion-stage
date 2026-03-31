#pragma once

#include <JuceHeader.h>
#include "DawLookAndFeel.h"

// Lissajous / stereo field visualizer.
// Plots L vs R channels as an XY scope. Mono = diagonal line,
// stereo = wide patterns, phase issues = horizontal spread.
class LissajousComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr int bufferSize = 2048;
    static constexpr int trailLength = 1024;  // points drawn per frame

    LissajousComponent()
    {
        startTimerHz(30);
    }

    ~LissajousComponent() override { stopTimer(); }

    // ── Public controls ──
    void setZoom(float z) { zoom = juce::jlimit(0.5f, 10.0f, z); }
    float getZoom() const { return zoom; }
    void zoomIn()  { setZoom(zoom * 1.3f); }
    void zoomOut() { setZoom(zoom / 1.3f); }

    void setDotCount(int n) { dotCount = juce::jlimit(64, 1024, n); }
    int getDotCount() const { return dotCount; }
    void cycleDots()
    {
        if (dotCount <= 128) dotCount = 256;
        else if (dotCount <= 384) dotCount = 512;
        else if (dotCount <= 768) dotCount = 1024;
        else dotCount = 64;
    }

    // Called from audio thread — push stereo sample pairs
    void pushSamples(const float* L, const float* R, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            bufL[writePos] = L[i];
            bufR[writePos] = R[i];
            writePos = (writePos + 1) % bufferSize;
        }
        dataReady.store(true);
    }

    void timerCallback() override
    {
        if (dataReady.exchange(false))
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // Get theme color
        uint32_t dotColor = 0xffc8e4ff;  // ice-blue default
        uint32_t bgColor  = 0xff0a0e14;
        if (auto* lnf = dynamic_cast<DawLookAndFeel*>(&getLookAndFeel()))
        {
            dotColor = lnf->getTheme().lcdAmber;
            bgColor  = lnf->getTheme().bodyDark;
        }

        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(bgColor));
        g.fillRoundedRectangle(bounds, 4.0f);

        // Reserve margins for labels
        float margin = 14.0f;
        auto plotArea = bounds.reduced(margin, margin);

        float cx = plotArea.getCentreX();
        float cy = plotArea.getCentreY();
        float scale = juce::jmin(plotArea.getWidth(), plotArea.getHeight()) * zoom;

        // Value markers on sides
        auto labelColor = juce::Colour(dotColor).withAlpha(0.35f);
        g.setColour(labelColor);
        g.setFont(9.0f);

        // Measure current peak levels for the markers
        int recent = 128;
        int rStart = (writePos - recent + bufferSize) % bufferSize;
        float peakL = 0.0f, peakR = 0.0f, peakSum = 0.0f, peakDiff = 0.0f;
        for (int i = 0; i < recent; ++i)
        {
            int idx = (rStart + i) % bufferSize;
            float absL = std::abs(bufL[idx]);
            float absR = std::abs(bufR[idx]);
            peakL = juce::jmax(peakL, absL);
            peakR = juce::jmax(peakR, absR);
            peakSum = juce::jmax(peakSum, std::abs(bufL[idx] + bufR[idx]) * 0.5f);
            peakDiff = juce::jmax(peakDiff, std::abs(bufL[idx] - bufR[idx]));
        }

        // Left side: L level
        float lBarH = juce::jlimit(0.0f, 1.0f, peakL) * plotArea.getHeight();
        g.setColour(juce::Colour(dotColor).withAlpha(0.25f));
        g.fillRect(bounds.getX() + 1, plotArea.getBottom() - lBarH, 4.0f, lBarH);
        g.setColour(labelColor);
        g.drawText("L", static_cast<int>(bounds.getX()), static_cast<int>(bounds.getY()), 12, 12, juce::Justification::centred);

        // Right side: R level
        float rBarH = juce::jlimit(0.0f, 1.0f, peakR) * plotArea.getHeight();
        g.setColour(juce::Colour(dotColor).withAlpha(0.25f));
        g.fillRect(bounds.getRight() - 5, plotArea.getBottom() - rBarH, 4.0f, rBarH);
        g.setColour(labelColor);
        g.drawText("R", static_cast<int>(bounds.getRight()) - 12, static_cast<int>(bounds.getY()), 12, 12, juce::Justification::centred);

        // Bottom: stereo width indicator
        float widthPct = juce::jlimit(0.0f, 1.0f, peakDiff * 2.0f);
        float widthBarW = widthPct * plotArea.getWidth();
        g.setColour(juce::Colour(dotColor).withAlpha(0.2f));
        g.fillRect(cx - widthBarW * 0.5f, bounds.getBottom() - 5, widthBarW, 3.0f);
        g.setColour(labelColor);
        g.drawText("W", static_cast<int>(bounds.getX()), static_cast<int>(bounds.getBottom()) - 13, 12, 12, juce::Justification::centred);

        // Top: mono correlation (sum level)
        float monoPct = juce::jlimit(0.0f, 1.0f, peakSum * 2.0f);
        float monoBarW = monoPct * plotArea.getWidth();
        g.setColour(juce::Colour(dotColor).withAlpha(0.2f));
        g.fillRect(cx - monoBarW * 0.5f, bounds.getY() + 2, monoBarW, 3.0f);
        g.setColour(labelColor);
        g.drawText("M", static_cast<int>(bounds.getRight()) - 12, static_cast<int>(bounds.getBottom()) - 13, 12, 12, juce::Justification::centred);

        // Draw crosshairs (faint)
        g.setColour(juce::Colour(dotColor).withAlpha(0.08f));
        g.drawHorizontalLine(static_cast<int>(cy), plotArea.getX(), plotArea.getRight());
        g.drawVerticalLine(static_cast<int>(cx), plotArea.getY(), plotArea.getBottom());
        // Diagonal guides (mono line)
        g.drawLine(cx - scale, cy + scale, cx + scale, cy - scale, 0.5f);

        // Draw the Lissajous pattern
        int readPos = (writePos - trailLength + bufferSize) % bufferSize;

        juce::Path path;
        bool started = false;

        for (int i = 0; i < trailLength; ++i)
        {
            int idx = (readPos + i) % bufferSize;
            float l = bufL[idx];
            float r = bufR[idx];

            // Mid-Side rotation: X = L-R (width), Y = L+R (mono/sum)
            float x = cx + (l - r) * scale;
            float y = cy - (l + r) * 0.5f * scale;

            if (!started)
            {
                path.startNewSubPath(x, y);
                started = true;
            }
            else
            {
                path.lineTo(x, y);
            }
        }

        if (started)
        {
            // Brighter recent points, faded trail
            g.setColour(juce::Colour(dotColor).withAlpha(0.3f));
            g.strokePath(path, juce::PathStrokeType(1.5f));
        }

        // Draw recent points as bright dots
        int brightCount = juce::jmin(dotCount, trailLength);
        int brightStart = (writePos - brightCount + bufferSize) % bufferSize;

        for (int i = 0; i < brightCount; ++i)
        {
            int idx = (brightStart + i) % bufferSize;
            float l = bufL[idx];
            float r = bufR[idx];

            float x = cx + (l - r) * scale;
            float y = cy - (l + r) * 0.5f * scale;

            float age = static_cast<float>(i) / static_cast<float>(brightCount);
            float alpha = age * age * 0.9f;

            g.setColour(juce::Colour(dotColor).withAlpha(alpha));
            g.fillEllipse(x - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
    }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

private:
    float bufL[bufferSize] = {};
    float bufR[bufferSize] = {};
    int writePos = 0;
    std::atomic<bool> dataReady { false };

    // Controllable parameters
    float zoom = 3.0f;        // 0.5-10.0 scale factor
    int dotCount = 256;        // 64-1024 bright dot count

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LissajousComponent)
};
