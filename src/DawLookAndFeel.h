#pragma once

#include <JuceHeader.h>
#include "DawTheme.h"

// Base class for all DAW themes.  Provides:
//   - A DawTheme color struct for paint() code
//   - Common custom drawing (buttons, sliders, combos)
//   - Subclasses only need to supply colors + optionally override drawing
class DawLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DawLookAndFeel() = default;
    virtual ~DawLookAndFeel() = default;

    const DawTheme& getTheme() const { return theme; }

    // Side panel width — override to add decorative side panels (e.g. wood cheeks)
    virtual int getSidePanelWidth() const { return 0; }
    virtual void drawSidePanels(juce::Graphics&, int /*width*/, int /*height*/) {}

    // Top bar background — override for custom textures (e.g. wood grain)
    virtual void drawTopBarBackground(juce::Graphics&, int /*x*/, int /*y*/, int /*width*/, int /*height*/) {}

    // The font name used for UI controls (monospace by default)
    virtual juce::String getUIFontName() const { return "Consolas"; }

    // The font name used for display/headings (can be serif for some themes)
    virtual juce::String getDisplayFontName() const { return getUIFontName(); }

protected:
    DawTheme theme {};

    // Call from subclass constructor after filling in `theme`
    void applyThemeColors()
    {
        setColour(juce::ResizableWindow::backgroundColourId,       juce::Colour(theme.body));
        setColour(juce::TextButton::buttonColourId,                juce::Colour(theme.buttonFace));
        setColour(juce::TextButton::buttonOnColourId,              juce::Colour(theme.amber));
        setColour(juce::TextButton::textColourOffId,               juce::Colour(theme.textPrimary));
        setColour(juce::TextButton::textColourOnId,                juce::Colour(theme.bodyDark));
        setColour(juce::ComboBox::backgroundColourId,              juce::Colour(theme.bodyDark));
        setColour(juce::ComboBox::textColourId,                    juce::Colour(theme.textPrimary));
        setColour(juce::ComboBox::outlineColourId,                 juce::Colour(theme.border));
        setColour(juce::PopupMenu::backgroundColourId,             juce::Colour(theme.bodyLight));
        setColour(juce::PopupMenu::textColourId,                   juce::Colour(theme.textPrimary));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,  juce::Colour(theme.amber));
        setColour(juce::PopupMenu::highlightedTextColourId,        juce::Colour(theme.bodyDark));
        setColour(juce::Label::textColourId,                       juce::Colour(theme.textPrimary));
        setColour(juce::Slider::thumbColourId,                     juce::Colour(theme.amber));
        setColour(juce::Slider::rotarySliderFillColourId,          juce::Colour(theme.amber));
        setColour(juce::Slider::rotarySliderOutlineColourId,       juce::Colour(theme.border));
        setColour(juce::Slider::trackColourId,                     juce::Colour(theme.bodyDark));
        setColour(juce::Slider::textBoxTextColourId,               juce::Colour(theme.lcdText));
        setColour(juce::Slider::textBoxBackgroundColourId,         juce::Colour(theme.lcdBg));
        setColour(juce::Slider::textBoxOutlineColourId,            juce::Colour(theme.border));

        setDefaultSansSerifTypefaceName(getUIFontName());
    }

    // ── OLED pixel animation constants & helpers ──
    static constexpr int OLED_W = 32;
    static constexpr int OLED_H = 12;

    static void oledPlot(juce::Image& img, int x, int y, juce::Colour col)
    {
        if (x >= 0 && x < img.getWidth() && y >= 0 && y < img.getHeight())
            img.setPixelAt(x, y, col);
    }

    static void oledLine(juce::Image& img, int x0, int y0, int x1, int y1, juce::Colour col)
    {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true)
        {
            oledPlot(img, x0, y0, col);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    static void oledCircle(juce::Image& img, int cx, int cy, int r, juce::Colour col, bool fill = false)
    {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
            {
                int d2 = dx * dx + dy * dy;
                if (fill ? (d2 <= r * r) : (d2 >= (r - 1) * (r - 1) && d2 <= r * r))
                    oledPlot(img, cx + dx, cy + dy, col);
            }
    }

    // Check if a button name should get OLED pixel animation
    static bool isOledAnimatedButton(const juce::String& text)
    {
        return text == "PLAY" || text == "STOP" || text == "MET" ||
               text == "LOOP" || text == "Count-In" || text == "PANIC" ||
               text == "LEARN" || text == "MIX" || text == "KEYS" ||
               text == "VIS" || text == "PROJ";
    }

    // Render OLED pixel art for a button. Returns true if handled.
    virtual bool drawOledButtonArt(juce::Image& oled, const juce::String& text,
                                   bool on, float t, juce::Colour bright, juce::Colour dim) const
    {
        int icx = OLED_W / 2;
        int icy = OLED_H / 2;
        juce::Colour col = on ? bright : dim;

        if (text == "PLAY")
        {
            // Triangle play icon
            for (int row = -3; row <= 3; ++row)
            {
                int pw = 3 - std::abs(row);
                for (int c = 0; c <= pw; ++c)
                    oledPlot(oled, icx - 1 + c, icy + row, col);
            }
            if (on)
            {
                // Bouncing EQ bars behind
                for (int i = 0; i < 4; ++i)
                {
                    int bx = 3 + i * 3;
                    int bh = 2 + static_cast<int>((std::sin(t * 6.0f + i * 1.8f) + 1.0f) * 2.5f);
                    for (int by = OLED_H - 2; by >= OLED_H - 2 - bh && by >= 1; --by)
                        oledPlot(oled, bx, by, bright.withAlpha(0.35f));
                }
                // Scanline sweep
                int sweep = static_cast<int>(t * 14.0f) % OLED_W;
                for (int py = 2; py < OLED_H - 1; ++py)
                    oledPlot(oled, sweep, py, bright.withAlpha(0.15f));
            }
        }
        else if (text == "STOP")
        {
            // Square stop icon
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    oledPlot(oled, icx + dx, icy + dy, col);
        }
        else if (text == "MET")
        {
            // Pendulum metronome
            float swing = on ? std::sin(t * 4.0f) * 5.0f : 0.0f;
            int px = icx + static_cast<int>(swing);
            oledLine(oled, icx, 2, px, OLED_H - 3, col);
            // Bob at end
            oledPlot(oled, px - 1, OLED_H - 3, col);
            oledPlot(oled, px, OLED_H - 3, col);
            oledPlot(oled, px + 1, OLED_H - 3, col);
            // Pivot at top
            oledPlot(oled, icx - 1, 2, col);
            oledPlot(oled, icx, 2, col);
            oledPlot(oled, icx + 1, 2, col);
            if (on)
            {
                // Tick trail dots
                for (int i = 0; i < 3; ++i)
                {
                    float pastT = t - i * 0.15f;
                    float pastSwing = std::sin(pastT * 4.0f) * 5.0f;
                    int dotX = icx + static_cast<int>(pastSwing);
                    oledPlot(oled, dotX, OLED_H - 2, bright.withAlpha(0.2f - i * 0.06f));
                }
            }
        }
        else if (text == "LOOP")
        {
            if (on)
            {
                // Rotating dots chasing around an elliptical path
                for (int i = 0; i < 12; ++i)
                {
                    float a = t * 3.0f + i * 0.52f;
                    float alpha = 0.3f + 0.7f * (static_cast<float>(i) / 12.0f);
                    oledPlot(oled, icx + static_cast<int>(std::cos(a) * 5),
                             icy + static_cast<int>(std::sin(a) * 3), bright.withAlpha(alpha));
                }
                // Arrow head at the lead dot
                float ha = t * 3.0f;
                int ax = icx + static_cast<int>(std::cos(ha) * 5);
                int ay = icy + static_cast<int>(std::sin(ha) * 3);
                oledPlot(oled, ax + 1, ay, bright);
                oledPlot(oled, ax, ay + 1, bright);
            }
            else
            {
                // Static infinity/figure-8
                for (int i = 0; i < 20; ++i)
                {
                    float a = i * 0.314f;
                    float ix = std::sin(a) * 5.0f;
                    float iy = std::sin(a * 2.0f) * 3.0f;
                    oledPlot(oled, icx + static_cast<int>(ix), icy + static_cast<int>(iy), dim);
                }
            }
        }
        else if (text == "Count-In")
        {
            if (on)
            {
                // Cycling countdown digits
                int digit = 3 - (static_cast<int>(t * 4.0f) % 4);
                if (digit <= 0) digit = 4;
                // Draw digit as pixels (simple 3x5 font)
                auto drawDigit = [&](int d, int ox, int oy, juce::Colour c)
                {
                    // Minimal pixel digits
                    const uint8_t digits[5][5] = {
                        { 0b111, 0b101, 0b101, 0b101, 0b111 }, // 0
                        { 0b010, 0b110, 0b010, 0b010, 0b111 }, // 1
                        { 0b111, 0b001, 0b111, 0b100, 0b111 }, // 2
                        { 0b111, 0b001, 0b111, 0b001, 0b111 }, // 3
                        { 0b101, 0b101, 0b111, 0b001, 0b001 }, // 4
                    };
                    if (d < 0 || d > 4) return;
                    for (int row = 0; row < 5; ++row)
                        for (int bit = 0; bit < 3; ++bit)
                            if (digits[d][row] & (1 << (2 - bit)))
                                oledPlot(oled, ox + bit, oy + row, c);
                };
                drawDigit(digit, icx - 1, icy - 2, bright);
            }
            else
            {
                // Static "3 2 1"
                oledPlot(oled, icx - 4, icy, dim);
                oledPlot(oled, icx, icy, dim);
                oledPlot(oled, icx + 4, icy, dim);
            }
        }
        else if (text == "LEARN")
        {
            // Crosshair/target
            oledCircle(oled, icx, icy, 4, col);
            oledPlot(oled, icx, icy, col);
            if (on)
            {
                // Radar sweep beam
                float sweepA = t * 6.0f;
                for (int r = 1; r <= 4; ++r)
                    oledPlot(oled, icx + static_cast<int>(std::cos(sweepA) * r),
                             icy + static_cast<int>(std::sin(sweepA) * r), bright);
                // Trailing beam
                float trailA = sweepA - 0.5f;
                for (int r = 1; r <= 3; ++r)
                    oledPlot(oled, icx + static_cast<int>(std::cos(trailA) * r),
                             icy + static_cast<int>(std::sin(trailA) * r), bright.withAlpha(0.3f));
            }
        }
        else if (text == "MIX")
        {
            // Fader bars
            for (int i = 0; i < 5; ++i)
            {
                int fx = icx - 6 + i * 3;
                int thumbY = icy + static_cast<int>(std::sin(i * 1.5f + (on ? t * 3.0f : 0)) * 2);
                oledLine(oled, fx, 2, fx, OLED_H - 3, dim);
                oledPlot(oled, fx - 1, thumbY, col);
                oledPlot(oled, fx, thumbY, col);
                oledPlot(oled, fx + 1, thumbY, col);
            }
            if (on)
            {
                // Subtle VU glow at top of each fader
                for (int i = 0; i < 5; ++i)
                {
                    int fx = icx - 6 + i * 3;
                    int level = 2 + static_cast<int>((std::sin(t * 3.0f + i * 1.2f) + 1.0f) * 2);
                    for (int ly = OLED_H - 3; ly >= OLED_H - 3 - level && ly >= 2; --ly)
                        oledPlot(oled, fx, ly, bright.withAlpha(0.2f));
                }
            }
        }
        else if (text == "KEYS")
        {
            // Heartbeat piano — flat line that morphs into key silhouette with traveling pulse
            int lineY = icy;
            int kx0 = 2;
            int kx1 = OLED_W - 3;

            if (!on)
            {
                // Static: flat line with subtle piano key bumps poking up
                for (int x = kx0; x <= kx1; ++x)
                    oledPlot(oled, x, lineY, col);
                // Key bumps at piano-key intervals — white keys poke up 2px
                int keyPositions[] = { 4, 7, 10, 14, 17, 20, 23, 27 };
                for (int kp : keyPositions)
                {
                    if (kp > kx1) break;
                    oledPlot(oled, kp, lineY - 1, col);
                    oledPlot(oled, kp, lineY - 2, col.withAlpha(0.6f));
                }
                // Black key notches dip down between some white keys
                int blackPositions[] = { 5, 8, 15, 18, 21 };
                for (int bp : blackPositions)
                {
                    if (bp > kx1) break;
                    oledPlot(oled, bp, lineY + 1, col.withAlpha(0.5f));
                }
            }
            else
            {
                // Animated: pulse travels across, keys rise as it passes
                float pulseX = std::fmod(t * 10.0f, static_cast<float>(kx1 - kx0 + 8)) + kx0 - 4;

                // Base line
                for (int x = kx0; x <= kx1; ++x)
                    oledPlot(oled, x, lineY, col.withAlpha(0.4f));

                // Piano key positions and heights
                int keyPositions[] = { 4, 7, 10, 14, 17, 20, 23, 27 };
                int blackPositions[] = { 5, 8, 15, 18, 21 };

                // Keys rise and fall as pulse passes
                for (int kp : keyPositions)
                {
                    if (kp > kx1) break;
                    float dist = std::abs(static_cast<float>(kp) - pulseX);
                    float rise = juce::jmax(0.0f, 1.0f - dist / 5.0f);
                    int height = static_cast<int>(rise * 4.0f);
                    for (int dy = 0; dy <= height; ++dy)
                    {
                        float fade = 1.0f - static_cast<float>(dy) / 5.0f;
                        oledPlot(oled, kp, lineY - dy, bright.withAlpha(rise * fade));
                    }
                    // Restore the base pixel bright where key is
                    oledPlot(oled, kp, lineY, col);
                }

                // Black key notches pulse downward
                for (int bp : blackPositions)
                {
                    if (bp > kx1) break;
                    float dist = std::abs(static_cast<float>(bp) - pulseX);
                    float dip = juce::jmax(0.0f, 1.0f - dist / 4.0f);
                    int depth = static_cast<int>(dip * 2.0f);
                    for (int dy = 1; dy <= depth; ++dy)
                        oledPlot(oled, bp, lineY + dy, col.withAlpha(dip * 0.6f));
                    oledPlot(oled, bp, lineY, col);
                }

                // Bright pulse dot traveling along the line
                int px = static_cast<int>(pulseX);
                if (px >= kx0 && px <= kx1)
                {
                    oledPlot(oled, px, lineY, bright);
                    if (px - 1 >= kx0) oledPlot(oled, px - 1, lineY, bright.withAlpha(0.5f));
                    if (px + 1 <= kx1) oledPlot(oled, px + 1, lineY, bright.withAlpha(0.5f));
                }
            }
        }
        else if (text == "VIS")
        {
            // Waveform icon
            for (int i = 0; i < OLED_W - 4; ++i)
            {
                float phase = on ? t * 5.0f : 0.0f;
                float wave = std::sin(i * 0.5f + phase) * 3.0f;
                int py = icy + static_cast<int>(wave);
                oledPlot(oled, 2 + i, py, col);
            }
            if (on)
            {
                // Second harmonic overlay
                for (int i = 0; i < OLED_W - 4; i += 2)
                {
                    float wave2 = std::sin(i * 0.8f + t * 7.0f) * 2.0f;
                    int py = icy + static_cast<int>(wave2);
                    oledPlot(oled, 2 + i, py, bright.withAlpha(0.3f));
                }
            }
        }
        else if (text == "PROJ")
        {
            // Projector icon — small box with light cone
            // Projector body
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -2; dx <= 0; ++dx)
                    oledPlot(oled, icx + dx - 3, icy + dy, col);
            // Lens
            oledPlot(oled, icx - 2, icy, bright);
            if (on)
            {
                // Expanding light cone
                float spread = 1.0f + std::sin(t * 3.0f) * 0.5f;
                for (int ray = 0; ray < 8; ++ray)
                {
                    int rx = icx - 1 + ray;
                    int halfH = static_cast<int>(ray * 0.5f * spread);
                    for (int ry = -halfH; ry <= halfH; ++ry)
                        oledPlot(oled, rx, icy + ry, bright.withAlpha(0.15f + 0.05f * (8 - ray)));
                }
            }
            else
            {
                // Static light cone
                for (int ray = 0; ray < 6; ++ray)
                {
                    int rx = icx - 1 + ray;
                    int halfH = ray / 3;
                    for (int ry = -halfH; ry <= halfH; ++ry)
                        oledPlot(oled, rx, icy + ry, dim.withAlpha(0.3f));
                }
            }
        }
        else if (text == "PANIC")
        {
            if (on)
            {
                // Pixelated mushroom cloud — rises and billows
                float phase = std::fmod(t * 3.5f, 4.0f); // 4-second cycle

                // Ground line
                for (int x = 4; x < OLED_W - 4; ++x)
                    oledPlot(oled, x, OLED_H - 1, bright.withAlpha(0.25f));

                // Stem — narrow column rising from ground
                float rise = juce::jmin(phase * 0.5f, 1.0f);
                int stemBottom = OLED_H - 2;
                int stemTop = stemBottom - static_cast<int>(rise * 6);
                // Stem widens slightly at base
                for (int y = stemBottom; y >= stemTop; --y)
                {
                    int w = (y >= stemBottom - 1) ? 2 : 1;
                    for (int dx = -w; dx <= w; ++dx)
                        oledPlot(oled, icx + dx, y, bright.withAlpha(0.7f));
                }

                // Mushroom cap — flat-bottomed dome that expands
                if (phase > 0.3f)
                {
                    float capPhase = juce::jmin((phase - 0.3f) * 0.8f, 1.0f);
                    int capW = 2 + static_cast<int>(capPhase * 6); // half-width
                    int capTop = juce::jmax(1, stemTop - 2 - static_cast<int>(capPhase * 2));
                    int capBot = stemTop;

                    // Draw cap as a filled rounded shape — row by row
                    for (int y = capTop; y <= capBot; ++y)
                    {
                        // Wider in the middle, narrower at top and bottom
                        float rowFrac = static_cast<float>(y - capTop) / juce::jmax(1.0f, static_cast<float>(capBot - capTop));
                        // Bulge: widest at ~60% down
                        float bulge = 1.0f - 1.5f * (rowFrac - 0.6f) * (rowFrac - 0.6f);
                        int rowW = static_cast<int>(capW * juce::jmax(0.3f, bulge));

                        for (int dx = -rowW; dx <= rowW; ++dx)
                        {
                            // Brighter in center, dimmer at edges
                            float edgeFade = 1.0f - std::abs(static_cast<float>(dx)) / (rowW + 1.0f);
                            oledPlot(oled, icx + dx, y, bright.withAlpha(0.4f + 0.4f * edgeFade));
                        }
                    }

                    // Billowing puffs on the cap edges — animated wobble
                    if (capPhase > 0.3f)
                    {
                        int capMidY = (capTop + capBot) / 2;
                        for (int i = 0; i < 4; ++i)
                        {
                            float wobble = std::sin(t * 3.0f + i * 1.5f) * 1.5f;
                            int side = (i % 2 == 0) ? 1 : -1;
                            int px = icx + side * (capW + static_cast<int>(wobble));
                            int py = capMidY + (i / 2) - 1;
                            oledPlot(oled, px, py, bright.withAlpha(0.35f));
                            oledPlot(oled, px + side, py, bright.withAlpha(0.2f));
                        }
                    }

                    // Top puff — small cloud on very top that pulses
                    if (capPhase > 0.5f)
                    {
                        float puff = 0.5f + 0.5f * std::sin(t * 4.0f);
                        oledPlot(oled, icx, capTop - 1, bright.withAlpha(0.5f * puff));
                        oledPlot(oled, icx - 1, capTop - 1, bright.withAlpha(0.3f * puff));
                        oledPlot(oled, icx + 1, capTop - 1, bright.withAlpha(0.3f * puff));
                    }
                }

                // Dust cloud at base — expanding
                if (phase > 0.1f)
                {
                    float dustW = juce::jmin((phase - 0.1f) * 4.0f, 10.0f);
                    for (int dx = static_cast<int>(-dustW); dx <= static_cast<int>(dustW); ++dx)
                    {
                        float fade = 1.0f - std::abs(static_cast<float>(dx)) / (dustW + 1.0f);
                        oledPlot(oled, icx + dx, OLED_H - 2, bright.withAlpha(0.2f * fade));
                    }
                }
            }
            else
            {
                // Pixelated "PANIC!" text — 3x5 pixel font
                // P A N I C !
                const uint8_t P[] = { 0b111, 0b101, 0b111, 0b100, 0b100 };
                const uint8_t A[] = { 0b010, 0b101, 0b111, 0b101, 0b101 };
                const uint8_t N[] = { 0b101, 0b111, 0b111, 0b101, 0b101 };
                const uint8_t I[] = { 0b111, 0b010, 0b010, 0b010, 0b111 };
                const uint8_t C[] = { 0b011, 0b100, 0b100, 0b100, 0b011 };
                const uint8_t EX[]= { 0b010, 0b010, 0b010, 0b000, 0b010 };
                const uint8_t* letters[] = { P, A, N, I, C, EX };

                int startX = icx - 13; // center 6 chars (each 3px wide + 1px gap = 4px * 6 - 1 = 23px, half = ~11)
                int startY = icy - 2;

                for (int ch = 0; ch < 6; ++ch)
                {
                    int ox = startX + ch * 4;
                    for (int row = 0; row < 5; ++row)
                        for (int bit = 0; bit < 3; ++bit)
                            if (letters[ch][row] & (1 << (2 - bit)))
                                oledPlot(oled, ox + bit, startY + row, dim);
                }
            }
        }
        else if (text == "TAP")
        {
            // Finger tap icon — hand pressing down
            // Fingertip
            oledPlot(oled, icx, icy - 2, col);
            oledPlot(oled, icx - 1, icy - 2, col);
            oledPlot(oled, icx + 1, icy - 2, col);
            oledPlot(oled, icx, icy - 3, col);
            // Finger body
            oledPlot(oled, icx, icy - 1, col);
            oledPlot(oled, icx, icy, col);
            oledPlot(oled, icx, icy + 1, col);
            if (on)
            {
                // Impact ripples expanding outward from tap point
                float ripple = std::fmod(t * 6.0f, 2.0f);
                int r1 = static_cast<int>(ripple * 3);
                int r2 = static_cast<int>(juce::jmax(0.0f, ripple - 0.5f) * 3);
                if (r1 > 0)
                    oledCircle(oled, icx, icy + 2, r1, bright.withAlpha(0.5f / r1));
                if (r2 > 0)
                    oledCircle(oled, icx, icy + 2, r2, bright.withAlpha(0.3f / r2));
            }
            else
            {
                // Surface line
                for (int dx = -4; dx <= 4; ++dx)
                    oledPlot(oled, icx + dx, icy + 2, dim.withAlpha(0.4f));
            }
        }
        else
        {
            return false;
        }

        return true;
    }

    // Render OLED image scaled up into the button area
    void drawOledImage(juce::Graphics& g, const juce::Image& oled, juce::Rectangle<float> dispBounds) const
    {
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        float oledAspect = static_cast<float>(OLED_W) / static_cast<float>(OLED_H);
        float btnAspect = dispBounds.getWidth() / dispBounds.getHeight();
        float drawW, drawH;
        if (btnAspect > oledAspect)
        {
            drawH = dispBounds.getHeight();
            drawW = drawH * oledAspect;
        }
        else
        {
            drawW = dispBounds.getWidth();
            drawH = drawW / oledAspect;
        }
        float drawX = dispBounds.getCentreX() - drawW * 0.5f;
        float drawY = dispBounds.getCentreY() - drawH * 0.5f;
        g.drawImage(oled,
                    static_cast<int>(drawX), static_cast<int>(drawY),
                    static_cast<int>(drawW), static_cast<int>(drawH),
                    0, 0, OLED_W, OLED_H);
    }

    // ── Default custom button drawing ──
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto baseColour = backgroundColour;

        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, getButtonRadius());

        if (!shouldDrawButtonAsDown)
        {
            g.setColour(baseColour.brighter(0.15f));
            g.drawLine(bounds.getX() + 3, bounds.getY() + 1,
                       bounds.getRight() - 3, bounds.getY() + 1, 1.0f);
        }

        g.setColour(juce::Colour(theme.border));
        g.drawRoundedRectangle(bounds, getButtonRadius(), 1.0f);
    }

    virtual float getButtonRadius() const { return 3.0f; }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool, bool) override
    {
        auto text = button.getButtonText();
        bool on = button.getToggleState();

        // Check if this button should get OLED pixel animation
        if (isOledAnimatedButton(text))
        {
            auto dispBounds = button.getLocalBounds().toFloat().reduced(2.0f);
            float t = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.001);
            juce::Colour bright(theme.amber);
            juce::Colour dim = bright.withAlpha(0.35f);

            juce::Image oled(juce::Image::ARGB, OLED_W, OLED_H, true);

            if (drawOledButtonArt(oled, text, on, t, bright, dim))
            {
                drawOledImage(g, oled, dispBounds);
                return;
            }
        }

        // Default: text rendering
        auto font = juce::Font(getUIFontName(),
                               juce::jmin(14.0f, button.getHeight() * 0.55f),
                               juce::Font::bold);
        g.setFont(font);

        auto textColour = button.findColour(button.getToggleState()
            ? juce::TextButton::textColourOnId
            : juce::TextButton::textColourOffId);
        g.setColour(textColour);

        g.drawText(formatButtonText(button.getButtonText()),
                   button.getLocalBounds().reduced(4, 2),
                   juce::Justification::centred);
    }

    virtual juce::String formatButtonText(const juce::String& text) const
    {
        return text.toUpperCase();
    }

    // ── Rotary slider ──
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override
    {
        auto radius = static_cast<float>(juce::jmin(width, height)) / 2.0f - 4.0f;
        auto centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(juce::Colour(theme.bodyDark));
        g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

        juce::Path track;
        track.addArc(centreX - radius + 2, centreY - radius + 2,
                     (radius - 2) * 2.0f, (radius - 2) * 2.0f,
                     rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(theme.border));
        g.strokePath(track, juce::PathStrokeType(3.0f));

        juce::Path valueArc;
        valueArc.addArc(centreX - radius + 2, centreY - radius + 2,
                        (radius - 2) * 2.0f, (radius - 2) * 2.0f,
                        rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(theme.amber));
        g.strokePath(valueArc, juce::PathStrokeType(3.0f));

        juce::Path pointer;
        auto pointerLength = radius * 0.6f;
        pointer.addRectangle(-1.5f, -pointerLength, 3.0f, pointerLength);
        pointer.applyTransform(juce::AffineTransform::rotation(angle)
                               .translated(centreX, centreY));
        g.setColour(juce::Colour(theme.textBright));
        g.fillPath(pointer);

        g.setColour(juce::Colour(theme.buttonFace));
        g.fillEllipse(centreX - 4, centreY - 4, 8.0f, 8.0f);
    }

    // ── Linear slider ──
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider&) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            auto trackWidth = 6.0f;
            auto trackX = static_cast<float>(x) + static_cast<float>(width) * 0.5f - trackWidth * 0.5f;
            g.setColour(juce::Colour(theme.bodyDark));
            g.fillRoundedRectangle(trackX, static_cast<float>(y), trackWidth, static_cast<float>(height), 3.0f);

            g.setColour(juce::Colour(theme.amber));
            auto fillHeight = static_cast<float>(height) - (sliderPos - static_cast<float>(y));
            g.fillRoundedRectangle(trackX, sliderPos, trackWidth, fillHeight, 3.0f);

            g.setColour(juce::Colour(theme.textBright));
            g.fillRoundedRectangle(trackX - 4, sliderPos - 6, trackWidth + 8, 12.0f, 3.0f);
            g.setColour(juce::Colour(theme.border));
            g.drawRoundedRectangle(trackX - 4, sliderPos - 6, trackWidth + 8, 12.0f, 3.0f, 1.0f);
        }
        else
        {
            auto trackHeight = 6.0f;
            auto trackY = static_cast<float>(y) + static_cast<float>(height) * 0.5f - trackHeight * 0.5f;
            g.setColour(juce::Colour(theme.bodyDark));
            g.fillRoundedRectangle(static_cast<float>(x), trackY, static_cast<float>(width), trackHeight, 3.0f);

            g.setColour(juce::Colour(theme.amber));
            g.fillRoundedRectangle(static_cast<float>(x), trackY, sliderPos - static_cast<float>(x), trackHeight, 3.0f);

            g.setColour(juce::Colour(theme.textBright));
            g.fillRoundedRectangle(sliderPos - 6, trackY - 4, 12.0f, trackHeight + 8, 3.0f);
            g.setColour(juce::Colour(theme.border));
            g.drawRoundedRectangle(sliderPos - 6, trackY - 4, 12.0f, trackHeight + 8, 3.0f, 1.0f);
        }
    }

    // ── Combo box ──
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));

        g.setColour(juce::Colour(theme.bodyDark));
        g.fillRoundedRectangle(bounds, getButtonRadius());
        g.setColour(juce::Colour(theme.border));
        g.drawRoundedRectangle(bounds.reduced(0.5f), getButtonRadius(), 1.0f);

        auto arrowZone = bounds.removeFromRight(20.0f).reduced(5.0f);
        juce::Path arrow;
        arrow.addTriangle(arrowZone.getX(), arrowZone.getY(),
                          arrowZone.getRight(), arrowZone.getY(),
                          arrowZone.getCentreX(), arrowZone.getBottom());
        g.setColour(juce::Colour(theme.amber));
        g.fillPath(arrow);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DawLookAndFeel)
};
