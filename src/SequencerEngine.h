#pragma once

#include <JuceHeader.h>
#include <atomic>

class SequencerEngine
{
public:
    SequencerEngine();

    // Transport controls (called from UI thread)
    void play();
    void stop();
    void toggleRecord();
    void setBpm(double bpm);

    // State queries
    bool isPlaying() const { return playing.load(); }
    bool isRecording() const { return recording.load(); }
    double getBpm() const { return bpm.load(); }
    double getPositionInBeats() const { return positionInBeats.load(); }
    bool isInCountIn() const { return countingIn.load(); }
    double getCountInBeatsRemaining() const { return countInBeatsRemaining; }

    // Called from audio thread each block
    double advancePosition(int numSamples, double sampleRate);

    // Metronome
    void toggleMetronome();
    bool isMetronomeOn() const { return metronomeEnabled.load(); }
    void renderMetronome(juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate);

    // Count-in
    void toggleCountIn();
    bool isCountInEnabled() const { return countInEnabled.load(); }

    // Loop
    void toggleLoop();
    bool isLoopEnabled() const { return loopEnabled.load(); }
    void setLoopRegion(double startBeat, double endBeat);
    void clearLoopRegion();
    double getLoopStart() const { return loopStart.load(); }
    double getLoopEnd() const { return loopEnd.load(); }
    bool hasLoopRegion() const { return loopEnd.load() > loopStart.load(); }

    // Position control
    void resetPosition();
    void setPosition(double beats) { positionInBeats.store(beats); }

private:
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> positionInBeats { 0.0 };

    // Metronome
    std::atomic<bool> metronomeEnabled { false };
    double clickPhase = 0.0;
    int clickSamplesRemaining = 0;
    double clickFrequency = 1000.0;

    // Count-in (4 bars = 16 beats before recording starts)
    std::atomic<bool> countInEnabled { false };
    std::atomic<bool> countingIn { false };
    double countInBeatsRemaining = 0.0;
    double savedPosition = 0.0;  // where to start after count-in
    bool countInFirstClick = false;  // fire click on first audio block of count-in

    bool playFirstClick = false;  // fire metronome on first audio block of play

    // Loop
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 0.0 };
};
