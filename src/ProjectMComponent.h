#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

// MilkDrop-inspired software visualizer.
// Per-pixel warp field with multiple layered effects, audio-reactive
// motion blur, and smooth palette morphing. No OpenGL required.
class ProjectMComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr int WAVE_SIZE = 576;
    static constexpr int NUM_SCENES = 8;
    static constexpr int MAP_RECOMPUTE_FRAMES = 45;

    ProjectMComponent()
    {
        waveBuffer.fill(0.0f);
        buildPalette();
        startTimerHz(30);
    }

    ~ProjectMComponent() override { stopTimer(); }

    // ── Public controls ──
    void nextScene()
    {
        sceneIndex = (sceneIndex + 1) % NUM_SCENES;
        mapFrameCounter = 0;
    }
    void prevScene()
    {
        sceneIndex = (sceneIndex - 1 + NUM_SCENES) % NUM_SCENES;
        mapFrameCounter = 0;
    }
    void randomScene()
    {
        juce::Random& rng = juce::Random::getSystemRandom();
        sceneIndex = rng.nextInt(NUM_SCENES);
        mapFrameCounter = 0;
        paletteA = rng.nextInt(NUM_PALETTES);
        paletteB = (paletteA + 1 + rng.nextInt(NUM_PALETTES - 1)) % NUM_PALETTES;
        paletteMorph = 0.0f;
        buildPalette();
    }
    int getSceneIndex() const { return sceneIndex; }

    void toggleLock() { locked = !locked; }
    bool isLocked() const { return locked; }

    void setBlackBg(bool on) { blackBg = on; buildPalette(); }
    bool isBlackBg() const { return blackBg; }

    void pushSamples(const float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            waveBuffer[writePos] = data[i];
            writePos = (writePos + 1) % WAVE_SIZE;
        }

        float sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sum += data[i] * data[i];
        float rms = std::sqrt(sum / static_cast<float>(juce::jmax(1, numSamples)));
        smoothedRms.store(smoothedRms.load() * 0.85f + rms * 0.15f);

        float cur = smoothedRms.load();
        if (cur > avgRms.load() * 1.8f && cur > 0.02f)
            beatHit.store(true);
        avgRms.store(avgRms.load() * 0.95f + cur * 0.05f);
    }

    void timerCallback() override
    {
        phase += 0.015;
        effectPhase += 0.025;

        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);
        bool beat = beatHit.load();

        // Morph palette continuously
        paletteMorph += 0.003f + energy * 0.01f;
        if (paletteMorph >= 1.0f)
        {
            paletteMorph = 0.0f;
            paletteA = paletteB;
            paletteB = (paletteB + 1) % NUM_PALETTES;
        }
        buildPalette();

        // Rotate palette with energy
        paletteRotation += 0.5f + energy * 3.0f;

        // Beat: chance to change scene
        if (beat && !locked)
        {
            beatCounter++;
            if (beatCounter >= 16)
            {
                beatCounter = 0;
                sceneIndex = (sceneIndex + 1) % NUM_SCENES;
                mapFrameCounter = 0;
            }
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        int w = getWidth();
        int h = getHeight();
        if (w < 4 || h < 4) return;

        if (w != bufW || h != bufH)
        {
            bufW = w;
            bufH = h;
            vs1.assign(static_cast<size_t>(w * h), 0);
            vs2.assign(static_cast<size_t>(w * h), 0);
            mapDx.assign(static_cast<size_t>(w * h), 0);
            mapDy.assign(static_cast<size_t>(w * h), 0);
            mapFrameCounter = 0;
        }

        if (mapFrameCounter <= 0)
        {
            computeWarpMap();
            mapFrameCounter = MAP_RECOMPUTE_FRAMES;
        }
        mapFrameCounter--;

        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);
        bool beat = beatHit.exchange(false);

        // Warp + blur
        applyWarpBlur();

        // Render effects
        renderWarpedWave(energy);
        renderPulsars(energy, beat);
        renderNebulaField(energy);
        renderFlowParticles(energy, beat);

        // Swap
        std::swap(vs1, vs2);

        // Blit with palette rotation
        int rot = static_cast<int>(paletteRotation) % 256;
        int energyShift = static_cast<int>(energy * 25.0f);

        juce::Image img(juce::Image::ARGB, w, h, false);
        {
            juce::Image::BitmapData bmp(img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    int idx = vs1[static_cast<size_t>(y * w + x)];
                    idx = (juce::jlimit(0, 255, idx) + rot + energyShift) % 256;
                    uint32_t col = palette[static_cast<size_t>(idx)];
                    auto* pixel = bmp.getPixelPointer(x, y);
                    pixel[0] = static_cast<uint8_t>(col & 0xFF);
                    pixel[1] = static_cast<uint8_t>((col >> 8) & 0xFF);
                    pixel[2] = static_cast<uint8_t>((col >> 16) & 0xFF);
                    pixel[3] = 0xFF;
                }
            }
        }
        g.drawImageAt(img, 0, 0);
    }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

