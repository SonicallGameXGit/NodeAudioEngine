# Node System Tutorial Parts Roadmap

This roadmap defines the step-by-step tutorial progression for building the node system.
Each part has a status checkbox and a concrete scope so tutorials can be generated consistently in any future session.

Status legend:
- [ ] Not started
- [~] In progress
- [x] Implemented baseline

## Part 1 - Single Audible Node Baseline [x]

### Goal
Prove the end-to-end audio path works with a minimal architecture: one world, one node type, one audible oscillator.

### Why this part exists
Without a guaranteed audible baseline, every later graph feature is hard to debug. This part isolates device callback integration, per-block processing, and oscillator DSP correctness.

### Required scope
- Minimal runtime data model for ports and process context
- Abstract Node interface with process contract
- OscillatorNode with stable phase accumulation across blocks
- World that owns nodes and calls each node process once per block
- Audio callback forwards device buffers and sample rate into World

### Current implemented result (project baseline)
- Node-based processing path exists and is called from the audio callback
- OscillatorNode exists with shape, frequency, and amplitude controls
- World spawns nodes and processes them each block
- Main creates at least one oscillator and sound output is active

### Constraints
- No dynamic graph routing required yet
- No execution plan or topological scheduling required yet
- Direct write to device output is acceptable for this part only

### Exit criteria
- Application launches and consistently outputs audible tone
- No allocations/locks added inside per-sample hot loops
- No crashes when output channels are missing or sample rate is invalid

## Part 2 - First Routed Chain (Oscillator -> Gain) [ ]

### Goal
Introduce the first explicit signal routing primitive and a second node type, while keeping execution simple.

### Why this part exists
The project needs a graph model, not just isolated nodes. This part adds minimal routing semantics without committing to full planner complexity.

### Required scope
- Add GainNode as second DSP primitive
- Add SignalConnection model with source/target node + port indices
- Add World connection API with basic validation (node existence, index bounds, type compatibility)
- Route source output values into target input values before target processing

### Constraints
- Manual/simple execution order is acceptable (no full DAG planner yet)
- Keep all processing deterministic for identical graph state

### Exit criteria
- A simple chain Oscillator -> Gain produces audible output
- Invalid connections are rejected safely
- Connection state is stored in World and reused block-to-block

## Part 3 - Multi-Source Mixing and Deterministic Summing [ ]

### Goal
Support multiple simultaneous audio sources without overwrite behavior.

### Why this part exists
Current direct device writes from multiple nodes cause last-writer-wins behavior. This part introduces explicit summing so many sources can be heard together.

### Required scope
- Node outputs written to node-owned/preassigned buffers
- World clears device output at block start
- World sums selected node outputs into final device output
- Define sink behavior (which nodes contribute to final output)

### Constraints
- Keep summing and buffer operations bounded and real-time safe
- Avoid hidden dependence on container iteration order

### Exit criteria
- Two oscillators can be heard simultaneously as a mix
- Output is deterministic for same patch and same parameter state

## Part 4 - Output/Mixer Block (Implicit Sink) [ ]

### Goal
Replace ad-hoc sink node lists with a single built-in output/mixer block that every world owns.

### Why this part exists
Explicit sinks are a temporary bridge. A dedicated output/mixer block gives a stable, predictable target for all audio outputs and makes mixing behavior consistent and visible in the graph model.

### Required scope
- Add a built-in Output/Mixer block that is always present in `World`
- It accepts only `AudioBuffer` connections
- Any `AudioBuffer` connected to it is summed into the final device output
- Remove or ignore `sinkNodes` in favor of this block
- Mixing order is deterministic (stable ordering of connections)

### Constraints
- Output/Mixer block is not a `Node`
- It must never be deleted
- It must not allocate in the audio callback

### Exit criteria
- Two oscillators routed into the Output/Mixer block are audible together
- No ad-hoc sink list is required for correct mixing

## Part 5 - Execution Graph and Plan Rebuild (DAG) [ ]

### Goal
Separate edit-time graph validation/planning from real-time block execution.

### Why this part exists
A scalable node engine cannot recompute arbitrary graph behavior in the audio callback. This part introduces a rebuild step and execution plan artifact.

### Required scope
- ExecutionEdge model and cycle detection
- Topological ordering for execution
- Rebuild operation that validates graph and stores execution order
- Runtime uses cached plan only

### Constraints
- Any cycle policy must be explicit and deterministic
- Rebuild errors should be retained for UI/debug visibility

### Exit criteria
- Invalid cyclic execution graphs are rejected
- Valid graphs execute strictly in planned topological order
- Audio callback does not perform expensive graph analysis

## Part 6 - Input Requirements and Port Validation [ ]

### Goal
Make graph contracts explicit with required vs optional inputs and defaults.

