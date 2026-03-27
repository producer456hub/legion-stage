#pragma once

#include <JuceHeader.h>
#include <juce_opengl/juce_opengl.h>
#include <projectM-4/projectM.h>

// MilkDrop visualizer using projectM and JUCE OpenGL.
// Renders classic MilkDrop presets driven by audio.
class MilkDropComponent : public juce::Component,
                          public juce::OpenGLRenderer
{
public:
    MilkDropComponent()
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(false, false);
        glContext.setRenderer(this);
        glContext.setContinuousRepainting(true);
        glContext.setComponentPaintingEnabled(false);
    }

    ~MilkDropComponent() override
    {
        glContext.detach();
    }

    void setPresetPath(const juce::String& path) { presetPath = path; }

    void pushSamples(const float* data, int numSamples)
    {
        if (pm != nullptr)
            projectm_pcm_add_float(pm, data, static_cast<unsigned int>(numSamples), PROJECTM_MONO);
    }

    // OpenGLRenderer callbacks — called on the GL thread
    void newOpenGLContextCreated() override
    {
        pm = projectm_create();
        if (pm != nullptr)
        {
            auto scale = static_cast<float>(glContext.getRenderingScale());
            int pw = static_cast<int>(getWidth() * scale);
            int ph = static_cast<int>(getHeight() * scale);
            projectm_set_window_size(pm, static_cast<size_t>(juce::jmax(1, pw)),
                                          static_cast<size_t>(juce::jmax(1, ph)));
            projectm_set_preset_duration(pm, 20.0);

            if (presetPath.isNotEmpty())
                loadRandomPreset();
        }
    }

    void renderOpenGL() override
    {
        if (pm == nullptr) return;

        auto scale = static_cast<float>(glContext.getRenderingScale());
        int pw = static_cast<int>(getWidth() * scale);
        int ph = static_cast<int>(getHeight() * scale);

        if (pw != lastPW || ph != lastPH)
        {
            projectm_set_window_size(pm, static_cast<size_t>(juce::jmax(1, pw)),
                                          static_cast<size_t>(juce::jmax(1, ph)));
            lastPW = pw;
            lastPH = ph;
        }

        juce::gl::glViewport(0, 0, pw, ph);
        projectm_opengl_render_frame(pm);

        // Auto-switch presets periodically
        double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (now - lastPresetSwitch > presetDuration)
        {
            loadRandomPreset();
            lastPresetSwitch = now;
        }
    }

    void openGLContextClosing() override
    {
        if (pm != nullptr)
        {
            projectm_destroy(pm);
            pm = nullptr;
        }
    }

    void visibilityChanged() override
    {
        if (isVisible() && !glContext.isAttached())
            glContext.attachTo(*this);
        else if (!isVisible() && glContext.isAttached())
            glContext.detach();
    }

    void parentHierarchyChanged() override
    {
        if (isVisible() && isShowing() && !glContext.isAttached())
            glContext.attachTo(*this);
    }

    void nextPreset()
    {
        loadRandomPreset();
    }

private:
    juce::OpenGLContext glContext;
    projectm_handle pm = nullptr;
    juce::String presetPath;
    juce::StringArray presetFiles;
    int lastPW = 0, lastPH = 0;
    double lastPresetSwitch = 0.0;
    double presetDuration = 20.0;

    void loadRandomPreset()
    {
        if (presetPath.isEmpty() || pm == nullptr) return;

        if (presetFiles.isEmpty())
        {
            juce::File dir(presetPath);
            if (dir.isDirectory())
            {
                for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.milk"))
                    presetFiles.add(f.getFullPathName());
            }
        }

        if (presetFiles.isEmpty()) return;

        juce::Random rng;
        int idx = rng.nextInt(presetFiles.size());
        projectm_load_preset_file(pm, presetFiles[idx].toRawUTF8(), true);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDropComponent)
};
