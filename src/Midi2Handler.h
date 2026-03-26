#pragma once

#include <JuceHeader.h>

// MIDI 2.0 CI handler — sets up a MIDI-CI Device that exposes
// plugin parameters via Property Exchange for auto-mapping
// with MIDI 2.0 controllers like the Korg Keystage.
class Midi2Handler : public juce::midi_ci::DeviceListener,
                     public juce::midi_ci::PropertyDelegate
{
public:
    Midi2Handler();
    ~Midi2Handler() override;

    // Set the plugin whose parameters should be exposed
    void setPlugin(juce::AudioProcessor* plugin);

    // Process incoming MIDI from the device (raw sysex)
    void processMessage(const juce::MidiMessage& msg);

    // Get outgoing MIDI messages to send to the device
    juce::MidiBuffer& getOutgoingMessages() { return outgoingMidi; }
    void clearOutgoing() { outgoingMidi.clear(); }

    // Status
    bool isConnected() const;
    juce::String getStatusText() const;

    // DeviceListener overrides
    void deviceAdded(juce::midi_ci::MUID muid) override;
    void deviceRemoved(juce::midi_ci::MUID muid) override;

    // PropertyDelegate overrides
    uint8_t getNumSimultaneousRequestsSupported() const override { return 4; }

    juce::midi_ci::PropertyReplyData propertyGetDataRequested(juce::midi_ci::MUID,
        const juce::midi_ci::PropertyRequestHeader& header) override;

    juce::midi_ci::PropertyReplyHeader propertySetDataRequested(juce::midi_ci::MUID,
        const juce::midi_ci::PropertyRequestData& data) override;

    bool subscriptionStartRequested(juce::midi_ci::MUID,
        const juce::midi_ci::PropertySubscriptionHeader&) override { return true; }

    void subscriptionDidStart(juce::midi_ci::MUID, const juce::String&,
        const juce::midi_ci::PropertySubscriptionHeader&) override {}

    void subscriptionWillEnd(juce::midi_ci::MUID,
        const juce::midi_ci::Subscription&) override {}

private:
    class OutputHandler;
    std::unique_ptr<OutputHandler> outputHandler;
    std::unique_ptr<juce::midi_ci::Device> ciDevice;

    juce::AudioProcessor* currentPlugin = nullptr;
    juce::midi_ci::MUID connectedMuid;
    juce::MidiBuffer outgoingMidi;

    // Build JSON resource list of plugin parameters
    juce::String buildResourceList() const;
    juce::String buildParameterData(int paramIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Midi2Handler)
};