### Why this part exists
Nodes need predictable behavior when inputs are disconnected. This part prevents silent bad states and enables clear diagnostics.

### Required scope
- InputPortSpec and OutputPortSpec used as authoritative interface contract
- Required input validation on rebuild
- Optional input default values applied consistently
- Port type compatibility checks enforced by connect API

### Exit criteria
- Missing required inputs make the plan invalid with explicit reason
- Optional inputs always resolve to deterministic runtime values

## Part 7 - MIDI/Event State Bridge [ ]

### Goal
Provide a stable, sample-accurate path from MIDI/event ingestion to DSP nodes.

### Why this part exists
Musical interaction requires note/gate/mod sources that are globally visible but real-time safe. Block-boundary-only event delivery would accumulate latency proportional to buffer size (up to 10+ms at large buffers), which makes MIDI feel sluggish and ruins timing for fast percussive parts. Sample-accurate delivery keeps latency fixed at 1 sample regardless of buffer size.

### Required scope
- **Sample-accurate `MidiEvent`** — each event stores a `sampleOffset` (0 to blockSize-1), allowing nodes to react at the exact sample within a block
- **Lock-free ring buffer** — accumulates events from the UI/MIDI thread, consumed once per block by the audio thread
- **Per-block event dispatcher** — drains the ring buffer, sorts events by `sampleOffset`, builds an **event-per-sample lookup** (e.g. `std::array<std::vector<MidiEvent>, MAX_BLOCK_SIZE>` or an index table) so nodes can query "what events happen at sample N?"
- **`ProcessContext` carries the per-sample event table** — nodes like ADSR iterate sample-by-sample and process events inline at the correct position
- **Utility nodes** — `MidiInputNode` exposes events as per-sample signal outputs (gate, note number, velocity); `NoteToFrequencyNode` converts note number to frequency

### Implementation notes
- The per-sample event table must be pre-allocated and reused each block (no allocations on audio thread)
- Ring buffer uses atomic head/tail with a fixed capacity (e.g. 1024 events) to guarantee no blocking
- `MidiEvent` struct:
  ```cpp
  struct MidiEvent {
      enum class Type : uint8_t { NoteOn, NoteOff, CC, PitchBend };
      Type type;
      uint8_t channel;
      uint8_t note;
      float velocity;
      uint32_t sampleOffset;  // sample index within the current block
  };
  ```
- Event dispatch happens once at the top of `World::process()` — after that, the per-sample table is read-only for all nodes

### Constraints
- No blocking synchronization on audio thread
- State handoff must avoid tearing and undefined partial updates
- Per-sample event index must be **pre-allocated**, not grown dynamically
- No heap allocations inside the audio callback

### Exit criteria
- MIDI note on/off changes audible output through node graph
- Events are delivered sample-accurately: a NoteOn at buffer position 200 starts the ADSR attack at sample 200, not at the next block boundary
- Latency does not increase with buffer size
- Behavior is deterministic and thread-safe under rapid input

## Part 8 - Preallocation and Real-Time Hardening [ ]

### Goal
Eliminate runtime allocations and hidden unbounded work from audio processing.

### Why this part exists
Dropout-resistant audio requires strict real-time discipline, especially as graphs scale.

### Required scope
- Preallocate node buffers/routing scratch in about-to-start or rebuild phase
- Ensure per-block processing performs no heap allocation
- Audit lock usage from callback path

### Exit criteria
- No allocations or locks on audio callback path
- Stable playback under larger graph sizes

## Part 9 - Persistence v1 and Schema Safety [ ]

### Goal
Persist and restore patches deterministically.

### Why this part exists
Without persistence, graph editing has no production value. Deterministic serialization is also required for reproducibility.

### Required scope
- Versioned patch format
- Save/load node ids, node params, node positions/sizes, all edges
- Migration strategy hooks for future schema changes

### Exit criteria
- Save then load reproduces equivalent graph and behavior
- Version handling is explicit and testable

## Part 10 - Modulation System (Control + Audio Rate) [ ]

### Goal
Enable arbitrary modulation routing to parameter endpoints.

### Why this part exists
The product identity depends on flexible modulation as a first-class concept.

### Required scope
- Modulation-capable parameter endpoints
- Mix policy for base value + modulation contributions
- Support audio-rate where target allows, control-rate fallback where acceptable

### Exit criteria
- Envelope/LFO and audio-rate sources can modulate key node parameters
- Modulation behavior is stable and documented per target

## How to use this roadmap in tutorials

- Tutorials should always anchor to one part at a time
- Explain why each structure/function/field exists for that part scope
- Avoid implementation details from later parts unless explicitly previewed
- When a part becomes implemented, update the checkbox and current-result notes