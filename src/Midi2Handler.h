#pragma once

#include <JuceHeader.h>

// Korg Keystage MIDI 2.0 CI handler.
// Implements MIDI-CI Property Exchange manually using raw SysEx,
// exposing plugin parameters via X-ParameterList and X-ProgramEdit
// for the Keystage's auto-mapping OLED knob display.
//
// The Keystage knobs send CCs 24-31. We map those to plugin params.
class Midi2Handler
{
public:
    Midi2Handler();

    // Set the plugin whose parameters should be exposed
    void setPlugin(juce::AudioProcessor* plugin);

    // Process incoming MIDI — checks for CI SysEx
    // Returns true if the message was a CI message (handled)
    bool processIncoming(const juce::MidiMessage& msg);

    // Get outgoing CI response messages
    juce::MidiBuffer& getOutgoing() { return outgoingMidi; }
    void clearOutgoing() { outgoingMidi.clear(); }

    // Handle CC 24-31 from Keystage knobs → set plugin parameters
    void handleCC(int ccNumber, int value);

    // Send X-ProgramEdit update (call when a parameter changes)
    void sendParameterUpdate();

    // Initiate discovery — send broadcast Discovery message
    void sendDiscovery();

    // Status
    bool isConnected() const { return keystageMuid[0] != 0 || keystageMuid[1] != 0; }

    // Parameter mapping info
    struct ParamMapping {
        int pluginParamIndex = -1;
        int cc = 0;
        juce::String name;
    };
    const juce::Array<ParamMapping>& getMappings() const { return mappings; }

private:
    juce::AudioProcessor* currentPlugin = nullptr;
    juce::MidiBuffer outgoingMidi;

    // Our MUID (4 bytes, LSB first)
    uint8_t ourMuid[4] = { 0x12, 0x34, 0x56, 0x00 };

    // Keystage's MUID (learned from Discovery)
    uint8_t keystageMuid[4] = { 0, 0, 0, 0 };
    bool hasXProgramEditSubscription = false;
    juce::int64 lastUpdateTime = 0;
    static constexpr int UPDATE_INTERVAL_MS = 15; // max ~66 updates/sec
    int lastChangedParamIndex = -1;
    int currentPage = 0;

public:
    // Page navigation — changes which 8 params are mapped to knobs
    void nextPage();
    void prevPage();
    int getCurrentPage() const { return currentPage; }
    int getNumPages() const;

    // Preset navigation
    void nextPreset();
    void prevPreset();

    // Custom slot assignments — pin a specific plugin param to a knob slot (-1 = auto)
    void setCustomMapping(int slotIndex, int pluginParamIndex);
    void clearCustomMapping(int slotIndex);
    void clearAllCustomMappings();

private:

    // Parameter mappings (up to 8 knobs → CCs 24-31)
    juce::Array<ParamMapping> mappings;
    int customMappings[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    void buildMappings();

    // CI message builders
    void sendDiscoveryReply(const uint8_t* destMuid);
    void sendPECapabilityReply(const uint8_t* destMuid, uint8_t requestId);
    void sendPropertyReply(const uint8_t* destMuid, uint8_t requestId,
                           const juce::String& headerJson, const juce::String& bodyJson);

    // SysEx helpers
    void addCISysEx(uint8_t subId2, const uint8_t* destMuid,
                    const juce::Array<uint8_t>& payload);

    // Push data to Keystage (using Get Property Data Reply format)
    void pushParameterListToKeystage();
    void pushProgramEditToKeystage();

    // JSON builders
    juce::String buildResourceList() const;
    juce::String buildDeviceInfo() const;
    juce::String buildParameterList() const;
    juce::String buildProgramEdit() const;

    // Encode JSON to 7-bit safe (for SysEx)
    static juce::Array<uint8_t> encodeJsonForSysex(const juce::String& json);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Midi2Handler)
};
