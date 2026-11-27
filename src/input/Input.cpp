#include "input/Input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace anyps1 {

namespace {

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::uint16_t ps1_button_bit(Ps1Button button)
{
    return static_cast<std::uint16_t>(1u << static_cast<std::uint16_t>(button));
}

std::optional<Ps1Button> parse_ps1_button(const std::string& value)
{
    const std::string name = lower(trim(value));
    if (name == "select") return Ps1Button::Select;
    if (name == "l3") return Ps1Button::L3;
    if (name == "r3") return Ps1Button::R3;
    if (name == "start") return Ps1Button::Start;
    if (name == "up") return Ps1Button::Up;
    if (name == "right") return Ps1Button::Right;
    if (name == "down") return Ps1Button::Down;
    if (name == "left") return Ps1Button::Left;
    if (name == "l2") return Ps1Button::L2;
    if (name == "r2") return Ps1Button::R2;
    if (name == "l1") return Ps1Button::L1;
    if (name == "r1") return Ps1Button::R1;
    if (name == "triangle") return Ps1Button::Triangle;
    if (name == "circle") return Ps1Button::Circle;
    if (name == "cross" || name == "x") return Ps1Button::Cross;
    if (name == "square") return Ps1Button::Square;
    return std::nullopt;
}

std::optional<Ps1Analog> parse_ps1_analog(const std::string& value)
{
    const std::string name = lower(trim(value));
    if (name == "left_x") return Ps1Analog::LeftX;
    if (name == "left_y") return Ps1Analog::LeftY;
    if (name == "right_x") return Ps1Analog::RightX;
    if (name == "right_y") return Ps1Analog::RightY;
    return std::nullopt;
}

InputBinding parse_binding(const std::string& value)
{
    InputBinding binding;
    std::string name = lower(trim(value));
    bool negative = false;
    if (!name.empty() && name[0] == '-') {
        negative = true;
        name = trim(name.substr(1));
    }
    binding.negative = negative;

    if (name.rfind("controller_axis:", 0) == 0) {
        binding.source = InputBinding::Source::ControllerAxis;
        binding.value = trim(name.substr(16));
    } else if (name.rfind("controller:", 0) == 0) {
        binding.source = InputBinding::Source::ControllerButton;
        binding.value = trim(name.substr(11));
    } else if (name.rfind("mouse_axis:", 0) == 0) {
        binding.source = InputBinding::Source::MouseAxis;
        binding.value = trim(name.substr(11));
    } else if (name.rfind("mouse:", 0) == 0) {
        binding.source = InputBinding::Source::MouseButton;
        binding.value = trim(name.substr(6));
    } else if (name.rfind("key:", 0) == 0) {
        binding.source = InputBinding::Source::Keyboard;
        binding.value = trim(name.substr(4));
    } else {
        const SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            binding.source = InputBinding::Source::Keyboard;
            binding.value = name;
        } else {
            binding.source = InputBinding::Source::ControllerButton;
            binding.value = name;
        }
    }
    return binding;
}

SDL_GamepadAxis parse_controller_axis(const std::string& name)
{
    if (name == "leftx") return SDL_GAMEPAD_AXIS_LEFTX;
    if (name == "lefty") return SDL_GAMEPAD_AXIS_LEFTY;
    if (name == "rightx") return SDL_GAMEPAD_AXIS_RIGHTX;
    if (name == "righty") return SDL_GAMEPAD_AXIS_RIGHTY;
    if (name == "lefttrigger") return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    if (name == "righttrigger") return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    return SDL_GAMEPAD_AXIS_INVALID;
}

