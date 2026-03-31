#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

// Geiss-inspired visualizer.
// Software-rendered per-pixel warping with audio-reactive waveforms,
// shade bobs, solar particles, and color palette cycling.
// Based on the classic Geiss screensaver/Winamp plugin by Ryan Geiss.
class GeissComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr int WAVE_SIZE = 576;
    static constexpr int MAP_RECOMPUTE_FRAMES = 60;

    GeissComponent()
    {
        waveBuffer.fill(0.0f);
        startTimerHz(30);
    }

    ~GeissComponent() override { stopTimer(); }

    // ── Public controls ──

    // Cycle through the 6 waveform drawing modes
    void cycleWaveform() { waveMode = (waveMode + 1) % 6; }
    int getWaveformMode() const { return waveMode; }
    void setWaveformMode(int mode) { waveMode = juce::jlimit(0, 5, mode); }

    // Cycle to the next color palette style
    static constexpr int NUM_PALETTE_STYLES = 10;
    void cyclePalette()
    {
        paletteStyle = (paletteStyle + 1) % NUM_PALETTE_STYLES;
        buildPalette();
    }
    void setPaletteStyle(int style) { paletteStyle = juce::jlimit(0, NUM_PALETTE_STYLES - 1, style); buildPalette(); }
    int getPaletteStyle() const { return paletteStyle; }

    // Randomize the scene — new warp parameters, clear buffers, random palette
    void newRandomScene()
    {
        juce::Random rng = juce::Random::getSystemRandom();
        phase = rng.nextDouble() * 100.0;
        effectPhase = rng.nextDouble() * 100.0;
        paletteStyle = rng.nextInt(NUM_PALETTE_STYLES);
        mapFrameCounter = 0;
        if (!vs1.empty()) std::fill(vs1.begin(), vs1.end(), 0);
        if (!vs2.empty()) std::fill(vs2.begin(), vs2.end(), 0);
        buildPalette();
    }

    // Wave amplitude scale (0.0 – 3.0, default 1.0)
    void setWaveScale(float s) { waveScale = juce::jlimit(0.0f, 3.0f, s); }
    float getWaveScale() const { return waveScale; }
    void waveScaleUp()   { setWaveScale(waveScale + 0.25f); }
    void waveScaleDown() { setWaveScale(waveScale - 0.25f); }

    // Lock/unlock the warp map (freeze the current distortion)
    void toggleWarpLock()   { warpLocked = !warpLocked; }
    bool isWarpLocked() const { return warpLocked; }

    // Lock/unlock palette cycling
    void togglePaletteLock()   { paletteLocked = !paletteLocked; }
    bool isPaletteLocked() const { return paletteLocked; }

    // Animation speed multiplier (0.25 – 4.0, default 1.0)
    void setSpeed(float s) { speedMult = juce::jlimit(0.25f, 4.0f, s); }
    float getSpeed() const { return speedMult; }

    // Auto-pilot mode — audio drives all parameter changes
    void toggleAutoPilot() { autoPilot = !autoPilot; }
    bool isAutoPilot() const { return autoPilot; }

    // Black background — forces low palette values to pure black
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

        // Beat detection — simple threshold on RMS jump
        float cur = smoothedRms.load();
        if (cur > avgRms.load() * 1.8f && cur > 0.02f)
            beatHit.store(true);
        avgRms.store(avgRms.load() * 0.95f + cur * 0.05f);
    }

    void timerCallback() override
    {
        double spd = static_cast<double>(speedMult);
        phase += 0.012 * spd;
        effectPhase += 0.02 * spd;

        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);
        bool isBeat = beatHit.load();

        // ── Auto-pilot: audio drives everything ──
        if (autoPilot)
        {
            juce::Random& rng = juce::Random::getSystemRandom();

            // Speed reacts to energy: quiet = slow & dreamy, loud = fast & intense
            autoSpeedTarget = 0.5f + energy * 2.5f;
            speedMult += (autoSpeedTarget - speedMult) * 0.1f;
            speedMult = juce::jlimit(0.25f, 4.0f, speedMult);

            // Wave scale pulses with energy
            autoWaveScaleTarget = 0.3f + energy * 2.2f;
            waveScale += (autoWaveScaleTarget - waveScale) * 0.15f;
            waveScale = juce::jlimit(0.0f, 3.0f, waveScale);

            // On beats: cycle waveform, sometimes randomize scene
            if (isBeat)
            {
                autoBeatsTotal++;

                // Cycle waveform every 8 beats
                if (autoBeatsTotal % 8 == 0)
                    waveMode = (waveMode + 1) % 6;

                // Toggle warp lock occasionally (every ~24 beats)
                if (autoBeatsTotal % 24 == 0)
                    warpLocked = !warpLocked;

                // Full scene reset every ~48 beats with blackout
                if (autoBeatsTotal % 48 == 0)
                {
                    phase = rng.nextDouble() * 100.0;
                    effectPhase = rng.nextDouble() * 100.0;
                    mapFrameCounter = 0;
                    warpLocked = false;
                    blackoutFade = 1.0f;
                }
            }

            // During quiet sections, slowly drift waveform mode
            autoSceneTimer += 0.033f * (1.0f - energy);
            if (autoSceneTimer > 8.0f)
            {
                autoSceneTimer = 0.0f;
                waveMode = (waveMode + 1) % 6;
            }
        }

        if (!paletteLocked)
        {
            // Audio-reactive palette rotation: louder = faster cycling
            palettePhase += (0.002 + 0.015 * static_cast<double>(energy)) * spd;

            // Beat hit → jump to a new palette style
            if (isBeat)
            {
                beatPaletteCounter++;
                if (beatPaletteCounter >= beatsPerPaletteChange)
                {
                    paletteStyle = (paletteStyle + 1) % NUM_PALETTE_STYLES;
                    beatPaletteCounter = 0;
                    blackoutFade = 1.0f; // flash to black on palette change
                }
            }

            // Decay blackout
            if (blackoutFade > 0.0f)
                blackoutFade = juce::jmax(0.0f, blackoutFade - 0.08f);

            // Rebuild palette every frame with phase-shifted colors
            buildPalette();
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        int w = getWidth();
        int h = getHeight();
        if (w < 4 || h < 4) return;

        // Reallocate buffers on size change
        if (w != bufW || h != bufH)
        {
            bufW = w;
            bufH = h;
            vs1.assign(static_cast<size_t>(w * h), 0);
            vs2.assign(static_cast<size_t>(w * h), 0);
            mapDx.assign(static_cast<size_t>(w * h), 0);
            mapDy.assign(static_cast<size_t>(w * h), 0);
            mapFrameCounter = 0;
            buildPalette();
        }

        // Recompute warp map periodically for morphing effect
        if (!warpLocked && mapFrameCounter <= 0)
        {
            computeWarpMap();
            mapFrameCounter = MAP_RECOMPUTE_FRAMES;
        }
        if (!warpLocked) mapFrameCounter--;

        // Step 1: Apply per-pixel warp map (VS1 → VS2) with blur
        applyWarpMapBlur();

        // Step 2: Render effects into VS2
        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);
        bool beat = beatHit.exchange(false);

        renderShadeBobs(energy, beat);
        renderSolarParticles(energy, beat);
        renderChasers(energy);

        // Step 3: Render audio waveform
        renderWaveform(energy);

        // Step 4: Swap buffers
        std::swap(vs1, vs2);

        // Step 5: Blit to JUCE image with palette rotation and energy offset
        int paletteRotation = static_cast<int>(palettePhase * 256.0) % 256;
        int energyOffset = static_cast<int>(energy * 30.0f);
        float brightMult = 1.0f - blackoutFade; // 0 during blackout, 1 normally

        juce::Image img(juce::Image::ARGB, w, h, false);
        {
            juce::Image::BitmapData bmp(img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    int idx = vs1[static_cast<size_t>(y * w + x)];
                    idx = juce::jlimit(0, 255, idx);
                    // Rotate through palette and shift by energy
                    idx = (idx + paletteRotation + energyOffset) % 256;
                    uint32_t col = palette[static_cast<size_t>(idx)];
                    auto* pixel = bmp.getPixelPointer(x, y);
                    // JUCE ARGB Image: pixel order is BGRA in memory
                    pixel[0] = static_cast<uint8_t>(static_cast<float>(col & 0xFF) * brightMult);          // B
                    pixel[1] = static_cast<uint8_t>(static_cast<float>((col >> 8) & 0xFF) * brightMult);   // G
                    pixel[2] = static_cast<uint8_t>(static_cast<float>((col >> 16) & 0xFF) * brightMult);  // R
                    pixel[3] = 0xFF;                                                                        // A
                }
            }
        }
        g.drawImageAt(img, 0, 0);
    }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

