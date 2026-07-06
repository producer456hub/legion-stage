#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstring>

// Ink-in-Water fluid simulation visualizer.
// Based on Jos Stam's "Stable Fluids" algorithm.
// Audio injects colored dye and velocity into the fluid.
class FluidComponent : public juce::Component, public juce::Timer
{
public:
    static constexpr int N = 128;          // grid resolution
    static constexpr int SIZE = (N + 2) * (N + 2);

    FluidComponent()
    {
        vx.resize(SIZE, 0); vy.resize(SIZE, 0);
        vx0.resize(SIZE, 0); vy0.resize(SIZE, 0);
        dR.resize(SIZE, 0); dG.resize(SIZE, 0); dB.resize(SIZE, 0);
        dR0.resize(SIZE, 0); dG0.resize(SIZE, 0); dB0.resize(SIZE, 0);
        startTimerHz(30);
    }

    ~FluidComponent() override { stopTimer(); }

    void pushSamples(const float* data, int numSamples)
    {
        float sum = 0.0f;
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float v = std::abs(data[i]);
            sum += data[i] * data[i];
            if (v > peak) peak = v;
        }
        float rms = std::sqrt(sum / static_cast<float>(juce::jmax(1, numSamples)));
        smoothedRms.store(smoothedRms.load() * 0.8f + rms * 0.2f);
        smoothedPeak.store(smoothedPeak.load() * 0.7f + peak * 0.3f);
    }

    void timerCallback() override
    {
        float energy = juce::jmin(1.0f, smoothedRms.load() * 5.0f);
        float peak = juce::jmin(1.0f, smoothedPeak.load() * 3.0f);
        phase += 0.02;

        // Inject dye and velocity based on audio
        int cx = N / 2;
        int cy = N / 2;

        // Multiple injection points that orbit slowly
        for (int p = 0; p < 3; ++p)
        {
            float angle = static_cast<float>(phase * (0.3 + p * 0.2) + p * 2.094);
            float radius = N * 0.25f;
            int ix = cx + static_cast<int>(std::cos(angle) * radius);
            int iy = cy + static_cast<int>(std::sin(angle) * radius);
            ix = juce::jlimit(2, N - 1, ix);
            iy = juce::jlimit(2, N - 1, iy);

            float strength = energy * 80.0f + peak * 120.0f;

            // Velocity — tangential to orbit
            float tvx = -std::sin(angle) * strength;
            float tvy = std::cos(angle) * strength;

            for (int di = -1; di <= 1; ++di)
                for (int dj = -1; dj <= 1; ++dj)
                {
                    int idx = IX(ix + di, iy + dj);
                    vx0[idx] += tvx;
                    vy0[idx] += tvy;
                }

            // Color injection — cycling hues
            float hue = std::fmod(static_cast<float>(phase * 0.1 + p * 0.33), 1.0f);
            float r, g, b;
            hueToRgb(hue, r, g, b);

            float dyeAmount = (energy * 15.0f + peak * 25.0f);
            for (int di = -2; di <= 2; ++di)
                for (int dj = -2; dj <= 2; ++dj)
                {
                    int idx = IX(juce::jlimit(1, N, ix + di), juce::jlimit(1, N, iy + dj));
                    dR0[idx] += r * dyeAmount;
                    dG0[idx] += g * dyeAmount;
                    dB0[idx] += b * dyeAmount;
                }
        }

        // Fluid simulation step
        float dt = 0.1f;
        float visc = 0.0001f;
        float diff = 0.00005f;

        velStep(vx, vy, vx0, vy0, visc, dt);
        densStep(dR, dR0, vx, vy, diff, dt);
        densStep(dG, dG0, vx, vy, diff, dt);
        densStep(dB, dB0, vx, vy, diff, dt);

        // Slow decay
        for (int i = 0; i < SIZE; ++i)
        {
            dR[i] *= 0.995f;
            dG[i] *= 0.995f;
            dB[i] *= 0.995f;
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        float w = static_cast<float>(bounds.getWidth());
        float h = static_cast<float>(bounds.getHeight());

        // Dark background
        g.fillAll(juce::Colour(0xFF060612));

        float cellW = w / N;
        float cellH = h / N;

        for (int i = 1; i <= N; ++i)
        {
            for (int j = 1; j <= N; ++j)
            {
                int idx = IX(i, j);
                float r = juce::jlimit(0.0f, 1.0f, dR[idx]);
                float gr = juce::jlimit(0.0f, 1.0f, dG[idx]);
                float b = juce::jlimit(0.0f, 1.0f, dB[idx]);

                if (r < 0.01f && gr < 0.01f && b < 0.01f)
                    continue;

                g.setColour(juce::Colour::fromFloatRGBA(r, gr, b, juce::jmin(1.0f, (r + gr + b) * 0.8f)));
                g.fillRect(static_cast<float>(i - 1) * cellW,
                           static_cast<float>(j - 1) * cellH,
                           cellW + 1.0f, cellH + 1.0f);
            }
        }
    }

private:
    double phase = 0.0;
    std::atomic<float> smoothedRms { 0.0f };
    std::atomic<float> smoothedPeak { 0.0f };

    std::vector<float> vx, vy, vx0, vy0;
    std::vector<float> dR, dG, dB, dR0, dG0, dB0;

    static int IX(int i, int j) { return i + (N + 2) * j; }

    static void hueToRgb(float h, float& r, float& g, float& b)
    {
        float s = 0.85f, v = 1.0f;
        int hi = static_cast<int>(h * 6.0f) % 6;
        float f = h * 6.0f - static_cast<float>(hi);
        float p = v * (1.0f - s);
        float q = v * (1.0f - f * s);
        float t = v * (1.0f - (1.0f - f) * s);
        switch (hi)
        {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }

    // ── Fluid solver routines (Jos Stam) ──

    static void setBnd(int b, std::vector<float>& x)
    {
        for (int i = 1; i <= N; ++i)
        {
            x[IX(0, i)]     = b == 1 ? -x[IX(1, i)] : x[IX(1, i)];
            x[IX(N + 1, i)] = b == 1 ? -x[IX(N, i)] : x[IX(N, i)];
            x[IX(i, 0)]     = b == 2 ? -x[IX(i, 1)] : x[IX(i, 1)];
            x[IX(i, N + 1)] = b == 2 ? -x[IX(i, N)] : x[IX(i, N)];
        }
        x[IX(0, 0)]         = 0.5f * (x[IX(1, 0)] + x[IX(0, 1)]);
        x[IX(0, N + 1)]     = 0.5f * (x[IX(1, N + 1)] + x[IX(0, N)]);
        x[IX(N + 1, 0)]     = 0.5f * (x[IX(N, 0)] + x[IX(N + 1, 1)]);
        x[IX(N + 1, N + 1)] = 0.5f * (x[IX(N, N + 1)] + x[IX(N + 1, N)]);
    }

    static void linSolve(int b, std::vector<float>& x, std::vector<float>& x0, float a, float c)
    {
        float cRecip = 1.0f / c;
        for (int k = 0; k < 4; ++k)
        {
            for (int j = 1; j <= N; ++j)
                for (int i = 1; i <= N; ++i)
                    x[IX(i, j)] = (x0[IX(i, j)] + a * (x[IX(i - 1, j)] + x[IX(i + 1, j)]
                                 + x[IX(i, j - 1)] + x[IX(i, j + 1)])) * cRecip;
            setBnd(b, x);
        }
    }

    static void diffuse(int b, std::vector<float>& x, std::vector<float>& x0, float diff, float dt)
    {
        float a = dt * diff * N * N;
        linSolve(b, x, x0, a, 1.0f + 4.0f * a);
    }

    static void advect(int b, std::vector<float>& d, std::vector<float>& d0,
                       std::vector<float>& u, std::vector<float>& v, float dt)
    {
        float dtx = dt * N;
        float dty = dt * N;

        for (int j = 1; j <= N; ++j)
        {
            for (int i = 1; i <= N; ++i)
            {
                float x = static_cast<float>(i) - dtx * u[IX(i, j)];
                float y = static_cast<float>(j) - dty * v[IX(i, j)];

                x = juce::jlimit(0.5f, N + 0.5f, x);
                y = juce::jlimit(0.5f, N + 0.5f, y);

                int i0 = static_cast<int>(x);
                int i1 = i0 + 1;
                int j0 = static_cast<int>(y);
                int j1 = j0 + 1;

                float s1 = x - i0;
                float s0 = 1.0f - s1;
                float t1 = y - j0;
                float t0 = 1.0f - t1;

                d[IX(i, j)] = s0 * (t0 * d0[IX(i0, j0)] + t1 * d0[IX(i0, j1)])
                            + s1 * (t0 * d0[IX(i1, j0)] + t1 * d0[IX(i1, j1)]);
            }
        }
        setBnd(b, d);
    }

    static void project(std::vector<float>& u, std::vector<float>& v,
                        std::vector<float>& p, std::vector<float>& div)
    {
        float h = 1.0f / N;
        for (int j = 1; j <= N; ++j)
            for (int i = 1; i <= N; ++i)
            {
                div[IX(i, j)] = -0.5f * h * (u[IX(i + 1, j)] - u[IX(i - 1, j)]
                                            + v[IX(i, j + 1)] - v[IX(i, j - 1)]);
                p[IX(i, j)] = 0;
            }
        setBnd(0, div);
        setBnd(0, p);
        linSolve(0, p, div, 1.0f, 4.0f);

        for (int j = 1; j <= N; ++j)
            for (int i = 1; i <= N; ++i)
            {
                u[IX(i, j)] -= 0.5f * N * (p[IX(i + 1, j)] - p[IX(i - 1, j)]);
                v[IX(i, j)] -= 0.5f * N * (p[IX(i, j + 1)] - p[IX(i, j - 1)]);
            }
        setBnd(1, u);
        setBnd(2, v);
    }

    static void velStep(std::vector<float>& u, std::vector<float>& v,
                        std::vector<float>& u0, std::vector<float>& v0,
                        float visc, float dt)
    {
        for (int i = 0; i < SIZE; ++i) { u[i] += dt * u0[i]; v[i] += dt * v0[i]; }
        std::fill(u0.begin(), u0.end(), 0.0f);
        std::fill(v0.begin(), v0.end(), 0.0f);

        std::swap(u0, u); diffuse(1, u, u0, visc, dt);
        std::swap(v0, v); diffuse(2, v, v0, visc, dt);

        project(u, v, u0, v0);

        std::swap(u0, u);
        std::swap(v0, v);
        advect(1, u, u0, u0, v0, dt);
        advect(2, v, v0, u0, v0, dt);

        project(u, v, u0, v0);
    }

    static void densStep(std::vector<float>& x, std::vector<float>& x0,
                         std::vector<float>& u, std::vector<float>& v,
                         float diff, float dt)
    {
        for (int i = 0; i < SIZE; ++i) x[i] += dt * x0[i];
        std::fill(x0.begin(), x0.end(), 0.0f);
        std::swap(x0, x); diffuse(0, x, x0, diff, dt);
        std::swap(x0, x); advect(0, x, x0, u, v, dt);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluidComponent)
};