SDL_GamepadButton parse_controller_button(const std::string& name)
{
    if (name == "south" || name == "cross") return SDL_GAMEPAD_BUTTON_SOUTH;
    if (name == "east" || name == "circle") return SDL_GAMEPAD_BUTTON_EAST;
    if (name == "west" || name == "square") return SDL_GAMEPAD_BUTTON_WEST;
    if (name == "north" || name == "triangle") return SDL_GAMEPAD_BUTTON_NORTH;
    if (name == "back" || name == "select") return SDL_GAMEPAD_BUTTON_BACK;
    if (name == "start") return SDL_GAMEPAD_BUTTON_START;
    if (name == "left_stick" || name == "l3") return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (name == "right_stick" || name == "r3") return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (name == "left_shoulder" || name == "l1") return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (name == "right_shoulder" || name == "r1") return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (name == "dpad_up" || name == "up") return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (name == "dpad_down" || name == "down") return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (name == "dpad_left" || name == "left") return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (name == "dpad_right" || name == "right") return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    return SDL_GAMEPAD_BUTTON_INVALID;
}

}

bool Ps1InputState::is_pressed(Ps1Button button) const
{
    return (buttons & ps1_button_bit(button)) == 0;
}

InputConfig InputConfig::defaults()
{
    InputConfig config;

    const auto controller_button =
        [&](Ps1Button button, const char* controller_name) {
            config.button_bindings[static_cast<std::uint16_t>(button)].push_back(
                {InputBinding::Source::ControllerButton, controller_name});
        };
    controller_button(Ps1Button::Select, "back");
    controller_button(Ps1Button::Start, "start");
    controller_button(Ps1Button::Up, "dpad_up");
    controller_button(Ps1Button::Down, "dpad_down");
    controller_button(Ps1Button::Left, "dpad_left");
    controller_button(Ps1Button::Right, "dpad_right");
    controller_button(Ps1Button::L1, "left_shoulder");
    controller_button(Ps1Button::R1, "right_shoulder");
    controller_button(Ps1Button::L3, "left_stick");
    controller_button(Ps1Button::R3, "right_stick");
    controller_button(Ps1Button::Triangle, "north");
    controller_button(Ps1Button::Circle, "east");
    controller_button(Ps1Button::Cross, "south");
    controller_button(Ps1Button::Square, "west");
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::L2)].push_back(
        {InputBinding::Source::ControllerAxis, "lefttrigger"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::R2)].push_back(
        {InputBinding::Source::ControllerAxis, "righttrigger"});

    config.analog_bindings[static_cast<std::uint16_t>(Ps1Analog::LeftX)].push_back(
        {InputBinding::Source::ControllerAxis, "leftx"});
    config.analog_bindings[static_cast<std::uint16_t>(Ps1Analog::LeftY)].push_back(
        {InputBinding::Source::ControllerAxis, "lefty"});
    config.analog_bindings[static_cast<std::uint16_t>(Ps1Analog::RightX)].push_back(
        {InputBinding::Source::ControllerAxis, "rightx"});
    config.analog_bindings[static_cast<std::uint16_t>(Ps1Analog::RightY)].push_back(
        {InputBinding::Source::ControllerAxis, "righty"});

    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Up)].push_back(
        {InputBinding::Source::Keyboard, "w"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Down)].push_back(
        {InputBinding::Source::Keyboard, "s"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Left)].push_back(
        {InputBinding::Source::Keyboard, "a"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Right)].push_back(
        {InputBinding::Source::Keyboard, "d"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Cross)].push_back(
        {InputBinding::Source::Keyboard, "j"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Circle)].push_back(
        {InputBinding::Source::Keyboard, "k"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Square)].push_back(
        {InputBinding::Source::Keyboard, "u"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Triangle)].push_back(
        {InputBinding::Source::Keyboard, "i"});
    config.button_bindings[static_cast<std::uint16_t>(Ps1Button::Start)].push_back(
        {InputBinding::Source::Keyboard, "return"});

    return config;
}

void press_ps1_button(Ps1InputState& state, Ps1Button button)
{
    state.buttons &= ~ps1_button_bit(button);
}

void release_ps1_button(Ps1InputState& state, Ps1Button button)
{
    state.buttons |= ps1_button_bit(button);
}

void apply_input_binding(Ps1InputState& state, Ps1Button button, bool active)
{
    if (active) press_ps1_button(state, button);
    else release_ps1_button(state, button);
}

