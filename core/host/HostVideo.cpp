#include "HostVideo.h"
#include "runtime/Runtime.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace anyps1 {

HostVideo::HostVideo()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
    int gamepad_count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count);
    if (gamepads && gamepad_count > 0) {
        gamepad = SDL_OpenGamepad(gamepads[0]);
    }
    SDL_free(gamepads);
}

HostVideo::~HostVideo()
{
    if (gamepad) {
        SDL_CloseGamepad(gamepad);
    }
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
    if (keys[SDL_SCANCODE_UP]) state &= ~0x0010;
    if (keys[SDL_SCANCODE_DOWN]) state &= ~0x0040;
    if (keys[SDL_SCANCODE_RETURN]) state &= ~0x0008;
    if (keys[SDL_SCANCODE_ESCAPE]) state &= ~0x0001;
    if (keys[SDL_SCANCODE_Z]) state &= ~0x4000;
    if (keys[SDL_SCANCODE_X]) state &= ~0x8000;
    if (keys[SDL_SCANCODE_C]) state &= ~0x2000;
    if (keys[SDL_SCANCODE_V]) state &= ~0x1000;
    if (keys[SDL_SCANCODE_Q]) state &= ~0x0400;
    if (keys[SDL_SCANCODE_E]) state &= ~0x0800;
    if (keys[SDL_SCANCODE_A]) state &= ~0x0100;
    if (keys[SDL_SCANCODE_D]) state &= ~0x0200;
    if (keys[SDL_SCANCODE_F]) state &= ~0x0002;
    if (keys[SDL_SCANCODE_G]) state &= ~0x0004;
    return state;
}

ControllerState HostVideo::controller_state() const
{
    ControllerState result;
    result.digital = pad_state();
    if (!gamepad) {
        return result;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) result.digital &= ~0x0080;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) result.digital &= ~0x0020;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) result.digital &= ~0x0010;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) result.digital &= ~0x0040;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)) result.digital &= ~0x0008;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK)) result.digital &= ~0x0001;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) result.digital &= ~0x4000;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)) result.digital &= ~0x2000;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST)) result.digital &= ~0x8000;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH)) result.digital &= ~0x1000;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) result.digital &= ~0x0400;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) result.digital &= ~0x0800;

    const Sint16 left_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    const Sint16 left_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    const Sint16 right_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    const Sint16 right_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    result.left_x = static_cast<std::uint8_t>((static_cast<int>(left_x) + 32768) / 257);
    result.left_y = static_cast<std::uint8_t>((static_cast<int>(left_y) + 32768) / 257);
    result.right_x = static_cast<std::uint8_t>((static_cast<int>(right_x) + 32768) / 257);
    result.right_y = static_cast<std::uint8_t>((static_cast<int>(right_y) + 32768) / 257);
    return result;
}

void HostVideo::apply_controller_rumble(std::uint32_t small, std::uint32_t large)
{
    if (gamepad) {
        SDL_RumbleGamepad(gamepad, static_cast<Uint16>(small), static_cast<Uint16>(large), 100);
    }
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
