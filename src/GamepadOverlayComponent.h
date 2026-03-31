#pragma once

#include <JuceHeader.h>
#include "GamepadHandler.h"

// Transparent HUD overlay showing left controller mode + right controller vis context.
class GamepadOverlayComponent : public juce::Component, public juce::Timer
{
public:
    GamepadOverlayComponent() { setInterceptsMouseClicks(false, false); }

    void setMode(GamepadHandler::Mode mode)
    {
        if (mode != currentMode)
        {
            currentMode = mode;
            showOverlay();
        }
    }

    void setConnected(bool conn)
    {
        if (conn != connected) { connected = conn; showOverlay(); }
    }

    void setVisMode(int mode)
    {
        if (mode != visMode) { visMode = mode; showOverlay(); }
    }

    void timerCallback() override
    {
        double elapsed = (juce::Time::getMillisecondCounterHiRes() - fadeStartTime) * 0.001;
        if (elapsed > 4.0)
        {
            opacity = juce::jmax(0.0f, opacity - 0.04f);
            if (opacity <= 0.0f) stopTimer();
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (opacity <= 0.0f) return;

        // ── Left panel: DAW mode ──
        {
            auto box = getLocalBounds().reduced(8).removeFromBottom(160).removeFromLeft(210).toFloat();
            drawPanel(g, box);
            auto inner = box.toNearestIntEdges().reduced(10, 8);

            juce::String modeName;
            juce::Colour modeCol;
            switch (currentMode)
            {
                case GamepadHandler::Mode::Navigate:
                    modeName = "NAV"; modeCol = juce::Colour(0xffb8d8f0); break;
                case GamepadHandler::Mode::Play:
                    modeName = "PLAY"; modeCol = juce::Colour(0xff80e080); break;
                case GamepadHandler::Mode::Edit:
                    modeName = "EDIT"; modeCol = juce::Colour(0xffe0c060); break;
            }

            g.setColour(modeCol.withAlpha(opacity));
            g.setFont(juce::Font("Consolas", 16.0f, juce::Font::bold));
            auto titleRow = inner.removeFromTop(20);
            g.drawText("L: " + modeName, titleRow, juce::Justification::centredLeft);

            auto dotArea = titleRow.removeFromRight(20).toFloat().reduced(4.0f, 2.0f);
            g.setColour((connected ? juce::Colour(0xff60c060) : juce::Colour(0xffc06060)).withAlpha(opacity));
            g.fillEllipse(dotArea);

            inner.removeFromTop(3);
            g.setFont(juce::Font("Consolas", 10.0f, juce::Font::plain));
            g.setColour(juce::Colour(0xffa0a0a0).withAlpha(opacity));

            juce::StringArray lines;
            switch (currentMode)
            {
                case GamepadHandler::Mode::Navigate:
                    lines = { "D-Pad: Track/Clip",
                              "L-Stick: Scroll",
                              "LT: Play  LS: Stop",
                              "LB: Record",
                              "Back: Undo",
                              "Back+LB: Mode" };
                    break;
                case GamepadHandler::Mode::Play:
                    lines = { "D-Pad: Scale notes",
                              "L-Stick: Mod/Bend",
                              "LT: Trigger root",
                              "LB: Oct-  LS: Oct+",
                              "Back: Stop",
                              "Back+LB: Mode" };
                    break;
                case GamepadHandler::Mode::Edit:
                    lines = { "D-Pad: Move cursor",
                              "L-Stick: Transpose/Len",
                              "LT: Place note",
                              "LB: Delete note",
                              "LS: Quantize",
                              "Back+LB: Mode" };
                    break;
            }
            for (auto& line : lines)
                g.drawText(line, inner.removeFromTop(15), juce::Justification::centredLeft);
        }

        // ── Right panel: Visuals ──
        {
            auto box = getLocalBounds().reduced(8).removeFromBottom(160).removeFromRight(210).toFloat();
            drawPanel(g, box);
            auto inner = box.toNearestIntEdges().reduced(10, 8);

            juce::String visName;
            switch (visMode)
            {
                case 0: visName = "SPECTRUM"; break;
                case 1: visName = "LISSAJOUS"; break;
                case 2: visName = "G-FORCE"; break;
                case 3: visName = "GEISS"; break;
                case 4: visName = "PROJECTM"; break;
                default: visName = "VIS"; break;
            }

            g.setColour(juce::Colour(0xffb080e0).withAlpha(opacity));
            g.setFont(juce::Font("Consolas", 16.0f, juce::Font::bold));
            g.drawText("R: " + visName, inner.removeFromTop(20), juce::Justification::centredLeft);

            inner.removeFromTop(3);
            g.setFont(juce::Font("Consolas", 10.0f, juce::Font::plain));
            g.setColour(juce::Colour(0xffa0a0a0).withAlpha(opacity));

            juce::StringArray lines;
            switch (visMode)
            {
                case 0: // Spectrum
                    lines = { "A: Cycle decay", "X: Sens+  B: Sens-",
                              "Start: Next vis", "RS: Fullscreen" };
                    break;
                case 1: // Lissajous
                    lines = { "A: Cycle dots", "X: Zoom+  B: Zoom-",
                              "Start: Next vis", "RS: Fullscreen" };
                    break;
                case 2: // G-Force
                    lines = { "A: Cycle trail", "X: Ribbons+  B: Ribbons-",
                              "Start: Next vis", "RS: Fullscreen" };
                    break;
                case 3: // Geiss
                    lines = { "A: Wave  B: Palette",
                              "X: Scene  Y: AutoPilot",
                              "R-Stick: Wave scale",
                              "RB: Warp lock",
                              "Start: Next vis", "RS: Fullscreen" };
                    break;
                case 4: // ProjectM
                    lines = { "A: Next  B: Prev",
                              "X: Random  Y: Lock",
                              "Start: Next vis", "RS: Fullscreen" };
                    break;
            }
            for (auto& line : lines)
                g.drawText(line, inner.removeFromTop(15), juce::Justification::centredLeft);
        }
    }

private:
    GamepadHandler::Mode currentMode = GamepadHandler::Mode::Navigate;
    bool connected = false;
    int visMode = 0;
    float opacity = 0.0f;
    double fadeStartTime = 0.0;

    void showOverlay()
    {
        opacity = 1.0f;
        fadeStartTime = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(30);
        repaint();
    }

    void drawPanel(juce::Graphics& g, juce::Rectangle<float> box)
    {
        g.setColour(juce::Colour(0xff000000).withAlpha(opacity * 0.8f));
        g.fillRoundedRectangle(box, 8.0f);
        g.setColour(juce::Colour(0xff3a3530).withAlpha(opacity * 0.6f));
        g.drawRoundedRectangle(box, 8.0f, 1.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GamepadOverlayComponent)
};
