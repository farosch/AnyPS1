#include "gpu/Graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    try {
        const std::vector<std::uint32_t> gp1{
            0x03000000u,
            0x05010040u,
            0x07000000u
        };
        const std::vector<std::uint32_t> gp0{
            0x20FFFFFFu,
            0x00000010u,
            0x00100000u,
            0x00100010u
        };

        const anyps1::GraphicsIr graphics =
            anyps1::lower_gp0_gp1(gp0, gp1);
        if (graphics.commands.size() != 4 ||
            graphics.display_width != 320 ||
            graphics.display_height != 240) {
            throw std::runtime_error("unexpected graphics IR");
        }

        const anyps1::VulkanResourcePlan plan =
            anyps1::lower_graphics_ir_to_vulkan(
                graphics, anyps1::VulkanBackendPlatform::Linux);
        if (plan.framebuffer_width != 320 ||
            plan.pipeline_primitives.size() != 1) {
            throw std::runtime_error("unexpected Vulkan resource plan");
        }

        const anyps1::GraphicsPrimitive primitives[] = {
            anyps1::GraphicsPrimitive::Polygon,
            anyps1::GraphicsPrimitive::TexturedPolygon,
            anyps1::GraphicsPrimitive::Sprite,
            anyps1::GraphicsPrimitive::Rectangle,
            anyps1::GraphicsPrimitive::Framebuffer
        };
        for (const anyps1::GraphicsPrimitive primitive : primitives) {
            const anyps1::SpirvShader shader =
                anyps1::emit_spirv_shader(primitive, true, true);
            if (!anyps1::validate_spirv_words(shader.words)) {
                throw std::runtime_error("invalid generated SPIR-V module");
            }

            const std::string path =
                "/tmp/anyps1-" + anyps1::graphics_primitive_name(primitive) + ".spv";
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char*>(shader.words.data()),
                static_cast<std::streamsize>(shader.words.size() * sizeof(std::uint32_t)));
            if (!output) throw std::runtime_error("cannot write SPIR-V module");
            output.close();

            if (std::system(nullptr) != 0) {
                const char* validator = std::getenv("SPIRV_VAL_EXECUTABLE");
                if (validator == nullptr || validator[0] == '\0') {
                    validator = "spirv-val";
                }
                const std::string validate = std::string("\"") + validator +
                    "\" \"" + path + "\"";
                if (std::system(validate.c_str()) != 0) {
                    throw std::runtime_error("spirv-val rejected generated shader");
                }
                const char* optimizer = std::getenv("SPIRV_OPT_EXECUTABLE");
                if (optimizer != nullptr && optimizer[0] != '\0') {
                    const std::string optimized = "/tmp/anyps1-" +
                        anyps1::graphics_primitive_name(primitive) + "-opt.spv";
                    const std::string optimize = std::string("\"") + optimizer +
                        "\" \"" + path + "\" -O -o \"" + optimized + "\"";
                    if (std::system(optimize.c_str()) != 0) {
                        throw std::runtime_error("spirv-opt failed to optimize generated shader");
                    }
                    std::remove(optimized.c_str());
                }
            }
            std::remove(path.c_str());
        }

        try {
            anyps1::lower_gp0_gp1({0x01000000u}, {});
            throw std::runtime_error("unsupported GP0 command unexpectedly accepted");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()).find("unsupported PS1 GP0") ==
                std::string::npos) {
                throw;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Graphics test failed: " << error.what() << "\n";
        return 1;
    }
}
