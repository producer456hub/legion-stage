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
    if (currentPlugin != plugin)
        clearAllCustomMappings();  // fresh plugin = fresh assignments
    currentPlugin = plugin;
    buildMappings();
}

void Midi2Handler::setCustomMapping(int slotIndex, int pluginParamIndex)
{
    if (slotIndex >= 0 && slotIndex < 8)
    {
        customMappings[slotIndex] = pluginParamIndex;
        buildMappings();
    }
}

void Midi2Handler::clearCustomMapping(int slotIndex)
{
    if (slotIndex >= 0 && slotIndex < 8)
    {
        customMappings[slotIndex] = -1;
        buildMappings();
    }
}

void Midi2Handler::clearAllCustomMappings()
{
    for (int i = 0; i < 8; ++i)
        customMappings[i] = -1;
}

void Midi2Handler::buildMappings()
{
    mappings.clear();
    if (currentPlugin == nullptr) return;

    auto& params = currentPlugin->getParameters();

    // Page 0: try to find macro parameters first (Arturia)
    // Other pages: use sequential parameter blocks
    if (currentPage == 0)
    {
        juce::Array<int> macroIndices;
        for (int i = 0; i < params.size(); ++i)
        {
            juce::String name = params[i]->getName(30).toLowerCase();
            if (name.contains("macro") || name.contains("mcr"))
                macroIndices.add(i);
        }

        if (!macroIndices.isEmpty())
        {
            for (int i = 0; i < juce::jmin(8, macroIndices.size()); ++i)
            {
                ParamMapping m;
                m.pluginParamIndex = macroIndices[i];
                m.cc = i;
                m.name = params[macroIndices[i]]->getName(16);
                mappings.add(m);
            }
            return;
        }
    }

    // Use sequential params based on page
    int startParam = currentPage * 8;
    for (int i = 0; i < 8 && (startParam + i) < params.size(); ++i)
    {
        ParamMapping m;
        m.pluginParamIndex = startParam + i;
        m.cc = i;
        m.name = params[startParam + i]->getName(16);
        mappings.add(m);
    }

    // Apply custom slot overrides
    for (int slot = 0; slot < 8 && slot < mappings.size(); ++slot)
    {
        int custom = customMappings[slot];
        if (custom >= 0 && custom < params.size())
        {
            mappings.getReference(slot).pluginParamIndex = custom;
            mappings.getReference(slot).name = params[custom]->getName(16);
        }
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

                // Throttle OLED updates to prevent MIDI flooding
                if (hasXProgramEditSubscription && isConnected())
                {
                    lastChangedParamIndex = m.pluginParamIndex;
                    juce::int64 now = juce::Time::currentTimeMillis();
                    if (now - lastUpdateTime >= UPDATE_INTERVAL_MS)
                    {
                        lastUpdateTime = now;
                        sendParameterUpdate();
                    }
                }
            }
            return;
        }
    }
}

int Midi2Handler::getNumPages() const
{
    if (currentPlugin == nullptr) return 1;
    int totalParams = currentPlugin->getParameters().size();
    return juce::jmax(1, (totalParams + 7) / 8);
}

void Midi2Handler::nextPage()
{
    if (currentPlugin == nullptr) return;
    currentPage++;
    if (currentPage >= getNumPages())
        currentPage = 0; // wrap around
    buildMappings();
    if (isConnected())
    {
        pushParameterListToKeystage();
        pushProgramEditToKeystage();
    }
}

void Midi2Handler::prevPage()
{
    if (currentPlugin == nullptr) return;
    if (currentPage > 0)
    {
        currentPage--;
        buildMappings();
        if (isConnected())
        {
            pushParameterListToKeystage();
            pushProgramEditToKeystage();
        }
    }
}

void Midi2Handler::nextPreset()
{
    if (currentPlugin == nullptr) return;
    int current = currentPlugin->getCurrentProgram();
    int total = currentPlugin->getNumPrograms();
    if (total > 1 && current < total - 1)
        currentPlugin->setCurrentProgram(current + 1);
}

