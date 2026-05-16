# AGENTS.md

## Project Identity

NodeAudioEngine is a node-based synthesizer.

The product goal is maximum creative routing freedom built from minimal, understandable primitives:
- Basic nodes (MIDI Input, ADSR, Oscillator, Wavetable, Audio Output, filters, reverb, etc.)
- Any signal can modulate almost any parameter
- Audio-rate and control-rate modulation are both first-class
- Patching should make FM, AM, feedback, cross-modulation, and unusual behaviors easy

## Product Direction

### Core UX Promise
- Keep node set simple and composable
- Prefer fewer highly reusable nodes over many specialized nodes
- Let users connect everything to everything when technically safe
- Preserve musical immediacy: low friction, low latency, clear visual feedback

### Visual Direction
- Inspired by Arturia Pigments 7 style language (modern, premium, dense-yet-readable)
- Do not copy proprietary assets, fonts, icons, or exact layout from third-party products
- Build an original UI system with similar polish level

### Canvas Rules
- Infinite graph canvas
- Dotted grid background
- Nodes snap to grid by default (position and size)
- Keep grid snapping always on for now
- Plan architecture so optional future per-user snap disable is possible

## Tech Stack (Non-Negotiable)

- Audio/DSP engine: JUCE
- Window/input/context setup: SDL3
- OpenGL function loading: GLAD
- Rendering: custom OpenGL renderer
- Build system: CMake

## Architectural Constraints

### Separation of Responsibilities
- JUCE handles DSP, timing, MIDI/audio primitives, and audio device integration
- SDL3 handles window lifecycle, input, and platform window concerns
- OpenGL renderer handles all UI drawing
- Keep rendering and audio engine strictly decoupled except through explicit state bridges

### Real-Time Audio Safety
- No heap allocations on the real-time audio thread
- No locks on the real-time audio thread
- No file/network/blocking operations on the audio thread
- Parameter/state handoff between UI and DSP must use lock-free or double-buffered patterns
- Avoid unbounded graph operations in per-sample hot paths

### Graph Execution Model
- Represent processing as a directed node graph with explicit pins/ports
- Support multiple signal types at minimum:
  - Audio stream
  - Control signal
  - Event/MIDI
- Define deterministic node evaluation order per block
- Detect cycles and apply a clear policy (reject, explicit delay node requirement, or bounded feedback handling)
- Keep behavior deterministic for offline rendering and preset recall

### Modulation Model
- Parameters are modulatable endpoints
- Inputs can accept mixed sources where meaningful (base value + modulation sum/mix)
- Audio-rate modulation should be supported when target node supports it
- Control-rate fallback is acceptable for non-critical targets if clearly documented

## Initial Node Set (MVP Guidance)

Prioritize these nodes first:
- MIDI Input
- Note to Frequency / Gate utilities
- ADSR Envelope
- Basic Oscillator (sine, saw, square, triangle)
- Wavetable Oscillator (single-table playback first)
- Gain/Attenuator
- Mixer
- Basic Filter (LP/HP minimum)
- Reverb (simple but stable)
- Audio Output

Optional early utility nodes:
- Constant Value
- Math nodes (add, multiply, clamp)
- LFO

## UI/Interaction Standards

- Node placement and sizing should align to a shared grid unit
- Connections should remain legible at high graph density
- Prioritize performance for large patches (hundreds of nodes)
- Keep camera controls smooth (pan/zoom) and independent from DSP load
- Selection, drag, cable creation, and deletion interactions must feel immediate

## Data and Persistence

- Patch format should be deterministic and versioned
- Persist node ids, positions, sizes, parameters, and all edges
- Include a migration strategy for future node schema changes
- Avoid storing transient runtime buffers in serialized patch data

## Performance and Reliability Targets

- Stable real-time playback without dropouts under normal patch complexity
- Predictable CPU scaling with graph size
- Graceful handling of invalid connections or incompatible pin types
- Fast load/save for medium and large patches

