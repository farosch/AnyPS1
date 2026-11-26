#pragma once

#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace anyps1 {

class HostVideo {
public:
    HostVideo();
    ~HostVideo();

    HostVideo(const HostVideo&) = delete;
    HostVideo& operator=(const HostVideo&) = delete;

    bool poll_events();
    void present();
    void clear();

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
};

}
