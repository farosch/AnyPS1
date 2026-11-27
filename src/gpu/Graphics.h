#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anyps1 {

enum class GraphicsPrimitive {
    Polygon,
    TexturedPolygon,
    Sprite,
    Rectangle,
    Framebuffer
};

enum class GraphicsCommandType {
    SetDisplayEnable,
    SetDisplayMode,
    SetDisplayArea,
    SetDrawArea,
    SetDrawOffset,
    SetTextureWindow,
    SetDmaDirection,
    VRAMTransfer,
    Draw
};

struct GraphicsVertex {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint16_t u = 0;
    std::uint16_t v = 0;
    std::uint32_t color = 0;
};

struct GraphicsCommand {
    GraphicsCommandType type = GraphicsCommandType::Draw;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
    GraphicsPrimitive primitive = GraphicsPrimitive::Polygon;
    std::vector<GraphicsVertex> vertices;
};

struct GraphicsIr {
    std::uint32_t display_width = 320;
    std::uint32_t display_height = 240;
    std::vector<GraphicsCommand> commands;
};

enum class VulkanBackendPlatform {
    Linux,
    Windows
};

struct VulkanResourcePlan {
    std::uint32_t framebuffer_width = 1024;
    std::uint32_t framebuffer_height = 512;
    std::vector<std::uint32_t> pipeline_primitives;
    std::vector<std::uint32_t> transfer_word_offsets;
};

struct SpirvShader {
    GraphicsPrimitive primitive = GraphicsPrimitive::Polygon;
    std::vector<std::uint32_t> words;
};

GraphicsIr lower_gp0_gp1(
    const std::vector<std::uint32_t>& gp0_words,
    const std::vector<std::uint32_t>& gp1_words);

VulkanResourcePlan lower_graphics_ir_to_vulkan(
    const GraphicsIr& graphics,
    VulkanBackendPlatform platform);

SpirvShader emit_spirv_shader(
    GraphicsPrimitive primitive,
    bool textured,
    bool blending);

bool validate_spirv_words(const std::vector<std::uint32_t>& words);
std::string graphics_primitive_name(GraphicsPrimitive primitive);

}