private:
    std::array<float, WAVE_SIZE> waveBuffer;
    int writePos = 0;
    std::atomic<float> smoothedRms { 0.0f };
    std::atomic<float> avgRms { 0.0f };
    std::atomic<bool> beatHit { false };

    double phase = 0.0;
    double effectPhase = 0.0;
    float paletteRotation = 0.0f;
    int mapFrameCounter = 0;
    int sceneIndex = 0;
    bool locked = false;
    bool blackBg = false;
    int beatCounter = 0;

    int bufW = 0, bufH = 0;
    std::vector<int> vs1, vs2;
    std::vector<int> mapDx, mapDy;

    // Palette morphing between two palette styles
    static constexpr int NUM_PALETTES = 12;
    int paletteA = 0, paletteB = 1;
    float paletteMorph = 0.0f;
    std::array<uint32_t, 256> palette;

    // ── Palette system ──
    static uint32_t packRGB(float r, float g, float b)
    {
        return (0xFFu << 24)
             | (static_cast<uint32_t>(juce::jlimit(0.0f, 255.0f, r * 255.0f)) << 16)
             | (static_cast<uint32_t>(juce::jlimit(0.0f, 255.0f, g * 255.0f)) << 8)
             |  static_cast<uint32_t>(juce::jlimit(0.0f, 255.0f, b * 255.0f));
    }

    static uint32_t lerpRGB(uint32_t a, uint32_t b, float t)
    {
        float ar = static_cast<float>((a >> 16) & 0xFF) / 255.0f;
        float ag = static_cast<float>((a >> 8) & 0xFF) / 255.0f;
        float ab = static_cast<float>(a & 0xFF) / 255.0f;
        float br = static_cast<float>((b >> 16) & 0xFF) / 255.0f;
        float bg = static_cast<float>((b >> 8) & 0xFF) / 255.0f;
        float bb = static_cast<float>(b & 0xFF) / 255.0f;
        return packRGB(ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t);
    }

    void getPaletteStops(int style, uint32_t* stops, int& count)
    {
        switch (style)
        {
            case 0: // Plasma
                stops[0] = packRGB(0.1f,0,0.2f); stops[1] = packRGB(0.8f,0,0.4f);
                stops[2] = packRGB(1,0.5f,0); stops[3] = packRGB(1,1,0);
                stops[4] = packRGB(0,0.8f,1); stops[5] = packRGB(0.1f,0,0.5f);
                count = 6; break;
            case 1: // Embers
                stops[0] = packRGB(0,0,0); stops[1] = packRGB(0.4f,0,0);
                stops[2] = packRGB(0.8f,0.15f,0); stops[3] = packRGB(1,0.5f,0);
                stops[4] = packRGB(1,0.9f,0.3f); stops[5] = packRGB(1,1,0.8f);
                count = 6; break;
            case 2: // Deep Sea
                stops[0] = packRGB(0,0.02f,0.05f); stops[1] = packRGB(0,0.15f,0.3f);
                stops[2] = packRGB(0,0.4f,0.5f); stops[3] = packRGB(0.2f,0.7f,0.6f);
                stops[4] = packRGB(0.5f,1,0.8f); stops[5] = packRGB(0.9f,1,0.95f);
                count = 6; break;
            case 3: // Neon Ride
                stops[0] = packRGB(0,0,0); stops[1] = packRGB(0,0.2f,0.6f);
                stops[2] = packRGB(0.8f,0,1); stops[3] = packRGB(1,0,0.4f);
                stops[4] = packRGB(1,0.8f,0); stops[5] = packRGB(0,1,0.5f);
                count = 6; break;
            case 4: // Infrared
                stops[0] = packRGB(0,0,0.05f); stops[1] = packRGB(0.3f,0,0.15f);
                stops[2] = packRGB(0.7f,0,0.05f); stops[3] = packRGB(1,0.2f,0);
                stops[4] = packRGB(1,0.6f,0); stops[5] = packRGB(1,1,0.5f);
                count = 6; break;
            case 5: // Aurora
                stops[0] = packRGB(0,0.03f,0.05f); stops[1] = packRGB(0,0.3f,0.2f);
                stops[2] = packRGB(0.1f,0.7f,0.4f); stops[3] = packRGB(0.3f,1,0.7f);
                stops[4] = packRGB(0.6f,0.4f,1); stops[5] = packRGB(0.2f,0.1f,0.5f);
                count = 6; break;
            case 6: // Copper
                stops[0] = packRGB(0.02f,0.01f,0); stops[1] = packRGB(0.25f,0.12f,0.03f);
                stops[2] = packRGB(0.6f,0.3f,0.08f); stops[3] = packRGB(0.85f,0.55f,0.15f);
                stops[4] = packRGB(1,0.8f,0.35f); stops[5] = packRGB(1,0.95f,0.7f);
                count = 6; break;
            case 7: // Rainbow
                stops[0] = packRGB(1,0,0); stops[1] = packRGB(1,0.5f,0);
                stops[2] = packRGB(1,1,0); stops[3] = packRGB(0,1,0);
                stops[4] = packRGB(0,0.5f,1); stops[5] = packRGB(0.5f,0,1);
                count = 6; break;
            case 8: // Toxic
                stops[0] = packRGB(0,0.02f,0); stops[1] = packRGB(0,0.25f,0);
                stops[2] = packRGB(0.15f,0.7f,0); stops[3] = packRGB(0.5f,1,0);
                stops[4] = packRGB(0.85f,1,0.2f); stops[5] = packRGB(1,1,0.7f);
                count = 6; break;
            case 9: // Candy
                stops[0] = packRGB(1,0.2f,0.4f); stops[1] = packRGB(1,0.55f,0.25f);
                stops[2] = packRGB(1,0.85f,0.25f); stops[3] = packRGB(0.25f,1,0.45f);
                stops[4] = packRGB(0.25f,0.7f,1); stops[5] = packRGB(0.7f,0.25f,1);
                count = 6; break;
            case 10: // Electric
                stops[0] = packRGB(0,0,0); stops[1] = packRGB(0.15f,0,0.4f);
                stops[2] = packRGB(0.4f,0,1); stops[3] = packRGB(0.8f,0.4f,1);
                stops[4] = packRGB(1,0.8f,1); stops[5] = packRGB(1,1,1);
                count = 6; break;
            default: // Sunset
                stops[0] = packRGB(0.2f,0,0.05f); stops[1] = packRGB(0.6f,0.05f,0);
                stops[2] = packRGB(1,0.3f,0); stops[3] = packRGB(1,0.6f,0.1f);
                stops[4] = packRGB(1,0.85f,0.3f); stops[5] = packRGB(1,0.95f,0.7f);
                count = 6; break;
        }
    }

    void buildPalette()
    {
        uint32_t stopsA[8], stopsB[8];
        int countA = 0, countB = 0;
        getPaletteStops(paletteA, stopsA, countA);
        getPaletteStops(paletteB, stopsB, countB);

        for (int i = 0; i < 256; ++i)
        {
            // Build color from palette A
            float posA = static_cast<float>(i) / 256.0f * static_cast<float>(countA);
            int idxA = static_cast<int>(posA) % countA;
            int nextA = (idxA + 1) % countA;
            float fracA = posA - std::floor(posA);
            uint32_t colA = lerpRGB(stopsA[idxA], stopsA[nextA], fracA);

            // Build color from palette B
            float posB = static_cast<float>(i) / 256.0f * static_cast<float>(countB);
            int idxB = static_cast<int>(posB) % countB;
            int nextB = (idxB + 1) % countB;
            float fracB = posB - std::floor(posB);
            uint32_t colB = lerpRGB(stopsB[idxB], stopsB[nextB], fracB);

            // Morph between A and B
            palette[static_cast<size_t>(i)] = lerpRGB(colA, colB, paletteMorph);
        }

        // Force low palette values to black for black background mode
        if (blackBg)
        {
            uint32_t black = packRGB(0, 0, 0);
            for (int i = 0; i < 64; ++i)
            {
                float t = static_cast<float>(i) / 64.0f;
                palette[static_cast<size_t>(i)] = lerpRGB(black, palette[static_cast<size_t>(i)], t * t);
            }
        }
    }

    // ── 8 warp scenes — each is a different per-pixel motion field ──
    void computeWarpMap()
    {
        if (bufW < 4 || bufH < 4) return;

        float cx = bufW * 0.5f;
        float cy = bufH * 0.5f;
        float t = static_cast<float>(phase);
        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);

        for (int y = 0; y < bufH; ++y)
        {
            for (int x = 0; x < bufW; ++x)
            {
                float dx = static_cast<float>(x) - cx;
                float dy = static_cast<float>(y) - cy;
                float dist = std::sqrt(dx * dx + dy * dy) + 0.001f;
                float nx = dx, ny = dy;

                switch (sceneIndex)
                {
                    case 0: // Spiral inward
                    {
                        float zoom = 0.96f + 0.02f * std::sin(t * 0.5f);
                        float rot = 0.03f + 0.01f * std::sin(t * 0.3f) + energy * 0.02f;
                        nx = dx * zoom; ny = dy * zoom;
                        float c = std::cos(rot), s = std::sin(rot);
                        float rx = nx * c - ny * s;
                        ny = nx * s + ny * c;
                        nx = rx;
                        break;
                    }
                    case 1: // Zoom pulse
                    {
                        float zoom = 0.95f + 0.04f * std::sin(t * 1.2f) + energy * 0.03f;
                        nx = dx * zoom; ny = dy * zoom;
                        break;
                    }
                    case 2: // Vortex
                    {
                        float twist = (0.04f + energy * 0.03f) * std::sin(t * 0.4f);
                        float angle = twist + 0.0005f * dist;
                        float c = std::cos(angle), s = std::sin(angle);
                        nx = dx * 0.97f; ny = dy * 0.97f;
                        float rx = nx * c - ny * s;
                        ny = nx * s + ny * c;
                        nx = rx;
                        break;
                    }
                    case 3: // Wave distortion
                    {
                        float wave = 3.0f * std::sin(static_cast<float>(y) * 0.02f + t * 2.0f) * (1.0f + energy);
                        nx = dx * 0.97f + wave;
                        ny = dy * 0.97f;
                        break;
                    }
                    case 4: // Kaleidoscope
                    {
                        float angle = std::atan2(dy, dx);
                        float sectors = 6.0f;
                        float sectorAngle = std::fmod(angle + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi / sectors);
                        sectorAngle = std::abs(sectorAngle - juce::MathConstants<float>::twoPi / sectors * 0.5f);
                        float newAngle = sectorAngle + 0.02f * std::sin(t * 0.6f);
                        float zoom = 0.97f;
                        nx = std::cos(newAngle) * dist * zoom - dx;
                        ny = std::sin(newAngle) * dist * zoom - dy;
                        nx += dx * 0.97f; ny += dy * 0.97f;
                        break;
                    }
                    case 5: // Ripple
                    {
                        float ripple = 3.0f * std::sin(dist * 0.05f - t * 3.0f) * (1.0f + energy * 2.0f);
                        float invDist = 1.0f / (dist + 1.0f);
                        nx = (dx + dx * invDist * ripple) * 0.97f;
                        ny = (dy + dy * invDist * ripple) * 0.97f;
                        break;
                    }
                    case 6: // Double spiral
                    {
                        float rot = 0.025f * std::sin(t * 0.4f + dist * 0.003f);
                        float zoom = 0.96f + 0.015f * std::cos(t * 0.7f);
                        nx = dx * zoom; ny = dy * zoom;
                        float c = std::cos(rot), s = std::sin(rot);
                        float rx = nx * c - ny * s;
                        ny = nx * s + ny * c;
                        nx = rx;
                        break;
                    }
                    case 7: // Tunnel
                    {
                        float zoom = 0.94f + 0.04f * (dist / (juce::jmin(bufW, bufH) * 0.5f));
                        float rot = 0.02f / (dist * 0.01f + 1.0f);
                        nx = dx * zoom; ny = dy * zoom;
                        float c = std::cos(rot), s = std::sin(rot);
                        float rx = nx * c - ny * s;
                        ny = nx * s + ny * c;
                        nx = rx;
                        break;
                    }
                }

                int sx = static_cast<int>(cx + nx);
                int sy = static_cast<int>(cy + ny);
                size_t idx = static_cast<size_t>(y * bufW + x);
                mapDx[idx] = juce::jlimit(0, bufW - 1, sx) - x;
                mapDy[idx] = juce::jlimit(0, bufH - 1, sy) - y;
            }
        }
    }

    void applyWarpBlur()
    {
        if (bufW < 4 || bufH < 4) return;

        for (int y = 0; y < bufH; ++y)
        {
            for (int x = 0; x < bufW; ++x)
            {
                size_t i = static_cast<size_t>(y * bufW + x);
                int sx = x + mapDx[i];
                int sy = y + mapDy[i];

                if (sx <= 0 || sx >= bufW - 1 || sy <= 0 || sy >= bufH - 1)
                {
                    sx = juce::jlimit(0, bufW - 1, sx);
                    sy = juce::jlimit(0, bufH - 1, sy);
                    vs2[i] = juce::jmax(0, vs1[static_cast<size_t>(sy * bufW + sx)] - 1);
                    continue;
                }

                int sum = vs1[static_cast<size_t>((sy - 1) * bufW + (sx - 1))]
                        + vs1[static_cast<size_t>((sy - 1) * bufW + sx)] * 2
                        + vs1[static_cast<size_t>((sy - 1) * bufW + (sx + 1))]
                        + vs1[static_cast<size_t>(sy * bufW + (sx - 1))] * 2
                        + vs1[static_cast<size_t>(sy * bufW + sx)] * 4
                        + vs1[static_cast<size_t>(sy * bufW + (sx + 1))] * 2
                        + vs1[static_cast<size_t>((sy + 1) * bufW + (sx - 1))]
                        + vs1[static_cast<size_t>((sy + 1) * bufW + sx)] * 2
                        + vs1[static_cast<size_t>((sy + 1) * bufW + (sx + 1))];

                vs2[i] = juce::jmax(0, (sum >> 4) - 1);
            }
        }
    }

    // ── Effects ──

    // Wave drawing mode — changes with scene for maximum variety
    int waveDrawMode = 0;
    static constexpr int NUM_WAVE_MODES = 8;

    void renderWarpedWave(float energy)
    {
        std::array<float, WAVE_SIZE> wave;
        int rp = writePos;
        for (int i = 0; i < WAVE_SIZE; ++i)
            wave[i] = waveBuffer[(rp + i) % WAVE_SIZE];

        float cx = bufW * 0.5f;
        float cy = bufH * 0.5f;
        float t = static_cast<float>(effectPhase);
        int brightness = static_cast<int>(120 + energy * 135);

        // Pick wave mode based on scene for variety (or override)
        int mode = (waveDrawMode + sceneIndex) % NUM_WAVE_MODES;

        switch (mode)
        {
            case 0: // Dual interleaved circles (original)
            {
                for (int w = 0; w < 2; ++w)
                {
                    float wOffset = w * 3.14159f;
                    for (int i = 1; i < WAVE_SIZE; ++i)
                    {
                        float frac = static_cast<float>(i) / WAVE_SIZE;
                        float sample = wave[i] * (1.0f + energy * 3.0f);
                        float angle = frac * juce::MathConstants<float>::twoPi + t * (0.5f + w * 0.3f) + wOffset;
                        float r = juce::jmin(bufW, bufH) * (0.15f + 0.15f * std::sin(t * 0.3f + w)) + sample * bufH * 0.2f;
                        plotThick(static_cast<int>(cx + std::cos(angle) * r),
                                  static_cast<int>(cy + std::sin(angle) * r), brightness);
                    }
                }
                break;
            }
            case 1: // Horizontal mirrored bars
            {
                for (int i = 0; i < WAVE_SIZE; ++i)
                {
                    float frac = static_cast<float>(i) / WAVE_SIZE;
                    float sample = std::abs(wave[i]) * (1.0f + energy * 2.5f);
                    int px = static_cast<int>(frac * bufW);
                    int barH = static_cast<int>(sample * bufH * 0.4f);
                    int brt = static_cast<int>(brightness * (0.5f + sample));
                    for (int dy = 0; dy < barH; dy += 2)
                    {
                        plotPixel(px, static_cast<int>(cy) - dy, brt);
                        plotPixel(px, static_cast<int>(cy) + dy, brt);
                    }
                }
                break;
            }
            case 2: // Star burst — waveform as radial spokes
            {
                int numSpokes = 12 + static_cast<int>(energy * 12);
                for (int s = 0; s < numSpokes; ++s)
                {
                    float spokeAngle = static_cast<float>(s) / numSpokes * juce::MathConstants<float>::twoPi + t * 0.2f;
                    for (int i = 0; i < WAVE_SIZE; i += 3)
                    {
                        float frac = static_cast<float>(i) / WAVE_SIZE;
                        float sample = wave[i] * (1.0f + energy * 2.0f);
                        float r = frac * juce::jmin(bufW, bufH) * 0.45f + sample * 20.0f;
                        plotThick(static_cast<int>(cx + std::cos(spokeAngle) * r),
                                  static_cast<int>(cy + std::sin(spokeAngle) * r), brightness / 2);
                    }
                }
                break;
            }
            case 3: // Lissajous figure-8
            {
                float freqX = 2.0f + std::floor(std::sin(t * 0.2f) * 2.0f);
                float freqY = 3.0f + std::floor(std::cos(t * 0.15f) * 2.0f);
                for (int i = 0; i < WAVE_SIZE; ++i)
                {
                    float frac = static_cast<float>(i) / WAVE_SIZE;
                    float sample = wave[i] * (1.0f + energy * 2.0f);
                    float angle = frac * juce::MathConstants<float>::twoPi;
                    float rx = juce::jmin(bufW, bufH) * 0.3f + sample * 30.0f;
                    plotThick(static_cast<int>(cx + std::sin(angle * freqX + t * 0.5f) * rx),
                              static_cast<int>(cy + std::cos(angle * freqY + t * 0.3f) * rx * 0.7f), brightness);
                }
                break;
            }
            case 4: // Grid waveform — horizontal + vertical cross
            {
                int step = 4;
                for (int i = 0; i < WAVE_SIZE; i += step)
                {
                    float frac = static_cast<float>(i) / WAVE_SIZE;
                    float sample = wave[i] * (1.0f + energy * 2.5f);
                    // Horizontal wave
                    int hx = static_cast<int>(frac * bufW);
                    int hy = static_cast<int>(cy + sample * bufH * 0.35f);
                    plotThick(hx, hy, brightness);
                    // Vertical wave
                    int vy = static_cast<int>(frac * bufH);
                    int vx = static_cast<int>(cx + sample * bufW * 0.35f);
                    plotThick(vx, vy, brightness);
                }
                break;
            }
            case 5: // Spiral outward
            {
                for (int i = 0; i < WAVE_SIZE; ++i)
                {
                    float frac = static_cast<float>(i) / WAVE_SIZE;
                    float sample = wave[i] * (1.0f + energy * 2.0f);
                    float angle = frac * juce::MathConstants<float>::twoPi * 4.0f + t;
                    float r = frac * juce::jmin(bufW, bufH) * 0.4f + sample * 25.0f;
                    plotThick(static_cast<int>(cx + std::cos(angle) * r),
                              static_cast<int>(cy + std::sin(angle) * r), brightness);
                }
                break;
            }
            case 6: // Diamond/square wave
            {
                for (int i = 0; i < WAVE_SIZE; ++i)
                {
                    float frac = static_cast<float>(i) / WAVE_SIZE;
                    float sample = wave[i] * (1.0f + energy * 2.5f);
                    float side = juce::jmin(bufW, bufH) * 0.3f + sample * 30.0f;
                    // Walk around a diamond shape
                    float px, py;
                    float seg = std::fmod(frac * 4.0f, 4.0f);
                    if (seg < 1.0f)      { px = cx + side * seg;         py = cy - side * (1.0f - seg); }
                    else if (seg < 2.0f) { px = cx + side * (2.0f - seg); py = cy + side * (seg - 1.0f); }
                    else if (seg < 3.0f) { px = cx - side * (seg - 2.0f); py = cy + side * (3.0f - seg); }
                    else                 { px = cx - side * (4.0f - seg); py = cy - side * (seg - 3.0f); }
                    // Rotate over time
                    float ca = std::cos(t * 0.3f), sa = std::sin(t * 0.3f);
                    float rx = (px - cx) * ca - (py - cy) * sa + cx;
                    float ry = (px - cx) * sa + (py - cy) * ca + cy;
                    plotThick(static_cast<int>(rx), static_cast<int>(ry), brightness);
                }
                break;
            }
            case 7: // Scatter dots — random positions weighted by wave amplitude
            {
                uint32_t seed = static_cast<uint32_t>(t * 777.0);
                for (int i = 0; i < WAVE_SIZE; i += 2)
                {
                    float sample = std::abs(wave[i]) * (1.0f + energy * 2.0f);
                    seed = seed * 1664525u + 1013904223u;
                    float angle = static_cast<float>(seed & 0xFFFF) / 65536.0f * juce::MathConstants<float>::twoPi;
                    float r = sample * juce::jmin(bufW, bufH) * 0.45f;
                    int px = static_cast<int>(cx + std::cos(angle) * r);
                    int py = static_cast<int>(cy + std::sin(angle) * r);
                    int brt = static_cast<int>(60 + sample * 300.0f);
                    plotPixel(px, py, brt);
                }
                break;
            }
        }
    }

    void plotPixel(int x, int y, int brightness)
    {
        if (x < 0 || x >= bufW || y < 0 || y >= bufH) return;
        size_t idx = static_cast<size_t>(y * bufW + x);
        vs2[idx] = juce::jmin(255, vs2[idx] + brightness);
    }

    void plotThick(int x, int y, int brightness)
    {
        plotPixel(x, y, brightness);
        plotPixel(x + 1, y, brightness / 2);
        plotPixel(x, y + 1, brightness / 2);
        plotPixel(x - 1, y, brightness / 3);
        plotPixel(x, y - 1, brightness / 3);
    }

    // Pulsars — bright expanding rings on beats
    void renderPulsars(float energy, bool beat)
    {
        float t = static_cast<float>(effectPhase);

        if (beat)
        {
            // Place a new pulsar
            pulsarAge[nextPulsar] = 0.0f;
            pulsarX[nextPulsar] = bufW * (0.3f + 0.4f * static_cast<float>(std::sin(t * 1.7)));
            pulsarY[nextPulsar] = bufH * (0.3f + 0.4f * static_cast<float>(std::cos(t * 1.3)));
            nextPulsar = (nextPulsar + 1) % MAX_PULSARS;
        }

        for (int p = 0; p < MAX_PULSARS; ++p)
        {
            if (pulsarAge[p] > 1.0f) continue;

            float age = pulsarAge[p];
            pulsarAge[p] += 0.03f + energy * 0.02f;

            float radius = age * juce::jmin(bufW, bufH) * 0.4f;
            float fade = 1.0f - age;
            int brightness = static_cast<int>(fade * (100 + energy * 100));

            int cx = static_cast<int>(pulsarX[p]);
            int cy = static_cast<int>(pulsarY[p]);
            int numPoints = static_cast<int>(radius * 6.28f);
            numPoints = juce::jmax(20, juce::jmin(numPoints, 400));

            for (int i = 0; i < numPoints; ++i)
            {
                float angle = static_cast<float>(i) / numPoints * juce::MathConstants<float>::twoPi;
                int px = cx + static_cast<int>(std::cos(angle) * radius);
                int py = cy + static_cast<int>(std::sin(angle) * radius);

                if (px >= 0 && px < bufW && py >= 0 && py < bufH)
                {
                    size_t idx = static_cast<size_t>(py * bufW + px);
                    vs2[idx] = juce::jmin(255, vs2[idx] + brightness);
                }
            }
        }
    }

    static constexpr int MAX_PULSARS = 6;
    float pulsarAge[MAX_PULSARS] = { 2, 2, 2, 2, 2, 2 };
    float pulsarX[MAX_PULSARS] = {};
    float pulsarY[MAX_PULSARS] = {};
    int nextPulsar = 0;

    // Nebula field — slowly drifting bright spots
    void renderNebulaField(float energy)
    {
        float t = static_cast<float>(effectPhase);

        for (int n = 0; n < 5; ++n)
        {
            float nx = bufW * (0.5f + 0.35f * std::sin(t * 0.4f * (n + 1) + n * 1.2f));
            float ny = bufH * (0.5f + 0.35f * std::cos(t * 0.3f * (n + 1) + n * 0.8f));
            float radius = 6.0f + energy * 10.0f + 4.0f * std::sin(t * (1.0f + n * 0.5f));

            int ix = static_cast<int>(nx);
            int iy = static_cast<int>(ny);
            int r = static_cast<int>(radius);

            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    int px = ix + dx;
                    int py = iy + dy;
                    if (px < 0 || px >= bufW || py < 0 || py >= bufH) continue;

                    float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                    if (d > radius) continue;

                    float intensity = (1.0f - d / radius);
                    intensity *= intensity;
                    int add = static_cast<int>(intensity * (40.0f + energy * 80.0f));

                    size_t idx = static_cast<size_t>(py * bufW + px);
                    vs2[idx] = juce::jmin(255, vs2[idx] + add);
                }
            }
        }
    }

    // Flow particles — stream of dots following the warp field
    void renderFlowParticles(float energy, bool beat)
    {
        float t = static_cast<float>(effectPhase);
        int numParticles = static_cast<int>(30 + energy * 80);

        uint32_t seed = static_cast<uint32_t>(t * 500.0);
        for (int i = 0; i < numParticles; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            int px = static_cast<int>(seed % static_cast<uint32_t>(bufW));
            seed = seed * 1664525u + 1013904223u;
            int py = static_cast<int>(seed % static_cast<uint32_t>(bufH));

            if (px >= 0 && px < bufW && py >= 0 && py < bufH)
            {
                int add = static_cast<int>(30 + energy * 60);
                if (beat) add += 40;
                size_t idx = static_cast<size_t>(py * bufW + px);
                vs2[idx] = juce::jmin(255, vs2[idx] + add);
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectMComponent)
};
