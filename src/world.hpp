#pragma once

enum class DataType : uint8_t {
    AudioBuffer,
    Float,
    Enum,
};
struct AudioBuffer {
    float *left, *right;
    size_t numSamples;
};
union DataValue {
    AudioBuffer *audioBufferValue;
    float floatValue;
    uint8_t enumValue;
};
struct PortValue {
private:
    DataType type;
    DataValue value;
    PortValue(DataType type, const DataValue &value) : type(type), value(value) {}
public:
    ~PortValue() = default;

    static PortValue audioBufferValue() {
        return PortValue(DataType::AudioBuffer, DataValue {
            .audioBufferValue = nullptr,
        });
    }
    static PortValue floatValue(float defaultValue) {
        return PortValue(DataType::Float, DataValue { .floatValue = defaultValue });
    }
    static PortValue enumValue(uint8_t defaultValue) {
        return PortValue(DataType::Enum, DataValue { .enumValue = defaultValue });
    }

    // I know it's all unsafe, but who cares lol
    void setAudioBufferValue(AudioBuffer &audioBuffer) {
        this->value.audioBufferValue = &audioBuffer;
    }
    void setFloatValue(float value) { this->value.floatValue = value; }
    void setEnumValue(uint8_t value) { this->value.enumValue = value; }
    void setAnyValue(const PortValue &other) {
        this->type = other.type;
        this->value = other.value;
    }

    DataType getType() const { return this->type; }
    const DataValue &getValue() const { return this->value; }
};

struct ProcessContext {
    size_t numSamples;
    double sampleRate;
};

using NodeId = uint32_t;
using PortId = uint32_t;
class Node {
protected:
    std::vector<PortValue> inputs, outputs;
public:
    Node() : inputs(), outputs() {}
    virtual ~Node() = default;
    virtual void process(const ProcessContext &context) = 0;

    std::vector<PortValue> &getInputs() { return this->inputs; }
    std::vector<PortValue> &getOutputs() { return this->outputs; }
};
struct SignalConnection {
    NodeId fromNode;
    PortId fromPort;
    NodeId toNode;
    PortId toPort;
};

struct World {
private:
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes;
    std::vector<SignalConnection> connections;
    std::vector<std::pair<NodeId, PortId>> sinkNodesPorts; // The "output" nodes that'll be summed togeter and sent to the final output buffer. It's not a required thing, but just a way to avoid useless calculations.
    std::vector<NodeId> executionOrder; // FIXME: Just a placeholder, until a proper scheduling algorithm will be implemented.
    std::deque<AudioBuffer> audioBuffers;

    std::optional<size_t> maxAudioSamples;
    NodeId nextNodeId;
public:
    constexpr static float DEBUG_MAX_VOLUME = 1.0f;

    World() : nodes(), connections(), sinkNodesPorts(), executionOrder(), maxAudioSamples(std::nullopt), nextNodeId(1) {}
    ~World() {
        for (const AudioBuffer &audioBuffer : this->audioBuffers) {
            delete[] audioBuffer.left;
            delete[] audioBuffer.right;
        }
    }

    void audioDeviceAboutToStart(size_t maxAudioSamples) {
        this->maxAudioSamples.emplace(maxAudioSamples);
        for (AudioBuffer &audioBuffer : this->audioBuffers) {
            delete[] audioBuffer.left;
            delete[] audioBuffer.right;
            audioBuffer.left = new float[maxAudioSamples];
            audioBuffer.right = new float[maxAudioSamples];
            audioBuffer.numSamples = maxAudioSamples;
        }
    }

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<Node, T>>, typename ...Args>
    NodeId spawn(Args &&...args) {
        NodeId id = this->nextNodeId++;
        this->nodes[id] = std::make_unique<T>(std::forward<Args>(args)...);
        size_t sinkNodePort = 0;
        for (size_t i = 0; i < this->nodes[id]->getOutputs().size(); i++) {
            PortValue &output = this->nodes[id]->getOutputs()[i];
            if (output.getType() == DataType::AudioBuffer) {
                AudioBuffer &audioBuffer = this->audioBuffers.emplace_back(this->maxAudioSamples.has_value() ?
                AudioBuffer {
                    .left = new float[this->maxAudioSamples.value()],
                    .right = new float[this->maxAudioSamples.value()],
                    .numSamples = this->maxAudioSamples.value()
                } :
                    AudioBuffer {
                        .left = nullptr,
                        .right = nullptr,
                        .numSamples = 0
                    }
                );
                output.setAudioBufferValue(audioBuffer);
                if (sinkNodePort == 0) { sinkNodePort = i + 1; }
            }
        }
        if (sinkNodePort > 0) { this->sinkNodesPorts.emplace_back(id, sinkNodePort - 1); }
        this->executionOrder.push_back(id); // FIXME: Just a placeholder, until a proper scheduling algorithm will be implemented.
        return id;
    }

