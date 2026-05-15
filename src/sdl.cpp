#include "sdl.hpp"

SDLContext::SDLContext() : initialized(false) {
    if (SDL_Init(SDL_INIT_VIDEO)) {
        this->initialized = true;
    }
}
SDLContext::~SDLContext() {
    if (this->initialized) {
        SDL_Quit();
    }
}
bool SDLContext::isInitialized() const { return this->initialized; }

SDLWindow::SDLWindow(const char *title, int width, int height, Uint32 flags) {
    this->window = SDL_CreateWindow(title, width, height, flags);
}
SDLWindow::~SDLWindow() {
    if (this->window != nullptr) {
        SDL_DestroyWindow(this->window);
    }
}
SDLWindow::operator SDL_Window*() const { return this->window; }
SDL_Window *SDLWindow::operator->() const { return this->window; }
bool SDLWindow::isValid() const { return this->window != nullptr; }

SDLGLContext::SDLGLContext(SDL_Window *window) {
    this->context = SDL_GL_CreateContext(window);
}
SDLGLContext::~SDLGLContext() {
    if (this->context != nullptr) {
        SDL_GL_DestroyContext(this->context);
    }
}
SDLGLContext::operator SDL_GLContext() const { return this->context; }
SDL_GLContext SDLGLContext::operator->() const { return this->context; }
bool SDLGLContext::isValid() const { return this->context != nullptr; }