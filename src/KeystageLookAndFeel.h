#pragma once

#include "DawLookAndFeel.h"

// "Keystage" theme — inspired by the Korg Keystage MIDI controller.
// Matte black body, warm oak wood accents, crisp white OLED text,
// minimal and clean. Feels like a natural extension of the hardware.
class KeystageLookAndFeel : public DawLookAndFeel
{
public:
    KeystageLookAndFeel()
    {
        // ── Surfaces — matte black with warm oak undertone ──
        theme.body        = 0xff12100e;  // matte black with warm tint
        theme.bodyLight   = 0xff1e1a16;  // warm dark oak
        theme.bodyDark    = 0xff0a0908;  // near-black
        theme.border      = 0xff3a3530;  // cool oak border
        theme.borderLight = 0xff4a4440;  // lighter cool border

        // ── Text — crisp OLED white ──
        theme.textPrimary   = 0xfff0ece6;  // warm white
        theme.textSecondary = 0xff8a7e6e;  // oak-tinted grey
        theme.textBright    = 0xffffffff;  // pure white (OLED bright)

        // ── Accent — ice blue OLED only (no colored buttons) ──
        theme.red       = 0xffb8d8f0;  // ice blue replaces red
        theme.redDark   = 0xff1a1714;  // dark matte
        theme.amber     = 0xffd6cbb8;  // pale white oak for non-button accents
        theme.amberDark = 0xff9e9484;
        theme.green     = 0xffb8d8f0;  // ice blue replaces green
        theme.greenDark = 0xff1a1714;  // dark matte

        // ── LCD — OLED style: black bg, ice blue text ──
        theme.lcdBg    = 0xff000000;
        theme.lcdText  = 0xffb8d8f0;  // ice blue OLED
        theme.lcdAmber = 0xffb8d8f0;  // ice blue for values

        // ── Buttons — uniform dark matte, ice blue when active ──
        theme.buttonFace  = 0xff1a1714;
        theme.buttonHover = 0xff222018;
        theme.buttonDown  = 0xff0e0d0c;

        theme.btnStop       = 0xff1a1714;
        theme.btnMetronome  = 0xff1a1714;
        theme.btnMetronomeOn = 0xff162838;  // ice blue tint when on
        theme.btnCountIn    = 0xff1a1714;
        theme.btnCountInOn  = 0xff162838;
        theme.btnNewClip    = 0xff1a1714;
        theme.btnDeleteClip = 0xff1a1714;
        theme.btnDuplicate  = 0xff1a1714;
        theme.btnSplit      = 0xff1a1714;
        theme.btnQuantize   = 0xff1a1714;
        theme.btnEditNotes  = 0xff1a1714;
        theme.btnNav        = 0xff1a1714;
        theme.btnSave       = 0xff1a1714;
        theme.btnLoad       = 0xff1a1714;
        theme.btnUndoRedo   = 0xff1a1714;
        theme.btnMidi2      = 0xff1a1714;
        theme.btnMidi2On    = 0xff162838;
        theme.btnLoop       = 0xff1a1714;
        theme.btnLoopOn     = 0xff162838;
        theme.loopRegion    = 0x28d6cbb8;  // white oak translucent
        theme.loopBorder    = 0xffb0a898;  // pale oak border

        // ── Timeline — black with warm oak grid ──
        theme.timelineBg         = 0xff0c0b0a;
        theme.timelineAltRow     = 0xff141210;
        theme.timelineSelectedRow = 0xff241e18;  // warm oak selection
        theme.timelineGridMajor  = 0xff7a7468;  // cool oak grid lines
        theme.timelineGridMinor  = 0xff3a3228;
        theme.timelineGridFaint  = 0xff1e1a14;
        theme.timelineGridBeat   = 0xff302a22;

        // ── Clips — dark with warm oak fills ──
        theme.clipDefault   = 0xff282018;  // warm oak clip
        theme.clipRecording = 0xff3a2020;
        theme.clipQueued    = 0xff2e2818;
        theme.clipPlaying   = 0xff1e2e1a;

        // ── Playhead — bright white OLED style ──
        theme.playhead     = 0xddffffff;
        theme.playheadGlow = 0x22ffffff;

        theme.accentStripe = 0xffd6cbb8;  // white oak stripe

        theme.trackSelected = 0xff241e18;
        theme.trackArmed    = 0xff162838;  // ice blue tint
        theme.trackMuteOn   = 0xffb8d8f0;  // ice blue
        theme.trackSoloOn   = 0xffb8d8f0;  // ice blue
        theme.trackSoloText = 0xff0a0908;

        applyThemeColors();

        // Ice blue OLED text in combo boxes and popups
        setColour(juce::ComboBox::textColourId,                   juce::Colour(0xffb8d8f0));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(0xffb8d8f0));
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(0xff000000));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xffb8d8f0));

        // Slider text boxes — OLED style: black bg, ice blue text, no outline
        setColour(juce::Slider::textBoxTextColourId,       juce::Colour(0xffb8d8f0));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff000000));
        setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0x00000000));

        // Button on-state text stays ice blue (not dark background)
        setColour(juce::TextButton::buttonOnColourId,      juce::Colour(0xff162838));
        setColour(juce::TextButton::textColourOnId,        juce::Colour(0xffb8d8f0));
    }

    float getButtonRadius() const override { return 4.0f; }

    // White oak side panels — like the Korg Keystage wood cheeks
    int getSidePanelWidth() const override { return 18; }

    void drawSidePanels(juce::Graphics& g, int width, int height) override
    {
        if (width != sideCacheKey || height != sideCacheH)
        {
            int panelW = getSidePanelWidth();
            sideCache = juce::Image(juce::Image::ARGB, width, height, true);
            juce::Graphics sg(sideCache);

            juce::Colour oakBase(0xffc8bda8);
            juce::Colour oakLight(0xffd6ccba);
            juce::Colour oakGrain(0xffa89880);

            sg.setColour(oakBase);
            sg.fillRect(0, 0, panelW, height);
            sg.fillRect(width - panelW, 0, panelW, height);

            juce::Random rng(42);
            for (int i = 0; i < 40; ++i)
            {
                float x = rng.nextFloat() * panelW;
                float grainW = 0.5f + rng.nextFloat() * 1.0f;
                float alpha = 0.08f + rng.nextFloat() * 0.15f;
                sg.setColour((rng.nextBool() ? oakLight : oakGrain).withAlpha(alpha));

                juce::Path grain;
                grain.startNewSubPath(x, 0);
                for (int y = 0; y < height; y += 20)
                    grain.lineTo(x + std::sin(static_cast<float>(y) * 0.015f + i * 0.7f) * 1.5f,
                                 static_cast<float>(y));
                grain.lineTo(x, static_cast<float>(height));
                sg.strokePath(grain, juce::PathStrokeType(grainW));

                auto rg = grain;
                rg.applyTransform(juce::AffineTransform::translation(static_cast<float>(width - panelW), 0));
                sg.strokePath(rg, juce::PathStrokeType(grainW));
            }

            sg.setColour(juce::Colour(0x30000000));
            sg.fillRect(static_cast<float>(panelW - 2), 0.0f, 2.0f, static_cast<float>(height));
            sg.fillRect(static_cast<float>(width - panelW), 0.0f, 2.0f, static_cast<float>(height));
            sg.setColour(oakLight.withAlpha(0.3f));
            sg.fillRect(0.0f, 0.0f, 1.0f, static_cast<float>(height));
            sg.fillRect(static_cast<float>(width - 1), 0.0f, 1.0f, static_cast<float>(height));

            sideCacheKey = width;
            sideCacheH = height;
        }
        g.drawImageAt(sideCache, 0, 0);
    }

    mutable juce::Image sideCache;
    mutable int sideCacheKey = 0, sideCacheH = 0;

    // Wood grain top bar — cached
    void drawTopBarBackground(juce::Graphics& g, int x, int y, int width, int height) override
    {
        if (width != topBarCacheW || height != topBarCacheH)
        {
            topBarCache = juce::Image(juce::Image::RGB, width, height, false);
            juce::Graphics wg(topBarCache);

            juce::Colour oakBase(0xffc0b49e);
            juce::Colour oakLight(0xffcec2ae);
            juce::Colour oakGrain(0xffa89882);

            wg.setColour(oakBase);
            wg.fillAll();

            juce::Random rng(77);
            for (int i = 0; i < 50; ++i)
            {
                float gy = rng.nextFloat() * height;
                float grainH = 0.5f + rng.nextFloat() * 1.2f;
                float alpha = 0.06f + rng.nextFloat() * 0.12f;
                bool isLight = rng.nextBool();

                wg.setColour((isLight ? oakLight : oakGrain).withAlpha(alpha));

                juce::Path grain;
                grain.startNewSubPath(0, gy);
                for (int gx = 0; gx < width; gx += 15)
                {
                    float wobble = std::sin(static_cast<float>(gx) * 0.01f + i * 0.9f) * 1.2f;
                    grain.lineTo(static_cast<float>(gx), gy + wobble);
                }
                grain.lineTo(static_cast<float>(width), gy);
                wg.strokePath(grain, juce::PathStrokeType(grainH));
            }

            wg.setColour(juce::Colour(0x25000000));
            wg.fillRect(0, height - 2, width, 2);

            topBarCacheW = width;
            topBarCacheH = height;
        }
        g.drawImageAt(topBarCache, x, y);
    }

    mutable juce::Image topBarCache;
    mutable int topBarCacheW = 0, topBarCacheH = 0;

    static constexpr int OLED_W = 32;
    static constexpr int OLED_H = 12;

    // Keystage: OLED screen buttons with pixelated animated icons
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

        g.setColour(juce::Colour(0xff000000));
        g.fillRoundedRectangle(bounds, 3.0f);

        if (shouldDrawButtonAsDown)
            g.setColour(juce::Colour(0xffb8d8f0).withAlpha(0.5f));
        else if (shouldDrawButtonAsHighlighted)
            g.setColour(juce::Colour(0xff4a6878));
        else
            g.setColour(juce::Colour(0xff2a3530));
        g.drawRoundedRectangle(bounds, 3.0f, 0.8f);
    }

    // Keystage overrides the base OLED art with ice-blue colors and custom REC
    bool drawOledButtonArt(juce::Image& oled, const juce::String& text,
                           bool on, float t, juce::Colour, juce::Colour) const override
    {
        juce::Colour ice(0xffb8d8f0);
        juce::Colour dim(0xff3a5060);
        int icx = OLED_W / 2;
        int icy = OLED_H / 2;

        // REC: Keystage-specific pulsing circle with expanding rings
        if (text == "REC")
        {
            juce::Colour col = on ? ice : dim;
            float pulse = on ? (0.5f + 0.5f * std::sin(t * 8.0f)) : 1.0f;
            oledCircle(oled, icx, icy, 3, col.withAlpha(pulse), true);
            if (on)
            {
                int ringR = 4 + (static_cast<int>(t * 6.0f) % 3);
                oledCircle(oled, icx, icy, ringR, ice.withAlpha(0.3f));
            }
            return true;
        }

        // For all other animated buttons, delegate to base class with ice-blue colors
        return DawLookAndFeel::drawOledButtonArt(oled, text, on, t, ice, dim);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool, bool) override
    {
        auto text = button.getButtonText();
        bool on = button.getToggleState();

        juce::Colour ice(0xffb8d8f0);
        juce::Colour dim(0xff3a5060);

        // Use OLED animation for animated buttons
        if (isOledAnimatedButton(text) || text == "REC" || text == "PANIC")
        {
            auto dispBounds = button.getLocalBounds().toFloat().reduced(2.0f);
            float t = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.001);

            juce::Image oled(juce::Image::ARGB, OLED_W, OLED_H, true);
            if (drawOledButtonArt(oled, text, on, t, ice, dim))
            {
                drawOledImage(g, oled, dispBounds);
                return;
            }
        }

        // Default: ice blue text
        auto dispBounds = button.getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(on ? ice : ice.withAlpha(0.7f));
        g.setFont(juce::Font(getUIFontName(),
                   juce::jmin(12.0f, dispBounds.getHeight() * 0.5f), juce::Font::bold));
        g.drawText(formatButtonText(text), button.getLocalBounds().reduced(2),
                   juce::Justification::centred);
    }

    // Custom combo box with OLED-style display
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));

        g.setColour(juce::Colour(0xff000000));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(juce::Colour(0xff2a3530));
        g.drawRoundedRectangle(bounds, 3.0f, 0.8f);

        auto arrowZone = bounds.removeFromRight(20.0f).reduced(5.0f);
        juce::Path arrow;
        arrow.addTriangle(arrowZone.getX(), arrowZone.getCentreY() - 3,
                         arrowZone.getRight(), arrowZone.getCentreY() - 3,
                         arrowZone.getCentreX(), arrowZone.getCentreY() + 3);
        g.setColour(juce::Colour(0xffb8d8f0));
        g.fillPath(arrow);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeystageLookAndFeel)
};
