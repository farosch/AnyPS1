#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace anyps1 {

enum class Ps1Button : std::uint16_t {
    Select = 0,
    L3,
    R3,
    Start,
    Up,
    Right,
    Down,
    Left,
    L2,
    R2,
    L1,
    R1,
    Triangle,
    Circle,
    Cross,
    Square
};

enum class Ps1Analog {
    LeftX,
    LeftY,
    RightX,
    RightY
};

struct Ps1InputState {
    std::uint16_t buttons = 0xFFFFu;
    std::int16_t left_x = 0;
    std::int16_t left_y = 0;
    std::int16_t right_x = 0;
    std::int16_t right_y = 0;

    bool is_pressed(Ps1Button button) const;
};

struct InputBinding {
    enum class Source {
        ControllerButton,
        ControllerAxis,
        Keyboard,
        MouseButton,
        MouseAxis
    };

    Source source = Source::Keyboard;
    std::string value;
    bool negative = false;
};

struct InputConfig {
    std::unordered_map<std::uint16_t, std::vector<InputBinding>> button_bindings;
    std::unordered_map<std::uint16_t, std::vector<InputBinding>> analog_bindings;

    static InputConfig defaults();
};

bool load_input_config(
    const std::string& path,
    InputConfig& config,
    std::string& error);

InputConfig load_input_config_or_default(
    const std::string& path,
    std::string& error);

void press_ps1_button(Ps1InputState& state, Ps1Button button);
void release_ps1_button(Ps1InputState& state, Ps1Button button);

void apply_input_binding(
    Ps1InputState& state,
    Ps1Button button,
    bool active);

class SdlInputRuntime {
public:
    SdlInputRuntime();
    ~SdlInputRuntime();

    SdlInputRuntime(const SdlInputRuntime&) = delete;
    SdlInputRuntime& operator=(const SdlInputRuntime&) = delete;

    bool initialize(const InputConfig& config, std::string& error);
    bool poll(Ps1InputState& state, std::string& error);
    void shutdown();

private:
    struct Impl;
    Impl* impl_;
};

}
