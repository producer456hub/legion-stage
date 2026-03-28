#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

// OLED-style pixel art splash screen.
// Phase 1: Pixels scatter then morph into "LEGION STAGE"
// Phase 2: Hold with glow + subtitle
// Phase 3: Scrolling source code boot sequence
class SplashComponent : public juce::Component, public juce::Timer
{
public:
    std::function<void()> onFinished;

    SplashComponent()
    {
        setOpaque(true);
        buildTextPixels();
        buildBootLines();
        startTimerHz(60);
    }

    ~SplashComponent() override { stopTimer(); }

    void timerCallback() override
    {
        elapsed += 1.0f / 60.0f;

        // Scroll boot text during phase 3
        if (elapsed > bootStartTime)
        {
            float bootElapsed = elapsed - bootStartTime;
            // Add new lines over time
            float linesPerSec = 8.0f + bootElapsed * 4.0f; // accelerating
            int targetLines = juce::jmin(static_cast<int>(bootElapsed * linesPerSec),
                                         static_cast<int>(bootLines.size()));
            if (visibleBootLines < targetLines)
                visibleBootLines = targetLines;
        }

        repaint();

        // Once boot text has fully scrolled, signal we're done
        if (visibleBootLines >= bootLines.size() && onFinished)
        {
            stopTimer();
            onFinished();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);

        int w = getWidth();
        int h = getHeight();
        if (w < 10 || h < 10) return;

        juce::Colour ice(0xffb8d8f0);

        // Phase timing
        float assembleEnd = 1.5f;
        float holdEnd = 2.5f;

        // ── Phase 1 & 2: Logo animation ──
        if (elapsed < holdEnd + 0.5f)
        {
            drawLogoPhase(g, w, h, ice);
        }

        // ── Phase 3: Boot sequence (starts fading in during hold) ──
        if (elapsed > bootStartTime)
        {
            float bootAlpha = juce::jlimit(0.0f, 1.0f, (elapsed - bootStartTime) / 0.5f);

            // Fade logo out as boot text takes over
            float logoFade = 1.0f;
            if (elapsed > holdEnd)
                logoFade = juce::jmax(0.0f, 1.0f - (elapsed - holdEnd) / 0.8f);

            drawBootSequence(g, w, h, ice, bootAlpha);
        }

        // No fade-out — code stays visible until main UI launches
    }

private:
    float elapsed = 0.0f;
    static constexpr float bootStartTime = 2.2f;
    static constexpr float totalDuration = 5.5f;

    // ── Logo pixels ──
    struct Pixel {
        int row, col;
        float startX, startY, phase;
    };
    std::vector<Pixel> textPixels;
    int maxRow = 0, maxCol = 0;

    // ── Boot sequence lines ──
    juce::StringArray bootLines;
    int visibleBootLines = 0;

    void drawLogoPhase(juce::Graphics& g, int w, int h, juce::Colour ice)
    {
        if (textPixels.empty()) return;

        float textW = static_cast<float>(maxCol + 1);
        float textH = static_cast<float>(maxRow + 1);
        float pixScale = juce::jmin(
            static_cast<float>(w) * 0.7f / textW,
            static_cast<float>(h) * 0.3f / textH);
        pixScale = juce::jmax(2.0f, std::floor(pixScale));

        float textDrawW = textW * pixScale;
        float textDrawH = textH * pixScale;
        float offsetX = (w - textDrawW) * 0.5f;
        float offsetY = (h * 0.35f - textDrawH * 0.5f);

        float assembleT = juce::jlimit(0.0f, 1.0f, elapsed / 1.5f);
        float eased = 1.0f - (1.0f - assembleT) * (1.0f - assembleT) * (1.0f - assembleT);

        float glowPulse = 0.0f;
        if (elapsed > 1.5f && elapsed < 2.5f)
            glowPulse = 0.3f + 0.2f * std::sin((elapsed - 1.5f) * 6.0f);

        // Fade logo during boot sequence
        float logoAlpha = 1.0f;
        if (elapsed > 2.5f)
            logoAlpha = juce::jmax(0.0f, 1.0f - (elapsed - 2.5f) / 1.0f);
        if (logoAlpha <= 0.0f) return;

        for (size_t i = 0; i < textPixels.size(); ++i)
        {
            auto& px = textPixels[i];
            float fx = px.startX + (px.col * pixScale + offsetX - px.startX) * eased;
            float fy = px.startY + (px.row * pixScale + offsetY - px.startY) * eased;

            if (assembleT < 1.0f)
            {
                float wobble = (1.0f - eased) * 3.0f;
                fx += std::sin(elapsed * 8.0f + px.phase) * wobble;
                fy += std::cos(elapsed * 7.0f + px.phase * 1.3f) * wobble;
            }

            float brightness = eased * 0.6f + 0.4f;
            if (assembleT < 0.3f)
                brightness = 0.3f + 0.7f * std::sin(elapsed * 12.0f + px.phase);

            float alpha = brightness * logoAlpha;

            if (glowPulse > 0.0f)
            {
                g.setColour(ice.withAlpha(glowPulse * logoAlpha * 0.3f));
                g.fillRect(fx - pixScale * 0.3f, fy - pixScale * 0.3f,
                           pixScale * 1.6f, pixScale * 1.6f);
            }

            g.setColour(ice.withAlpha(alpha));
            g.fillRect(fx, fy, pixScale, pixScale);
        }

        // Trail particles during scatter
        if (assembleT < 1.0f)
        {
            juce::Random rng(static_cast<int>(elapsed * 100));
            int numTrails = static_cast<int>((1.0f - eased) * 40);
            for (int i = 0; i < numTrails; ++i)
            {
                float tx = rng.nextFloat() * w;
                float ty = rng.nextFloat() * h;
                g.setColour(ice.withAlpha((1.0f - eased) * 0.15f));
                g.fillRect(tx, ty, pixScale * 0.5f, pixScale * 0.5f);
            }
        }

        // Subtitle
        if (elapsed > 1.8f && logoAlpha > 0.0f)
        {
            float subAlpha = juce::jmin(1.0f, (elapsed - 1.8f) / 0.5f) * logoAlpha;
            g.setColour(ice.withAlpha(subAlpha * 0.5f));
            g.setFont(juce::Font("Consolas", 11.0f, juce::Font::plain));
            g.drawText("T O U C H   D A W", 0, static_cast<int>(offsetY + textDrawH + pixScale * 3),
                       w, 20, juce::Justification::centred);
        }
    }

