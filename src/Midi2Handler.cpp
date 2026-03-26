#include "Midi2Handler.h"

Midi2Handler::Midi2Handler()
{
    // Generate a random-ish MUID
    juce::Random rng;
    ourMuid[0] = static_cast<uint8_t>(rng.nextInt(128));
    ourMuid[1] = static_cast<uint8_t>(rng.nextInt(128));
    ourMuid[2] = static_cast<uint8_t>(rng.nextInt(128));
    ourMuid[3] = static_cast<uint8_t>(rng.nextInt(128));
}

void Midi2Handler::setPlugin(juce::AudioProcessor* plugin)
{
    currentPlugin = plugin;
    buildMappings();
}

void Midi2Handler::buildMappings()
{
    mappings.clear();
    if (currentPlugin == nullptr) return;

    auto& params = currentPlugin->getParameters();

    // Try to find macro parameters first (Arturia)
    juce::Array<int> macroIndices;
    for (int i = 0; i < params.size(); ++i)
    {
        juce::String name = params[i]->getName(30).toLowerCase();
        if (name.contains("macro") || name.contains("mcr"))
            macroIndices.add(i);
    }

    // If no macros, use first 8 params
    if (macroIndices.isEmpty())
    {
        for (int i = 0; i < juce::jmin(8, params.size()); ++i)
            macroIndices.add(i);
    }

    // Map to CCs 24-31
    for (int i = 0; i < juce::jmin(8, macroIndices.size()); ++i)
    {
        ParamMapping m;
        m.pluginParamIndex = macroIndices[i];
        m.cc = 24 + i;
        m.name = params[macroIndices[i]]->getName(16);
        mappings.add(m);
    }
}

void Midi2Handler::handleCC(int ccNumber, int value)
{
    if (currentPlugin == nullptr) return;

    for (auto& m : mappings)
    {
        if (m.cc == ccNumber && m.pluginParamIndex >= 0)
        {
            auto& params = currentPlugin->getParameters();
            if (m.pluginParamIndex < params.size())
            {
                float normalized = static_cast<float>(value) / 127.0f;
                params[m.pluginParamIndex]->setValue(normalized);
            }
            return;
        }
    }
}

// ── Incoming CI message processing ───────────────────────────────────────────

bool Midi2Handler::processIncoming(const juce::MidiMessage& msg)
{
    if (!msg.isSysEx()) return false;

    auto data = msg.getSysExData();
    auto size = msg.getSysExDataSize();

    // Check for Universal SysEx MIDI-CI header: 7E 7F 0D
    if (size < 14 || data[0] != 0x7E || data[1] != 0x7F || data[2] != 0x0D)
        return false;

    uint8_t subId2 = data[3];
    // Skip version byte (data[4] = 0x01)

    // Source MUID (bytes 5-8)
    uint8_t srcMuid[4] = { data[5], data[6], data[7], data[8] };

    // Destination MUID (bytes 9-12)
    // uint8_t dstMuid[4] = { data[9], data[10], data[11], data[12] };

    switch (subId2)
    {
        case 0x70: // Discovery
        {
            // Store Keystage's MUID
            memcpy(keystageMuid, srcMuid, 4);

            // Send Discovery Reply
            sendDiscoveryReply(srcMuid);
            return true;
        }

        case 0x30: // PE Capabilities Inquiry
        {
            uint8_t requestId = (size > 13) ? data[13] : 0;
            sendPECapabilityReply(srcMuid, requestId);
            return true;
        }

        case 0x34: // Get Property Data
        {
            if (size < 16) return true;

            uint8_t requestId = data[13];
            // Header length (14-15, LSB first)
            int headerLen = data[14] | (data[15] << 7);

            // Extract header JSON
            juce::String headerStr;
            for (int i = 0; i < headerLen && (16 + i) < size; ++i)
                headerStr += juce::String::charToString(static_cast<char>(data[16 + i]));

            // Parse the resource name from the header
            auto headerJson = juce::JSON::parse(headerStr);
            juce::String resource = headerJson.getProperty("resource", "").toString();

            // Build response
            juce::String responseBody;
            juce::String responseHeader = "{\"status\":200,\"mediaType\":\"application/json\"}";

            if (resource == "ResourceList")
                responseBody = buildResourceList();
            else if (resource == "DeviceInfo")
                responseBody = buildDeviceInfo();
            else if (resource == "X-ParameterList")
                responseBody = buildParameterList();
            else if (resource == "X-ProgramEdit")
                responseBody = buildProgramEdit();
            else
                responseHeader = "{\"status\":404}";

            sendPropertyReply(srcMuid, requestId, responseHeader, responseBody);
            return true;
        }

        case 0x38: // Subscription
        {
            if (size < 16) return true;

            uint8_t requestId = data[13];
            // Just accept all subscriptions
            juce::String responseHeader = "{\"status\":200}";
            sendPropertyReply(srcMuid, requestId, responseHeader, "");
            return true;
        }

        default:
            return false;
    }
}

