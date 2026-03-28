#pragma once

#include <JuceHeader.h>
#include "UpdateChecker.h"

class UpdateDialog : public juce::Component
{
public:
    UpdateDialog();
    ~UpdateDialog() override;

    void resized() override;

private:
    enum class State { Checking, UpdatesAvailable, Updating, Complete };
    State state = State::Checking;

    UpdateChecker checker;
    juce::Component::SafePointer<UpdateDialog> safeThis { this };

    juce::TextEditor logDisplay;
    juce::TextButton updateButton { "Update Now" };
    juce::TextButton closeButton { "Close" };
    juce::Label statusLabel;

    void startCheck();
    void onCommitsAvailable(const juce::String& commits);
    void onStatusUpdate(const juce::String& message);
    void onComplete(bool success, const juce::String& message);
    void startUpdate();
    void restartApp();
    void setState(State newState);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateDialog)
};
