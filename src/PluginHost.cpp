#include "PluginHost.h"

PluginHost::PluginHost()
{
    formatManager.addFormat(new juce::VST3PluginFormat());
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

bool PluginHost::loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg)
{
    unloadPlugin();

    auto instance = formatManager.createPluginInstance(desc, storedSampleRate, storedBlockSize, errorMsg);
    if (instance == nullptr)
        return false;

    currentPlugin = instance.get();

    pluginNode = addNode(std::move(instance));
    if (pluginNode == nullptr)
    {
        currentPlugin = nullptr;
        errorMsg = "Failed to add plugin to graph";
        return false;
    }

    connectPlugin();
    prepareToPlay(storedSampleRate, storedBlockSize);

    return true;
}

void PluginHost::unloadPlugin()
{
    if (pluginNode != nullptr)
    {
        auto connections = getConnections();
        for (auto& conn : connections)
        {
            if (conn.source.nodeID == pluginNode->nodeID ||
                conn.destination.nodeID == pluginNode->nodeID)
            {
                removeConnection(conn);
            }
        }

        removeNode(pluginNode->nodeID);
        pluginNode = nullptr;
        currentPlugin = nullptr;
    }
}

void PluginHost::connectPlugin()
{
    if (pluginNode == nullptr) return;

    addConnection({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { pluginNode->nodeID, AudioProcessorGraph::midiChannelIndex } });

    for (int ch = 0; ch < 2; ++ch)
    {
        addConnection({ { pluginNode->nodeID, ch },
                        { audioOutputNode->nodeID, ch } });
    }
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
    // Pull any buffered MIDI from the collector into the processing chain
    midiCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

    // Process the graph (routes MIDI to plugin, audio to output)
    AudioProcessorGraph::processBlock(buffer, midiMessages);
}
