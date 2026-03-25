#include "PluginHost.h"

PluginHost::PluginHost()
{
    formatManager.addFormat(new juce::VST3PluginFormat());

    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        tracks[static_cast<size_t>(i)].index = i;
        tracks[static_cast<size_t>(i)].name = "Track " + juce::String(i + 1);
    }

    setupGraph();
}

PluginHost::~PluginHost()
{
    clear();
}

void PluginHost::setupGraph()
{
    setPlayConfigDetails(0, 2, storedSampleRate, storedBlockSize);

    midiInputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    audioOutputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        auto& track = tracks[static_cast<size_t>(i)];

        // Gain node
        auto gainProc = std::make_unique<GainProcessor>();
        gainProc->soloCount = &soloCount;
        track.gainProcessor = gainProc.get();
        track.gainNode = addNode(std::move(gainProc));

        // ClipPlayer node
        auto clipProc = std::make_unique<ClipPlayerNode>(engine);
        track.clipPlayer = clipProc.get();
        track.clipPlayerNode = addNode(std::move(clipProc));

        // Gain -> audio output
        for (int ch = 0; ch < 2; ++ch)
        {
            addConnection({ { track.gainNode->nodeID, ch },
                            { audioOutputNode->nodeID, ch } });
        }
    }
}

void PluginHost::scanForPlugins()
{
    auto* format = formatManager.getFormat(0);
    if (format == nullptr) return;

    auto searchPaths = format->getDefaultLocationsToSearch();
    auto foundFiles = format->searchPathsForPlugins(searchPaths, true, false);

    for (const auto& file : foundFiles)
    {
        juce::OwnedArray<juce::PluginDescription> foundTypes;
        knownPluginList.scanAndAddFile(file, true, foundTypes, *format);
    }
}

void PluginHost::setAudioParams(double sampleRate, int blockSize)
{
    storedSampleRate = sampleRate;
    storedBlockSize = blockSize;
    midiCollector.reset(sampleRate);
}

bool PluginHost::loadPlugin(int trackIndex, const juce::PluginDescription& desc, juce::String& errorMsg)
{
    if (trackIndex < 0 || trackIndex >= NUM_TRACKS) return false;

    unloadPlugin(trackIndex);

    auto instance = formatManager.createPluginInstance(desc, storedSampleRate, storedBlockSize, errorMsg);
    if (instance == nullptr)
        return false;

    auto& track = tracks[static_cast<size_t>(trackIndex)];
    track.plugin = instance.get();
    track.pluginNode = addNode(std::move(instance));

    if (track.pluginNode == nullptr)
    {
        track.plugin = nullptr;
        errorMsg = "Failed to add plugin to graph";
        return false;
    }

    connectTrackAudio(trackIndex);
    updateMidiRouting();
    prepareToPlay(storedSampleRate, storedBlockSize);

    return true;
}

void PluginHost::unloadPlugin(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= NUM_TRACKS) return;

    auto& track = tracks[static_cast<size_t>(trackIndex)];
    if (track.pluginNode == nullptr) return;

    auto connections = getConnections();
    for (auto& conn : connections)
    {
        if (conn.source.nodeID == track.pluginNode->nodeID ||
            conn.destination.nodeID == track.pluginNode->nodeID)
        {
            removeConnection(conn);
        }
    }

    removeNode(track.pluginNode->nodeID);
    track.pluginNode = nullptr;
    track.plugin = nullptr;
}

void PluginHost::connectTrackAudio(int trackIndex)
{
    auto& track = tracks[static_cast<size_t>(trackIndex)];
    if (track.pluginNode == nullptr) return;

    // ClipPlayer MIDI -> Plugin MIDI
    addConnection({ { track.clipPlayerNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { track.pluginNode->nodeID, AudioProcessorGraph::midiChannelIndex } });

    // Plugin audio -> Gain
    for (int ch = 0; ch < 2; ++ch)
    {
        addConnection({ { track.pluginNode->nodeID, ch },
                        { track.gainNode->nodeID, ch } });
    }
}

void PluginHost::setSelectedTrack(int index)
{
    if (index < 0 || index >= NUM_TRACKS) return;
    selectedTrack = index;
    updateMidiRouting();
}

void PluginHost::updateMidiRouting()
{
    // Remove all MIDI connections from MIDI input node
    auto connections = getConnections();
    for (auto& conn : connections)
    {
        if (conn.source.nodeID == midiInputNode->nodeID &&
            conn.source.channelIndex == AudioProcessorGraph::midiChannelIndex)
        {
            removeConnection(conn);
        }
    }

    // Connect MIDI input to selected track's ClipPlayerNode
    auto& track = tracks[static_cast<size_t>(selectedTrack)];
    addConnection({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { track.clipPlayerNode->nodeID, AudioProcessorGraph::midiChannelIndex } });
}

void PluginHost::sendTestNoteOn(int noteNumber, float velocity)
{
    auto msg = juce::MidiMessage::noteOn(1, noteNumber, velocity);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue(msg);
}

void PluginHost::sendTestNoteOff(int noteNumber)
{
    auto msg = juce::MidiMessage::noteOff(1, noteNumber);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue(msg);
}

void PluginHost::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    midiCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

    // Advance transport
    engine.advancePosition(buffer.getNumSamples(), storedSampleRate);

    AudioProcessorGraph::processBlock(buffer, midiMessages);
}