void Midi2Handler::prevPreset()
{
    if (currentPlugin == nullptr) return;
    int current = currentPlugin->getCurrentProgram();
    int total = currentPlugin->getNumPrograms();
    if (total > 1 && current > 0)
        currentPlugin->setCurrentProgram(current - 1);
}

void Midi2Handler::sendDiscovery()
{
    // Send Discovery broadcast — same format as the Keystage sends
    uint8_t broadcastMuid[4] = { 0x7F, 0x7F, 0x7F, 0x7F };

    juce::Array<uint8_t> payload;

    // Manufacturer ID
    payload.add(0x7D); payload.add(0x00); payload.add(0x00);
    // Family ID
    payload.add(0x01); payload.add(0x00);
    // Model Number
    payload.add(0x01); payload.add(0x00);
    // Software Revision
    payload.add(0x01); payload.add(0x00); payload.add(0x00); payload.add(0x00);
    // Capability (Property Exchange = bit3 = 0x08)
    payload.add(0x08);
    // Max SysEx Size (4096)
    payload.add(0x00); payload.add(0x20); payload.add(0x00); payload.add(0x00);

    addCISysEx(0x70, broadcastMuid, payload); // 0x70 = Discovery
}

// ── Incoming CI message processing ───────────────────────────────────────────

