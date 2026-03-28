#pragma once

#include <JuceHeader.h>
#include <functional>

class UpdateChecker : public juce::Thread
{
public:
    using StatusCallback = std::function<void(const juce::String&)>;
    using CompletionCallback = std::function<void(bool success, const juce::String&)>;
    using CommitsCallback = std::function<void(const juce::String& commitLog)>;

    UpdateChecker();
    ~UpdateChecker() override;

    void checkForUpdates(CommitsCallback onCommits, StatusCallback onStatus, CompletionCallback onComplete);
    void performUpdate(StatusCallback onStatus, CompletionCallback onComplete);

    void run() override;

private:
    enum class Mode { Check, Update };
    Mode currentMode = Mode::Check;

    CommitsCallback commitsCallback;
    StatusCallback statusCallback;
    CompletionCallback completionCallback;

    juce::String repoPath { "C:\\Users\\goremote\\legion-stage" };
    juce::String installPath { "C:\\Program Files\\Legion Stage\\Legion Stage.exe" };

    bool runProcess(const juce::String& command, juce::String& output, int timeoutMs = 600000);
    void postStatus(const juce::String& msg);
    void postCompletion(bool success, const juce::String& msg);
};
