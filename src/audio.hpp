#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "world.hpp"

class MidiHandler : public juce::MidiInputCallback {
private:
    std::function<void(const juce::MidiMessage &message)> callback;
public:
    explicit MidiHandler(std::function<void(const juce::MidiMessage &message)> callback);
    ~MidiHandler() override;

    void start(juce::AudioDeviceManager &audioDeviceManager);
    void stop(juce::AudioDeviceManager &audioDeviceManager);
    void call(const juce::MidiMessage &message);
    void handleIncomingMidiMessage(juce::MidiInput *input, const juce::MidiMessage &message) override;
};
class AudioEngine : public juce::AudioIODeviceCallback {
private:
    std::atomic<double> sampleRate;
    std::atomic<uint8_t> note;
public:
    World world;

    AudioEngine();
    ~AudioEngine() override;

    void audioDeviceAboutToStart(juce::AudioIODevice *device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(
        const float *const *input,
        int numInputs,
        float *const *output,
        int numOutputs,
        int numSamples,
        const juce::AudioIODeviceCallbackContext &context
    ) override;

    void handleMidiMessage(const juce::MidiMessage &message);
};