bool Midi2Handler::processIncoming(const juce::MidiMessage& msg)
{
    if (!msg.isSysEx()) return false;

    auto data = msg.getSysExData();
    auto size = msg.getSysExDataSize();

    // Only handle Universal SysEx MIDI-CI messages: 7E 7F 0D
    // Strictly check — reject anything that isn't exactly CI format
    if (size < 14)
        return false;
    if (data[0] != 0x7E || data[1] != 0x7F || data[2] != 0x0D)
        return false;
    // Version must be 0x01
    if (data[4] != 0x01)
        return false;

    uint8_t subId2 = data[3];
    // Skip version byte (data[4] = 0x01)

    // Source MUID (bytes 5-8)
    uint8_t srcMuid[4] = { data[5], data[6], data[7], data[8] };

    // Destination MUID (bytes 9-12)
    // uint8_t dstMuid[4] = { data[9], data[10], data[11], data[12] };

    // Log incoming CI to file
    {
        juce::String inHex;
        for (int i = 0; i < size; ++i)
            inHex += juce::String::toHexString(data[i]) + " ";
        juce::File("C:/dev/sequencer/ci-debug.log").appendText(
            "IN  0x" + juce::String::toHexString(subId2) + " [" + juce::String(size) + "b]: " + inHex + "\n");
    }

    switch (subId2)
    {
        case 0x70: // Discovery (Keystage is looking for us)
        {
            memcpy(keystageMuid, srcMuid, 4);
            sendDiscoveryReply(srcMuid);

            // After replying, push our data
            pushParameterListToKeystage();
            pushProgramEditToKeystage();
            return true;
        }

        case 0x71: // Discovery Reply (Keystage responded to our Discovery)
        {
            memcpy(keystageMuid, srcMuid, 4);

            // Send PE Capabilities Inquiry
            {
                juce::Array<uint8_t> pePayload;
                pePayload.add(4);
                addCISysEx(0x30, keystageMuid, pePayload);
            }

            // Proactively push our parameter list and current values to the Keystage
            pushParameterListToKeystage();
            pushProgramEditToKeystage();
            return true;
        }

        case 0x30: // PE Capabilities Inquiry (Keystage asking us)
        {
            uint8_t requestId = (size > 13) ? data[13] : 0;
            sendPECapabilityReply(srcMuid, requestId);
            return true;
        }

        case 0x31: // PE Capabilities Reply (Keystage responding to our inquiry)
        {
            // Keystage supports PE — now query its ResourceList
            // (This confirms the CI connection is established)
            return true;
        }

        case 0x34: // Get Property Data
        {
            // Extract request ID — it's right after the MUID fields
            uint8_t requestId = (size > 13) ? data[13] : 1;

            // Extract the full raw header to figure out what's being asked
            juce::String rawContent;
            for (int i = 13; i < size; ++i)
                rawContent += juce::String::toHexString(data[i]) + " ";

            // Try to find and extract the header JSON
            // Header length is at bytes 14-15 (after requestId at 13)
            juce::String headerStr;
            if (size > 16)
            {
                int headerLen = data[14] | (data[15] << 7);
                for (int i = 0; i < headerLen && (16 + i) < size; ++i)
                    headerStr += juce::String::charToString(static_cast<char>(data[16 + i]));
            }

            auto hdrLower = headerStr.toLowerCase();

            juce::String responseBody;
            juce::String responseHeader = "{\"status\":200,\"mediaType\":\"application/json\"}";

            if (hdrLower.contains("resourcelist"))
                responseBody = buildResourceList();
            else if (hdrLower.contains("deviceinfo"))
                responseBody = buildDeviceInfo();
            else if (hdrLower.contains("x-parameterlist"))
                responseBody = buildParameterList();
            else if (hdrLower.contains("x-programedit"))
                responseBody = buildProgramEdit();
            else if (hdrLower.contains("channellist"))
                responseBody = "[{\"title\":\"DAW3\",\"channel\":1}]";
            else if (hdrLower.contains("programlist"))
                responseBody = "[]";
            else
            {
                // Unknown resource — respond with parameter list anyway for debugging
                responseBody = buildParameterList();
            }

            sendPropertyReply(srcMuid, requestId, responseHeader, responseBody);

            // Force: always add the outgoing count so we can verify
            DBG("CI: Sent reply for '" + headerStr + "', outgoing count: " + juce::String(outgoingMidi.getNumEvents()));

            return true;
        }

        case 0x38: // Subscription request
        {
            if (size < 16) return true;

            uint8_t requestId = data[13];

            // Extract the header to check what resource
            int headerLen = data[14] | (data[15] << 7);
            juce::String subHeader;
            for (int i = 0; i < headerLen && (16 + i) < size; ++i)
                subHeader += juce::String::charToString(static_cast<char>(data[16 + i]));

            auto subLower = subHeader.toLowerCase();

            if (subLower.contains("x-programedit") && subLower.contains("start"))
            {
                // Accept subscription — reply with 0x39 and include subscribeId + the current values
                hasXProgramEditSubscription = true;

                juce::String replyHeader = "{\"status\":200,\"subscribeId\":\"xpe1\",\"command\":\"start\",\"resource\":\"X-ProgramEdit\"}";
                juce::String replyBody = buildProgramEdit();

                // Send as Subscription Reply (0x39)
                auto hdrBytes = encodeJsonForSysex(replyHeader);
                auto bodyBytes = encodeJsonForSysex(replyBody);

                juce::Array<uint8_t> payload;
                payload.add(requestId);

                payload.add(static_cast<uint8_t>(hdrBytes.size() & 0x7F));
                payload.add(static_cast<uint8_t>((hdrBytes.size() >> 7) & 0x7F));
                payload.addArray(hdrBytes);

                payload.add(0x01); payload.add(0x00);
                payload.add(0x01); payload.add(0x00);

                payload.add(static_cast<uint8_t>(bodyBytes.size() & 0x7F));
                payload.add(static_cast<uint8_t>((bodyBytes.size() >> 7) & 0x7F));
                payload.addArray(bodyBytes);

                addCISysEx(0x39, srcMuid, payload); // 0x39 = Subscription Reply
            }
            else
            {
                // Generic subscription accept
                juce::String replyHeader = "{\"status\":200}";
                auto hdrBytes = encodeJsonForSysex(replyHeader);

                juce::Array<uint8_t> payload;
                payload.add(requestId);
                payload.add(static_cast<uint8_t>(hdrBytes.size() & 0x7F));
                payload.add(static_cast<uint8_t>((hdrBytes.size() >> 7) & 0x7F));
                payload.addArray(hdrBytes);
                payload.add(0x01); payload.add(0x00);
                payload.add(0x01); payload.add(0x00);
                payload.add(0x00); payload.add(0x00);

                addCISysEx(0x39, srcMuid, payload);
            }
            return true;
        }

        case 0x7E: // Invalidate MUID
        {
            // Keystage is resetting — clear our connection state
            memset(keystageMuid, 0, 4);
            hasXProgramEditSubscription = false;
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

    // Debug: log the exact bytes to file
    juce::String hexDump;
    for (auto b : sysex)
        hexDump += juce::String::toHexString(b) + " ";
    juce::File("C:/dev/sequencer/ci-debug.log").appendText(
        "OUT 0x" + juce::String::toHexString(subId2) + " [" + juce::String(sysex.size()) + "b]: " + hexDump + "\n");

    outgoingMidi.addEvent(juce::MidiMessage(sysex.getRawDataPointer(), sysex.size()), 0);
}

// ── Push data to Keystage ─────────────────────────────────────────────────────

void Midi2Handler::pushParameterListToKeystage()
{
    if (!isConnected()) return;

    juce::String header = "{\"resource\":\"X-ParameterList\"}";
    juce::String body = buildParameterList();

    // Use Subscription message (0x38) to push data
    auto headerBytes = encodeJsonForSysex(header);
    auto bodyBytes = encodeJsonForSysex(body);

    juce::Array<uint8_t> payload;
    payload.add(0x01); // RequestID

    payload.add(static_cast<uint8_t>(headerBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((headerBytes.size() >> 7) & 0x7F));
    payload.addArray(headerBytes);

    payload.add(0x01); payload.add(0x00);
    payload.add(0x01); payload.add(0x00);

    payload.add(static_cast<uint8_t>(bodyBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((bodyBytes.size() >> 7) & 0x7F));
    payload.addArray(bodyBytes);

    addCISysEx(0x38, keystageMuid, payload);
}

void Midi2Handler::pushProgramEditToKeystage()
{
    if (!isConnected()) return;

    juce::String header = "{\"resource\":\"X-ProgramEdit\"}";
    juce::String body = buildProgramEdit();

    auto headerBytes = encodeJsonForSysex(header);
    auto bodyBytes = encodeJsonForSysex(body);

    juce::Array<uint8_t> payload;
    payload.add(0x02); // RequestID

    payload.add(static_cast<uint8_t>(headerBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((headerBytes.size() >> 7) & 0x7F));
    payload.addArray(headerBytes);

    payload.add(0x01); payload.add(0x00);
    payload.add(0x01); payload.add(0x00);

    payload.add(static_cast<uint8_t>(bodyBytes.size() & 0x7F));
    payload.add(static_cast<uint8_t>((bodyBytes.size() >> 7) & 0x7F));
    payload.addArray(bodyBytes);

    addCISysEx(0x38, keystageMuid, payload);
}

// ── Parameter update notification ────────────────────────────────────────────

void Midi2Handler::sendParameterUpdate()
{
    if (!isConnected() || currentPlugin == nullptr) return;

    // Don't queue if there are already pending updates
    if (outgoingMidi.getNumEvents() > 0) return;

    // Send only the changed parameter for speed, or all if unknown
    juce::String body;
    if (lastChangedParamIndex >= 0)
    {
        // Find which mapping this param belongs to
        for (auto& m : mappings)
        {
            if (m.pluginParamIndex == lastChangedParamIndex)
            {
                auto& params = currentPlugin->getParameters();
                if (m.pluginParamIndex < params.size())
                {
                    float fVal = params[m.pluginParamIndex]->getValue();
                    int val = static_cast<int>(fVal * 127.0f);
                    body = "{\"currentValues\":[{\"name\":\"" + m.name + "\","
                           + "\"value\":" + juce::String(val) + ","
                           + "\"displayValue\":\"" + juce::String(fVal * 100.0f, 1) + "\","
                           + "\"displayUnit\":\"%\"}]}";
                }
                break;
            }
        }
    }

    if (body.isEmpty())
        body = buildProgramEdit();

    juce::String header = "{\"status\":200,\"subscribeId\":\"xpe1\",\"command\":\"notify\"}";

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
           "{\"resource\":\"ChannelList\"},"
           "{\"resource\":\"ProgramList\"},"
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