    void drawBootSequence(juce::Graphics& g, int w, int h, juce::Colour ice, float alpha)
    {
        if (visibleBootLines <= 0) return;

        float fontSize = 10.0f;
        float lineH = 14.0f;
        int maxVisible = static_cast<int>(h / lineH);
        int startLine = juce::jmax(0, visibleBootLines - maxVisible);

        g.setFont(juce::Font("Consolas", fontSize, juce::Font::plain));

        for (int i = startLine; i < visibleBootLines && i < bootLines.size(); ++i)
        {
            int screenLine = i - startLine;
            float y = static_cast<float>(screenLine) * lineH + 4.0f;

            // Newest lines are brighter
            float lineAge = static_cast<float>(visibleBootLines - i) / static_cast<float>(maxVisible);
            float lineAlpha = (1.0f - lineAge * 0.7f) * alpha;

            // All OLED ice blue — vary brightness for hierarchy
            auto& line = bootLines[i];
            if (line.startsWith("//") || line.startsWith("  //"))
            {
                g.setColour(ice.withAlpha(lineAlpha * 0.3f));
            }
            else if (line.startsWith(">>") || line.contains("OK") || line.contains("Ready"))
            {
                g.setColour(ice.withAlpha(lineAlpha));
            }
            else if (line.startsWith("#") || line.contains("class ") || line.contains("void "))
            {
                g.setColour(ice.withAlpha(lineAlpha * 0.8f));
            }
            else
            {
                g.setColour(ice.withAlpha(lineAlpha * 0.55f));
            }

            g.drawText(line, 12, static_cast<int>(y), w - 24, static_cast<int>(lineH),
                       juce::Justification::centredLeft);
        }

        // Blinking cursor at bottom
        if (static_cast<int>(elapsed * 3.0f) % 2 == 0)
        {
            int cursorLine = juce::jmin(visibleBootLines - startLine, maxVisible);
            float cursorY = static_cast<float>(cursorLine) * lineH + 6.0f;
            g.setColour(ice.withAlpha(alpha * 0.8f));
            g.fillRect(12.0f, cursorY, 7.0f, fontSize);
        }
    }

