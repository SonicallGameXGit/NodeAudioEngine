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

AudioEngine::AudioEngine() : sampleRate(0.0), note(0), world() {}
AudioEngine::~AudioEngine() = default;

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice *device) {
    if (device == nullptr) { return; }
    this->sampleRate.store(device->getCurrentSampleRate(), std::memory_order_release);
    this->world.audioDeviceAboutToStart(static_cast<size_t>(device->getCurrentBufferSizeSamples()));
}
void AudioEngine::audioDeviceStopped() {
    this->sampleRate.store(0.0, std::memory_order_release);
}
void AudioEngine::audioDeviceIOCallbackWithContext(
    const float *const *input,
    int numInputs,
    float *const *output,
    int numOutputs,
    int numSamples,
    const juce::AudioIODeviceCallbackContext &context
) {
    this->world.process(output, numOutputs, numSamples, this->sampleRate.load(std::memory_order_acquire));
}
void AudioEngine::handleMidiMessage(const juce::MidiMessage &message) {
    // if (message.isNoteOn()) {
    //     this->phaseDelta.store(juce::MathConstants<double>::twoPi * midiNoteToFrequency(static_cast<uint8_t>(message.getNoteNumber())) / this->sampleRate.load(std::memory_order_acquire), std::memory_order_release);
    //     this->amplitude.store(static_cast<double>(message.getFloatVelocity()) * 0.15, std::memory_order_release);
    //     this->note.store(static_cast<uint8_t>(message.getNoteNumber()), std::memory_order_release);
    // } else if (message.isNoteOff() && static_cast<uint8_t>(message.getNoteNumber()) == this->note.load(std::memory_order_acquire)) {
    //     this->amplitude.store(0.0, std::memory_order_release);
    // }
}