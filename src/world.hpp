#pragma once

enum class DataType : uint8_t {
    Float,
    Buffer,
    Enum,
};
union DataValue {
    float floatValue;
    float *const *bufferValue;
    uint8_t enumValue;
};
struct PortValue {
    DataType type;
    DataValue value;
};

struct ProcessContext {
    float *const *buffer;
    int numChannels;
    int numSamples;
    double sampleRate;
};

using NodeId = uint32_t;
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
    OscillatorNode() : Node(), phase(0.0f) {
        this->inputs.push_back(PortValue {
            .type = DataType::Float,
            .value = {
                .floatValue = 440.0f,
            },
        }); // Frequency
        this->inputs.push_back(PortValue {
            .type = DataType::Float,
            .value = {
                .floatValue = 0.5f,
            },
        }); // Amplitude
        this->inputs.push_back(PortValue {
            .type = DataType::Enum,
            .value = {
                .enumValue = static_cast<uint8_t>(OscillatorShape::Sine),
            },
        }); // Shape
    }
    void process(const ProcessContext &context) override {
        if (context.buffer == nullptr) { return; }
        if (context.sampleRate <= 0.0) { return; }
        if (context.numChannels <= 0 || context.numSamples <= 0) { return; }

        const float frequency = this->inputs[0].value.floatValue;
        const float amplitude = this->inputs[1].value.floatValue;
        const OscillatorShape shape = static_cast<OscillatorShape>(this->inputs[2].value.enumValue);
        const float phaseDelta = juce::MathConstants<double>::twoPi * frequency / context.sampleRate;

        for (int j = 0; j < context.numSamples; j++) {
            float sample = 0.0f;
            switch (shape) {
                case OscillatorShape::Sine:
                    sample = std::sin(this->phase) * amplitude;
                    break;
                case OscillatorShape::Square:
                    sample = (std::sin(this->phase) >= 0.0f ? 1.0f : -1.0f) * amplitude;
                    break;
                case OscillatorShape::Triangle:
                    sample = (2.0f / juce::MathConstants<float>::pi) * std::asin(std::sin(this->phase)) * amplitude;
                    break;
                case OscillatorShape::Sawtooth:
                    sample = (2.0f * (this->phase / juce::MathConstants<float>::twoPi) - 1.0f) * amplitude;
                    break;
            }
            this->phase += phaseDelta;
            if (this->phase >= juce::MathConstants<float>::twoPi) {
                this->phase -= juce::MathConstants<float>::twoPi;
            }
            for (int i = 0; i < context.numChannels; i++) {
                if (context.buffer[i] != nullptr) {
                    context.buffer[i][j] = sample;
                }
            }
        }
    }

    void setFrequency(float frequency) { this->inputs[0].value.floatValue = frequency; }
    void setAmplitude(float amplitude) { this->inputs[1].value.floatValue = amplitude; }
    void setShape(OscillatorShape shape) { this->inputs[2].value.enumValue = static_cast<uint8_t>(shape); }
};

struct World {
private:
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes;
    NodeId nextNodeId;
public:
    World() : nodes(), nextNodeId(1) {}
    ~World() = default;

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<Node, T>>, typename ...Args>
    NodeId spawn(Args &&...args) {
        NodeId id = this->nextNodeId++;
        this->nodes[id] = std::make_unique<T>(std::forward<Args>(args)...);
        return id;
    }
    void process(const ProcessContext &context) {
        for (const auto &[id, node] : this->nodes) {
            node->process(context);
        }
    }

    std::optional<std::reference_wrapper<Node>> getNode(NodeId id) {
        std::unordered_map<NodeId, std::unique_ptr<Node>>::iterator it = this->nodes.find(id);
        if (it != this->nodes.end()) { return std::ref(*(it->second)); }
        return std::nullopt;
    }
};