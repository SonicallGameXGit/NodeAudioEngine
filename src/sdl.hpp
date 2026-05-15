#pragma once
#include <SDL3/SDL.h>

struct SDLContext {
private:
    bool initialized;
public:
    SDLContext();
    ~SDLContext();
    bool isInitialized() const;
};
struct SDLWindow {
private:
    SDL_Window *window;
public:
    SDLWindow(const char *title, int width, int height, Uint32 flags);
    ~SDLWindow();

    operator SDL_Window*() const;
    SDL_Window *operator->() const;
    bool isValid() const;
};
struct SDLGLContext {
private:
    SDL_GLContext context;
public:
    explicit SDLGLContext(SDL_Window *window);
    ~SDLGLContext();

    operator SDL_GLContext() const;
    SDL_GLContext operator->() const;
    bool isValid() const;
};