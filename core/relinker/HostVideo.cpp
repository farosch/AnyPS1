#include "HostVideo.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace anyps1 {

HostVideo::HostVideo()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
    }
    window = SDL_CreateWindow("AnyPS1", 640, 480, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError());
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("SDL renderer creation failed: ") + SDL_GetError());
    }
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        1024,
        512
    );
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("SDL texture creation failed: ") + SDL_GetError());
    }
    clear();
}

HostVideo::~HostVideo()
{
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

bool HostVideo::poll_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
    }
    return true;
}

std::uint16_t HostVideo::pad_state() const
{
    const bool* keys = SDL_GetKeyboardState(nullptr);
    std::uint16_t state = 0xFFFF;
    if (keys[SDL_SCANCODE_LEFT]) state &= ~0x0080;
    if (keys[SDL_SCANCODE_RIGHT]) state &= ~0x0020;
    if (keys[SDL_SCANCODE_UP]) state &= ~0x1000;
    if (keys[SDL_SCANCODE_DOWN]) state &= ~0x4000;
    if (keys[SDL_SCANCODE_RETURN]) state &= ~0x0800;
    if (keys[SDL_SCANCODE_ESCAPE]) state &= ~0xF800;
    if (keys[SDL_SCANCODE_SPACE]) state &= ~0x8000;
    if (keys[SDL_SCANCODE_Z]) state &= ~0x0040;
    if (keys[SDL_SCANCODE_X]) state &= ~0x0010;
    if (keys[SDL_SCANCODE_A]) state &= ~0x0008;
    if (keys[SDL_SCANCODE_S]) state &= ~0x0004;
    if (keys[SDL_SCANCODE_Q]) state &= ~0x0002;
    if (keys[SDL_SCANCODE_W]) state &= ~0x0001;
    return state;
}

void HostVideo::clear()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

void HostVideo::present()
{
    static std::vector<std::uint32_t> frame(1024 * 512, 0);
    SDL_UpdateTexture(texture, nullptr, frame.data(), 1024 * sizeof(std::uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void HostVideo::present_vram(
    const std::array<std::uint16_t, 1024 * 512>& vram,
    std::uint32_t display_x,
    std::uint32_t display_y,
    std::uint32_t display_width,
    std::uint32_t display_height
)
{
    static std::vector<std::uint32_t> frame(1024 * 512);
    std::fill(frame.begin(), frame.end(), 0xFF000000);
    bool has_vram_content = false;
    for (std::uint32_t y = 0; y < display_height && display_y + y < 512; ++y) {
        for (std::uint32_t x = 0; x < display_width && display_x + x < 1024; ++x) {
            const std::uint16_t pixel = vram[(display_y + y) * 1024 + display_x + x];
            if (pixel != 0) {
                has_vram_content = true;
            }
            const std::uint32_t r = (pixel & 0x1F) << 3;
            const std::uint32_t g = ((pixel >> 5) & 0x1F) << 3;
            const std::uint32_t b = ((pixel >> 10) & 0x1F) << 3;
            frame[y * 1024 + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
    if (!has_vram_content) {
        for (std::uint32_t y = 0; y < 512; ++y) {
            for (std::uint32_t x = 0; x < 1024; ++x) {
                const bool border = x < 8 || y < 8 || x >= 1016 || y >= 504;
                const bool grid = (x % 64 == 0) || (y % 64 == 0);
                frame[y * 1024 + x] =
                    border ? 0xFF00A0FF :
                    grid ? 0xFF244878 :
                    0xFF102038;
            }
        }
    }
    SDL_UpdateTexture(texture, nullptr, frame.data(), 1024 * sizeof(std::uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

}
