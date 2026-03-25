#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible(testToneButton);
    testToneButton.onClick = [this] { testToneEnabled.store(testToneButton.getToggleState()); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setText("No audio device", juce::dontSendNotification);

    setSize(800, 600);
    setAudioChannels(0, 2);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate)
{
    currentSampleRate = sampleRate;
    phase = 0.0;

    // prepareToPlay runs on the audio thread — must bounce UI update to message thread
    juce::MessageManager::callAsync([this] { updateStatusLabel(); });
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (!testToneEnabled.load())
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    const double frequency = 440.0;
    const double amplitude = 0.25;
    const double phaseIncrement = juce::MathConstants<double>::twoPi * frequency / currentSampleRate;

    auto* buffer = bufferToFill.buffer;
    const int numSamples = bufferToFill.numSamples;
    const int startSample = bufferToFill.startSample;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float value = static_cast<float>(std::sin(phase) * amplitude);
        phase += phaseIncrement;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        for (int channel = 0; channel < buffer->getNumChannels(); ++channel)
            buffer->setSample(channel, startSample + sample, value);
    }
}

void MainComponent::releaseResources()
{
    // Nothing to release
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    audioSettingsButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);

    testToneButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    statusLabel.setBounds(area.removeFromTop(30));
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        0, 0,    // min/max input channels
        1, 2,    // min/max output channels
        false,   // show MIDI inputs
        false,   // show MIDI outputs
        false,   // show channels as stereo pairs
        false    // hide advanced options
    );

    selector->setSize(500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Audio Settings";
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();

    // Update status after dialog closes
    juce::Timer::callAfterDelay(500, [this] { updateStatusLabel(); });
}

void MainComponent::updateStatusLabel()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        juce::String text = device->getName()
                          + " | " + juce::String(device->getCurrentSampleRate(), 0) + " Hz"
                          + " | " + juce::String(device->getCurrentBufferSizeSamples()) + " samples";
        statusLabel.setText(text, juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText("No audio device", juce::dontSendNotification);
    }
}
