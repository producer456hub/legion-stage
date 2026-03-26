#include "Midi2Handler.h"

// Output handler that captures CI messages to send back via MIDI
class Midi2Handler::OutputHandler : public juce::midi_ci::DeviceMessageHandler
{
public:
    OutputHandler(juce::MidiBuffer& buf) : outBuf(buf) {}

    void processMessage(juce::ump::BytesOnGroup msg) override
    {
        // Convert CI bytes to standard MIDI SysEx message
        juce::Array<uint8_t> data;
        data.add(0xF0); // SysEx start

        for (size_t i = 0; i < msg.bytes.size(); ++i)
            data.add(static_cast<uint8_t>(msg.bytes.data()[i]));

        data.add(0xF7); // SysEx end

        outBuf.addEvent(juce::MidiMessage(data.getRawDataPointer(), data.size()), 0);
    }

private:
    juce::MidiBuffer& outBuf;
};

Midi2Handler::Midi2Handler()
    : connectedMuid(juce::midi_ci::MUID::makeUnchecked(0))
{
    outputHandler = std::make_unique<OutputHandler>(outgoingMidi);

    // Set up CI Device as a host with Property Exchange
    juce::Random rng;

    auto options = juce::midi_ci::DeviceOptions{}
        .withOutputs({ outputHandler.get() })
        .withFeatures(juce::midi_ci::DeviceFeatures{}
            .withPropertyExchangeSupported(true)
            .withProfileConfigurationSupported(true))
        .withDeviceInfo(juce::ump::DeviceInfo{
            { std::byte{0x7D}, std::byte{0x00}, std::byte{0x00} }, // Manufacturer (dev/test)
            { std::byte{0x01}, std::byte{0x00} },                   // Family
            { std::byte{0x01}, std::byte{0x00} },                   // Model
            { std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00} } // Revision
        })
        .withProductInstanceId(juce::midi_ci::DeviceOptions::makeProductInstanceId(rng))
        .withPropertyDelegate(this)
        .withMaxSysExSize(4096);

    ciDevice = std::make_unique<juce::midi_ci::Device>(options);
    ciDevice->addListener(*this);
}

Midi2Handler::~Midi2Handler()
{
    if (ciDevice)
        ciDevice->removeListener(*this);
}

void Midi2Handler::setPlugin(juce::AudioProcessor* plugin)
{
    currentPlugin = plugin;
}

void Midi2Handler::processMessage(const juce::MidiMessage& msg)
{
    if (!ciDevice || !msg.isSysEx()) return;

    auto sysexData = msg.getSysExData();
    auto sysexSize = msg.getSysExDataSize();

    if (sysexSize < 4) return;

    // Build bytes span and feed to CI device
    std::vector<std::byte> byteVec;
    for (int i = 0; i < sysexSize; ++i)
        byteVec.push_back(static_cast<std::byte>(sysexData[i]));

    juce::ump::BytesOnGroup bog;
    bog.group = 0;
    bog.bytes = juce::Span<const std::byte>(byteVec.data(), byteVec.size());

    ciDevice->processMessage(bog);
}

void Midi2Handler::deviceAdded(juce::midi_ci::MUID muid)
{
    connectedMuid = muid;
}

void Midi2Handler::deviceRemoved(juce::midi_ci::MUID muid)
{
    if (connectedMuid == muid)
        connectedMuid = juce::midi_ci::MUID::makeUnchecked(0);
}

juce::String Midi2Handler::getStatusText() const
{
    if (isConnected())
        return "MIDI 2.0 CI Connected";
    return "MIDI 2.0 CI: Waiting...";
}

bool Midi2Handler::isConnected() const
{
    return connectedMuid != juce::midi_ci::MUID::makeUnchecked(0);
}

// Property Exchange — respond to parameter queries

juce::midi_ci::PropertyReplyData Midi2Handler::propertyGetDataRequested(
    juce::midi_ci::MUID,
    const juce::midi_ci::PropertyRequestHeader& header)
{
    juce::midi_ci::PropertyReplyData reply;

    auto resource = header.resource;

    if (resource == "ResourceList")
    {
        auto json = buildResourceList();
        std::vector<std::byte> body;
        for (auto c : json)
            body.push_back(static_cast<std::byte>(c));

        reply.header.status = 200;
        reply.header.mediaType = "application/json";
        reply.body = body;
    }
    else if (resource.startsWith("param/"))
    {
        int paramIdx = resource.fromLastOccurrenceOf("/", false, false).getIntValue();
        auto json = buildParameterData(paramIdx);

        std::vector<std::byte> body;
        for (auto c : json)
            body.push_back(static_cast<std::byte>(c));

        reply.header.status = 200;
        reply.header.mediaType = "application/json";
        reply.body = body;
    }
    else
    {
        reply.header.status = 404;
    }

    return reply;
}

juce::midi_ci::PropertyReplyHeader Midi2Handler::propertySetDataRequested(
    juce::midi_ci::MUID,
    const juce::midi_ci::PropertyRequestData& data)
{
    juce::midi_ci::PropertyReplyHeader reply;
    reply.status = 200;

    auto resource = data.header.resource;

    if (resource.startsWith("param/") && currentPlugin != nullptr)
    {
        int paramIdx = resource.fromLastOccurrenceOf("/", false, false).getIntValue();
        auto& params = currentPlugin->getParameters();

        if (paramIdx >= 0 && paramIdx < params.size())
        {
            juce::String bodyStr;
            for (auto b : data.body)
                bodyStr += juce::String::charToString(static_cast<char>(b));

            auto json = juce::JSON::parse(bodyStr);
            if (json.hasProperty("value"))
            {
                float val = static_cast<float>(static_cast<double>(json.getProperty("value", 0.0)));
                params[paramIdx]->setValue(val);
            }
        }
    }

    return reply;
}

juce::String Midi2Handler::buildResourceList() const
{
    juce::String json = "[";

    if (currentPlugin != nullptr)
    {
        auto& params = currentPlugin->getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            if (i > 0) json += ",";
            json += "{\"resource\":\"param/" + juce::String(i) + "\","
                   + "\"name\":\"" + params[i]->getName(30) + "\","
                   + "\"canGet\":true,\"canSet\":true}";
        }
    }

    json += "]";
    return json;
}

juce::String Midi2Handler::buildParameterData(int paramIndex) const
{
    if (currentPlugin == nullptr) return "{}";

    auto& params = currentPlugin->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return "{}";

    auto* param = params[paramIndex];
    return "{\"name\":\"" + param->getName(30) + "\","
           + "\"value\":" + juce::String(param->getValue(), 4) + ","
           + "\"text\":\"" + param->getCurrentValueAsText() + "\"}";
}
