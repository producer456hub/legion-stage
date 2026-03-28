#include "UpdateDialog.h"

UpdateDialog::UpdateDialog()
{
    addAndMakeVisible(statusLabel);
    statusLabel.setFont(juce::Font(15.0f));
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(logDisplay);
    logDisplay.setMultiLine(true);
    logDisplay.setReadOnly(true);
    logDisplay.setScrollbarsShown(true);
    logDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e1e));
    logDisplay.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    logDisplay.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

    addAndMakeVisible(updateButton);
    updateButton.onClick = [this] { startUpdate(); };
    updateButton.setVisible(false);

    addAndMakeVisible(closeButton);
    closeButton.onClick = [this]
    {
        if (state == State::Complete && statusLabel.getText().contains("successfully"))
            restartApp();
        else if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };

    setSize(550, 420);
    startCheck();
}

UpdateDialog::~UpdateDialog()
{
    checker.stopThread(5000);
}

void UpdateDialog::resized()
{
    auto area = getLocalBounds().reduced(12);

    statusLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);

    auto buttonRow = area.removeFromBottom(32);
    closeButton.setBounds(buttonRow.removeFromRight(100));
    buttonRow.removeFromRight(8);
    updateButton.setBounds(buttonRow.removeFromRight(120));

    area.removeFromBottom(6);
    logDisplay.setBounds(area);
}

void UpdateDialog::startCheck()
{
    setState(State::Checking);
    statusLabel.setText("Checking for updates...", juce::dontSendNotification);

    auto safe = safeThis;

    checker.checkForUpdates(
        [safe](const juce::String& commits) { if (safe != nullptr) safe->onCommitsAvailable(commits); },
        [safe](const juce::String& msg)     { if (safe != nullptr) safe->onStatusUpdate(msg); },
        [safe](bool ok, const juce::String& msg) { if (safe != nullptr) safe->onComplete(ok, msg); }
    );
}

void UpdateDialog::onCommitsAvailable(const juce::String& commits)
{
    setState(State::UpdatesAvailable);
    statusLabel.setText("Updates available!", juce::dontSendNotification);
    logDisplay.setText("New commits on master:\n\n" + commits);
    updateButton.setVisible(true);
}

void UpdateDialog::onStatusUpdate(const juce::String& message)
{
    statusLabel.setText(message, juce::dontSendNotification);
    logDisplay.moveCaretToEnd();
    logDisplay.insertTextAtCaret(message + "\n");
}

void UpdateDialog::onComplete(bool success, const juce::String& message)
{
    setState(State::Complete);
    statusLabel.setText(message, juce::dontSendNotification);
    updateButton.setVisible(false);

    if (success && message.contains("successfully"))
        closeButton.setButtonText("Restart Now");
    else
        closeButton.setButtonText("Close");
}

void UpdateDialog::startUpdate()
{
    setState(State::Updating);
    statusLabel.setText("Updating...", juce::dontSendNotification);
    logDisplay.clear();
    updateButton.setVisible(false);

    auto safe = safeThis;

    checker.performUpdate(
        [safe](const juce::String& msg)          { if (safe != nullptr) safe->onStatusUpdate(msg); },
        [safe](bool ok, const juce::String& msg)  { if (safe != nullptr) safe->onComplete(ok, msg); }
    );
}

void UpdateDialog::restartApp()
{
    juce::String exePath = "C:\\Program Files\\Legion Stage\\Legion Stage.exe";
    juce::File(exePath).startAsProcess();
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void UpdateDialog::setState(State newState)
{
    state = newState;
}