// ── CI SysEx message builders ────────────────────────────────────────────────

void Midi2Handler::sendDiscoveryReply(const uint8_t* destMuid)
{
    juce::Array<uint8_t> payload;

    // Manufacturer ID (3 bytes) — using dev/test ID
    payload.add(0x7D); payload.add(0x00); payload.add(0x00);

    // Family ID (2 bytes)
    payload.add(0x01); payload.add(0x00);

    // Model Number (2 bytes)
    payload.add(0x01); payload.add(0x00);

    // Software Revision (4 bytes)
    payload.add(0x01); payload.add(0x00); payload.add(0x00); payload.add(0x00);

    // Capability Category Supported (Property Exchange = bit3)
    payload.add(0x08);

    // Max SysEx Size (4 bytes, LSB first) — 4096
    payload.add(0x00); payload.add(0x20); payload.add(0x00); payload.add(0x00);

    addCISysEx(0x71, destMuid, payload); // 0x71 = Discovery Reply
}

void Midi2Handler::sendPECapabilityReply(const uint8_t* destMuid, uint8_t /*requestId*/)
{
    juce::Array<uint8_t> payload;
    payload.add(4); // Number of simultaneous requests supported

    addCISysEx(0x31, destMuid, payload); // 0x31 = PE Capability Reply
}

