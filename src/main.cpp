#include <cstdio>
#include <unordered_map>
#include <glad/glad.h>
#include "sdl.hpp"
#include "audio.hpp"
#include "world.hpp"

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
    const std::unordered_map<SDL_Keycode, int> keyToMidiNote = {
        { SDLK_Z, 60 },
        { SDLK_S, 61 },
        { SDLK_X, 62 },
        { SDLK_D, 63 },
        { SDLK_C, 64 },
        { SDLK_V, 65 },
        { SDLK_G, 66 },
        { SDLK_B, 67 },
        { SDLK_H, 68 },
        { SDLK_N, 69 },
        { SDLK_J, 70 },
        { SDLK_M, 71 },
        { SDLK_COMMA, 72 },
        { SDLK_L, 73 },
        { SDLK_PERIOD, 74 },
        { SDLK_SEMICOLON, 75 },
        { SDLK_SLASH, 76 },
        { SDLK_BACKSLASH, 78 },
        { SDLK_Q, 72 },
        { SDLK_2, 73 },
        { SDLK_W, 74 },
        { SDLK_3, 75 },
        { SDLK_E, 76 },
        { SDLK_R, 77 },
        { SDLK_5, 78 },
        { SDLK_T, 79 },
        { SDLK_6, 80 },
        { SDLK_Y, 81 },
        { SDLK_7, 82 },
        { SDLK_U, 83 },
        { SDLK_I, 84 },
        { SDLK_9, 85 },
        { SDLK_O, 86 },
        { SDLK_0, 87 },
        { SDLK_P, 88 },
        { SDLK_LEFTBRACKET, 89 },
        { SDLK_EQUALS, 90 },
        { SDLK_RIGHTBRACKET, 91 }
    };

    NodeId oscillatorNodeId = audioEngine.world.spawn<OscillatorNode>();
    OscillatorNode &oscillatorNode = static_cast<OscillatorNode&>(audioEngine.world.getNode(oscillatorNodeId)->get());
    oscillatorNode.setShape(OscillatorShape::Triangle);
    NodeId gainNodeId = audioEngine.world.spawn<GainNode>();
    
    NodeId oscillator2NodeId = audioEngine.world.spawn<OscillatorNode>();
    OscillatorNode &oscillator2Node = static_cast<OscillatorNode&>(audioEngine.world.getNode(oscillator2NodeId)->get());
    oscillator2Node.setShape(OscillatorShape::Square);
    oscillator2Node.setAmplitude(0.3f);
    audioEngine.world.connectToOutput(oscillator2NodeId, OscillatorNode::BUFFER_OUTPUT_ID);
    
    GainNode &gainNode = static_cast<GainNode&>(audioEngine.world.getNode(gainNodeId)->get());
    gainNode.setGain(0.3f);
    audioEngine.world.connectToOutput(gainNodeId, GainNode::BUFFER_OUTPUT_ID);
    audioEngine.world.connect(oscillatorNodeId, OscillatorNode::BUFFER_OUTPUT_ID, gainNodeId, GainNode::BUFFER_INPUT_ID);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (!event.key.repeat && keyToMidiNote.find(event.key.key) != keyToMidiNote.end()) {
                    midiHandler.call(juce::MidiMessage::noteOn(1, keyToMidiNote.at(event.key.key), 1.0f));
                }
            }
            if (event.type == SDL_EVENT_KEY_UP) {
                if (keyToMidiNote.find(event.key.key) != keyToMidiNote.end()) {
                    midiHandler.call(juce::MidiMessage::noteOff(1, keyToMidiNote.at(event.key.key)));
                }
            }
        }

        oscillatorNode.setFrequency(440.0f * std::pow(2.0f, (std::floor(std::powf(std::sinf(SDL_GetTicks() * 0.001f) * 0.5f + 0.5f, 2.0f) * 3.0f) * 2.0f - 24.0f) / 12.0f));
        oscillator2Node.setFrequency(440.0f * std::pow(2.0f, (std::floor(std::powf(std::cosf(SDL_GetTicks() * 0.001f) * 0.5f + 0.5f, 2.0f) * 3.0f) * 2.0f - 24.0f) / 12.0f));
        oscillatorNode.setShape(static_cast<OscillatorShape>(static_cast<uint8_t>((std::sinf(SDL_GetTicks() * 0.0025f) * 0.5f + 0.5f) * 3.99f)));
        oscillator2Node.setShape(static_cast<OscillatorShape>(static_cast<uint8_t>((std::cosf(SDL_GetTicks() * 0.0025f) * 0.5f + 0.5f) * 3.99f)));

        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
    }

    midiHandler.stop(deviceManager);
    deviceManager.removeAudioCallback(&audioEngine);
    deviceManager.closeAudioDevice();

    return 0;
}