    void buildBootLines()
    {
        bootLines.add("#include <JuceHeader.h>");
        bootLines.add("#include \"MainComponent.h\"");
        bootLines.add("#include \"PluginHost.h\"");
        bootLines.add("#include \"SequencerEngine.h\"");
        bootLines.add("");
        bootLines.add("// Legion Stage v1.1.0");
        bootLines.add("// Touch DAW for Legion Go");
        bootLines.add("");
        bootLines.add(">> Initializing audio subsystem...");
        bootLines.add("  deviceManager.initialiseWithDefaultDevices(0, 2)");
        bootLines.add("  sampleRate: 44100 Hz");
        bootLines.add("  bufferSize: 512 samples");
        bootLines.add("  >> WASAPI output OK");
        bootLines.add("");
        bootLines.add(">> Building audio processor graph...");
        bootLines.add("  addNode(midiInputNode)");
        bootLines.add("  addNode(audioOutputNode)");
        bootLines.add("  for (int t = 0; t < 16; ++t)");
        bootLines.add("  {");
        bootLines.add("    tracks[t].clipPlayer = new ClipPlayerNode(engine);");
        bootLines.add("    tracks[t].gainProcessor = new GainProcessor();");
        bootLines.add("    connectTrackAudio(t);");
        bootLines.add("  }");
        bootLines.add("  >> 16 tracks initialized");
        bootLines.add("");
        bootLines.add(">> Scanning VST3 plugins...");
        bootLines.add("  formatManager.addFormat(new VST3PluginFormat())");
        bootLines.add("  // Searching: C:/Program Files/Common Files/VST3/");
        bootLines.add("  >> Plugin scan complete");
        bootLines.add("");
        bootLines.add(">> Initializing MIDI subsystem...");
        bootLines.add("  midiCollector.reset(44100.0)");
        bootLines.add("  // Scanning MIDI devices...");
        bootLines.add("  >> MIDI ready");
        bootLines.add("");
        bootLines.add(">> Loading theme: Keystage");
        bootLines.add("  theme.body = 0xff12100e  // matte black");
        bootLines.add("  theme.amber = 0xffd6cbb8  // white oak");
        bootLines.add("  theme.lcdText = 0xffb8d8f0  // ice blue OLED");
        bootLines.add("  applyThemeColors()");
        bootLines.add("  >> Caching wood grain textures...");
        bootLines.add("  >> Side panels rendered");
        bootLines.add("  >> Top bar rendered");
        bootLines.add("");
        bootLines.add(">> Initializing visualizers...");
        bootLines.add("  addAndMakeVisible(spectrumDisplay)");
        bootLines.add("  addAndMakeVisible(lissajousDisplay)");
        bootLines.add("  addAndMakeVisible(gforceDisplay)");
        bootLines.add("  addAndMakeVisible(geissDisplay)");
        bootLines.add("  addAndMakeVisible(projectMDisplay)");
        bootLines.add("  >> 5 visualizers ready");
        bootLines.add("");
        bootLines.add(">> Building UI components...");
        bootLines.add("  // Transport: REC PLAY STOP MET LOOP PANIC");
        bootLines.add("  // Mixer: 16 OLED channel strips");
        bootLines.add("  // Timeline: arrangement view");
        bootLines.add("  // TouchPiano: on-screen keyboard");
        bootLines.add("  >> MIDI Learn system initialized");
        bootLines.add("  >> Projector mode available");
        bootLines.add("");
        bootLines.add(">> SequencerEngine ready");
        bootLines.add("  bpm: 120.0");
        bootLines.add("  metronome: OFF");
        bootLines.add("  loop: OFF");
        bootLines.add("");
        bootLines.add("class MainComponent : public juce::Component");
        bootLines.add("{");
        bootLines.add("  // All systems nominal");
        bootLines.add("  startTimerHz(15);");
        bootLines.add("};");
        bootLines.add("");
        bootLines.add(">> Legion Stage loaded successfully");
        bootLines.add(">> Ready.");
    }

    void buildTextPixels()
    {
        struct Glyph { char ch; const char* rows[5]; };
        Glyph glyphs[] = {
            { 'L', { "10000", "10000", "10000", "10000", "11111" } },
            { 'E', { "11111", "10000", "11110", "10000", "11111" } },
            { 'G', { "01110", "10000", "10011", "10001", "01110" } },
            { 'I', { "11111", "00100", "00100", "00100", "11111" } },
            { 'O', { "01110", "10001", "10001", "10001", "01110" } },
            { 'N', { "10001", "11001", "10101", "10011", "10001" } },
            { 'S', { "01111", "10000", "01110", "00001", "11110" } },
            { 'T', { "11111", "00100", "00100", "00100", "00100" } },
            { 'A', { "01110", "10001", "11111", "10001", "10001" } },
        };
        int numGlyphs = 9;

        juce::Random rng(42);

        auto addLine = [&](const char* text, int row0)
        {
            int len = static_cast<int>(std::strlen(text));
            int maxLineW = 6 * 6 - 1;
            int totalW = len * 6 - 1;
            int startCol = (maxLineW - totalW) / 2;

            for (int c = 0; c < len; ++c)
            {
                const char* rows[5] = {};
                for (int gi = 0; gi < numGlyphs; ++gi)
                    if (glyphs[gi].ch == text[c])
                        for (int r = 0; r < 5; ++r)
                            rows[r] = glyphs[gi].rows[r];

                for (int r = 0; r < 5; ++r)
                {
                    if (rows[r] == nullptr) continue;
                    for (int p = 0; p < 5; ++p)
                    {
                        if (rows[r][p] == '1')
                        {
                            Pixel px;
                            px.row = row0 + r;
                            px.col = startCol + c * 6 + p;
                            px.startX = rng.nextFloat() * 1920;
                            px.startY = rng.nextFloat() * 1080;
                            px.phase = rng.nextFloat() * 6.28f;
                            textPixels.push_back(px);
                            maxRow = juce::jmax(maxRow, px.row);
                            maxCol = juce::jmax(maxCol, px.col);
                        }
                    }
                }
            }
        };

        addLine("LEGION", 0);
        addLine("STAGE", 7);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashComponent)
};