void Midi2Handler::sendPropertyReply(const uint8_t* destMuid, uint8_t requestId,
                                      const juce::String& headerJson, const juce::String& bodyJson)
{
    auto headerBytes = encodeJsonForSysex(headerJson);
    auto bodyBytes = encodeJsonForSysex(bodyJson);

    juce::Array<uint8_t> payload;

    // Request ID
    payload.add(requestId);

    // Header length (LSB first, 2 bytes, 7-bit encoded)
    payload.add(static_cast<uint8_t>(headerBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((headerBytes.size() >> 7) & 0x7F));

    // Header data
    payload.addArray(headerBytes);

    // Number of chunks (1)
    payload.add(0x01); payload.add(0x00);

    // This chunk number (1)
    payload.add(0x01); payload.add(0x00);

    // Body length (LSB first, 2 bytes, 7-bit encoded)
    payload.add(static_cast<uint8_t>(bodyBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((bodyBytes.size() >> 7) & 0x7F));

    // Body data
    payload.addArray(bodyBytes);

    addCISysEx(0x35, destMuid, payload); // 0x35 = Get Property Data Reply
}

void Midi2Handler::addCISysEx(uint8_t subId2, const uint8_t* destMuid,
                               const juce::Array<uint8_t>& payload)
{
    juce::Array<uint8_t> sysex;
    sysex.add(0xF0);       // SysEx start
    sysex.add(0x7E);       // Universal Non-Real-Time
    sysex.add(0x7F);       // Destination: whole MIDI port
    sysex.add(0x0D);       // MIDI-CI
    sysex.add(subId2);     // Sub ID #2
    sysex.add(0x01);       // Version 1.1

    // Source MUID (our MUID)
    sysex.add(ourMuid[0]); sysex.add(ourMuid[1]);
    sysex.add(ourMuid[2]); sysex.add(ourMuid[3]);

    // Destination MUID
    sysex.add(destMuid[0]); sysex.add(destMuid[1]);
    sysex.add(destMuid[2]); sysex.add(destMuid[3]);

    // Payload
    for (auto b : payload)
        sysex.add(b);

    sysex.add(0xF7);       // SysEx end

    outgoingMidi.addEvent(juce::MidiMessage(sysex.getRawDataPointer(), sysex.size()), 0);
}

// ── Parameter update notification ────────────────────────────────────────────

void Midi2Handler::sendParameterUpdate()
{
    if (!isConnected() || currentPlugin == nullptr) return;

    juce::String body = buildProgramEdit();
    juce::String header = "{\"status\":200,\"resource\":\"X-ProgramEdit\",\"command\":\"notify\"}";

    // Build subscription notification (sub ID 0x38)
    auto headerBytes = encodeJsonForSysex(header);
    auto bodyBytes = encodeJsonForSysex(body);

    juce::Array<uint8_t> payload;
    payload.add(0x01); // RequestID

    payload.add(static_cast<uint8_t>(headerBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((headerBytes.size() >> 7) & 0x7F));
    payload.addArray(headerBytes);

    payload.add(0x01); payload.add(0x00); // 1 chunk
    payload.add(0x01); payload.add(0x00); // chunk 1

    payload.add(static_cast<uint8_t>(bodyBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((bodyBytes.size() >> 7) & 0x7F));
    payload.addArray(bodyBytes);

    addCISysEx(0x38, keystageMuid, payload);
}

// ── JSON builders ────────────────────────────────────────────────────────────

juce::String Midi2Handler::buildResourceList() const
{
    return "[{\"resource\":\"DeviceInfo\"},"
           "{\"resource\":\"X-ParameterList\"},"
           "{\"resource\":\"X-ProgramEdit\",\"canSubscribe\":true}]";
}

juce::String Midi2Handler::buildDeviceInfo() const
{
    return "{\"manufacturerId\":[125,0,0],"
           "\"manufacturer\":\"DAW3\","
           "\"familyId\":[1,0],"
           "\"family\":\"Sequencer\","
           "\"modelId\":[1,0],"
           "\"model\":\"DAW3\","
           "\"versionId\":[1,0,0,0],"
           "\"version\":\"1.0.0\"}";
}

juce::String Midi2Handler::buildParameterList() const
{
    juce::String json = "[";

    for (int i = 0; i < mappings.size(); ++i)
    {
        if (i > 0) json += ",";

        auto& m = mappings[i];
        int defaultVal = 0;

        if (currentPlugin != nullptr)
        {
            auto& params = currentPlugin->getParameters();
            if (m.pluginParamIndex < params.size())
                defaultVal = static_cast<int>(params[m.pluginParamIndex]->getValue() * 127.0f);
        }

        json += "{\"name\":\"" + m.name + "\","
               + "\"controlcc\":" + juce::String(m.cc) + ","
               + "\"default\":" + juce::String(defaultVal) + "}";
    }

    json += "]";
    return json;
}

juce::String Midi2Handler::buildProgramEdit() const
{
    juce::String json = "{\"currentValues\":[";

    for (int i = 0; i < mappings.size(); ++i)
    {
        if (i > 0) json += ",";

        auto& m = mappings[i];
        int val = 0;
        juce::String displayVal = "0.0";

        if (currentPlugin != nullptr)
        {
            auto& params = currentPlugin->getParameters();
            if (m.pluginParamIndex < params.size())
            {
                float fVal = params[m.pluginParamIndex]->getValue();
                val = static_cast<int>(fVal * 127.0f);
                displayVal = juce::String(fVal * 100.0f, 1);
            }
        }

        json += "{\"name\":\"" + m.name + "\","
               + "\"value\":" + juce::String(val) + ","
               + "\"displayValue\":\"" + displayVal + "\","
               + "\"displayUnit\":\"%\"}";
    }

    json += "]}";
    return json;
}

// ── SysEx encoding ───────────────────────────────────────────────────────────

juce::Array<uint8_t> Midi2Handler::encodeJsonForSysex(const juce::String& json)
{
    // JSON in CI Property Exchange is sent as ASCII bytes
    // All characters must be 7-bit safe (< 128)
    juce::Array<uint8_t> bytes;

    for (int i = 0; i < json.length(); ++i)
    {
        auto c = static_cast<uint8_t>(json[i] & 0x7F);
        bytes.add(c);
    }

    return bytes;
}
