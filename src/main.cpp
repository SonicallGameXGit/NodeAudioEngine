#include <cmath>
#include <cstdio>
#include <juce_audio_devices/juce_audio_devices.h>

class SineWaveGenerator final : public juce::AudioIODeviceCallback {
private:
    void updatePhaseDelta() {
        phaseDelta = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;
    }

    double frequencyHz = 440.0;
    double amplitude = 0.15;
    double sampleRate = 44100.0;
    double currentPhase = 0.0;
    double phaseDelta = 0.0;
public:
    void audioDeviceIOCallbackWithContext(
        const float *const *inputChannelData,
        int numInputChannels,
        float *const *outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext &context
    ) override {
        for (size_t sampleIndex = 0; sampleIndex < static_cast<size_t>(numSamples); sampleIndex++) {
            float sample = static_cast<float>(std::sin(this->currentPhase) * this->amplitude);
            this->currentPhase += this->phaseDelta;

            if (this->currentPhase >= juce::MathConstants<double>::twoPi) {
                this->currentPhase -= juce::MathConstants<double>::twoPi;
            }

            for (size_t channel = 0; channel < static_cast<size_t>(numOutputChannels); channel++) {
                if (outputChannelData[channel] == nullptr) { continue; }
                outputChannelData[channel][sampleIndex] = sample;
            }
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice *device) override {
        if (device == nullptr) { return; }
        this->sampleRate = device->getCurrentSampleRate();
        this->updatePhaseDelta();
    }

    void audioDeviceStopped() override {
        this->sampleRate = 44100.0;
        this->phaseDelta = 0.0;
        this->currentPhase = 0.0;
    }
};

int main() {
    juce::AudioDeviceManager deviceManager = juce::AudioDeviceManager();
    SineWaveGenerator sineWaveGenerator = SineWaveGenerator();

    const juce::String error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()) {
        fprintf(stderr, "Failed to initialize audio device: %s\n", error.toRawUTF8());
        return 1;
    }

    deviceManager.addAudioCallback(&sineWaveGenerator);

    printf("Playing sine wave at 440 Hz. Press Enter to stop...\n");
    getchar();

    deviceManager.removeAudioCallback(&sineWaveGenerator);
    return 0;
}