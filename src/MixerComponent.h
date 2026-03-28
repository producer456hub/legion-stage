#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"
#include "DawLookAndFeel.h"

// Minimalist mixer with white oak wood surface, OLED-style displays
// inset into the wood, and ice blue illuminated faders/meters.
class MixerComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr uint32_t oledBg    = 0xff000000;
    static constexpr uint32_t oledText  = 0xffb8d8f0;  // ice blue
    static constexpr uint32_t oledDim   = 0xff4a6878;
    static constexpr uint32_t oledRed   = 0xffcc4444;
    static constexpr uint32_t oledWarm  = 0xffc9a96e;

    // Callback when a track is selected in the mixer
    std::function<void(int)> onTrackSelected;

    MixerComponent(PluginHost& host) : pluginHost(host)
    {
        setWantsKeyboardFocus(false);
        startTimerHz(20);
    }

    ~MixerComponent() override { stopTimer(); }

    void timerCallback() override { if (isVisible()) repaint(); }

    void paint(juce::Graphics& g) override
    {
        int w = getWidth();
        int h = getHeight();

        // ── Wood background (cached) ──
        if (w != woodCacheW || h != woodCacheH)
        {
            woodCache = juce::Image(juce::Image::RGB, w, h, false);
            juce::Graphics wg(woodCache);
            drawWoodBackground(wg, w, h);
            woodCacheW = w;
            woodCacheH = h;
        }
        g.drawImageAt(woodCache, 0, 0);

        int numTracks = PluginHost::NUM_TRACKS;
        int stripW = w / numTracks;

        for (int t = 0; t < numTracks; ++t)
        {
            int sx = t * stripW;
            auto& trk = pluginHost.getTrack(t);
            auto* gp = trk.gainProcessor;

            float vol = gp ? gp->volume.load() : 0.0f;
            float pan = gp ? gp->pan.load() : 0.0f;
            bool muted = gp ? gp->muted.load() : false;
            bool soloed = gp ? gp->soloed.load() : false;
            float peakL = gp ? gp->peakLevelL.load() : 0.0f;
            float peakR = gp ? gp->peakLevelR.load() : 0.0f;

            // ── One tall OLED touchscreen per channel — everything drawn on it ──
            bool isSelected = (t == pluginHost.getSelectedTrack());
            auto screenRect = juce::Rectangle<float>(
                static_cast<float>(sx + 3), 4.0f,
                static_cast<float>(stripW - 6), static_cast<float>(h - 8));
            drawOledScreen(g, screenRect);

            // Selected track border glow
            if (isSelected)
            {
                g.setColour(juce::Colour(oledText).withAlpha(0.4f));
                g.drawRoundedRectangle(screenRect.expanded(1.0f), 3.0f, 1.5f);
            }

            float scrX = screenRect.getX();
            float scrY = screenRect.getY();
            float scrW = screenRect.getWidth();
            float scrH = screenRect.getHeight();

            // Track name at top
            juce::String name = trk.name.isEmpty() ? ("TR " + juce::String(t + 1)) : trk.name;
            g.setColour(juce::Colour(oledText));
            g.setFont(9.0f);
            g.drawText(name, static_cast<int>(scrX + 2), static_cast<int>(scrY + 3),
                       static_cast<int>(scrW - 4), 12, juce::Justification::centred);

            // Pan indicator below name
            float panBarY = scrY + 18;
            float panBarW = scrW * 0.6f;
            float panBarX = scrX + (scrW - panBarW) * 0.5f;
            g.setColour(juce::Colour(oledDim));
            g.drawHorizontalLine(static_cast<int>(panBarY), panBarX, panBarX + panBarW);
            g.setColour(juce::Colour(oledText));
            float panDotX = panBarX + panBarW * 0.5f + pan * panBarW * 0.5f;
            g.fillRect(panDotX - 1.5f, panBarY - 2, 3.0f, 5.0f);
            // Center tick
            g.setColour(juce::Colour(oledDim));
            g.fillRect(panBarX + panBarW * 0.5f - 0.5f, panBarY - 1, 1.0f, 3.0f);

            // ── Fader area — drawn on the OLED screen ──
            float faderTop = scrY + 26;
            float faderBot = scrY + scrH - 48;
            float faderH = faderBot - faderTop;
            float faderCx = scrX + scrW * 0.5f;

            // Fader track lines (graduated marks)
            g.setColour(juce::Colour(oledDim).withAlpha(0.4f));
            for (int mark = 0; mark <= 10; ++mark)
            {
                float my = faderBot - (static_cast<float>(mark) / 10.0f) * faderH;
                float markW = (mark % 5 == 0) ? scrW * 0.3f : scrW * 0.15f;
                g.drawHorizontalLine(static_cast<int>(my),
                                     faderCx - markW, faderCx + markW);
            }

            // Fader groove
            g.setColour(juce::Colour(oledDim).withAlpha(0.3f));
            g.fillRect(faderCx - 1.0f, faderTop, 2.0f, faderH);

            // Fader fill (ice blue below thumb)
            float thumbY = faderBot - vol * faderH;
            g.setColour(juce::Colour(oledText).withAlpha(0.2f));
            g.fillRect(faderCx - 1.0f, thumbY, 2.0f, faderBot - thumbY);

            // Fader thumb — wide bar drawn on the screen
            float thumbW = scrW * 0.6f;
            g.setColour(juce::Colour(oledText));
            g.fillRect(faderCx - thumbW * 0.5f, thumbY - 3.0f, thumbW, 6.0f);
            // Thumb grip lines
            g.setColour(juce::Colour(oledBg));
            g.drawHorizontalLine(static_cast<int>(thumbY), faderCx - thumbW * 0.3f, faderCx + thumbW * 0.3f);

            // Level meters on sides of fader
            float meterW = 3.0f;
            float meterLx = scrX + 3;
            float meterRx = scrX + scrW - 3 - meterW;
            drawOledMeter(g, meterLx, faderTop, meterW, faderH, peakL);
            drawOledMeter(g, meterRx, faderTop, meterW, faderH, peakR);

            // ── dB readout ──
            float dB = (vol > 0.001f) ? 20.0f * std::log10(vol) : -60.0f;
            juce::String dbStr = (dB <= -60.0f) ? "-inf" : juce::String(dB, 1);
            g.setColour(juce::Colour(oledText));
            g.setFont(9.0f);
            g.drawText(dbStr, static_cast<int>(scrX + 2), static_cast<int>(scrY + scrH - 44),
                       static_cast<int>(scrW - 4), 12, juce::Justification::centred);

            // ── M / S as pixel text on the screen ──
            float btnY = scrY + scrH - 30;
            float btnW = scrW * 0.4f;

            // Mute
            g.setColour(muted ? juce::Colour(oledText) : juce::Colour(oledDim));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText("M", static_cast<int>(scrX + 2), static_cast<int>(btnY),
                       static_cast<int>(btnW), 14, juce::Justification::centred);
            if (muted)
            {
                g.setColour(juce::Colour(oledText).withAlpha(0.3f));
                g.fillRect(scrX + 2, btnY, btnW, 14.0f);
            }

            // Solo
            g.setColour(soloed ? juce::Colour(oledText) : juce::Colour(oledDim));
            g.drawText("S", static_cast<int>(scrX + scrW - btnW - 2), static_cast<int>(btnY),
                       static_cast<int>(btnW), 14, juce::Justification::centred);
            if (soloed)
            {
                g.setColour(juce::Colour(oledText).withAlpha(0.3f));
                g.fillRect(scrX + scrW - btnW - 2, btnY, btnW, 14.0f);
            }

            // Track number at very bottom
            g.setColour(juce::Colour(oledDim));
            g.setFont(8.0f);
            g.drawText(juce::String(t + 1), static_cast<int>(scrX), static_cast<int>(scrY + scrH - 14),
                       static_cast<int>(scrW), 12, juce::Justification::centred);

            // Channel divider
            if (t > 0)
            {
                g.setColour(juce::Colour(0x20000000));
                g.drawVerticalLine(sx, 0, static_cast<float>(h));
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override { handleMouse(e); }
    void mouseDrag(const juce::MouseEvent& e) override { handleMouse(e); }

private:
    PluginHost& pluginHost;

    void handleMouse(const juce::MouseEvent& e)
    {
        int stripW = getWidth() / PluginHost::NUM_TRACKS;
        int t = e.x / stripW;
        if (t < 0 || t >= PluginHost::NUM_TRACKS) return;

        // Select this track
        if (onTrackSelected)
            onTrackSelected(t);

        auto& trk = pluginHost.getTrack(t);
        auto* gp = trk.gainProcessor;
        if (gp == nullptr) return;

        int h = getHeight();
        float fy = static_cast<float>(e.y);

        // Fader area (inside the OLED screen: y 30 to h-52)
        int faderTop = 30;
        int faderBot = h - 52;
        if (e.y >= faderTop && e.y <= faderBot)
        {
            float vol = 1.0f - (fy - static_cast<float>(faderTop)) / static_cast<float>(faderBot - faderTop);
            gp->volume.store(juce::jlimit(0.0f, 1.0f, vol));
            return;
        }

        // Pan area (y 16-24 on the screen)
        if (e.y >= 14 && e.y <= 26)
        {
            int sx = t * stripW;
            float panNorm = (static_cast<float>(e.x - sx) / static_cast<float>(stripW) - 0.5f) * 2.0f;
            gp->pan.store(juce::jlimit(-1.0f, 1.0f, panNorm));
            return;
        }

        // Mute/Solo buttons (bottom of screen)
        if (e.y >= h - 38 && e.y <= h - 16 && !e.mouseWasDraggedSinceMouseDown())
        {
            int sx = t * stripW;
            int halfW = (stripW - 12) / 2;
            if (e.x < sx + 4 + halfW)
                gp->muted.store(!gp->muted.load());
            else if (e.x > sx + 8 + halfW)
            {
                bool newSolo = !gp->soloed.load();
                bool wasSoloed = gp->soloed.load();
                gp->soloed.store(newSolo);
                if (newSolo && !wasSoloed) pluginHost.soloCount.fetch_add(1);
                else if (!newSolo && wasSoloed) pluginHost.soloCount.fetch_sub(1);
            }
        }
    }

    void drawOledMeter(juce::Graphics& g, float x, float y, float w, float h, float level)
    {
        level = juce::jlimit(0.0f, 1.0f, level);
        int barH = static_cast<int>(level * h);
        // Segmented meter look
        int segH = 3;
        for (int sy = static_cast<int>(h) - 1; sy >= 0; sy -= (segH + 1))
        {
            bool lit = (static_cast<int>(h) - sy) <= barH;
            float segLevel = static_cast<float>(static_cast<int>(h) - sy) / h;
            juce::Colour col = segLevel > 0.9f ? juce::Colour(oledRed) :
                               segLevel > 0.7f ? juce::Colour(oledWarm) :
                               juce::Colour(oledText);
            g.setColour(lit ? col.withAlpha(0.8f) : juce::Colour(oledDim).withAlpha(0.1f));
            g.fillRect(x, y + sy - segH + 1, w, static_cast<float>(segH));
        }
    }

    void drawOledScreen(juce::Graphics& g, juce::Rectangle<float> rect)
    {
        // Black OLED inset into wood — subtle bevel
        g.setColour(juce::Colour(0x30000000));
        g.fillRoundedRectangle(rect.expanded(1.0f), 3.0f);
        g.setColour(juce::Colour(oledBg));
        g.fillRoundedRectangle(rect, 2.0f);
    }

    void drawMeter(juce::Graphics& g, int x, int y, int w, int h, float level)
    {
        level = juce::jlimit(0.0f, 1.0f, level);
        int barH = static_cast<int>(level * h);

        // Dark slot
        g.setColour(juce::Colour(0x30000000));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(w), static_cast<float>(h), 2.0f);

        // Level fill
        juce::Colour col = level > 0.9f ? juce::Colour(oledRed) :
                           level > 0.7f ? juce::Colour(oledWarm) :
                           juce::Colour(oledText);
        g.setColour(col.withAlpha(0.8f));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + h - barH),
                               static_cast<float>(w), static_cast<float>(barH), 2.0f);

        // Glow at peak
        if (level > 0.1f)
        {
            g.setColour(col.withAlpha(0.15f));
            g.fillRoundedRectangle(static_cast<float>(x - 1), static_cast<float>(y + h - barH - 2),
                                   static_cast<float>(w + 2), 6.0f, 2.0f);
        }
    }

    void drawWoodBackground(juce::Graphics& g, int w, int h)
    {
        juce::Colour oakBase(0xffc0b49e);
        juce::Colour oakLight(0xffcec2ae);
        juce::Colour oakGrain(0xffa89882);

        g.setColour(oakBase);
        g.fillAll();

        juce::Random rng(99);
        for (int i = 0; i < 80; ++i)
        {
            float gy = rng.nextFloat() * static_cast<float>(h);
            float grainH = 0.4f + rng.nextFloat() * 1.0f;
            float alpha = 0.05f + rng.nextFloat() * 0.1f;
            bool isLight = rng.nextBool();

            g.setColour((isLight ? oakLight : oakGrain).withAlpha(alpha));

            juce::Path grain;
            grain.startNewSubPath(0, gy);
            for (int gx = 0; gx < w; gx += 12)
            {
                float wobble = std::sin(static_cast<float>(gx) * 0.008f + i * 0.7f) * 1.5f;
                grain.lineTo(static_cast<float>(gx), gy + wobble);
            }
            grain.lineTo(static_cast<float>(w), gy);
            g.strokePath(grain, juce::PathStrokeType(grainH));
        }
    }

    juce::Image woodCache;
    int woodCacheW = 0, woodCacheH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerComponent)
};
