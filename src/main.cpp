#include <cstdio>
#include <glad/glad.h>
#include "sdl.hpp"
#include "audio.hpp"

int main() {
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