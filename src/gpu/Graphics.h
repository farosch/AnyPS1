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
    Line,
    Framebuffer
};

enum class GraphicsColorMode {
    Rgb16,
    Rgb24,
    Rgb16Clut
};

enum class GraphicsBlendMode {
    HalfBackground,
    SourcePlusBackground,
    SourceMinusBackground,
    SourcePlusQuarterBackground
};

enum class GraphicsTexturePageMode {
    Raw,
    Clut4,
    Clut8,
    Direct15
};

enum class GraphicsVramTransferDirection {
    None,
    HostToVram,
    VramToHost,
    VramToVram
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

struct GraphicsRect {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

struct GraphicsState {
    bool display_enabled = true;
    bool dithering = false;
    std::uint32_t display_mode = 0;
    GraphicsRect display_area;
    GraphicsRect draw_area;
    GraphicsRect texture_window;
    std::int16_t draw_offset_x = 0;
    std::int16_t draw_offset_y = 0;
    std::uint32_t dma_direction = 0;
};

struct GraphicsCommand {
    GraphicsCommandType type = GraphicsCommandType::Draw;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
    GraphicsState state;
    GraphicsRect rect;
    GraphicsColorMode color_mode = GraphicsColorMode::Rgb24;
    GraphicsTexturePageMode texture_mode = GraphicsTexturePageMode::Raw;
    GraphicsBlendMode blend_mode = GraphicsBlendMode::HalfBackground;
    GraphicsVramTransferDirection transfer_direction =
        GraphicsVramTransferDirection::None;
    bool textured = false;
    bool semi_transparent = false;
    bool read_framebuffer = false;
    bool dithering = false;
    GraphicsPrimitive primitive = GraphicsPrimitive::Polygon;
    std::vector<GraphicsVertex> vertices;
};

struct GraphicsIr {
    std::uint32_t display_width = 320;
    std::uint32_t display_height = 240;
    GraphicsState state;
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
    std::vector<std::uint32_t> transfer_directions;
    bool dithering = false;
    std::uint32_t display_mode = 0;
    GraphicsRect draw_area;
    GraphicsRect texture_window;
};

struct SpirvShader {
    GraphicsPrimitive primitive = GraphicsPrimitive::Polygon;
    std::vector<std::uint32_t> words;
};

class VulkanSwapchainRuntime {
public:
    VulkanSwapchainRuntime();
    ~VulkanSwapchainRuntime();

    bool initialize(
        const std::string& title,
        std::uint32_t width,
        std::uint32_t height,
        std::string& error);
    bool present(std::string& error);
    void shutdown();

private:
    struct Impl;
    Impl* impl_;
};

GraphicsIr lower_gp0_gp1(
    const std::vector<std::uint32_t>& gp0_words,
    const std::vector<std::uint32_t>& gp1_words);

bool decode_ps1_clut4(
    std::uint16_t texel,
    std::uint16_t clut_word,
    std::uint32_t& rgba);

std::uint32_t ps1_texel_to_rgba(
    std::uint16_t texel,
    GraphicsTexturePageMode mode,
    std::uint16_t clut_word);

bool copy_vram_to_host(
    const std::vector<std::uint32_t>& vram,
    GraphicsRect source,
    std::uint32_t vram_width,
    std::vector<std::uint32_t>& host_pixels,
    std::string& error);

bool copy_host_to_vram(
    const std::vector<std::uint32_t>& host_pixels,
    GraphicsRect destination,
    std::uint32_t vram_width,
    std::vector<std::uint32_t>& vram,
    std::string& error);

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
