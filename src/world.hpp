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
    DataValue value, defaultValue;
    bool valueSet;
    PortValue(DataType type, const DataValue &defaultValue) : type(type), value(), defaultValue(defaultValue), valueSet(false) {}
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
        this->valueSet = true;
    }
    void setFloatValue(float value) {
        this->value.floatValue = value;
        this->valueSet = true;
    }
    void setEnumValue(uint8_t value) {
        this->value.enumValue = value;
        this->valueSet = true;
    }
    void setAnyValue(const PortValue &other) {
        this->type = other.type;
        this->value = other.value;
        this->valueSet = other.valueSet;
    }

    void setDefaultFloatValue(float defaultValue) {
        this->defaultValue.floatValue = defaultValue;
        if (!this->valueSet) {
            this->value.floatValue = defaultValue;
        }
    }
    void setDefaultEnumValue(uint8_t defaultValue) {
        this->defaultValue.enumValue = defaultValue;
        if (!this->valueSet) {
            this->value.enumValue = defaultValue;
        }
    }

    void removeValue() {
        this->valueSet = false;
    }

    DataType getType() const { return this->type; }
    const DataValue &getValue() const { return this->valueSet ? this->value : this->defaultValue; }
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
struct OutputConnection {
    NodeId nodeId;
    PortId portId;
};

struct World {
private:
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes;
    std::vector<SignalConnection> connections;
    std::vector<OutputConnection> outputConnections;
    std::vector<NodeId> executionOrder; // FIXME: Just a placeholder, until a proper scheduling algorithm will be implemented.
    std::deque<std::pair<NodeId, AudioBuffer>> audioBuffers;

    std::optional<size_t> maxAudioSamples;
    NodeId nextNodeId;
public:
    constexpr static float DEBUG_MAX_VOLUME = 1.0f;

    World() : nodes(), connections(), outputConnections(), executionOrder(), maxAudioSamples(std::nullopt), nextNodeId(1) {}
    ~World() {
        for (const auto &[_, audioBuffer] : this->audioBuffers) {
            delete[] audioBuffer.left;
            delete[] audioBuffer.right;
        }
    }

    void audioDeviceAboutToStart(size_t maxAudioSamples) {
        this->maxAudioSamples.emplace(maxAudioSamples);
        for (auto &[_, audioBuffer] : this->audioBuffers) {
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
        for (size_t i = 0; i < this->nodes[id]->getOutputs().size(); i++) {
            PortValue &output = this->nodes[id]->getOutputs()[i];
            if (output.getType() == DataType::AudioBuffer) {
                std::pair<NodeId, AudioBuffer> &audioBuffer = this->audioBuffers.emplace_back(
                    id,
                    this->maxAudioSamples.has_value() ?
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
                output.setAudioBufferValue(audioBuffer.second);
            }
        }
        this->executionOrder.push_back(id); // FIXME: Just a placeholder, until a proper scheduling algorithm will be implemented.
        return id;
    }
    bool destroy(NodeId id) {
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator nodeIt = this->nodes.find(id);
        if (nodeIt == this->nodes.end()) { return false; }

        std::vector<SignalConnection>::iterator removeIt = std::remove_if(this->connections.begin(), this->connections.end(), [id](const SignalConnection &connection) {
            return connection.fromNode == id || connection.toNode == id;
        });
        for (std::vector<SignalConnection>::iterator it = removeIt; it != this->connections.end(); ++it) {
            std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator toIt = this->nodes.find(it->toNode);
            if (toIt != this->nodes.end() && it->toPort < toIt->second->getInputs().size()) {
                PortValue &input = toIt->second->getInputs()[it->toPort];
                if (input.getType() == DataType::AudioBuffer) {
                    input.removeValue();
                }
            }
        }
        this->connections.erase(removeIt, this->connections.end());
        this->outputConnections.erase(std::remove_if(this->outputConnections.begin(), this->outputConnections.end(), [&](const OutputConnection &connection) {
            return connection.nodeId == id;
        }), this->outputConnections.end());

        this->audioBuffers.erase(std::remove_if(this->audioBuffers.begin(), this->audioBuffers.end(), [&](const std::pair<NodeId, AudioBuffer> &pair) {
            bool remove = pair.first == id;
            if (remove) {
                delete[] pair.second.left;
                delete[] pair.second.right;
            }
            return remove;
        }), this->audioBuffers.end());

        this->executionOrder.erase(std::remove(this->executionOrder.begin(), this->executionOrder.end(), id), this->executionOrder.end());
        this->nodes.erase(nodeIt);
        return true;
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

        if (std::find_if(this->connections.begin(), this->connections.end(), [&](const SignalConnection &connection) {
            return connection.toNode == toNode && connection.toPort == toPort;
        }) != this->connections.end()) {
            return false; // Port is already connected to something else (or possibly this case itself, lol).
        }

        this->connections.emplace_back(SignalConnection {
            .fromNode = fromNode,
            .fromPort = fromPort,
            .toNode = toNode,
            .toPort = toPort,
        });

        return true;
    }
    bool disconnect(NodeId fromNode, PortId fromPort, NodeId toNode, PortId toPort) {
        // Here could be ton of checks, like in the connect function, but who cares, when we just disconnect nodes' ports? No matter if they exist. Nothing is being allocated and if they really don't exist, there's no way there'd be a connection.
        const std::vector<SignalConnection>::iterator connectionIt = std::find_if(this->connections.begin(), this->connections.end(), [&](const SignalConnection &connection) {
            return connection.fromNode == fromNode && connection.fromPort == fromPort && connection.toNode == toNode && connection.toPort == toPort;
        });
        if (connectionIt == this->connections.end()) { return false; }

        std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator toIt = this->nodes.find(toNode);
        if (toIt != this->nodes.end() && toPort < toIt->second->getInputs().size()) {
            toIt->second->getInputs()[toPort].removeValue();
        }

        this->connections.erase(connectionIt);
        return true;
    }
    bool connectToOutput(NodeId fromNode, PortId fromPort) {
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator fromIt = this->nodes.find(fromNode);
        if (fromIt == this->nodes.end()) { return false; }
        const std::vector<PortValue> &fromOutputs = fromIt->second->getOutputs();
        if (fromPort >= fromOutputs.size()) { return false; }
        if (fromOutputs[fromPort].getType() != DataType::AudioBuffer) { return false; }
        if (std::find_if(this->outputConnections.begin(), this->outputConnections.end(), [&](const OutputConnection &connection) {
            return connection.nodeId == fromNode && connection.portId == fromPort;
        }) != this->outputConnections.end()) {
            return false; // Port is already connected to output.
        }

        this->outputConnections.emplace_back(OutputConnection {
            .nodeId = fromNode,
            .portId = fromPort,
        });

        return true;
    }
    bool disconnectFromOutput(NodeId fromNode, PortId fromPort) {
        const std::vector<OutputConnection>::iterator connectionIt = std::find_if(this->outputConnections.begin(), this->outputConnections.end(), [&](const OutputConnection &connection) {
            return connection.nodeId == fromNode && connection.portId == fromPort;
        });
        if (connectionIt == this->outputConnections.end()) { return false; }
        this->outputConnections.erase(connectionIt);
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
        for (const OutputConnection &outputConnection : this->outputConnections) {
            Node &node = *this->nodes.at(outputConnection.nodeId); // FIXME: Potentially unsafe.
            const AudioBuffer *audioBuffer = node.getOutputs()[outputConnection.portId].getValue().audioBufferValue;
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