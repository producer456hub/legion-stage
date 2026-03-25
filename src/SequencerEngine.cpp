#include "SequencerEngine.h"

SequencerEngine::SequencerEngine() {}

void SequencerEngine::play()
{
    playing.store(true);
}

void SequencerEngine::stop()
{
    playing.store(false);
    recording.store(false);
    resetPosition();
}

void SequencerEngine::toggleRecord()
{
    recording.store(!recording.load());
}

void SequencerEngine::setBpm(double newBpm)
{
    bpm.store(juce::jlimit(20.0, 300.0, newBpm));
}

double SequencerEngine::advancePosition(int numSamples, double sampleRate)
{
    if (!playing.load())
        return 0.0;

    double currentBpm = bpm.load();
    double beatsPerSecond = currentBpm / 60.0;
    double beatsThisBlock = beatsPerSecond * (static_cast<double>(numSamples) / sampleRate);

    double oldPos = positionInBeats.load();
    positionInBeats.store(oldPos + beatsThisBlock);

    return beatsThisBlock;
}

void SequencerEngine::resetPosition()
{
    positionInBeats.store(0.0);
}
