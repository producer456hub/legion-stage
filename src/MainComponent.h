#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name, juce::AudioProcessorEditor* editor,
                       std::function<void()> onClose)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton),
          closeCallback(std::move(onClose))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(editor, true);
        setVisible(true);
        centreWithSize(getWidth(), getHeight());
    }

    void closeButtonPressed() override
    {
        // Notify owner to properly destroy editor + window (prevents dangling content pointer)
        if (closeCallback)
            closeCallback();
    }

private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    PluginHost pluginHost;

    // UI
    juce::ComboBox pluginSelector;
    juce::TextButton openEditorButton   { "Open Editor" };
    juce::TextButton testNoteButton     { "Play Test Note" };
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::Label statusLabel;

    // Plugin editor window
    std::unique_ptr<juce::AudioProcessorEditor> currentEditor;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    // Plugin descriptions (indexed by combo box)
    juce::Array<juce::PluginDescription> pluginDescriptions;

    void scanPlugins();
    void loadSelectedPlugin();
    void openPluginEditor();
    void closePluginEditor();
    void playTestNote();
    void showAudioSettings();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
