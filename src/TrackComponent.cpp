#include "TrackComponent.h"

TrackComponent::TrackComponent(int trackIndex)
    : index(trackIndex)
{
    // Make clicking anywhere on the track label/plugin label area select the track
    setInterceptsMouseClicks(true, true);

    addAndMakeVisible(armButton);
    armButton.setClickingTogglesState(true);
    armButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.darker());
    armButton.onClick = [this] {
        if (onArmChanged)
            onArmChanged(index, armButton.getToggleState());
    };

    addAndMakeVisible(selectButton);
    selectButton.setButtonText("Trk" + juce::String(index + 1));
    selectButton.onClick = [this] {
        if (onSelected)
            onSelected(index);
    };

    addAndMakeVisible(pluginLabel);
    pluginLabel.setText("----", juce::dontSendNotification);
    pluginLabel.setFont(juce::Font(11.0f));
    pluginLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    pluginLabel.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(0.8, juce::dontSendNotification);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.onValueChange = [this] {
        if (onVolumeChanged)
            onVolumeChanged(index, static_cast<float>(volumeSlider.getValue()));
    };

    addAndMakeVisible(muteButton);
    muteButton.setClickingTogglesState(true);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    muteButton.onClick = [this] {
        if (onMuteChanged)
            onMuteChanged(index, muteButton.getToggleState());
    };

    addAndMakeVisible(soloButton);
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton.onClick = [this] {
        if (onSoloChanged)
            onSoloChanged(index, soloButton.getToggleState());
    };
}

void TrackComponent::paint(juce::Graphics& g)
{
    if (selected)
        g.fillAll(juce::Colour(0xff3a5a8a));
    else
        g.fillAll(juce::Colour(0xff2a2a2a));

    g.setColour(juce::Colour(0xff444444));
    g.drawLine(0, static_cast<float>(getHeight()), static_cast<float>(getWidth()), static_cast<float>(getHeight()));
}

void TrackComponent::resized()
{
    auto area = getLocalBounds().reduced(2, 1);

    armButton.setBounds(area.removeFromLeft(22));
    area.removeFromLeft(2);
    selectButton.setBounds(area.removeFromLeft(42));
    pluginLabel.setBounds(area.removeFromLeft(70));
    muteButton.setBounds(area.removeFromRight(22));
    area.removeFromRight(2);
    soloButton.setBounds(area.removeFromRight(22));
    area.removeFromRight(2);
    volumeSlider.setBounds(area);
}

void TrackComponent::mouseDown(const juce::MouseEvent& /*e*/)
{
    if (onSelected)
        onSelected(index);
}

void TrackComponent::setSelected(bool sel)
{
    selected = sel;
    repaint();
}

void TrackComponent::setPluginName(const juce::String& name)
{
    pluginLabel.setText(name.isEmpty() ? "----" : name, juce::dontSendNotification);
}

float TrackComponent::getVolume() const
{
    return static_cast<float>(volumeSlider.getValue());
}

bool TrackComponent::isMuted() const
{
    return muteButton.getToggleState();
}

bool TrackComponent::isSoloed() const
{
    return soloButton.getToggleState();
}

bool TrackComponent::isArmed() const
{
    return armButton.getToggleState();
}
