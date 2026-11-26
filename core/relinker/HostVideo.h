#pragma once

#include <array>
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
    std::uint16_t pad_state() const;
    void present();
    void present_vram(
        const std::array<std::uint16_t, 1024 * 512>& vram,
        std::uint32_t display_x,
        std::uint32_t display_y,
        std::uint32_t display_width,
        std::uint32_t display_height
    );
    void clear();

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
};

}
