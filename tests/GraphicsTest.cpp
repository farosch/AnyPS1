#include "gpu/Graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    try {
        anyps1::VulkanSwapchainRuntime vulkan_runtime;
        std::string vulkan_error;
        if (vulkan_runtime.present(vulkan_error)) {
            throw std::runtime_error("uninitialized Vulkan swapchain unexpectedly presented");
        }
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
        const anyps1::VulkanResourcePlan windows_plan =
            anyps1::lower_graphics_ir_to_vulkan(
                graphics, anyps1::VulkanBackendPlatform::Windows);
        if (windows_plan.framebuffer_width != plan.framebuffer_width ||
            windows_plan.framebuffer_height != plan.framebuffer_height ||
            windows_plan.pipeline_primitives != plan.pipeline_primitives ||
            windows_plan.transfer_word_offsets != plan.transfer_word_offsets ||
            windows_plan.transfer_directions != plan.transfer_directions ||
            windows_plan.dithering != plan.dithering ||
            windows_plan.display_mode != plan.display_mode ||
            windows_plan.draw_area.x != plan.draw_area.x ||
            windows_plan.draw_area.y != plan.draw_area.y ||
            windows_plan.draw_area.width != plan.draw_area.width ||
            windows_plan.draw_area.height != plan.draw_area.height ||
            windows_plan.texture_window.x != plan.texture_window.x ||
            windows_plan.texture_window.y != plan.texture_window.y ||
            windows_plan.texture_window.width != plan.texture_window.width ||
            windows_plan.texture_window.height != plan.texture_window.height) {
            throw std::runtime_error(
                "Linux and Windows GPU resource plans differ");
        }

        const anyps1::GraphicsPrimitive primitives[] = {
            anyps1::GraphicsPrimitive::Polygon,
            anyps1::GraphicsPrimitive::TexturedPolygon,
            anyps1::GraphicsPrimitive::Sprite,
            anyps1::GraphicsPrimitive::Rectangle,
            anyps1::GraphicsPrimitive::Line,
            anyps1::GraphicsPrimitive::Framebuffer
        };
        std::vector<anyps1::SpirvShader> primitive_shaders;
        for (const anyps1::GraphicsPrimitive primitive : primitives) {
            const anyps1::SpirvShader shader =
                anyps1::emit_spirv_shader(primitive, true, true);
            if (!anyps1::validate_spirv_words(shader.words)) {
                throw std::runtime_error("invalid generated SPIR-V module");
            }
            primitive_shaders.push_back(shader);

            std::filesystem::path spv_directory =
                std::filesystem::temp_directory_path();
            const std::string path =
                (spv_directory /
                 ("anyps1-" + anyps1::graphics_primitive_name(primitive) + ".spv"))
                    .string();
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char*>(shader.words.data()),
                static_cast<std::streamsize>(shader.words.size() * sizeof(std::uint32_t)));
            if (!output) throw std::runtime_error("cannot write SPIR-V module");
            output.close();

            if (std::system(nullptr) != 0) {
                const char* validator = std::getenv("SPIRV_VAL_EXECUTABLE");
                if (validator != nullptr && validator[0] != '\0'
                    && std::string(validator).find("-NOTFOUND") == std::string::npos) {
                    const std::string validate = std::string("\"") + validator +
                        "\" \"" + path + "\"";
                    if (std::system(validate.c_str()) != 0) {
                        throw std::runtime_error("spirv-val rejected generated shader");
                    }
                }
                const char* optimizer = std::getenv("SPIRV_OPT_EXECUTABLE");
                if (optimizer != nullptr && optimizer[0] != '\0'
                    && std::string(optimizer).find("-NOTFOUND") == std::string::npos) {
                    const std::string optimized =
                        (std::filesystem::temp_directory_path() /
                         ("anyps1-" + anyps1::graphics_primitive_name(primitive) +
                          "-opt.spv"))
                            .string();
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
        if (primitive_shaders.size() != 6u) {
            throw std::runtime_error("missing primitive-specific SPIR-V modules");
        }
        for (std::size_t index = 1u; index < primitive_shaders.size(); ++index) {
            if (primitive_shaders[index].words ==
                primitive_shaders[index - 1u].words) {
                throw std::runtime_error("primitive SPIR-V modules are identical");
            }
        }

        const anyps1::GraphicsIr state_graphics = anyps1::lower_gp0_gp1(
            {
                0xE2000000u,
                0xE3002000u,
                0xE4000100u,
                0xA0000000u,
                0x00000010u,
                0x00020002u,
                0xFFFFFFFFu,
                0x12345678u,
                0xC0000000u,
                0x00000010u,
                0x00020002u
            },
            {
                0x03000000u,
                0x04000002u,
                0x05010040u,
                0x07000008u
            });
        if (!state_graphics.state.display_enabled ||
            state_graphics.state.dma_direction != 2u ||
            state_graphics.commands.empty()) {
            throw std::runtime_error("GPU state payload lowering failed");
        }
        bool host_to_vram = false;
        bool vram_to_host = false;
        for (const anyps1::GraphicsCommand& command : state_graphics.commands) {
            host_to_vram |=
                command.transfer_direction ==
                anyps1::GraphicsVramTransferDirection::HostToVram;
            vram_to_host |=
                command.transfer_direction ==
                anyps1::GraphicsVramTransferDirection::VramToHost;
        }
        if (!host_to_vram || !vram_to_host) {
            throw std::runtime_error("GPU VRAM transfer directions were not modeled");
        }

        std::vector<std::uint32_t> vram(1024u * 512u, 0u);
        const anyps1::GraphicsRect destination{16u, 16u, 2u, 2u};
        const std::vector<std::uint32_t> rgba{0xFF0000FFu, 0xFF00FF00u,
                                              0xFFFF0000u, 0xFFFFFFFFu};
        std::string transfer_error;
        if (!anyps1::copy_host_to_vram(rgba, destination, 1024u, vram,
                                       transfer_error)) {
            throw std::runtime_error("host-to-VRAM copy failed: " +
                                     transfer_error);
        }
        std::vector<std::uint32_t> host_pixels;
        if (!anyps1::copy_vram_to_host(vram, destination, 1024u,
                                       host_pixels, transfer_error) ||
            host_pixels.size() != 4u) {
            throw std::runtime_error("VRAM-to-host copy failed: " +
                                     transfer_error);
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
        try {
            anyps1::lower_gp0_gp1({}, {0x09000000u});
            throw std::runtime_error("unsupported GP1 command unexpectedly accepted");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()).find("unsupported PS1 GP1") ==
                std::string::npos) {
                throw;
            }
        }
        try {
            anyps1::lower_gp0_gp1({0x20FFFFFFu, 0x00000010u}, {});
            throw std::runtime_error("truncated GP0 polygon unexpectedly accepted");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()).find("truncated PS1 GPU command") ==
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
