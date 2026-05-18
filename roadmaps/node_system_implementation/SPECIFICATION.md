# NodeAudioEngine Redesign Plan

## Overview
This document outlines the complete redesign of the NodeAudioEngine system. The goal is to create a flexible, efficient, and scalable node-based audio engine that supports:
1. Arbitrary node graphs with audio, control, and event connections.
2. Validation of required/optional inputs.
3. Pre-allocated buffers for real-time safety.
4. Global MIDI state accessible to all nodes.
5. Automatic summing of all audio chains into a final output.

---

## Core Concepts

### **1. Node**
A `Node` is the basic processing unit in the graph. Each node has:
- **Input ports**: Parameters or data the node consumes.
- **Output ports**: Data the node produces.
- **Specs**: Metadata describing the ports (e.g., type, name, requirement).
- **Values**: Runtime storage for input/output data.

### **2. Port**
Ports define the interface of a node. There are two types:
- **InputPortSpec**: Describes an input port.
- **OutputPortSpec**: Describes an output port.

### **3. SignalConnection**
A `SignalConnection` represents a data flow between two nodes:
- `fromNode`: The source node.
- `fromOutputIndex`: The output port index on the source node.
- `toNode`: The destination node.
- `toInputIndex`: The input port index on the destination node.

### **4. ExecutionEdge**
An `ExecutionEdge` represents an execution dependency between two nodes:
- `fromNode`: The node that must run first.
- `toNode`: The node that runs after.

### **5. World**
The `World` is the central manager of the graph. It owns all nodes, connections, and execution edges. It validates the graph, builds execution plans, and processes audio blocks.

### **6. Output/Mixer Block**
The Output/Mixer block is a built-in graph endpoint owned by `World`.
- It is not a `Node`
- It is always present and cannot be deleted
- It accepts only `AudioBuffer` connections
- Every connected `AudioBuffer` is summed into the final device output
- Mixing order must be deterministic (stable ordering of connections)

---

## Detailed Design

### **Node Interface**
```cpp
class Node {
protected:
    std::vector<InputPortSpec> inputSpecs;
    std::vector<OutputPortSpec> outputSpecs;
    std::vector<PortValue> inputValues;
    std::vector<PortValue> outputValues;

public:
    virtual ~Node() = default;

    virtual void process(const ProcessContext& context) = 0;

    const std::vector<InputPortSpec>& getInputSpecs() const { return inputSpecs; }
    const std::vector<OutputPortSpec>& getOutputSpecs() const { return outputSpecs; }

    std::vector<PortValue>& getInputValues() { return inputValues; }
    std::vector<PortValue>& getOutputValues() { return outputValues; }
};
```

### **Port Specifications**
#### InputPortSpec
```cpp
struct InputPortSpec {
    std::string name; // For UI display
    DataType type; // Data type: Float, Int, Bool, Buffer
    InputRequirement requirement; // Required or Optional
    DataValue defaultValue; // Default value for Optional inputs
};
```

#### OutputPortSpec
```cpp
struct OutputPortSpec {
    std::string name; // For UI display
    DataType type; // Data type: Float, Int, Bool, Buffer
};
```

#### PortValue
```cpp
struct PortValue {
    DataType type;
    DataValue value;
};
```

### **Connections**
#### SignalConnection
```cpp
struct SignalConnection {
    NodeId fromNode;
    size_t fromOutputIndex;
    NodeId toNode;
    size_t toInputIndex;
};
```

#### ExecutionEdge
```cpp
struct ExecutionEdge {
    NodeId fromNode;
    NodeId toNode;
};
```

### **World**
#### Responsibilities
1. **Node Management**
   - Spawn nodes.
   - Remove nodes.
2. **Connection Management**
   - Add/remove `SignalConnection`s.
   - Add/remove `ExecutionEdge`s.
3. **Validation**
   - Ensure required inputs are connected.
   - Ensure no cycles in execution edges.
4. **Execution Plan**
   - Build topological execution order.
   - Pre-allocate buffers for all nodes.
5. **Audio Processing**
    - Process nodes layer by layer.
    - Sum all `AudioBuffer` connections into the Output/Mixer block.

#### API
```cpp
class World {
private:
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes;
    std::vector<SignalConnection> connections;
    std::vector<ExecutionEdge> executionEdges;
    NodeId nextId = 1;

    struct ExecutionPlan {
        std::vector<NodeId> order;
        std::vector<NodeId> sinks;
        // Preallocated buffers, routing tables, etc.
    };

    std::unique_ptr<ExecutionPlan> plan;

public:
    template <typename T, typename... Args>
    NodeId spawn(Args&&... args);

    bool connect(NodeId fromNode, size_t fromOut, NodeId toNode, size_t toIn);
    bool addExecutionEdge(NodeId fromNode, NodeId toNode);

    bool rebuildPlan(int numChannels, int maxSamples);

    void processBlock(float* const* deviceOutput, int numChannels, int numSamples, double sampleRate, const MidiState& midi);
};
```

---

## Validation Rules
1. **Required Inputs**
   - If a required input is not connected, the graph is invalid.
   - Validation errors are stored for UI feedback.
2. **Port Compatibility**
   - Data types of connected ports must match.
3. **Execution Order**
   - Execution edges must form a directed acyclic graph (DAG).
   - Cycles are detected and rejected during plan rebuild.

---

## Audio Processing Flow
1. **Pre-allocate Buffers**
   - Allocate buffers for all nodes at `audioDeviceAboutToStart`.
2. **Build Execution Plan**
   - Validate graph.
   - Build topological execution order.
   - Precompute routing tables.
3. **Process Block**
    - Clear buffers for current block.
    - Execute nodes layer by layer.
    - Route outputs to inputs using SignalConnections.
    - Sum Output/Mixer block inputs into device output.

---

## Example: Oscillator Node
```cpp
class OscillatorNode : public Node {
public:
    OscillatorNode() {
        inputSpecs = {
            { "Frequency", DataType::Float, InputRequirement::Optional, { .floatValue = 440.0f } },
            { "Amplitude", DataType::Float, InputRequirement::Optional, { .floatValue = 1.0f } },
        };

        outputSpecs = {
            { "Audio Out", DataType::Buffer },
        };

        inputValues.resize(inputSpecs.size());
        outputValues.resize(outputSpecs.size());
    }

    void process(const ProcessContext& context) override {
        float frequency = inputValues[0].value.floatValue;
        float amplitude = inputValues[1].value.floatValue;
        float* outputBuffer = outputValues[0].value.buffer;

        // Generate audio signal into outputBuffer...
    }
};
```

---

## Example: Connecting Nodes
```cpp
World world;

// Spawn nodes
NodeId osc1 = world.spawn<OscillatorNode>();
NodeId osc2 = world.spawn<OscillatorNode>();
NodeId mixer = world.spawn<MixerNode>();

// Connect nodes
world.connect(osc1, 0, mixer, 0); // Oscillator 1 -> Mixer input 0
world.connect(osc2, 0, mixer, 1); // Oscillator 2 -> Mixer input 1

// Add execution edges
world.addExecutionEdge(osc1, mixer);
world.addExecutionEdge(osc2, mixer);

// Rebuild execution plan
world.rebuildPlan(2, 512);
```

---

## Why This Design Works
1. **Flexibility**
   - Supports any graph structure (fan-in, fan-out, etc.).
2. **Efficiency**
   - Pre-allocated buffers ensure real-time safety.
3. **Validation**
   - Required inputs and execution order are enforced.
4. **Scalability**
   - Adding new node types or data types is simple.

---

Let me know if you’d like to refine or expand any section!