    bool connect(NodeId fromNode, PortId fromPort, NodeId toNode, PortId toPort) {
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator fromIt = this->nodes.find(fromNode);
        if (fromIt == this->nodes.end()) { return false; }
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator toIt = this->nodes.find(toNode);
        if (toIt == this->nodes.end()) { return false; }

        const std::vector<PortValue> &fromOutputs = fromIt->second->getOutputs();
        if (fromPort >= fromOutputs.size()) { return false; }
        const std::vector<PortValue> &toInputs = toIt->second->getInputs();
        if (toPort >= toInputs.size()) { return false; }
        if (fromOutputs[fromPort].getType() != toInputs[toPort].getType()) { return false; }

        this->connections.emplace_back(SignalConnection {
            .fromNode = fromNode,
            .fromPort = fromPort,
            .toNode = toNode,
            .toPort = toPort,
        });

        // If the from node was a sink node and it's first audio buffer (always the sink one by default) is now forwarded to the next node, then it should be removed from the sink list.
        this->sinkNodesPorts.erase(std::remove_if(this->sinkNodesPorts.begin(), this->sinkNodesPorts.end(), [fromNode, fromPort](const std::pair<NodeId, PortId> &sinkNodePort) {
            return sinkNodePort.first == fromNode && sinkNodePort.second == fromPort;
        }), this->sinkNodesPorts.end());

        return true;
    }

    void process(float *const *buffer, int numChannels, int numSamples, double sampleRate) {
        const ProcessContext context = {
            .numSamples = static_cast<size_t>(numSamples),
            .sampleRate = sampleRate,
        };
        for (size_t channel = 0; channel < static_cast<size_t>(numChannels); channel++) {
            std::fill(buffer[channel], buffer[channel] + static_cast<size_t>(numSamples), 0.0f);
        }

        for (const NodeId nodeId : this->executionOrder) {
            Node &node = *this->nodes.at(nodeId); // FIXME: Potentially unsafe.
            for (const SignalConnection &connection : this->connections) {
                if (connection.toNode != nodeId) { continue; }
                Node &fromNode = *this->nodes.at(connection.fromNode); // FIXME: Potentially unsafe.
                const PortValue &fromValue = fromNode.getOutputs()[connection.fromPort];
                node.getInputs()[connection.toPort].setAnyValue(fromValue); // FIXME: Potentially unsafe.
            }
            node.process(context); // FIXME: Potentially unsafe.
        }
        for (const auto &[sinkNodeId, sinkPortId] : this->sinkNodesPorts) {
            Node &node = *this->nodes.at(sinkNodeId); // FIXME: Potentially unsafe.
            const AudioBuffer *audioBuffer = node.getOutputs()[sinkPortId].getValue().audioBufferValue;
            if (
                audioBuffer == nullptr ||
                audioBuffer->left == nullptr || audioBuffer->right == nullptr ||
                audioBuffer->numSamples == 0 || context.numSamples > audioBuffer->numSamples
            ) { continue; }

            for (size_t i = 0; i < static_cast<size_t>(numSamples); i++) {
                buffer[0][i] += audioBuffer->left[i];
                if (numChannels > 1) {
                    buffer[1][i] += audioBuffer->right[i];
                }
            }
        }
        for (size_t channel = 0; channel < static_cast<size_t>(numChannels); channel++) {
            for (size_t i = 0; i < static_cast<size_t>(numSamples); i++) {
                if (!std::isfinite(buffer[channel][i])) {
                    buffer[channel][i] = 0.0f;
                }
                buffer[channel][i] = std::clamp(buffer[channel][i], -World::DEBUG_MAX_VOLUME, World::DEBUG_MAX_VOLUME);
            }
        }
    }

    std::optional<std::reference_wrapper<Node>> getNode(NodeId id) {
        std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator it = this->nodes.find(id);
        if (it != this->nodes.end()) { return std::ref(*(it->second)); }
        return std::nullopt;
    }
};

enum class OscillatorShape : uint8_t {
    Sine,
    Square,
    Triangle,
    Sawtooth,
};
class OscillatorNode : public Node {
private:
    float phase;
public:
    constexpr static PortId FREQUENCY_INPUT_ID = 0;
    constexpr static PortId AMPLITUDE_INPUT_ID = 1;
    constexpr static PortId SHAPE_INPUT_ID = 2;
    constexpr static PortId BUFFER_OUTPUT_ID = 0;

