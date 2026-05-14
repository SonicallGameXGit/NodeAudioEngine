#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

class AudioEngine : public juce::AudioIODeviceCallback {
private:
    double sampleRate = 44100.0;
    double frequencyHz = 440.0;
    double amplitude = 0.15;
    double phase = 0.0;
    double phaseDelta = 0.0;

    void updatePhaseDelta() {
        phaseDelta = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;
    }
public:
    void setFrequency(double newHz) {
        frequencyHz = newHz;
        updatePhaseDelta();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        if (device == nullptr) {
            return;
        }
        sampleRate = device->getCurrentSampleRate();
        updatePhaseDelta();
    }

    void audioDeviceStopped() override {
        phase = 0.0;
    }

    void audioDeviceIOCallbackWithContext(
        const float *const *input,
        int numInputs,
        float *const *output,
        int numOutputs,
        int numSamples,
        const juce::AudioIODeviceCallbackContext &context
    ) override {
        for (int i = 0; i < numSamples; i++) {
            const float s = static_cast<float>(std::sin(phase) * amplitude);
            phase += phaseDelta;
            if (phase >= juce::MathConstants<double>::twoPi) {
                phase -= juce::MathConstants<double>::twoPi;
            }

            for (int ch = 0; ch < numOutputs; ch++) {
                if (output[ch] != nullptr) {
                    output[ch][i] = s;
                }
            }
        }
    }
};