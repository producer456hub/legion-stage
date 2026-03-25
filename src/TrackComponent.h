#pragma once

#include <JuceHeader.h>
#include <functional>

class TrackComponent : public juce::Component
{
public:
    TrackComponent(int trackIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setSelected(bool selected);
    void setPluginName(const juce::String& name);
    float getVolume() const;
    bool isMuted() const;
    bool isSoloed() const;
    bool isArmed() const;

    std::function<void(int)> onSelected;
    std::function<void(int, float)> onVolumeChanged;
    std::function<void(int, bool)> onMuteChanged;
    std::function<void(int, bool)> onSoloChanged;
    std::function<void(int, bool)> onArmChanged;

private:
    int index;
    bool selected = false;

    juce::TextButton armButton { "A" };
    juce::TextButton selectButton;
    juce::Label pluginLabel;
    juce::Slider volumeSlider;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackComponent)
};
