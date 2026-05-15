// #include <cmath>
// #include <cstdio>
// #include <juce_audio_devices/juce_audio_devices.h>

// class SineWaveGenerator final : public juce::AudioIODeviceCallback {
// private:
//     void updatePhaseDelta() {
//         phaseDelta = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;
//     }

//     double frequencyHz = 440.0;
//     double amplitude = 0.15;
//     double sampleRate = 44100.0;
//     double currentPhase = 0.0;
//     double phaseDelta = 0.0;
// public:
//     void audioDeviceIOCallbackWithContext(
//         const float *const *inputChannelData,
//         int numInputChannels,
//         float *const *outputChannelData,
//         int numOutputChannels,
//         int numSamples,
//         const juce::AudioIODeviceCallbackContext &context
//     ) override {
//         for (size_t sampleIndex = 0; sampleIndex < static_cast<size_t>(numSamples); sampleIndex++) {
//             float sample = static_cast<float>(std::sin(this->currentPhase) * this->amplitude);
//             this->currentPhase += this->phaseDelta;

//             if (this->currentPhase >= juce::MathConstants<double>::twoPi) {
//                 this->currentPhase -= juce::MathConstants<double>::twoPi;
//             }

//             for (size_t channel = 0; channel < static_cast<size_t>(numOutputChannels); channel++) {
//                 if (outputChannelData[channel] == nullptr) { continue; }
//                 outputChannelData[channel][sampleIndex] = sample;
//             }
//         }
//     }

//     void audioDeviceAboutToStart(juce::AudioIODevice *device) override {
//         if (device == nullptr) { return; }
//         this->sampleRate = device->getCurrentSampleRate();
//         this->updatePhaseDelta();
//     }

//     void audioDeviceStopped() override {
//         this->sampleRate = 44100.0;
//         this->phaseDelta = 0.0;
//         this->currentPhase = 0.0;
//     }
// };

#include <cstdio>
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <juce_events/juce_events.h>
#include "audio.hpp"

struct SDLContext {
private:
    bool initialized;
public:
    SDLContext() : initialized(false) {
        if (!SDL_Init(SDL_INIT_VIDEO)) { return; }
        this->initialized = true;
    }
    ~SDLContext() {
        if (!this->initialized) { return; }
        SDL_Quit();
    }

    bool isInitialized() const {
        return this->initialized;
    }
};
struct SDLWindow {
private:
    SDL_Window *window;
public:
    SDLWindow(const char *title, int width, int height, Uint32 flags) : window(nullptr) {
       this->window = SDL_CreateWindow(title, width, height, flags);
    }
    ~SDLWindow() {
        if (this->window == nullptr) { return; }
        SDL_DestroyWindow(this->window);
    }

    operator SDL_Window*() const {
        return this->window;
    }
    SDL_Window *operator->() const {
        return this->window;
    }

    bool isValid() const {
        return this->window != nullptr;
    }
};
struct SDLGLContext {
private:
    SDL_GLContext context;
public:
    SDLGLContext(SDL_Window *window) : context(nullptr) {
        this->context = SDL_GL_CreateContext(window);
    }
    ~SDLGLContext() {
        if (this->context == nullptr) { return; }
        SDL_GL_DestroyContext(this->context);
    }

    operator SDL_GLContext() const {
        return this->context;
    }
    SDL_GLContext operator->() const {
        return this->context;
    }
    
    bool isValid() const {
        return this->context != nullptr;
    }
};

int main() {
    // juce::AudioDeviceManager deviceManager = juce::AudioDeviceManager();
    // SineWaveGenerator sineWaveGenerator = SineWaveGenerator();

    // const juce::String error = deviceManager.initialiseWithDefaultDevices(0, 2);
    // if (error.isNotEmpty()) {
    //     fprintf(stderr, "Failed to initialize audio device: %s\n", error.toRawUTF8());
    //     return 1;
    // }

    // deviceManager.addAudioCallback(&sineWaveGenerator);

    // printf("Playing sine wave at 440 Hz. Press Enter to stop...\n");
    // getchar();

    // deviceManager.removeAudioCallback(&sineWaveGenerator);

    SDLContext sdlContext = SDLContext();
    if (!sdlContext.isInitialized()) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDLWindow window = SDLWindow("NodeAudioEngine", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window.isValid()) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDLGLContext glContext = SDLGLContext(window);
    if (!glContext.isValid()) {
        fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
        return 1;
    }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    glClearColor(0.15f, 0.17f, 0.2f, 1.0f);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioDeviceManager deviceManager = juce::AudioDeviceManager();
    const juce::String error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()) {
        fprintf(stderr, "Failed to initialize audio device: %s\n", error.toRawUTF8());
        return 1;
    }
    AudioEngine audioEngine = AudioEngine();
    MidiHandler midiHandler = MidiHandler([&audioEngine](const juce::MidiMessage &message) {
        audioEngine.handleMidiMessage(message);
    });
    deviceManager.addAudioCallback(&audioEngine);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.key;
                if (key == SDLK_Z) { midiHandler.call(juce::MidiMessage::noteOn(1, 60, 1.0f)); }
                if (key == SDLK_S) { midiHandler.call(juce::MidiMessage::noteOn(1, 61, 1.0f)); }
                if (key == SDLK_X) { midiHandler.call(juce::MidiMessage::noteOn(1, 62, 1.0f)); }
                if (key == SDLK_D) { midiHandler.call(juce::MidiMessage::noteOn(1, 63, 1.0f)); }
                if (key == SDLK_C) { midiHandler.call(juce::MidiMessage::noteOn(1, 64, 1.0f)); }
            }
            if (event.type == SDL_EVENT_KEY_UP) {
                SDL_Keycode key = event.key.key;
                if (key == SDLK_Z) { midiHandler.call(juce::MidiMessage::noteOff(1, 60)); }
                if (key == SDLK_S) { midiHandler.call(juce::MidiMessage::noteOff(1, 61)); }
                if (key == SDLK_X) { midiHandler.call(juce::MidiMessage::noteOff(1, 62)); }
                if (key == SDLK_D) { midiHandler.call(juce::MidiMessage::noteOff(1, 63)); }
                if (key == SDLK_C) { midiHandler.call(juce::MidiMessage::noteOff(1, 64)); }
            }
        }

        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
    }

    midiHandler.stop(deviceManager);
    deviceManager.removeAudioCallback(&audioEngine);
    deviceManager.closeAudioDevice();

    return 0;
}