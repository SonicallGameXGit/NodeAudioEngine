#pragma once
#include <juce_dsp/juce_dsp.h>

enum class DataType : uint8_t {
    AudioBuffer,
    Float,
    Enum,
};
struct AudioBuffer {
    float *left, *right;
    size_t numSamples;

    bool isValid(size_t numUsedSamples) const { return this->left != nullptr && this->right != nullptr && this->numSamples >= numUsedSamples; }
};
union DataValue {
    AudioBuffer *audioBufferValue;
    float floatValue;
    uint8_t enumValue;
};
// FIXME: That's the perfect time to start using std::variant!
// FIXME: BRO, SPLIT THE PORT VALUE INTO InputPortSpecs AND OutputPortSpecs AND USE THEM ONCE IN THE NODE TYPE SETUP AND BASED ON THEM JUST GENERATE THE PortValue, THAT'S IT!
struct PortValue {
private:
    DataType type;
    DataValue value, defaultValue;
    bool required, valueSet;
    PortValue(DataType type, const DataValue &defaultValue, bool required) : type(type), value(), defaultValue(defaultValue), required(required), valueSet(false) {}
public:
    ~PortValue() = default;

    static PortValue audioBufferValue() {
        return PortValue(DataType::AudioBuffer, DataValue {
            .audioBufferValue = nullptr,
        }, true); // AudioBuffer ports are always required, because there's no intuitive default value for them.
    }
    static PortValue floatValue(float defaultValue, bool required) {
        return PortValue(DataType::Float, DataValue { .floatValue = defaultValue }, required);
    }
    static PortValue enumValue(uint8_t defaultValue, bool required) {
        return PortValue(DataType::Enum, DataValue { .enumValue = defaultValue }, required);
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

    bool isRequired() const { return this->required; }
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

struct RegularResult {
    bool success;
    std::string errorMessage;
};
struct TopologicalOrderComputeResult {
    std::vector<NodeId> order;
    bool valid;
    std::string errorMessage;
};
struct GraphState {
    struct UnsatisfiedNode {
        enum class PortAt : uint8_t { Input, Output };
        NodeId nodeId;
        std::optional<PortId> portId;
        PortAt portAt;
    };
    bool valid;
    std::string errorMessage;
    std::optional<UnsatisfiedNode> unsatisfiedNode;

    static GraphState validState() {
        return GraphState {
            .valid = true,
            .errorMessage = std::string(),
            .unsatisfiedNode = std::nullopt,
        };
    }
    static GraphState invalidState(const std::string &errorMessage, const std::optional<UnsatisfiedNode> &unsatisfiedNode) {
        return GraphState {
            .valid = false,
            .errorMessage = errorMessage,
            .unsatisfiedNode = unsatisfiedNode,
        };
    }
};

struct World {
private:
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes;
    std::vector<SignalConnection> connections;
    std::vector<OutputConnection> outputConnections;
    std::deque<std::pair<NodeId, AudioBuffer>> audioBuffers;
    std::optional<std::vector<NodeId>> cachedTopologicalOrder;
    GraphState cachedGraphState = GraphState::validState();

    std::optional<size_t> maxAudioSamples;
    NodeId nextNodeId;

    // FIXME: It counts literally all nodes, even the ones that are not affecting the output and shouldn't be processed at all.
    //  - Find a way to either use it inside the computeTopologicalOrder function call, or based on it's results, to ensure only the used nodes are processed.
    std::optional<GraphState::UnsatisfiedNode> findFirstUnsatisfiedRequiredInput() const {
        for (const auto &[nodeId, node] : this->nodes) {
            const std::vector<PortValue> &inputs = node->getInputs();
            for (PortId i = 0; i < static_cast<PortId>(inputs.size()); i++) {
                if (!inputs[i].isRequired()) { continue; }

                if (!std::any_of(this->connections.begin(), this->connections.end(), [&](const SignalConnection &connection) {
                    return connection.toNode == nodeId && connection.toPort == i;
                })) { return GraphState::UnsatisfiedNode { .nodeId = nodeId, .portId = i, .portAt = GraphState::UnsatisfiedNode::PortAt::Input }; }
            }
        }
        return std::nullopt;
    }

    // Kahn's algorithm
    TopologicalOrderComputeResult computeTopologicalOrder() const {
        // Map each node to it's in-degree.
        std::unordered_map<NodeId, size_t> inDegree = std::unordered_map<NodeId, size_t>();
        for (const auto &[nodeId, _] : this->nodes) { inDegree[nodeId] = 0; }
        // Build adjacency list and calculate in-degrees.
        std::unordered_map<NodeId, std::vector<NodeId>> adjacency = std::unordered_map<NodeId, std::vector<NodeId>>();
        for (const SignalConnection &connection : this->connections) {
            adjacency[connection.fromNode].push_back(connection.toNode);
            inDegree[connection.toNode]++;
        }

        // Initialize queue with only nodes that have in-degree of 0 (nodes with no dependencies).
        std::deque<NodeId> queue = std::deque<NodeId>();
        for (const auto &[nodeId, degree] : inDegree) {
            if (degree == 0) { queue.push_back(nodeId); }
        }

        // Iterate trough each node in the queue, add it to the result and decrease in-degree of it's neighbors (if any of the neighbors has in-degree of 0, add it to the queue).
        std::vector<NodeId> result = std::vector<NodeId>();
        while (!queue.empty()) {
            NodeId nodeId = queue.front();
            queue.pop_front();
            result.push_back(nodeId);
            for (NodeId neighbor : adjacency[nodeId]) {
                inDegree[neighbor]--;
                if (inDegree[neighbor] == 0) {
                    queue.push_back(neighbor);
                }
            }
        }

        // Nodes get to result only if they have in-degree of 0, but if there's a loop - they'll never have in-degree of 0 and in the result list there'd be less nodes than in the world.
        if (result.size() != this->nodes.size()) {
            return TopologicalOrderComputeResult { .order = std::vector<NodeId>(), .valid = false, .errorMessage = "Cycle detected in the graph" };
        }
        return TopologicalOrderComputeResult { .order = result, .valid = true, .errorMessage = std::string() };
    }
public:
    constexpr static float DEBUG_MAX_VOLUME = 1.0f;

    World() : nodes(), connections(), outputConnections(), audioBuffers(), cachedTopologicalOrder(std::nullopt), maxAudioSamples(std::nullopt), nextNodeId(1) {}
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
        if (!this->cachedTopologicalOrder.has_value()) {
            this->cachedTopologicalOrder.emplace(std::vector<NodeId> { id });
        } else { this->cachedTopologicalOrder->push_back(id); }
        if (this->cachedGraphState.valid) {
            std::optional<GraphState::UnsatisfiedNode> unsatisfiedNode = this->findFirstUnsatisfiedRequiredInput();
            if (unsatisfiedNode.has_value()) {
                this->cachedGraphState = GraphState::invalidState("Node has unsatisfied required inputs", unsatisfiedNode);
            }
        }
        return id;
    }
    RegularResult destroy(NodeId id) {
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator nodeIt = this->nodes.find(id);
        if (nodeIt == this->nodes.end()) {
            return RegularResult { .success = false, .errorMessage = "Node not found" };
        }

        std::vector<SignalConnection>::iterator removeIt = std::remove_if(this->connections.begin(), this->connections.end(), [id](const SignalConnection &connection) {
            return connection.fromNode == id || connection.toNode == id;
        });
        for (std::vector<SignalConnection>::iterator it = removeIt; it != this->connections.end(); ++it) {
            std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator toIt = this->nodes.find(it->toNode);
            if (toIt != this->nodes.end() && it->toPort < toIt->second->getInputs().size()) {
                toIt->second->getInputs()[it->toPort].removeValue();
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

        this->nodes.erase(nodeIt);
        TopologicalOrderComputeResult newTopologicalOrder = this->computeTopologicalOrder();
        if (!newTopologicalOrder.valid) {
            this->cachedTopologicalOrder.reset();
            this->cachedGraphState = GraphState::invalidState(newTopologicalOrder.errorMessage, std::nullopt);
            return RegularResult { .success = false, .errorMessage = newTopologicalOrder.errorMessage };
        }
        this->cachedTopologicalOrder.emplace(std::move(newTopologicalOrder.order));
        std::optional<GraphState::UnsatisfiedNode> unsatisfiedNode = this->findFirstUnsatisfiedRequiredInput();
        if (unsatisfiedNode.has_value()) {
            this->cachedGraphState = GraphState::invalidState("Node has unsatisfied required inputs", unsatisfiedNode);
            return RegularResult { .success = false, .errorMessage = "Node has unsatisfied required inputs" };
        }
        this->cachedGraphState = GraphState::validState();
        return RegularResult { .success = true, .errorMessage = std::string() };
    }

    RegularResult connect(NodeId fromNode, PortId fromPort, NodeId toNode, PortId toPort) {
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator fromIt = this->nodes.find(fromNode);
        if (fromIt == this->nodes.end()) {
            return RegularResult { .success = false, .errorMessage = "From node not found" };
        }
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator toIt = this->nodes.find(toNode);
        if (toIt == this->nodes.end()) {
            return RegularResult { .success = false, .errorMessage = "To node not found" };
        }

        const std::vector<PortValue> &fromOutputs = fromIt->second->getOutputs();
        if (fromPort >= fromOutputs.size()) {
            return RegularResult { .success = false, .errorMessage = "From port not found" };
        }
        const std::vector<PortValue> &toInputs = toIt->second->getInputs();
        if (toPort >= toInputs.size()) {
            return RegularResult { .success = false, .errorMessage = "To port not found" };
        }
        if (fromOutputs[fromPort].getType() != toInputs[toPort].getType()) {
            return RegularResult { .success = false, .errorMessage = "Port types do not match" };
        }

        if (std::find_if(this->connections.begin(), this->connections.end(), [&](const SignalConnection &connection) {
            return connection.toNode == toNode && connection.toPort == toPort;
        }) != this->connections.end()) {
            return RegularResult { .success = false, .errorMessage = "Port is already connected to something else" };
        }

        this->connections.emplace_back(SignalConnection {
            .fromNode = fromNode,
            .fromPort = fromPort,
            .toNode = toNode,
            .toPort = toPort,
        });

        TopologicalOrderComputeResult newTopologicalOrder = this->computeTopologicalOrder();
        if (!newTopologicalOrder.valid) {
            this->connections.pop_back();
            return RegularResult { .success = false, .errorMessage = "Connection would create an invalid graph" };
        }
        this->cachedTopologicalOrder.emplace(std::move(newTopologicalOrder.order));
        std::optional<GraphState::UnsatisfiedNode> unsatisfiedNode = this->findFirstUnsatisfiedRequiredInput();
        if (unsatisfiedNode.has_value()) {
            this->cachedGraphState = GraphState::invalidState("Node has unsatisfied required inputs", unsatisfiedNode);
            return RegularResult { .success = false, .errorMessage = "Node has unsatisfied required inputs" };
        }
        this->cachedGraphState = GraphState::validState();
        return RegularResult { .success = true, .errorMessage = std::string() };
    }
    RegularResult disconnect(NodeId fromNode, PortId fromPort, NodeId toNode, PortId toPort) {
        // Here could be ton of checks, like in the connect function, but who cares, when we just disconnect nodes' ports? No matter if they exist. Nothing is being allocated and if they really don't exist, there's no way there'd be a connection.
        const std::vector<SignalConnection>::iterator connectionIt = std::find_if(this->connections.begin(), this->connections.end(), [&](const SignalConnection &connection) {
            return connection.fromNode == fromNode && connection.fromPort == fromPort && connection.toNode == toNode && connection.toPort == toPort;
        });
        if (connectionIt == this->connections.end()) {
            return RegularResult { .success = false, .errorMessage = "Connection not found" };
        }

        std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator toIt = this->nodes.find(toNode);
        if (toIt != this->nodes.end() && toPort < toIt->second->getInputs().size()) {
            toIt->second->getInputs()[toPort].removeValue();
        }

        this->connections.erase(connectionIt);
        TopologicalOrderComputeResult newTopologicalOrder = this->computeTopologicalOrder();
        if (!newTopologicalOrder.valid) {
            this->cachedGraphState = GraphState::invalidState("Graph is in invalid state after disconnection", std::nullopt);
            return RegularResult { .success = false, .errorMessage = "Graph is in invalid state after disconnection" };
        }
        this->cachedTopologicalOrder.emplace(std::move(newTopologicalOrder.order));
        std::optional<GraphState::UnsatisfiedNode> unsatisfiedNode = this->findFirstUnsatisfiedRequiredInput();
        if (unsatisfiedNode.has_value()) {
            this->cachedGraphState = GraphState::invalidState("Node has unsatisfied required inputs", unsatisfiedNode);
            return RegularResult { .success = false, .errorMessage = "Node has unsatisfied required inputs" };
        }
        this->cachedGraphState = GraphState::validState();
        return RegularResult { .success = true, .errorMessage = std::string() };
    }
    RegularResult connectToOutput(NodeId fromNode, PortId fromPort) {
        const std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator fromIt = this->nodes.find(fromNode);
        if (fromIt == this->nodes.end()) {
            return RegularResult { .success = false, .errorMessage = "From node not found" };
        }
        const std::vector<PortValue> &fromOutputs = fromIt->second->getOutputs();
        if (fromPort >= fromOutputs.size()) {
            return RegularResult { .success = false, .errorMessage = "From port not found" };
        }
        if (fromOutputs[fromPort].getType() != DataType::AudioBuffer) {
            return RegularResult { .success = false, .errorMessage = "Invalid port type" };
        }
        if (std::find_if(this->outputConnections.begin(), this->outputConnections.end(), [&](const OutputConnection &connection) {
            return connection.nodeId == fromNode && connection.portId == fromPort;
        }) != this->outputConnections.end()) {
            return RegularResult { .success = false, .errorMessage = "Port is already connected to output" }; // Port is already connected to output.
        }

        this->outputConnections.emplace_back(OutputConnection {
            .nodeId = fromNode,
            .portId = fromPort,
        });
        return RegularResult { .success = true, .errorMessage = std::string() };
    }
    RegularResult disconnectFromOutput(NodeId fromNode, PortId fromPort) {
        const std::vector<OutputConnection>::iterator connectionIt = std::find_if(this->outputConnections.begin(), this->outputConnections.end(), [&](const OutputConnection &connection) {
            return connection.nodeId == fromNode && connection.portId == fromPort;
        });
        if (connectionIt == this->outputConnections.end()) {
            return RegularResult { .success = false, .errorMessage = "Connection not found" };
        }
        this->outputConnections.erase(connectionIt);
        return RegularResult { .success = true, .errorMessage = std::string() };
    }

    void process(float *const *buffer, int numChannels, int numSamples, double sampleRate) {
        const ProcessContext context = {
            .numSamples = static_cast<size_t>(numSamples),
            .sampleRate = sampleRate,
        };
        for (size_t channel = 0; channel < static_cast<size_t>(numChannels); channel++) {
            std::fill(buffer[channel], buffer[channel] + static_cast<size_t>(numSamples), 0.0f);
        }

        if (!this->cachedTopologicalOrder.has_value()) { return; }
        if (!this->cachedGraphState.valid) { return; }
        for (const NodeId fromNodeId : this->cachedTopologicalOrder.value()) {
            // Trallalello
            if (this->nodes.find(fromNodeId) == this->nodes.end()) { continue; }
            Node &fromNode = *this->nodes.at(fromNodeId);
            fromNode.process(context);

            for (const SignalConnection &connection : this->connections) {
                // Trallalla
                if (connection.fromNode != fromNodeId) { continue; }
                // Porco Dio e
                if (fromNode.getOutputs().size() <= connection.fromPort) { continue; }

                // Porco Allah
                if (this->nodes.find(connection.toNode) == this->nodes.end()) { continue; }
                Node &toNode = *this->nodes.at(connection.toNode);

                // Ero con il mio fottuto figlio merdardo a giocare a Fortnite
                if (toNode.getInputs().size() <= connection.toPort) { continue; }
                toNode.getInputs()[connection.toPort].setAnyValue(fromNode.getOutputs()[connection.fromPort]);
            }
        }
        for (const OutputConnection &outputConnection : this->outputConnections) {
            if (this->nodes.find(outputConnection.nodeId) == this->nodes.end()) { continue; }
            Node &node = *this->nodes.at(outputConnection.nodeId);

            if (node.getOutputs().size() <= outputConnection.portId) { continue; }
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
    const GraphState &getGraphState() const { return this->cachedGraphState; }
};

enum class OscillatorShape : uint8_t {
    Sine,
    Square,
    HalfSquare,
    Triangle,
    Sawtooth,
};
class OscillatorNode : public Node {
private:
    float phase;
public:
    constexpr static PortId FREQUENCY_INPUT_ID = 0;
    constexpr static PortId AMPLITUDE_INPUT_ID = 1;
    constexpr static PortId SHAPE_INPUT_ID     = 2;
    constexpr static PortId BUFFER_OUTPUT_ID   = 0;

    OscillatorNode() : Node(), phase(0.0f) {
        this->inputs.push_back(PortValue::floatValue(440.0f, false));                                     // Frequency
        this->inputs.push_back(PortValue::floatValue(1.0f, false));                                       // Amplitude
        this->inputs.push_back(PortValue::enumValue(static_cast<uint8_t>(OscillatorShape::Sine), false)); // Shape
        this->outputs.push_back(PortValue::audioBufferValue());                                           // Output
    }
    ~OscillatorNode() override = default;
    void process(const ProcessContext &context) override {
        const AudioBuffer *outputBuffer = this->outputs[OscillatorNode::BUFFER_OUTPUT_ID].getValue().audioBufferValue;
        if (outputBuffer == nullptr || !outputBuffer->isValid(context.numSamples)) { return; }

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
                case OscillatorShape::HalfSquare:
                    sample = (this->phase < juce::MathConstants<float>::pi * 0.5f ? 1.0f : -1.0f);
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

    void setFrequency(float frequency) { this->inputs[OscillatorNode::FREQUENCY_INPUT_ID].setFloatValue(frequency); }
    void setAmplitude(float amplitude) { this->inputs[OscillatorNode::AMPLITUDE_INPUT_ID].setFloatValue(amplitude); }
    void setShape(OscillatorShape shape) { this->inputs[OscillatorNode::SHAPE_INPUT_ID].setEnumValue(static_cast<uint8_t>(shape)); }
};
class GainNode : public Node {
public:
    constexpr static PortId BUFFER_INPUT_ID  = 0;
    constexpr static PortId GAIN_INPUT_ID    = 1;
    constexpr static PortId BUFFER_OUTPUT_ID = 0;

    GainNode() : Node() {
        this->inputs.push_back(PortValue::audioBufferValue());      // Input
        this->inputs.push_back(PortValue::floatValue(1.0f, false)); // Gain
        this->outputs.push_back(PortValue::audioBufferValue());     // Output
    }
    ~GainNode() override = default;
    void process(const ProcessContext &context) override {
        const AudioBuffer *inputBuffer = this->inputs[GainNode::BUFFER_INPUT_ID].getValue().audioBufferValue;
        const AudioBuffer *outputBuffer = this->outputs[GainNode::BUFFER_OUTPUT_ID].getValue().audioBufferValue;
        if (
            inputBuffer == nullptr || outputBuffer == nullptr ||
            !inputBuffer->isValid(context.numSamples) || !outputBuffer->isValid(context.numSamples)
        ) { return; }

        for (int j = 0; j < context.numSamples; j++) {
            outputBuffer->left[j] = inputBuffer->left[j] * this->inputs[GainNode::GAIN_INPUT_ID].getValue().floatValue;
            outputBuffer->right[j] = inputBuffer->right[j] * this->inputs[GainNode::GAIN_INPUT_ID].getValue().floatValue;
        }
    }
    void setGain(float gain) { this->inputs[GainNode::GAIN_INPUT_ID].setFloatValue(gain); }
};
class DelayNode : public Node {
private:
    juce::dsp::DelayLine<float> delayLine;
    bool prepared;
public:
    constexpr static PortId BUFFER_INPUT_ID   = 0;
    constexpr static PortId TIME_INPUT_ID     = 1;
    constexpr static PortId FEEDBACK_INPUT_ID = 2;
    constexpr static PortId MIX_INPUT_ID      = 3;
    constexpr static PortId BUFFER_OUTPUT_ID  = 0;

    DelayNode() : Node(), delayLine(), prepared(false) {
        this->inputs.push_back(PortValue::audioBufferValue());      // Input
        this->inputs.push_back(PortValue::floatValue(0.5f, false)); // Time (seconds)
        this->inputs.push_back(PortValue::floatValue(0.3f, false)); // Feedback
        this->inputs.push_back(PortValue::floatValue(0.5f, false)); // Mix
        this->outputs.push_back(PortValue::audioBufferValue());     // Output
    }
    ~DelayNode() override = default;

    void process(const ProcessContext &context) override {
        const AudioBuffer *inputBuffer = this->inputs[DelayNode::BUFFER_INPUT_ID].getValue().audioBufferValue;
        const AudioBuffer *outputBuffer = this->outputs[DelayNode::BUFFER_OUTPUT_ID].getValue().audioBufferValue;
        if (
            inputBuffer == nullptr || outputBuffer == nullptr ||
            !inputBuffer->isValid(context.numSamples) || !outputBuffer->isValid(context.numSamples)
        ) { return; }

        // Lazy prepare: first call sets up the delay line with max 2s delay
        if (!this->prepared) {
            this->delayLine.prepare(juce::dsp::ProcessSpec {
                .sampleRate = context.sampleRate,
                .maximumBlockSize = static_cast<juce::uint32>(context.numSamples),
                .numChannels = 2,
            });
            this->delayLine.setMaximumDelayInSamples(static_cast<int>(context.sampleRate * 2.0));
            this->delayLine.reset();
            this->prepared = true;
        }

        const float rawDelay = this->inputs[DelayNode::TIME_INPUT_ID].getValue().floatValue;
        const float delaySamples = std::clamp(
            rawDelay * static_cast<float>(context.sampleRate), 1.0f,
            static_cast<float>(this->delayLine.getMaximumDelayInSamples())
        );
        const float feedback = std::clamp(this->inputs[DelayNode::FEEDBACK_INPUT_ID].getValue().floatValue, 0.0f, 1.0f);
        const float mix = std::clamp(this->inputs[DelayNode::MIX_INPUT_ID].getValue().floatValue, 0.0f, 1.0f);

        for (size_t i = 0; i < context.numSamples; i++) {
            const float dryL = inputBuffer->left[i];
            const float dryR = inputBuffer->right[i];

            // Read from delay line
            float delayedL = this->delayLine.popSample(0, delaySamples);
            float delayedR = this->delayLine.popSample(1, delaySamples);

            // Push dry + feedback into the delay line
            this->delayLine.pushSample(0, dryL + delayedL * feedback);
            this->delayLine.pushSample(1, dryR + delayedR * feedback);

            // Wet/dry mix
            outputBuffer->left[i] = dryL * (1.0f - mix) + delayedL * mix;
            outputBuffer->right[i] = dryR * (1.0f - mix) + delayedR * mix;
        }
    }

    void setTime(float time) { this->inputs[DelayNode::TIME_INPUT_ID].setFloatValue(time); }
    void setFeedback(float feedback) { this->inputs[DelayNode::FEEDBACK_INPUT_ID].setFloatValue(feedback); }
    void setMix(float mix) { this->inputs[DelayNode::MIX_INPUT_ID].setFloatValue(mix); }
};
class ReverbNode : public Node {
private:
    juce::Reverb reverb;
    juce::Reverb::Parameters lastParams;
    bool prepared;
public:
    constexpr static PortId BUFFER_INPUT_ID    = 0;
    constexpr static PortId ROOM_SIZE_INPUT_ID = 1;
    constexpr static PortId DAMPING_INPUT_ID   = 2;
    constexpr static PortId MIX_INPUT_ID       = 3;
    constexpr static PortId BUFFER_OUTPUT_ID   = 0;

    ReverbNode() : Node(), reverb(), lastParams(), prepared(false) {
        this->inputs.push_back(PortValue::audioBufferValue());       // Input
        this->inputs.push_back(PortValue::floatValue(0.5f, false));  // Room Size
        this->inputs.push_back(PortValue::floatValue(0.5f, false));  // Damping
        this->inputs.push_back(PortValue::floatValue(0.33f, false)); // Mix
        this->outputs.push_back(PortValue::audioBufferValue());      // Output

        this->lastParams.roomSize  = 0.5f;
        this->lastParams.damping   = 0.5f;
        this->lastParams.wetLevel  = 0.5f;
        this->lastParams.dryLevel  = 0.5f;
        this->lastParams.width     = 1.0f;
        this->lastParams.freezeMode = 0.0f;
    }
    ~ReverbNode() override = default;

    void process(const ProcessContext &context) override {
        const AudioBuffer *inputBuffer = this->inputs[ReverbNode::BUFFER_INPUT_ID].getValue().audioBufferValue;
        const AudioBuffer *outputBuffer = this->outputs[ReverbNode::BUFFER_OUTPUT_ID].getValue().audioBufferValue;
        if (
            inputBuffer == nullptr || outputBuffer == nullptr ||
            !inputBuffer->isValid(context.numSamples) || !outputBuffer->isValid(context.numSamples)
        ) { return; }

        if (!this->prepared) {
            this->reverb.setSampleRate(context.sampleRate);
            this->reverb.reset();
            this->prepared = true;
        }

        // Copy input to output first (processStereo reads & writes in-place)
        std::copy(inputBuffer->left, inputBuffer->left + context.numSamples, outputBuffer->left);
        std::copy(inputBuffer->right, inputBuffer->right + context.numSamples, outputBuffer->right);

        // Update params if changed
        juce::Reverb::Parameters params;
        params.roomSize = std::clamp(this->inputs[ReverbNode::ROOM_SIZE_INPUT_ID].getValue().floatValue, 0.0f, 1.0f);
        params.damping = std::clamp(this->inputs[ReverbNode::DAMPING_INPUT_ID].getValue().floatValue, 0.0f, 1.0f);
        const float mix = std::clamp(this->inputs[ReverbNode::MIX_INPUT_ID].getValue().floatValue, 0.0f, 1.0f);
        params.wetLevel = mix;
        params.dryLevel = 1.0f - mix;
        params.width = 1.0f;
        params.freezeMode = 0.0f;

        if (std::memcmp(&params, &this->lastParams, sizeof(params)) != 0) {
            this->reverb.setParameters(params);
            this->lastParams = params;
        }
        this->reverb.processStereo(outputBuffer->left, outputBuffer->right, static_cast<int>(context.numSamples));
    }
    void setRoomSize(float size) { this->inputs[ReverbNode::ROOM_SIZE_INPUT_ID].setFloatValue(size); }
    void setDamping(float damping) { this->inputs[ReverbNode::DAMPING_INPUT_ID].setFloatValue(damping); }
    void setMix(float mix) { this->inputs[ReverbNode::MIX_INPUT_ID].setFloatValue(mix); }
};