## Recommended Repository Structure (Target)

- src/audio/ : graph engine, node implementations, DSP adapters
- src/graph/ : graph model, pin typing, scheduler, validation
- src/ui/ : editor state, interactions, widgets, styling system
- src/render/ : OpenGL renderer, primitives, text, theme
- src/platform/ : SDL3 bootstrap, input mapping, window lifecycle
- src/shared/ : common utilities, ids, serialization helpers

## Agent Working Rules

When implementing features in this repository:
- Preserve the stack decisions above
- Do not move UI rendering into JUCE components unless explicitly requested
- Do not introduce blocking calls into audio callbacks
- Prefer incremental PR-sized changes with clear compile checkpoints
- Add concise comments only where logic is non-obvious
- Keep behavior deterministic where possible

Before large changes:
- Confirm impact on real-time thread safety
- Confirm impact on graph determinism
- Confirm impact on patch compatibility

## Definition of Done for New Features

A feature is complete only when:
- It builds with CMake on the current target platform
- It does not violate real-time audio safety rules
- It includes serialization behavior (or explicit non-persistent rationale)
- It handles invalid user graph states safely
- It preserves visual alignment and interaction consistency on the node grid

## Near-Term Milestones

1. Boot pipeline: SDL3 window + OpenGL context + GLAD + clear frame loop
2. Audio pipeline: JUCE device init + stable callback + test tone path
3. Graph core: node/pin model + connection validation + topological scheduling
4. Minimal patch: MIDI -> Oscillator -> Gain -> Output
5. Editor core: infinite dotted grid + node spawn/move/snap + cable connect
6. Modulation pass: envelope/LFO to oscillator/filter/gain parameters
7. Persistence v1: save/load deterministic patch format

## Roadmap Files and Tutorial Parts

Roadmap markdown files are stored in the `roadmaps/` folder.

For node-system implementation and tutorial progression, agents must consult these files first:
- `roadmaps/node_system_implementation/SPECIFICATION.md` (architecture and design intent)
- `roadmaps/node_system_implementation/GUIDE.md` (part-by-part tutorial contract, checkboxes, exit criteria)

When the user references "Part 1", "Part 2", "continue", or asks for the next step:
- Treat `roadmaps/node_system_implementation/GUIDE.md` as the source of truth for part scope and status
- Keep explanations aligned with the requested part boundaries
- Update part checkboxes/status notes when implementation meaningfully reaches that part

## Explanatory Dialogue Style

When the user asks how something works, how to implement a feature, or requests a tutorial, use this style:

### What to explain
- **Why the construct exists** — what problem it solves, why you can't proceed without it
- **Why each field/member is there** — its role and what breaks if it's missing
- **Why the structure/class was chosen** — why not a plain array, a global, a different type, etc.
- **Why each function exists with these exact return types and parameters** — what those types communicate and why the signature is shaped that way
- **Why the code is written the way it is** — tradeoffs, alternatives considered, and why this path was chosen

### What NOT to explain
- Basic C/C++ syntax and features (includes, pragma once, basic control flow, etc.)
- Self-evident things that any C++ programmer would know at a glance
- **Do explain** advanced or non-obvious things: template metaprogramming, atomics, memory orders, mutexes, C++17/20 features, lock-free patterns, real-time safety constraints

### Format and tone
- Use **emojis** freely to mark concepts, warnings, tips, categories
- Use **rich markdown**: headers, bold, bullet points, nested lists, code blocks
- Pair small focused code examples with conceptual explanation — never dump large blocks without context
- Explain each example **after** showing it, not before
- Keep explanation tight — no unnecessary padding, but don't skip important reasoning

### Code examples
- Examples must be **small** — one concept at a time
- Each example should be immediately buildable or clearly a fragment
- Prefer examples that match the actual project files and naming conventions

## Notes

This file is the primary policy document for coding agents operating in this repository. If implementation details conflict with this file, update this file and the code in the same change set with a short rationale.
