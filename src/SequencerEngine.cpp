#include "SequencerEngine.h"

SequencerEngine::SequencerEngine() {}

void SequencerEngine::play()
{
    // If count-in is enabled and recording is armed, start count-in first
    if (countInEnabled.load() && recording.load() && !playing.load())
    {
        countingIn.store(true);
        countInBeatsRemaining = 16.0; // 4 bars
        savedPosition = positionInBeats.load();
    }

    playing.store(true);
}

void SequencerEngine::stop()
{
    playing.store(false);
    recording.store(false);
    countingIn.store(false);
    countInBeatsRemaining = 0.0;
}

void SequencerEngine::toggleRecord()
{
    recording.store(!recording.load());
}

void SequencerEngine::setBpm(double newBpm)
{
    bpm.store(juce::jlimit(20.0, 300.0, newBpm));
}

void SequencerEngine::toggleCountIn()
{
    countInEnabled.store(!countInEnabled.load());
}

double SequencerEngine::advancePosition(int numSamples, double sampleRate)
{
    if (!playing.load())
        return 0.0;

    double currentBpm = bpm.load();
    double beatsPerSecond = currentBpm / 60.0;
    double beatsThisBlock = beatsPerSecond * (static_cast<double>(numSamples) / sampleRate);

    // Handle count-in
    if (countingIn.load())
    {
        countInBeatsRemaining -= beatsThisBlock;

        // ALWAYS play metronome clicks during count-in (regardless of metronome toggle)
        double countInPos = 16.0 - countInBeatsRemaining;
        double prevPos = countInPos - beatsThisBlock;

        int oldBeat = static_cast<int>(std::floor(prevPos));
        int newBeat = static_cast<int>(std::floor(countInPos));

        if (newBeat > oldBeat)
        {
            bool isDownbeat = (newBeat % 4) == 0;
            clickFrequency = isDownbeat ? 1500.0 : 1000.0;
            clickSamplesRemaining = static_cast<int>(sampleRate * 0.03); // slightly longer click for count-in
            clickPhase = 0.0;
        }

        if (countInBeatsRemaining <= 0.0)
        {
            // Count-in finished — start actual playback/recording from saved position
            countingIn.store(false);
            positionInBeats.store(savedPosition);
        }

        return 0.0; // Don't advance the main position during count-in
    }

    double oldPos = positionInBeats.load();
    double newPos = oldPos + beatsThisBlock;

    // Metronome clicks during normal playback
    if (metronomeEnabled.load())
    {
        int oldBeat = static_cast<int>(std::floor(oldPos));
        int newBeat = static_cast<int>(std::floor(newPos));

        if (newBeat > oldBeat)
        {
            bool isDownbeat = (newBeat % 4) == 0;
            clickFrequency = isDownbeat ? 1500.0 : 1000.0;
            clickSamplesRemaining = static_cast<int>(sampleRate * 0.02);
            clickPhase = 0.0;
        }
    }

    positionInBeats.store(newPos);
    return beatsThisBlock;
}

void SequencerEngine::toggleMetronome()
{
    metronomeEnabled.store(!metronomeEnabled.load());
}

void SequencerEngine::renderMetronome(juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate)
{
    if (clickSamplesRemaining <= 0) return;

    int samplesToRender = juce::jmin(clickSamplesRemaining, numSamples);
    double phaseInc = juce::MathConstants<double>::twoPi * clickFrequency / sampleRate;

    for (int s = 0; s < samplesToRender; ++s)
    {
        double envelope = static_cast<double>(clickSamplesRemaining - s) /
                          static_cast<double>(clickSamplesRemaining + samplesToRender);
        float sample = static_cast<float>(std::sin(clickPhase) * envelope * 0.4);
        clickPhase += phaseInc;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, s, sample);
    }

    clickSamplesRemaining -= samplesToRender;
}

void SequencerEngine::resetPosition()
{
    positionInBeats.store(0.0);
    clickSamplesRemaining = 0;
    countingIn.store(false);
}
