#include "UpdateChecker.h"

UpdateChecker::UpdateChecker() : Thread("UpdateChecker") {}

UpdateChecker::~UpdateChecker()
{
    stopThread(10000);
}

void UpdateChecker::checkForUpdates(CommitsCallback onCommits, StatusCallback onStatus, CompletionCallback onComplete)
{
    if (isThreadRunning()) return;
    currentMode = Mode::Check;
    commitsCallback = std::move(onCommits);
    statusCallback = std::move(onStatus);
    completionCallback = std::move(onComplete);
    startThread();
}

void UpdateChecker::performUpdate(StatusCallback onStatus, CompletionCallback onComplete)
{
    if (isThreadRunning()) return;
    currentMode = Mode::Update;
    statusCallback = std::move(onStatus);
    completionCallback = std::move(onComplete);
    startThread();
}

void UpdateChecker::run()
{
    juce::String output;

    if (currentMode == Mode::Check)
    {
        postStatus("Fetching latest changes...");
        if (!runProcess("cmd.exe /c \"cd /d " + repoPath + " && git fetch origin main 2>&1\"", output))
        {
            postCompletion(false, "Failed to fetch: " + output);
            return;
        }

        juce::String localHash, remoteHash;
        if (!runProcess("cmd.exe /c \"cd /d " + repoPath + " && git rev-parse HEAD\"", localHash) ||
            !runProcess("cmd.exe /c \"cd /d " + repoPath + " && git rev-parse origin/main\"", remoteHash))
        {
            postCompletion(false, "Failed to compare versions.");
            return;
        }

        localHash = localHash.trim();
        remoteHash = remoteHash.trim();

        if (localHash == remoteHash)
        {
            postCompletion(true, "Already up to date!");
            return;
        }

        juce::String commitLog;
        runProcess("cmd.exe /c \"cd /d " + repoPath + " && git log --oneline HEAD..origin/main\"", commitLog);

        if (threadShouldExit()) return;

        auto cb = commitsCallback;
        juce::MessageManager::callAsync([cb, commitLog]() { if (cb) cb(commitLog); });
    }
    else // Mode::Update
    {
        postStatus("Pulling latest changes...");
        if (!runProcess("cmd.exe /c \"cd /d " + repoPath + " && git pull origin main 2>&1\"", output))
        {
            postCompletion(false, "Git pull failed:\n" + output);
            return;
        }
        postStatus("Pull complete.\n" + output);

        if (threadShouldExit()) return;

        postStatus("Building... this may take several minutes.");
        juce::String buildOutput;
        if (!runProcess("cmd.exe /c \"" + repoPath + "\\do_build.bat\" 2>&1", buildOutput, 600000))
        {
            postCompletion(false, "Build failed:\n" + buildOutput);
            return;
        }
        postStatus("Build complete!");

        if (threadShouldExit()) return;

        postStatus("Installing...");

        // Rename current exe (Windows allows renaming a running exe)
        juce::String oldPath = installPath.replace(".exe", ".exe.old");
        juce::String renameCmd = "cmd.exe /c \"rename \"" + installPath + "\" \"Legion Stage.exe.old\" 2>&1\"";
        runProcess(renameCmd, output); // OK if this fails (file might not exist)

        // Copy new exe
        juce::String buildExe = repoPath + "\\build\\Sequencer_artefacts\\Release\\Legion Stage.exe";
        juce::String copyCmd = "cmd.exe /c \"copy /Y \"" + buildExe + "\" \"" + installPath + "\" 2>&1\"";
        if (!runProcess(copyCmd, output))
        {
            // Try elevated copy
            postStatus("Requesting admin privileges to install...");
            juce::String psCmd = "powershell -Command \"Start-Process powershell -Verb RunAs -Wait -ArgumentList "
                "'-Command Copy-Item \\\"" + buildExe + "\\\" \\\"" + installPath + "\\\" -Force'\"";
            if (!runProcess(psCmd, output))
            {
                postCompletion(false, "Install failed (could not copy exe):\n" + output);
                return;
            }
        }

        postCompletion(true, "Update installed successfully! Restart to use the new version.");
    }
}

bool UpdateChecker::runProcess(const juce::String& command, juce::String& output, int timeoutMs)
{
    juce::ChildProcess proc;
    if (!proc.start(command))
    {
        output = "Failed to start process.";
        return false;
    }

    output = proc.readAllProcessOutput();
    bool finished = proc.waitForProcessToFinish(timeoutMs);

    if (!finished)
    {
        proc.kill();
        output += "\nProcess timed out.";
        return false;
    }

    return proc.getExitCode() == 0;
}

void UpdateChecker::postStatus(const juce::String& msg)
{
    auto cb = statusCallback;
    juce::MessageManager::callAsync([cb, msg]() { if (cb) cb(msg); });
}

void UpdateChecker::postCompletion(bool success, const juce::String& msg)
{
    auto cb = completionCallback;
    juce::MessageManager::callAsync([cb, success, msg]() { if (cb) cb(success, msg); });
}