private:
    // Audio state
    std::array<float, WAVE_SIZE> waveBuffer;
    int writePos = 0;
    std::atomic<float> smoothedRms { 0.0f };
    std::atomic<float> avgRms { 0.0f };
    std::atomic<bool> beatHit { false };

    // Animation phases
    double phase = 0.0;
    double effectPhase = 0.0;
    double palettePhase = 0.0;
    int mapFrameCounter = 0;

    // Controllable parameters
    int waveMode = 0;            // 0-5 waveform drawing mode
    float waveScale = 1.0f;      // wave amplitude multiplier
    bool warpLocked = false;     // freeze warp map
    bool paletteLocked = false;  // freeze palette cycling
    float speedMult = 1.0f;      // animation speed multiplier
    int paletteStyle = 0;        // 0-(NUM_PALETTE_STYLES-1) palette type
    int beatPaletteCounter = 0;  // count beats between palette changes
    int beatsPerPaletteChange = 4; // change palette every N beats
    float blackoutFade = 0.0f;    // 1.0 = full blackout, decays to 0

    bool blackBg = false;           // force black background

    // Auto-pilot state
    bool autoPilot = false;
    int autoBeatsTotal = 0;       // total beats counted in auto mode
    float autoSceneTimer = 0.0f;  // timer for full scene changes
    float autoSpeedTarget = 1.0f; // smoothly approach this speed
    float autoWaveScaleTarget = 1.0f;

    // Render buffers — indexed color (0-255)
    int bufW = 0, bufH = 0;
    std::vector<int> vs1, vs2;

    // Per-pixel warp displacement map
    std::vector<int> mapDx, mapDy;

    // 256-entry color palette (ARGB packed)
    std::array<uint32_t, 256> palette;

    // ── Palette generation — 10 distinct styles ──
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

    // Build a cyclic gradient palette from a list of color stops (wraps back to first stop)
    void buildGradientPalette(const uint32_t* stops, int numStops)
    {
        // Create a cyclic palette: the last stop wraps back to the first
        for (int i = 0; i < 256; ++i)
        {
            float pos = static_cast<float>(i) / 256.0f * static_cast<float>(numStops);
            int idx = static_cast<int>(pos) % numStops;
            int next = (idx + 1) % numStops;
            float frac = pos - std::floor(pos);
            palette[static_cast<size_t>(i)] = lerpRGB(stops[idx], stops[next], frac);
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

    void buildPalette()
    {
        switch (paletteStyle)
        {
            case 0: // Fire — black → red → orange → yellow → white
            {
                const uint32_t stops[] = { packRGB(0,0,0), packRGB(0.5f,0,0), packRGB(0.9f,0.2f,0),
                    packRGB(1,0.6f,0), packRGB(1,1,0.3f), packRGB(1,1,0.9f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 1: // Rainbow — full spectrum cycle
            {
                const uint32_t stops[] = { packRGB(1,0,0), packRGB(1,0.5f,0), packRGB(1,1,0),
                    packRGB(0,1,0), packRGB(0,0.5f,1), packRGB(0.5f,0,1), packRGB(1,0,0.5f) };
                buildGradientPalette(stops, 7);
                break;
            }
            case 2: // Copper — black → brown → copper → gold → bright gold
            {
                const uint32_t stops[] = { packRGB(0.02f,0.01f,0), packRGB(0.3f,0.15f,0.05f),
                    packRGB(0.7f,0.35f,0.1f), packRGB(0.9f,0.6f,0.2f), packRGB(1,0.85f,0.4f), packRGB(1,0.95f,0.7f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 3: // Forest — black → dark green → emerald → lime → yellow-green
            {
                const uint32_t stops[] = { packRGB(0,0.03f,0), packRGB(0,0.2f,0.05f), packRGB(0,0.5f,0.15f),
                    packRGB(0.2f,0.8f,0.1f), packRGB(0.6f,1,0.3f), packRGB(0.9f,1,0.5f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 4: // Lava — black → dark red → crimson → orange → gold → white
            {
                const uint32_t stops[] = { packRGB(0.05f,0,0), packRGB(0.3f,0,0), packRGB(0.7f,0.05f,0),
                    packRGB(1,0.3f,0), packRGB(1,0.7f,0.1f), packRGB(1,0.95f,0.7f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 5: // Candy — pink → peach → yellow → mint → pink
            {
                const uint32_t stops[] = { packRGB(1,0.3f,0.5f), packRGB(1,0.6f,0.3f), packRGB(1,0.9f,0.3f),
                    packRGB(0.3f,1,0.5f), packRGB(0.3f,0.8f,1), packRGB(0.8f,0.3f,1) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 6: // Sunset — deep red → orange → golden yellow → warm white
            {
                const uint32_t stops[] = { packRGB(0.3f,0.02f,0), packRGB(0.7f,0.1f,0), packRGB(1,0.35f,0),
                    packRGB(1,0.6f,0.1f), packRGB(1,0.8f,0.3f), packRGB(1,0.95f,0.7f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 7: // Electric — black → deep yellow → bright yellow → white
            {
                const uint32_t stops[] = { packRGB(0.02f,0.02f,0), packRGB(0.3f,0.25f,0),
                    packRGB(0.7f,0.6f,0), packRGB(1,0.9f,0), packRGB(1,1,0.4f), packRGB(1,1,0.85f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 8: // Toxic — black → dark green → bright green → yellow-green → white
            {
                const uint32_t stops[] = { packRGB(0,0.02f,0), packRGB(0,0.3f,0), packRGB(0.1f,0.8f,0),
                    packRGB(0.5f,1,0), packRGB(0.8f,1,0.3f), packRGB(1,1,0.8f) };
                buildGradientPalette(stops, 6);
                break;
            }
            case 9: // Infrared — black → deep magenta → red → orange → yellow → white
            {
                const uint32_t stops[] = { packRGB(0.05f,0,0.05f), packRGB(0.4f,0,0.2f), packRGB(0.8f,0.05f,0.05f),
                    packRGB(1,0.3f,0), packRGB(1,0.7f,0), packRGB(1,1,0.6f) };
                buildGradientPalette(stops, 6);
                break;
            }
        }
    }

    // ── Warp map — the signature Geiss per-pixel distortion ──
    void computeWarpMap()
    {
        if (bufW < 2 || bufH < 2) return;

        float cx = bufW * 0.5f;
        float cy = bufH * 0.5f;
        float t = static_cast<float>(phase);

        // Morphing warp parameters
        float zoomAmt = 0.97f + 0.02f * std::sin(t * 0.7f);
        float rotAmt = 0.02f * std::sin(t * 0.3f);
        float twistAmt = 0.0003f * std::sin(t * 0.5f);

        for (int y = 0; y < bufH; ++y)
        {
            for (int x = 0; x < bufW; ++x)
            {
                float dx = static_cast<float>(x) - cx;
                float dy = static_cast<float>(y) - cy;
                float dist = std::sqrt(dx * dx + dy * dy) + 0.001f;

                // Zoom toward center
                float nx = dx * zoomAmt;
                float ny = dy * zoomAmt;

                // Rotation (uniform + distance-based twist)
                float angle = rotAmt + twistAmt * dist;
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);
                float rx = nx * cosA - ny * sinA;
                float ry = nx * sinA + ny * cosA;

                // Source pixel
                int sx = static_cast<int>(cx + rx);
                int sy = static_cast<int>(cy + ry);

                // Store as displacement from current position
                size_t idx = static_cast<size_t>(y * bufW + x);
                mapDx[idx] = juce::jlimit(0, bufW - 1, sx) - x;
                mapDy[idx] = juce::jlimit(0, bufH - 1, sy) - y;
            }
        }

    }

    // ── Apply warp + 3x3 box blur: gives the classic soft Geiss smear ──
    void applyWarpMapBlur()
    {
        if (bufW < 4 || bufH < 4) return;

        for (int y = 0; y < bufH; ++y)
        {
            for (int x = 0; x < bufW; ++x)
            {
                size_t i = static_cast<size_t>(y * bufW + x);
                int sx = x + mapDx[i];
                int sy = y + mapDy[i];

                // For edge pixels, just do a simple warp with no blur
                if (sx <= 0 || sx >= bufW - 1 || sy <= 0 || sy >= bufH - 1)
                {
                    sx = juce::jlimit(0, bufW - 1, sx);
                    sy = juce::jlimit(0, bufH - 1, sy);
                    vs2[i] = juce::jmax(0, vs1[static_cast<size_t>(sy * bufW + sx)] - 1);
                    continue;
                }

                // 3x3 weighted average at the warped source position
                // Center weighted 4x, edges 2x, corners 1x (total 16)
                int sum = vs1[static_cast<size_t>((sy - 1) * bufW + (sx - 1))]
                        + vs1[static_cast<size_t>((sy - 1) * bufW + sx)] * 2
                        + vs1[static_cast<size_t>((sy - 1) * bufW + (sx + 1))]
                        + vs1[static_cast<size_t>(sy * bufW + (sx - 1))] * 2
                        + vs1[static_cast<size_t>(sy * bufW + sx)] * 4
                        + vs1[static_cast<size_t>(sy * bufW + (sx + 1))] * 2
                        + vs1[static_cast<size_t>((sy + 1) * bufW + (sx - 1))]
                        + vs1[static_cast<size_t>((sy + 1) * bufW + sx)] * 2
                        + vs1[static_cast<size_t>((sy + 1) * bufW + (sx + 1))];

                vs2[i] = juce::jmax(0, (sum >> 4) - 1); // divide by 16, slight fade
            }
        }
    }

    // ── Shade bobs: pulsing bright blobs that move in Lissajous patterns ──
    void renderShadeBobs(float energy, bool beat)
    {
        float t = static_cast<float>(effectPhase);
        int numBobs = 3;
        float bobRadius = 8.0f + energy * 12.0f;
        if (beat) bobRadius += 10.0f;

        for (int b = 0; b < numBobs; ++b)
        {
            float bPhase = t + b * 2.094f; // 2π/3 spacing
            float bx = bufW * 0.5f + bufW * 0.35f * std::sin(bPhase * 1.3f + b);
            float by = bufH * 0.5f + bufH * 0.35f * std::cos(bPhase * 0.9f + b * 1.7f);

            int ix = static_cast<int>(bx);
            int iy = static_cast<int>(by);
            int r = static_cast<int>(bobRadius);

            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    int px = ix + dx;
                    int py = iy + dy;
                    if (px < 0 || px >= bufW || py < 0 || py >= bufH) continue;

                    float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                    if (dist > bobRadius) continue;

                    float intensity = (1.0f - dist / bobRadius);
                    intensity *= intensity; // quadratic falloff
                    int add = static_cast<int>(intensity * (60.0f + energy * 120.0f));

                    size_t idx = static_cast<size_t>(py * bufW + px);
                    vs2[idx] = juce::jmin(255, vs2[idx] + add);
                }
            }
        }
    }

    // ── Solar particles: radial burst from center on beats ──
    void renderSolarParticles(float energy, bool beat)
    {
        if (!beat && energy < 0.3f) return;

        float cx = bufW * 0.5f;
        float cy = bufH * 0.5f;
        int numParticles = static_cast<int>(20 + energy * 60);
        float maxRadius = juce::jmin(bufW, bufH) * 0.4f * (0.5f + energy);

        // Use a deterministic seed that changes each frame for variety
        uint32_t seed = static_cast<uint32_t>(effectPhase * 1000.0);

        for (int i = 0; i < numParticles; ++i)
        {
            seed = seed * 1664525u + 1013904223u; // LCG
            float angle = static_cast<float>(seed & 0xFFFF) / 65536.0f * juce::MathConstants<float>::twoPi;
            seed = seed * 1664525u + 1013904223u;
            float radius = static_cast<float>(seed & 0xFFFF) / 65536.0f * maxRadius;

            int px = static_cast<int>(cx + std::cos(angle) * radius);
            int py = static_cast<int>(cy + std::sin(angle) * radius);
            if (px < 0 || px >= bufW || py < 0 || py >= bufH) continue;

            float falloff = 1.0f - (radius / maxRadius);
            int add = static_cast<int>(falloff * (40.0f + energy * 80.0f));
            size_t idx = static_cast<size_t>(py * bufW + px);
            vs2[idx] = juce::jmin(255, vs2[idx] + add);
        }
    }

    // ── Chasers: two bright dots tracing parametric curves ──
    void renderChasers(float energy)
    {
        float t = static_cast<float>(effectPhase);
        float cx = bufW * 0.5f;
        float cy = bufH * 0.5f;

        for (int c = 0; c < 2; ++c)
        {
            float ct = t * (1.2f + c * 0.3f) + c * 3.14159f;
            float px = cx + bufW * 0.3f * std::cos(ct * 1.1f) * std::sin(ct * 0.7f);
            float py = cy + bufH * 0.3f * std::sin(ct * 0.8f) * std::cos(ct * 1.3f);

            int ix = juce::jlimit(1, bufW - 2, static_cast<int>(px));
            int iy = juce::jlimit(1, bufH - 2, static_cast<int>(py));

            // Bright dot with small cross shape
            int brightness = static_cast<int>(180 + energy * 75);
            for (int d = -2; d <= 2; ++d)
            {
                int hx = juce::jlimit(0, bufW - 1, ix + d);
                int vy = juce::jlimit(0, bufH - 1, iy + d);
                vs2[static_cast<size_t>(iy * bufW + hx)] = juce::jmin(255,
                    vs2[static_cast<size_t>(iy * bufW + hx)] + brightness);
                vs2[static_cast<size_t>(vy * bufW + ix)] = juce::jmin(255,
                    vs2[static_cast<size_t>(vy * bufW + ix)] + brightness);
            }
        }
    }

    // ── Audio waveform overlay — 6 modes like the original Geiss ──
    void renderWaveform(float energy)
    {
        // Snapshot wave data
        std::array<float, WAVE_SIZE> wave;
        int rp = writePos;
        for (int i = 0; i < WAVE_SIZE; ++i)
            wave[i] = waveBuffer[(rp + i) % WAVE_SIZE];

        float cx = bufW * 0.5f;
        float cy = bufH * 0.5f;
        int brightness = static_cast<int>(140 + energy * 115);

        for (int i = 1; i < WAVE_SIZE; ++i)
        {
            float t = static_cast<float>(i) / WAVE_SIZE;
            float sample = wave[i] * waveScale * (1.0f + energy * 3.0f);
            int px = 0, py = 0;

            switch (waveMode)
            {
                case 0: // Horizontal centered waveform
                    px = static_cast<int>(t * bufW);
                    py = static_cast<int>(cy + sample * bufH * 0.35f);
                    break;
                case 1: // Circular waveform
                {
                    float angle = t * juce::MathConstants<float>::twoPi;
                    float r = juce::jmin(bufW, bufH) * 0.25f + sample * bufH * 0.15f;
                    px = static_cast<int>(cx + std::cos(angle) * r);
                    py = static_cast<int>(cy + std::sin(angle) * r);
                    break;
                }
                case 2: // Dual mirrored waveform
                    px = static_cast<int>(t * bufW);
                    py = static_cast<int>(cy + std::abs(sample) * bufH * 0.4f);
                    plotPixel(px, static_cast<int>(cy - std::abs(sample) * bufH * 0.4f), brightness);
                    break;
                case 3: // X-Y oscilloscope (stereo uses L vs R, mono wraps)
                {
                    float s2 = (i + 1 < WAVE_SIZE) ? wave[i + 1] : wave[i];
                    px = static_cast<int>(cx + sample * bufW * 0.35f);
                    py = static_cast<int>(cy + s2 * bufH * 0.35f);
                    break;
                }
                case 4: // Spiral waveform
                {
                    float angle = t * juce::MathConstants<float>::twoPi * 3.0f;
                    float r = t * juce::jmin(bufW, bufH) * 0.35f + sample * 30.0f;
                    px = static_cast<int>(cx + std::cos(angle) * r);
                    py = static_cast<int>(cy + std::sin(angle) * r);
                    break;
                }
                case 5: // Dotty scatter
                    px = static_cast<int>(t * bufW);
                    py = static_cast<int>(cy + sample * bufH * 0.4f);
                    brightness = static_cast<int>(80 + std::abs(sample) * 400.0f);
                    break;
            }

            plotPixel(px, py, brightness);
        }
    }

    // Plot a single pixel with additive blending, clamped to bounds
    void plotPixel(int x, int y, int brightness)
    {
        if (x < 0 || x >= bufW || y < 0 || y >= bufH) return;
        size_t idx = static_cast<size_t>(y * bufW + x);
        vs2[idx] = juce::jmin(255, vs2[idx] + brightness);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeissComponent)
};