bool load_input_config(
    const std::string& path,
    InputConfig& config,
    std::string& error)
{
    std::ifstream input(path);
    if (!input) {
        error = "cannot open input configuration: " + path;
        return false;
    }

    config = InputConfig::defaults();
    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            error = "invalid input configuration line: " + line;
            return false;
        }

        const std::string key = lower(trim(line.substr(0, separator)));
        const std::string value = trim(line.substr(separator + 1));
        if (section == "keyboard" || section == "mouse" ||
            section == "controller" || section == "bindings") {
            const std::optional<Ps1Button> button = parse_ps1_button(key);
            if (button) {
                InputBinding binding = parse_binding(value);
                if (section == "keyboard") binding.source = InputBinding::Source::Keyboard;
                if (section == "mouse") {
                    binding.source =
                        value.rfind("axis:", 0) == 0
                            ? InputBinding::Source::MouseAxis
                            : InputBinding::Source::MouseButton;
                }
                if (section == "controller") {
                    binding.source =
                        value.rfind("axis:", 0) == 0
                            ? InputBinding::Source::ControllerAxis
                            : InputBinding::Source::ControllerButton;
                }
                config.button_bindings[static_cast<std::uint16_t>(*button)].push_back(binding);
            } else {
                const std::optional<Ps1Analog> analog = parse_ps1_analog(key);
                if (!analog) {
                    error = "unknown input mapping key: " + key;
                    return false;
                }
                config.analog_bindings[static_cast<std::uint16_t>(*analog)].push_back(
                    parse_binding(value));
            }
        } else {
            error = "unknown input configuration section: " + section;
            return false;
        }
    }
    error.clear();
    return true;
}

InputConfig load_input_config_or_default(
    const std::string& path,
    std::string& error)
{
    if (path.empty()) {
        error.clear();
        return InputConfig::defaults();
    }
    InputConfig config;
    if (load_input_config(path, config, error)) return config;
    config = InputConfig::defaults();
    return config;
}

struct SdlInputRuntime::Impl {
    InputConfig config;
    std::vector<SDL_Gamepad*> gamepads;
};

SdlInputRuntime::SdlInputRuntime()
    : impl_(new Impl{})
{
}

SdlInputRuntime::~SdlInputRuntime()
{
    shutdown();
    delete impl_;
}

bool SdlInputRuntime::initialize(const InputConfig& config, std::string& error)
{
    impl_->config = config;
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        error = "cannot initialize SDL input: " + std::string(SDL_GetError());
        return false;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr) {
        for (int index = 0; index < count; ++index) {
            SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[index]);
            if (gamepad != nullptr) impl_->gamepads.push_back(gamepad);
        }
        SDL_free(ids);
    }
    error.clear();
    return true;
}