    OscillatorNode() : Node(), phase(0.0f) {
        this->inputs.push_back(PortValue::floatValue(440.0f)); // Frequency
        this->inputs.push_back(PortValue::floatValue(1.0f)); // Amplitude
        this->inputs.push_back(PortValue::enumValue(static_cast<uint8_t>(OscillatorShape::Sine))); // Shape
        this->outputs.push_back(PortValue::audioBufferValue());
    }
    ~OscillatorNode() override = default;
    void process(const ProcessContext &context) override {
        const AudioBuffer *outputBuffer = this->outputs[OscillatorNode::BUFFER_OUTPUT_ID].getValue().audioBufferValue;
        if (
            outputBuffer == nullptr ||
            outputBuffer->left == nullptr || outputBuffer->right == nullptr ||
            outputBuffer->numSamples == 0 || context.numSamples > outputBuffer->numSamples
        ) { return; }

        const float phaseDelta = juce::MathConstants<double>::twoPi * this->inputs[OscillatorNode::FREQUENCY_INPUT_ID].getValue().floatValue / context.sampleRate;
        for (int j = 0; j < context.numSamples; j++) {
            float sample = 0.0f;
            switch (static_cast<OscillatorShape>(this->inputs[OscillatorNode::SHAPE_INPUT_ID].getValue().enumValue)) {
                case OscillatorShape::Sine:
                    sample = std::sin(this->phase);
                    break;
                case OscillatorShape::Square:
                    sample = (std::sin(this->phase) >= 0.0f ? 1.0f : -1.0f);
                    break;
                case OscillatorShape::Triangle:
                    sample = (2.0f / juce::MathConstants<float>::pi) * std::asin(std::sin(this->phase));
                    break;
                case OscillatorShape::Sawtooth:
                    sample = (2.0f * (this->phase / juce::MathConstants<float>::twoPi) - 1.0f);
                    break;
            }
            sample *= this->inputs[OscillatorNode::AMPLITUDE_INPUT_ID].getValue().floatValue;
            this->phase += phaseDelta;
            if (this->phase >= juce::MathConstants<float>::twoPi) {
                this->phase -= juce::MathConstants<float>::twoPi;
            }
            outputBuffer->left[j] = sample;
            outputBuffer->right[j] = sample;
        }
    }

    void setFrequency(float frequency) { this->inputs[FREQUENCY_INPUT_ID].setFloatValue(frequency); }
    void setAmplitude(float amplitude) { this->inputs[AMPLITUDE_INPUT_ID].setFloatValue(amplitude); }
    void setShape(OscillatorShape shape) { this->inputs[SHAPE_INPUT_ID].setEnumValue(static_cast<uint8_t>(shape)); }
};
class GainNode : public Node {
public:
    constexpr static PortId BUFFER_INPUT_ID = 0;
    constexpr static PortId GAIN_INPUT_ID = 1;
    constexpr static PortId BUFFER_OUTPUT_ID = 0;

    GainNode() : Node() {
        this->inputs.push_back(PortValue::audioBufferValue()); // Input
        this->inputs.push_back(PortValue::floatValue(1.0f)); // Gain
        this->outputs.push_back(PortValue::audioBufferValue());
    }
    ~GainNode() override = default;
    void process(const ProcessContext &context) override {
        const AudioBuffer *inputBuffer = this->inputs[GainNode::BUFFER_INPUT_ID].getValue().audioBufferValue;
        const AudioBuffer *outputBuffer = this->outputs[GainNode::BUFFER_OUTPUT_ID].getValue().audioBufferValue;
        if (
            inputBuffer == nullptr || inputBuffer->left == nullptr || inputBuffer->right == nullptr ||
            outputBuffer == nullptr || outputBuffer->left == nullptr || outputBuffer->right == nullptr ||
            inputBuffer->numSamples == 0 || outputBuffer->numSamples == 0 ||
            context.numSamples > inputBuffer->numSamples || context.numSamples > outputBuffer->numSamples
        ) { return; }

        for (int j = 0; j < context.numSamples; j++) {
            outputBuffer->left[j] = inputBuffer->left[j] * this->inputs[GainNode::GAIN_INPUT_ID].getValue().floatValue;
            outputBuffer->right[j] = inputBuffer->right[j] * this->inputs[GainNode::GAIN_INPUT_ID].getValue().floatValue;
        }
    }
    void setGain(float gain) { this->inputs[GainNode::GAIN_INPUT_ID].setFloatValue(gain); }
};