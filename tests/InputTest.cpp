#include "input/Input.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: anyps1_input_test <input-ini>");
        }
        std::string error;
        anyps1::InputConfig config;
        if (!anyps1::load_input_config(
                argv[1],
                config,
                error)) {
            throw std::runtime_error(error);
        }

        anyps1::Ps1InputState state;
        anyps1::apply_input_binding(
            state,
            anyps1::Ps1Button::Cross,
            true);
        if (!state.is_pressed(anyps1::Ps1Button::Cross)) {
            throw std::runtime_error("cross button binding was not applied");
        }
        anyps1::apply_input_binding(
            state,
            anyps1::Ps1Button::Cross,
            false);
        if (state.is_pressed(anyps1::Ps1Button::Cross)) {
            throw std::runtime_error("cross button binding was not released");
        }

        if (config.button_bindings.count(
                static_cast<std::uint16_t>(anyps1::Ps1Button::Up)) == 0 ||
            config.button_bindings.count(
                static_cast<std::uint16_t>(anyps1::Ps1Button::Circle)) == 0) {
            throw std::runtime_error("fixture bindings were not parsed");
        }
        if (config.analog_bindings.count(
                static_cast<std::uint16_t>(anyps1::Ps1Analog::LeftX)) == 0 ||
            config.analog_bindings.count(
                static_cast<std::uint16_t>(anyps1::Ps1Analog::RightY)) == 0) {
            throw std::runtime_error("fixture analog bindings were not parsed");
        }

        anyps1::SdlInputRuntime runtime;
        if (!runtime.initialize(config, error)) {
            throw std::runtime_error(error);
        }
        anyps1::Ps1InputState polled;
        if (!runtime.poll(polled, error)) {
            throw std::runtime_error(error);
        }
        runtime.shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Input test failed: " << error.what() << "\n";
        return 1;
    }
}
