#include "audio.hpp"

MidiHandler::MidiHandler(std::function<void(const juce::MidiMessage &message)> callback) : callback(std::move(callback)) {}
MidiHandler::~MidiHandler() = default;

void MidiHandler::start(juce::AudioDeviceManager &audioDeviceManager) {
    const juce::Array<juce::MidiDeviceInfo> devices = juce::MidiInput::getAvailableDevices();
    for (const juce::MidiDeviceInfo &device : devices) {
        audioDeviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        audioDeviceManager.addMidiInputDeviceCallback(device.identifier, this);
    }
}
void MidiHandler::stop(juce::AudioDeviceManager &audioDeviceManager) {
    const juce::Array<juce::MidiDeviceInfo> devices = juce::MidiInput::getAvailableDevices();
    for (const juce::MidiDeviceInfo &device : devices) {
        audioDeviceManager.removeMidiInputDeviceCallback(device.identifier, this);
    }
}
void MidiHandler::call(const juce::MidiMessage &message) { this->callback(message); }
void MidiHandler::handleIncomingMidiMessage(juce::MidiInput *input, const juce::MidiMessage &message) { this->callback(message); }

AudioEngine::AudioEngine() : sampleRate(44100.0), phaseDelta(0.0), amplitude(0.15), note(0), phase(0.0) {}
AudioEngine::~AudioEngine() = default;

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice *device) {
    if (device == nullptr) { return; }
    this->sampleRate.store(device->getCurrentSampleRate(), std::memory_order_release);
}
void AudioEngine::audioDeviceStopped() { this->phase = 0.0; }
void AudioEngine::audioDeviceIOCallbackWithContext(
    const float *const *input,
    int numInputs,
    float *const *output,
    int numOutputs,
    int numSamples,
    const juce::AudioIODeviceCallbackContext &context
) {
    for (int i = 0; i < numSamples; i++) {
        const float s = static_cast<float>(std::sin(this->phase) * this->amplitude.load(std::memory_order_acquire));
        this->phase += this->phaseDelta.load(std::memory_order_acquire);
        if (this->phase >= juce::MathConstants<double>::twoPi) {
            this->phase -= juce::MathConstants<double>::twoPi;
        }
        for (int ch = 0; ch < numOutputs; ch++) {
            if (output[ch] != nullptr) {
                output[ch][i] = s;
            }
        }
    }
}

static double midiNoteToFrequency(uint8_t midiNote) {
    constexpr double A4_FREQUENCY = 440.0;
    constexpr int A4_MIDI_NOTE = 69;
    return A4_FREQUENCY * std::pow(2.0, (static_cast<double>(midiNote) - A4_MIDI_NOTE) / 12.0);
}
void AudioEngine::handleMidiMessage(const juce::MidiMessage &message) {
    if (message.isNoteOn()) {
        this->phaseDelta.store(juce::MathConstants<double>::twoPi * midiNoteToFrequency(static_cast<uint8_t>(message.getNoteNumber())) / this->sampleRate.load(std::memory_order_acquire), std::memory_order_release);
        this->amplitude.store(static_cast<double>(message.getFloatVelocity()) * 0.15, std::memory_order_release);
        this->note.store(static_cast<uint8_t>(message.getNoteNumber()), std::memory_order_release);
    } else if (message.isNoteOff() && static_cast<uint8_t>(message.getNoteNumber()) == this->note.load(std::memory_order_acquire)) {
        this->amplitude.store(0.0, std::memory_order_release);
    }
}