#include "GainProcessor.h"
#include <cmath>

GainProcessor::GainProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void GainProcessor::prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/)
{
}

bool GainProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GainProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    // Check mute
    if (muted.load())
    {
        buffer.clear();
        return;
    }

    // Check solo — if any track is soloed and this one isn't, silence
    if (soloCount != nullptr && soloCount->load() > 0 && !soloed.load())
    {
        buffer.clear();
        return;
    }

    float vol = volume.load();
    float p = pan.load(); // -1.0 to 1.0

    // Equal power pan law
    float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0 to pi/2
    float leftGain = vol * std::cos(angle);
    float rightGain = vol * std::sin(angle);

    if (buffer.getNumChannels() >= 1)
        buffer.applyGain(0, 0, buffer.getNumSamples(), leftGain);
    if (buffer.getNumChannels() >= 2)
        buffer.applyGain(1, 0, buffer.getNumSamples(), rightGain);
}