bool SdlInputRuntime::poll(Ps1InputState& state, std::string& error)
{
    SDL_PumpEvents();
    bool rescan_gamepads = false;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED ||
            event.type == SDL_EVENT_GAMEPAD_REMOVED ||
            event.type == SDL_EVENT_GAMEPAD_REMAPPED) {
            rescan_gamepads = true;
        }
    }
    if (rescan_gamepads) {
        for (SDL_Gamepad* gamepad : impl_->gamepads) {
            if (gamepad != nullptr) SDL_CloseGamepad(gamepad);
        }
        impl_->gamepads.clear();
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids != nullptr) {
            for (int index = 0; index < count; ++index) {
                SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[index]);
                if (gamepad != nullptr) impl_->gamepads.push_back(gamepad);
            }
            SDL_free(ids);
        }
    }
    state = {};
    state.buttons = 0xFFFFu;

    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    const SDL_MouseButtonFlags mouse_buttons =
        SDL_GetMouseState(&mouse_x, &mouse_y);
    float relative_mouse_x = 0.0f;
    float relative_mouse_y = 0.0f;
    SDL_GetRelativeMouseState(&relative_mouse_x, &relative_mouse_y);

    for (const auto& binding_entry : impl_->config.button_bindings) {
        const Ps1Button button =
            static_cast<Ps1Button>(binding_entry.first);
        bool active = false;
        for (const InputBinding& binding : binding_entry.second) {
            if (binding.source == InputBinding::Source::Keyboard) {
                const SDL_Scancode scancode =
                    SDL_GetScancodeFromName(binding.value.c_str());
                if (scancode >= 0 &&
                    scancode < key_count &&
                    keys[scancode]) {
                    active = true;
                }
            } else if (binding.source == InputBinding::Source::MouseButton) {
                const std::string name = lower(binding.value);
                SDL_MouseButtonFlags mask = 0;
                if (name == "left") mask = SDL_BUTTON_LMASK;
                else if (name == "middle") mask = SDL_BUTTON_MMASK;
                else if (name == "right") mask = SDL_BUTTON_RMASK;
                if ((mouse_buttons & mask) != 0) active = true;
            } else if (binding.source == InputBinding::Source::ControllerButton &&
                       !impl_->gamepads.empty()) {
                const SDL_GamepadButton mapped =
                    parse_controller_button(binding.value);
                if (mapped != SDL_GAMEPAD_BUTTON_INVALID &&
                    SDL_GetGamepadButton(impl_->gamepads.front(), mapped)) {
                    active = true;
                }
            } else if (binding.source == InputBinding::Source::ControllerAxis &&
                       !impl_->gamepads.empty()) {
                const SDL_GamepadAxis axis =
                    parse_controller_axis(binding.value);
                if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                    axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    const std::int16_t value =
                        SDL_GetGamepadAxis(impl_->gamepads.front(), axis);
                    if (value > 8192) active = true;
                }
            }
        }
        apply_input_binding(state, button, active);
    }

    for (const auto& binding_entry : impl_->config.analog_bindings) {
        const Ps1Analog analog =
            static_cast<Ps1Analog>(binding_entry.first);
        for (const InputBinding& binding : binding_entry.second) {
            if (binding.source != InputBinding::Source::MouseAxis) continue;
            const float movement =
                binding.value == "x" || binding.value == "leftx" || binding.value == "rightx"
                    ? relative_mouse_x
                    : relative_mouse_y;
            std::int64_t scaled =
                static_cast<std::int64_t>(movement * 128.0f);
            if (scaled > 32767) scaled = 32767;
            if (scaled < -32768) scaled = -32768;
            const std::int16_t value =
                binding.negative
                    ? static_cast<std::int16_t>(-scaled)
                    : static_cast<std::int16_t>(scaled);
            if (analog == Ps1Analog::LeftX) state.left_x = value;
            if (analog == Ps1Analog::LeftY) state.left_y = value;
            if (analog == Ps1Analog::RightX) state.right_x = value;
            if (analog == Ps1Analog::RightY) state.right_y = value;
        }
    }

    if (!impl_->gamepads.empty()) {
        SDL_Gamepad* gamepad = impl_->gamepads.front();
        for (const auto& binding_entry : impl_->config.analog_bindings) {
            const Ps1Analog analog =
                static_cast<Ps1Analog>(binding_entry.first);
            for (const InputBinding& binding : binding_entry.second) {
                if (binding.source != InputBinding::Source::ControllerAxis) continue;
                const SDL_GamepadAxis axis =
                    parse_controller_axis(binding.value);
                if (axis == SDL_GAMEPAD_AXIS_INVALID) continue;
                const std::int16_t value =
                    SDL_GetGamepadAxis(gamepad, axis);
                if (analog == Ps1Analog::LeftX) state.left_x = value;
                if (analog == Ps1Analog::LeftY) state.left_y = binding.negative ? -value : value;
                if (analog == Ps1Analog::RightX) state.right_x = value;
                if (analog == Ps1Analog::RightY) state.right_y = binding.negative ? -value : value;
            }
        }

    }

    error.clear();
    return true;
}

void SdlInputRuntime::shutdown()
{
    if (impl_ == nullptr) return;
    for (SDL_Gamepad* gamepad : impl_->gamepads) {
        SDL_CloseGamepad(gamepad);
    }
    impl_->gamepads.clear();
    SDL_Quit();
}

}
