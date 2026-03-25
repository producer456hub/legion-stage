#pragma once

#include <JuceHeader.h>
#include <atomic>

class GainProcessor : public juce::AudioProcessor
{
public:
    GainProcessor();

    const juce::String getName() const override { return "GainProcessor"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void releaseResources() override {}

    // Editor
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    // State
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // Bus layout
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Programs
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Controls — set from UI thread, read from audio thread
    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> muted { false };

    // Solo — shared counter across all tracks, set by PluginHost
    std::atomic<int>* soloCount = nullptr;
    std::atomic<bool> soloed { false };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainProcessor)
};
