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

    // Called from audio thread each block
    // Returns beats covered in this block
    double advancePosition(int numSamples, double sampleRate);

    // Reset position to 0
    void resetPosition();

private:
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> positionInBeats { 0.0 };
};
