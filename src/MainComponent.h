#pragma once

#include <JuceHeader.h>
#include <atomic>

class MainComponent : public juce::AudioAppComponent
{
public:
    MainComponent();
    ~MainComponent() override;

    // AudioAppComponent overrides
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::ToggleButton testToneButton    { "Test Tone" };
    juce::Label statusLabel;

    std::atomic<bool> testToneEnabled { false };
    double currentSampleRate = 0.0;
    double phase = 0.0;

    void showAudioSettings();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
