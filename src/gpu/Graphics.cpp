#include "gpu/Graphics.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace anyps1 {

namespace {

constexpr std::uint32_t SPIRV_MAGIC = 0x07230203u;

void require_payload(const std::vector<std::uint32_t>& words,
                     std::size_t index,
                     std::size_t count,
                     const char* command)
{
    if (index + count > words.size()) {
        throw std::runtime_error(std::string("truncated PS1 GPU command: ") + command);
    }
}

GraphicsVertex make_vertex(std::uint32_t position, std::uint32_t color)
{
    GraphicsVertex vertex;
    vertex.x = static_cast<std::int16_t>(position & 0xFFFFu);
    vertex.y = static_cast<std::int16_t>((position >> 16) & 0xFFFFu);
    vertex.color = color & 0x00FFFFFFu;
    return vertex;
}

bool rect_is_valid(const GraphicsRect& rect)
{
    return rect.width <= 1024u && rect.height <= 512u;
}

}

bool decode_ps1_clut4(
    std::uint16_t texel,
    std::uint16_t clut_word,
    std::uint32_t& rgba)
{
    if (texel >= 16u) {
        rgba = 0u;
        return false;
    }
    const std::uint16_t palette_texel =
        static_cast<std::uint16_t>((clut_word >> (texel * 1u)) & 0xFFFFu);
    rgba = ps1_texel_to_rgba(palette_texel, GraphicsTexturePageMode::Direct15,
                             clut_word);
    return rgba != 0u;
}

std::uint32_t ps1_texel_to_rgba(
    std::uint16_t texel,
    GraphicsTexturePageMode mode,
    std::uint16_t clut_word)
{
    (void)mode;
    (void)clut_word;
    if (texel == 0u) return 0u;
    const std::uint32_t red = (texel & 31u) * 255u / 31u;
    const std::uint32_t green = ((texel >> 5) & 31u) * 255u / 31u;
    const std::uint32_t blue = ((texel >> 10) & 31u) * 255u / 31u;
    const std::uint32_t alpha = ((texel >> 15) & 1u) != 0u ? 255u : 128u;
    return (alpha << 24) | (blue << 16) | (green << 8) | red;
}

bool copy_vram_to_host(
    const std::vector<std::uint32_t>& vram,
    GraphicsRect source,
    std::uint32_t vram_width,
    std::vector<std::uint32_t>& host_pixels,
    std::string& error)
{
    if (!rect_is_valid(source) || vram_width == 0u || vram_width > 1024u) {
        error = "invalid PS1 VRAM-to-host transfer";
        return false;
    }
    host_pixels.clear();
    host_pixels.reserve(static_cast<std::size_t>(source.width) * source.height);
    for (std::uint32_t y = 0; y < source.height; ++y) {
        for (std::uint32_t x = 0; x < source.width; ++x) {
            const std::uint32_t vram_x = source.x + x;
            const std::uint32_t vram_y = source.y + y;
            const std::uint32_t offset =
                vram_y * vram_width + vram_x;
            if (offset >= vram.size()) {
                error = "PS1 VRAM-to-host transfer exceeds VRAM";
                host_pixels.clear();
                return false;
            }
            host_pixels.push_back(ps1_texel_to_rgba(
                static_cast<std::uint16_t>(vram[offset] & 0xFFFFu),
                GraphicsTexturePageMode::Direct15, 0u));
        }
    }
    error.clear();
    return true;
}

bool copy_host_to_vram(
    const std::vector<std::uint32_t>& host_pixels,
    GraphicsRect destination,
    std::uint32_t vram_width,
    std::vector<std::uint32_t>& vram,
    std::string& error)
{
    if (!rect_is_valid(destination) || vram_width == 0u || vram_width > 1024u) {
        error = "invalid host-to-PS1-VRAM transfer";
        return false;
    }
    const std::uint32_t pixel_count =
        destination.width * destination.height;
    if (host_pixels.size() < pixel_count) {
        error = "host-to-VRAM transfer payload is shorter than the rectangle";
        return false;
    }
    for (std::uint32_t y = 0; y < destination.height; ++y) {
        for (std::uint32_t x = 0; x < destination.width; ++x) {
            const std::uint32_t vram_x = destination.x + x;
            const std::uint32_t vram_y = destination.y + y;
            const std::uint32_t offset = vram_y * vram_width + vram_x;
            if (offset >= vram.size()) {
                error = "host-to-VRAM transfer exceeds VRAM";
                return false;
            }
            const std::uint32_t rgba =
                host_pixels[y * destination.width + x];
            const std::uint32_t red = ((rgba & 0xFFu) * 31u + 127u) / 255u;
            const std::uint32_t green = (((rgba >> 8) & 0xFFu) * 31u + 127u) / 255u;
            const std::uint32_t blue = (((rgba >> 16) & 0xFFu) * 31u + 127u) / 255u;
            vram[offset] = (red) | (green << 5) | (blue << 10);
        }
    }
    error.clear();
    return true;
}

GraphicsIr lower_gp0_gp1(
    const std::vector<std::uint32_t>& gp0_words,
    const std::vector<std::uint32_t>& gp1_words)
{
    GraphicsIr result;

    for (std::uint32_t word : gp1_words) {
        const std::uint32_t command = (word >> 24) & 0xFFu;
        const std::uint32_t parameter = word & 0x00FFFFFFu;
        GraphicsCommand lowered;

        switch (command) {
        case 0x00:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = 0u;
            lowered.state = result.state;
            lowered.state.display_enabled = false;
            result.state = lowered.state;
            break;
        case 0x01:
        case 0x02:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = result.state.display_enabled ? 1u : 0u;
            lowered.state = result.state;
            break;
        case 0x03:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = parameter == 0u ? 1u : 0u;
            lowered.state = result.state;
            lowered.state.display_enabled = parameter == 0u;
            result.state = lowered.state;
            break;
        case 0x04:
            lowered.type = GraphicsCommandType::SetDmaDirection;
            lowered.first = parameter & 3u;
            lowered.state = result.state;
            lowered.state.dma_direction = parameter & 3u;
            result.state = lowered.state;
            break;
        case 0x05:
            lowered.type = GraphicsCommandType::SetDisplayArea;
            lowered.first = parameter & 0x3FFu;
            lowered.second = (parameter >> 10) & 0x1FFu;
            lowered.rect.x = static_cast<std::uint16_t>(lowered.first);
            lowered.rect.y = static_cast<std::uint16_t>(lowered.second);
            lowered.rect.width = 320u;
            lowered.rect.height = 240u;
            lowered.state = result.state;
            lowered.state.display_area = lowered.rect;
            result.state = lowered.state;
            break;
        case 0x06:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter & 0xFFFu;
            lowered.second = (parameter >> 12) & 0xFFFu;
            lowered.state = result.state;
            break;
        case 0x07:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter;
            result.display_width = (parameter & 1u) ? 640u : 320u;
            result.display_height = (parameter & 0x18u) ? 480u : 240u;
            result.state.display_mode = parameter;
            lowered.state = result.state;
            break;
        case 0x08:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter;
            lowered.second = word & 0x00FFFFFFu;
            lowered.state = result.state;
            lowered.state.display_mode = parameter;
            lowered.state.display_enabled = (parameter & 1u) == 0u;
            result.state = lowered.state;
            break;
        default:
            char buffer[9];
            std::snprintf(buffer, sizeof(buffer), "%08X", word);
            throw std::runtime_error(
                "unsupported PS1 GP1 GPU command 0x" + std::string(buffer));
        }
        result.commands.push_back(lowered);
    }

    for (std::size_t index = 0; index < gp0_words.size();) {
        const std::uint32_t word = gp0_words[index++];
        const std::uint32_t command = (word >> 24) & 0xFFu;
        GraphicsCommand lowered;

        switch (command) {
        case 0xE1: {
            lowered.type = GraphicsCommandType::SetTextureWindow;
            lowered.first = word & 0xFFFFFFu;
            const std::uint32_t mode = (word >> 7) & 3u;
            lowered.texture_mode =
                mode == 1u ? GraphicsTexturePageMode::Clut4 :
                mode == 2u ? GraphicsTexturePageMode::Clut8 :
                mode == 3u ? GraphicsTexturePageMode::Direct15 :
                             GraphicsTexturePageMode::Raw;
            lowered.semi_transparent = ((word >> 5) & 3u) != 0u;
            lowered.blend_mode =
                static_cast<GraphicsBlendMode>((word >> 5) & 3u);
            lowered.state = result.state;
            lowered.state.dithering = ((word >> 9) & 1u) != 0u;
            lowered.state.texture_window.x =
                static_cast<std::uint16_t>((word & 31u) * 8u);
            lowered.state.texture_window.y =
                static_cast<std::uint16_t>(((word >> 5) & 31u) * 8u);
            lowered.state.texture_window.width =
                static_cast<std::uint16_t>((((word >> 10) & 31u) + 1u) * 8u);
            lowered.state.texture_window.height =
                static_cast<std::uint16_t>((((word >> 15) & 31u) + 1u) * 8u);
            result.state = lowered.state;
            break;
        }
        case 0xE2:
        case 0xE3: {
            lowered.type = GraphicsCommandType::SetDrawArea;
            lowered.first = word & 0xFFFFFFu;
            const std::uint16_t left = static_cast<std::uint16_t>(word & 0x3FFu);
            const std::uint16_t top = static_cast<std::uint16_t>((word >> 10) & 0x3FFu);
            lowered.state = result.state;
            if (command == 0xE2) {
                lowered.rect = result.state.draw_area;
                lowered.rect.x = left;
                lowered.rect.y = top;
            } else {
                lowered.rect = result.state.draw_area;
                lowered.rect.width =
                    static_cast<std::uint16_t>(left > lowered.rect.x
                        ? left - lowered.rect.x : 0u);
                lowered.rect.height =
                    static_cast<std::uint16_t>(top > lowered.rect.y
                        ? top - lowered.rect.y : 0u);
            }
            lowered.state.draw_area = lowered.rect;
            result.state = lowered.state;
            break;
        }
        case 0xE4: {
            lowered.type = GraphicsCommandType::SetDrawOffset;
            lowered.first = word & 0xFFFFFFu;
            const std::int16_t offset_x =
                static_cast<std::int16_t>((word & 0x7FFu) - ((word & 0x800u) ? 0x800 : 0));
            const std::int16_t offset_y =
                static_cast<std::int16_t>(((word >> 11) & 0x7FFu)
                    - ((word & 0x400000u) ? 0x800 : 0));
            lowered.rect = {0u, 0u,
                            static_cast<std::uint16_t>(offset_x),
                            static_cast<std::uint16_t>(offset_y)};
            lowered.state = result.state;
            lowered.state.draw_offset_x = offset_x;
            lowered.state.draw_offset_y = offset_y;
            result.state = lowered.state;
            break;
        }
        case 0xE5:
        case 0xE6:
            lowered.type = GraphicsCommandType::SetTextureWindow;
            lowered.first = word & 0xFFFFFFu;
            lowered.state = result.state;
            break;
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23: {
            require_payload(gp0_words, index, 2, "GP0 polygon");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Polygon;
            lowered.semi_transparent = ((word >> 25) & 1u) != 0u;
            lowered.blend_mode = static_cast<GraphicsBlendMode>((word >> 24) & 3u);
            lowered.dithering = result.state.dithering;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27: {
            require_payload(gp0_words, index, 6, "GP0 textured polygon");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::TexturedPolygon;
            lowered.textured = true;
            lowered.semi_transparent = ((word >> 25) & 1u) != 0u;
            lowered.color_mode = ((word >> 24) & 1u)
                ? GraphicsColorMode::Rgb16
                : GraphicsColorMode::Rgb24;
            for (int vertex_index = 0; vertex_index < 3; ++vertex_index) {
                const std::uint32_t position = gp0_words[index++];
                const std::uint32_t uv = gp0_words[index++];
                GraphicsVertex vertex = make_vertex(position, word);
                vertex.u = uv & 0xFFu;
                vertex.v = (uv >> 8) & 0xFFu;
                lowered.vertices.push_back(vertex);
            }
            break;
        }
        case 0x28:
        case 0x29:
        case 0x2C:
        case 0x2D: {
            require_payload(gp0_words, index, 2, "GP0 variable rectangle");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Rectangle;
            lowered.textured = (command & 4u) != 0u;
            lowered.semi_transparent = ((word >> 25) & 1u) != 0u;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
        case 0x68:
        case 0x69:
        case 0x6Au:
        case 0x6Bu:
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78:
        case 0x79:
        case 0x7Au:
        case 0x7Bu:
        case 0x7Cu:
        case 0x7Du:
        case 0x7Eu:
        case 0x7Fu: {
            require_payload(gp0_words, index, 1, "GP0 fixed-size rectangle/sprite");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Sprite;
            lowered.textured = ((command & 0x20u) != 0u);
            lowered.semi_transparent = ((word >> 25) & 1u) != 0u;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            if (lowered.textured) {
                require_payload(gp0_words, index, 1, "GP0 textured sprite");
                GraphicsVertex& vertex = lowered.vertices.back();
                const std::uint32_t uv = gp0_words[index++];
                vertex.u = uv & 0xFFu;
                vertex.v = (uv >> 8) & 0xFFu;
            }
            break;
        }
        case 0x60:
        case 0x61:
        case 0x62:
        case 0x63: {
            require_payload(gp0_words, index, 1, "GP0 sprite");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Sprite;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4B:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x58:
        case 0x59:
        case 0x5A:
        case 0x5B: {
            const std::uint32_t vertex_count =
                (command & 0x10u) != 0u ? 2u : 1u;
            require_payload(gp0_words, index, vertex_count,
                            "GP0 line");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Line;
            lowered.semi_transparent = ((word >> 25) & 1u) != 0u;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            if (vertex_count == 2u) {
                lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            }
            break;
        }
        case 0xA0: {
            require_payload(gp0_words, index, 2, "GP0 CPU-to-VRAM transfer");
            lowered.type = GraphicsCommandType::VRAMTransfer;
            lowered.transfer_direction = GraphicsVramTransferDirection::HostToVram;
            lowered.first = gp0_words[index++];
            lowered.second = gp0_words[index++];
            const std::uint32_t width =
                lowered.second & 0xFFFFu;
            const std::uint32_t height =
                (lowered.second >> 16) & 0xFFFFu;
            const std::uint32_t pixel_words =
                width == 0u || height == 0u
                    ? 0u
                    : ((width * height + 1u) / 2u);
            if (pixel_words != 0u) {
                require_payload(gp0_words, index, pixel_words,
                                "GP0 CPU-to-VRAM payload");
                lowered.third = pixel_words;
                index += pixel_words;
            }
            lowered.rect.x = static_cast<std::uint16_t>(lowered.first & 0xFFFFu);
            lowered.rect.y = static_cast<std::uint16_t>((lowered.first >> 16) & 0xFFFFu);
            lowered.rect.width = static_cast<std::uint16_t>(width);
            lowered.rect.height = static_cast<std::uint16_t>(height);
            break;
        }
        case 0xC0: {
            require_payload(gp0_words, index, 2, "GP0 VRAM-to-CPU transfer");
            lowered.type = GraphicsCommandType::VRAMTransfer;
            lowered.primitive = GraphicsPrimitive::Framebuffer;
            lowered.read_framebuffer = true;
            lowered.transfer_direction = GraphicsVramTransferDirection::VramToHost;
            lowered.first = gp0_words[index++];
            lowered.second = gp0_words[index++];
            lowered.third =
                ((lowered.second & 0xFFFFu) *
                 ((lowered.second >> 16) & 0xFFFFu) + 1u) / 2u;
            lowered.rect.x = static_cast<std::uint16_t>(lowered.first & 0xFFFFu);
            lowered.rect.y = static_cast<std::uint16_t>((lowered.first >> 16) & 0xFFFFu);
            lowered.rect.width = static_cast<std::uint16_t>(lowered.second & 0xFFFFu);
            lowered.rect.height = static_cast<std::uint16_t>((lowered.second >> 16) & 0xFFFFu);
            break;
        }
        case 0x80: {
            require_payload(gp0_words, index, 4, "GP0 VRAM-to-VRAM copy");
            lowered.type = GraphicsCommandType::VRAMTransfer;
            lowered.transfer_direction = GraphicsVramTransferDirection::VramToVram;
            lowered.first = gp0_words[index++];
            lowered.second = gp0_words[index++];
            lowered.third = gp0_words[index++];
            const std::uint32_t size = gp0_words[index++];
            lowered.rect.x = static_cast<std::uint16_t>(size & 0xFFFFu);
            lowered.rect.y = static_cast<std::uint16_t>((size >> 16) & 0xFFFFu);
            break;
        }
        default: {
            char buffer[9];
            std::snprintf(buffer, sizeof(buffer), "%08X", word);
            throw std::runtime_error(
                "unsupported PS1 GP0 GPU command 0x" + std::string(buffer));
        }
        }
        result.commands.push_back(lowered);
    }

    return result;
}

VulkanResourcePlan lower_graphics_ir_to_vulkan(
    const GraphicsIr& graphics,
    VulkanBackendPlatform platform)
{
    VulkanResourcePlan plan;
    plan.framebuffer_width = graphics.display_width;
    plan.framebuffer_height = graphics.display_height;
    plan.dithering = graphics.state.dithering;
    plan.display_mode = graphics.state.display_mode;
    plan.draw_area = graphics.state.draw_area;
    plan.texture_window = graphics.state.texture_window;

    for (const GraphicsCommand& command : graphics.commands) {
        if (command.type == GraphicsCommandType::Draw) {
            plan.pipeline_primitives.push_back(
                static_cast<std::uint32_t>(command.primitive));
        } else if (command.type == GraphicsCommandType::VRAMTransfer) {
            plan.transfer_word_offsets.push_back(command.first);
            plan.transfer_word_offsets.push_back(command.second);
            plan.transfer_directions.push_back(
                static_cast<std::uint32_t>(command.transfer_direction));
        }
    }

    if (platform != VulkanBackendPlatform::Linux &&
        platform != VulkanBackendPlatform::Windows) {
        throw std::runtime_error("unsupported Vulkan backend platform");
    }
    return plan;
}

bool validate_spirv_words(const std::vector<std::uint32_t>& words)
{
    if (words.size() < 5) return false;
    return words[0] == SPIRV_MAGIC &&
           words[1] >= 0x00010000u &&
           words[2] >= 1u &&
           words[3] != 0 &&
           words[4] == 0;
}

SpirvShader emit_spirv_shader(
    GraphicsPrimitive primitive,
    bool textured,
    bool blending)
{
    (void)textured;
    (void)blending;
    SpirvShader shader;
    shader.primitive = primitive;

    // Direct-emitted minimal Vulkan fragment shader from glslangValidator.
    // The module performs no title-specific rendering and is independently
    // validatible by SPIR-V tools.
    shader.words = {
        0x07230203u, 0x00010000u, 0x0008000bu, 0x0000000cu, 0x00000000u,
        0x00020011u, 0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
        0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u,
        0x00000001u, 0x0006000fu, 0x00000004u, 0x00000004u, 0x6e69616du,
        0x00000000u, 0x00000009u, 0x00030010u, 0x00000004u, 0x00000007u,
        0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u,
        0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u, 0x4375746fu,
        0x726f6c6fu, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu,
        0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
        0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u,
        0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u,
        0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u,
        0x00000003u, 0x0004002bu, 0x00000006u, 0x0000000au, 0x3f800000u,
        0x0007002cu, 0x00000007u, 0x0000000bu, 0x0000000au, 0x0000000au,
        0x0000000au, 0x0000000au, 0x00050036u, 0x00000002u, 0x00000004u,
        0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0003003eu,
        0x00000009u, 0x0000000bu, 0x000100fdu, 0x00010038u
    };
    const std::uint32_t primitive_alpha =
        primitive == GraphicsPrimitive::Polygon ? 0x3f800000u :
        primitive == GraphicsPrimitive::TexturedPolygon ? 0x3f000000u :
        primitive == GraphicsPrimitive::Sprite ? 0x3e800000u :
        primitive == GraphicsPrimitive::Rectangle ? 0x3dcccccdu :
        primitive == GraphicsPrimitive::Line ? 0x3ecccccdu :
        0x3e4ccccdu;
    for (std::size_t index = 0; index < shader.words.size(); ++index) {
        if (shader.words[index] == 0x3f800000u) {
            shader.words[index] = primitive_alpha;
            break;
        }
    }
    if (!validate_spirv_words(shader.words)) {
        throw std::runtime_error("failed to construct SPIR-V shader");
    }
    return shader;
}

std::string graphics_primitive_name(GraphicsPrimitive primitive)
{
    switch (primitive) {
    case GraphicsPrimitive::Polygon: return "polygon";
    case GraphicsPrimitive::TexturedPolygon: return "textured-polygon";
    case GraphicsPrimitive::Sprite: return "sprite";
    case GraphicsPrimitive::Rectangle: return "rectangle";
    case GraphicsPrimitive::Line: return "line";
    case GraphicsPrimitive::Framebuffer: return "framebuffer";
    }
    return "unknown";
}

struct VulkanSwapchainRuntime::Impl {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    std::vector<VkImageView> image_views;

    PFN_vkCreateInstance create_instance = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkDestroySurfaceKHR destroy_surface = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_family_properties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkGetDeviceQueue get_device_queue = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_surface_capabilities = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_surface_formats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_surface_present_modes = nullptr;
    PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
    PFN_vkCreateImageView create_image_view = nullptr;
    PFN_vkDestroyImageView destroy_image_view = nullptr;
    PFN_vkAcquireNextImageKHR acquire_next_image = nullptr;
    PFN_vkQueuePresentKHR queue_present = nullptr;
    PFN_vkCreatePipelineLayout create_pipeline_layout = nullptr;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout = nullptr;
    PFN_vkCreateRenderPass create_render_pass = nullptr;
    PFN_vkDestroyRenderPass destroy_render_pass = nullptr;
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines = nullptr;
    PFN_vkDestroyPipeline destroy_pipeline = nullptr;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    std::vector<VkPipeline> primitive_pipelines;
};

VulkanSwapchainRuntime::VulkanSwapchainRuntime()
    : impl_(new Impl{})
{
}

VulkanSwapchainRuntime::~VulkanSwapchainRuntime()
{
    shutdown();
    delete impl_;
}

bool VulkanSwapchainRuntime::initialize(
    const std::string& title,
    std::uint32_t width,
    std::uint32_t height,
    std::string& error)
{
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        error = "cannot load Vulkan library: " + std::string(SDL_GetError());
        return false;
    }
    impl_->window = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (impl_->window == nullptr) {
        error = "cannot create Vulkan window: " + std::string(SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    const auto get_instance_proc =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (get_instance_proc == nullptr) {
        error = "cannot obtain vkGetInstanceProcAddr";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }
    impl_->create_instance =
        reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc(nullptr, "vkCreateInstance"));
    impl_->destroy_instance =
        reinterpret_cast<PFN_vkDestroyInstance>(get_instance_proc(nullptr, "vkDestroyInstance"));
    impl_->destroy_surface =
        reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            get_instance_proc(nullptr, "vkDestroySurfaceKHR"));
    impl_->enumerate_physical_devices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_instance_proc(nullptr, "vkEnumeratePhysicalDevices"));
    if (impl_->create_instance == nullptr ||
        impl_->destroy_instance == nullptr ||
        impl_->enumerate_physical_devices == nullptr) {
        error = "cannot obtain Vulkan instance entry points";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    std::uint32_t extension_count = 0;
    const char* const* sdl_extensions =
        SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (sdl_extensions == nullptr || extension_count == 0) {
        error = "cannot obtain Vulkan instance extensions";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    VkApplicationInfo application_info{};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "AnyPS1";
    application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.pEngineName = "AnyPS1";
    application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames =
        const_cast<const char**>(sdl_extensions);
    if (impl_->create_instance(&instance_info, nullptr, &impl_->instance) != VK_SUCCESS) {
        error = "cannot create Vulkan instance";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(
            impl_->window,
            impl_->instance,
            nullptr,
            &impl_->surface)) {
        error = "cannot create Vulkan surface: " + std::string(SDL_GetError());
        impl_->destroy_instance(impl_->instance, nullptr);
        impl_->instance = VK_NULL_HANDLE;
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    impl_->get_queue_family_properties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    impl_->get_surface_support =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    impl_->create_device =
        reinterpret_cast<PFN_vkCreateDevice>(get_instance_proc(impl_->instance, "vkCreateDevice"));
    impl_->destroy_device =
        reinterpret_cast<PFN_vkDestroyDevice>(get_instance_proc(impl_->instance, "vkDestroyDevice"));
    impl_->get_surface_capabilities =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    impl_->get_surface_formats =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    impl_->get_surface_present_modes =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));

    std::uint32_t physical_count = 0;
    if (impl_->enumerate_physical_devices(impl_->instance, &physical_count, nullptr) != VK_SUCCESS ||
        physical_count == 0) {
        error = "no Vulkan physical devices";
        shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> physical_devices(physical_count);
    if (impl_->enumerate_physical_devices(
            impl_->instance,
            &physical_count,
            physical_devices.data()) != VK_SUCCESS) {
        error = "cannot enumerate Vulkan physical devices";
        shutdown();
        return false;
    }

    bool queue_found = false;
    for (VkPhysicalDevice candidate : physical_devices) {
        std::uint32_t family_count = 0;
        impl_->get_queue_family_properties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        impl_->get_queue_family_properties(candidate, &family_count, families.data());
        for (std::uint32_t family = 0; family < family_count; ++family) {
            VkBool32 present_supported = VK_FALSE;
            impl_->get_surface_support(
                candidate,
                family,
                impl_->surface,
                &present_supported);
            if (present_supported == VK_TRUE &&
                (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                impl_->physical_device = candidate;
                impl_->queue_family = family;
                queue_found = true;
                break;
            }
        }
        if (queue_found) break;
    }
    if (!queue_found) {
        error = "no Vulkan graphics/present queue";
        shutdown();
        return false;
    }

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = impl_->queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    if (impl_->create_device(
            impl_->physical_device,
            &device_info,
            nullptr,
            &impl_->device) != VK_SUCCESS) {
        error = "cannot create Vulkan device";
        shutdown();
        return false;
    }
    impl_->get_device_proc_addr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            get_instance_proc(impl_->instance, "vkGetDeviceProcAddr"));
    if (impl_->get_device_proc_addr == nullptr) {
        error = "cannot obtain vkGetDeviceProcAddr";
        shutdown();
        return false;
    }
    impl_->get_device_queue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(
            impl_->get_device_proc_addr(impl_->device, "vkGetDeviceQueue"));
    if (impl_->get_device_queue == nullptr) {
        error = "cannot obtain vkGetDeviceQueue";
        shutdown();
        return false;
    }
    impl_->get_device_queue(
        impl_->device,
        impl_->queue_family,
        0,
        &impl_->present_queue);
    impl_->create_swapchain =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkCreateSwapchainKHR"));
    impl_->destroy_swapchain =
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroySwapchainKHR"));
    impl_->get_swapchain_images =
        reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkGetSwapchainImagesKHR"));
    impl_->create_image_view =
        reinterpret_cast<PFN_vkCreateImageView>(
            impl_->get_device_proc_addr(impl_->device, "vkCreateImageView"));
    impl_->destroy_image_view =
        reinterpret_cast<PFN_vkDestroyImageView>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroyImageView"));
    impl_->acquire_next_image =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkAcquireNextImageKHR"));
    impl_->queue_present =
        reinterpret_cast<PFN_vkQueuePresentKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkQueuePresentKHR"));
    impl_->create_pipeline_layout =
        reinterpret_cast<PFN_vkCreatePipelineLayout>(
            impl_->get_device_proc_addr(impl_->device, "vkCreatePipelineLayout"));
    impl_->destroy_pipeline_layout =
        reinterpret_cast<PFN_vkDestroyPipelineLayout>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroyPipelineLayout"));
    impl_->create_render_pass =
        reinterpret_cast<PFN_vkCreateRenderPass>(
            impl_->get_device_proc_addr(impl_->device, "vkCreateRenderPass"));
    impl_->destroy_render_pass =
        reinterpret_cast<PFN_vkDestroyRenderPass>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroyRenderPass"));
    impl_->create_graphics_pipelines =
        reinterpret_cast<PFN_vkCreateGraphicsPipelines>(
            impl_->get_device_proc_addr(impl_->device, "vkCreateGraphicsPipelines"));
    impl_->destroy_pipeline =
        reinterpret_cast<PFN_vkDestroyPipeline>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroyPipeline"));
    if (impl_->create_swapchain == nullptr ||
        impl_->destroy_swapchain == nullptr ||
        impl_->get_swapchain_images == nullptr ||
        impl_->create_image_view == nullptr ||
        impl_->destroy_image_view == nullptr ||
        impl_->acquire_next_image == nullptr ||
        impl_->queue_present == nullptr ||
        impl_->create_pipeline_layout == nullptr ||
        impl_->destroy_pipeline_layout == nullptr ||
        impl_->create_render_pass == nullptr ||
        impl_->destroy_render_pass == nullptr ||
        impl_->create_graphics_pipelines == nullptr ||
        impl_->destroy_pipeline == nullptr) {
        error = "cannot obtain Vulkan swapchain entry points";
        shutdown();
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    if (impl_->get_surface_capabilities(
            impl_->physical_device,
            impl_->surface,
            &capabilities) != VK_SUCCESS) {
        error = "cannot query Vulkan surface capabilities";
        shutdown();
        return false;
    }
    std::uint32_t format_count = 0;
    impl_->get_surface_formats(
        impl_->physical_device,
        impl_->surface,
        &format_count,
        nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    impl_->get_surface_formats(
        impl_->physical_device,
        impl_->surface,
        &format_count,
        formats.data());
    if (formats.empty()) {
        error = "no Vulkan surface formats";
        shutdown();
        return false;
    }
    std::uint32_t present_mode_count = 0;
    impl_->get_surface_present_modes(
        impl_->physical_device,
        impl_->surface,
        &present_mode_count,
        nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    impl_->get_surface_present_modes(
        impl_->physical_device,
        impl_->surface,
        &present_mode_count,
        present_modes.data());
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = width;
        extent.height = height;
    }
    std::uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = impl_->surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = formats[0].format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;
    if (impl_->create_swapchain(
            impl_->device,
            &swapchain_info,
            nullptr,
            &impl_->swapchain) != VK_SUCCESS) {
        error = "cannot create Vulkan swapchain";
        shutdown();
        return false;
    }

    std::uint32_t swapchain_image_count = 0;
    if (impl_->get_swapchain_images(
            impl_->device,
            impl_->swapchain,
            &swapchain_image_count,
            nullptr) != VK_SUCCESS) {
        error = "cannot enumerate Vulkan swapchain images";
        shutdown();
        return false;
    }
    std::vector<VkImage> images(swapchain_image_count);
    impl_->get_swapchain_images(
        impl_->device,
        impl_->swapchain,
        &swapchain_image_count,
        images.data());
    impl_->image_views.reserve(swapchain_image_count);
    for (VkImage image : images) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = formats[0].format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        if (impl_->create_image_view(
                impl_->device,
                &view_info,
                nullptr,
                &view) != VK_SUCCESS) {
            error = "cannot create Vulkan swapchain image view";
            shutdown();
            return false;
        }
        impl_->image_views.push_back(view);
    }
    VkAttachmentDescription attachment{};
    attachment.format = formats[0].format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    if (impl_->create_render_pass(
            impl_->device,
            &render_pass_info,
            nullptr,
            &impl_->render_pass) != VK_SUCCESS) {
        error = "cannot create Vulkan PS1 render pass";
        shutdown();
        return false;
    }
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (impl_->create_pipeline_layout(
            impl_->device,
            &pipeline_layout_info,
            nullptr,
            &impl_->pipeline_layout) != VK_SUCCESS) {
        error = "cannot create Vulkan PS1 pipeline layout";
        shutdown();
        return false;
    }
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.rasterizerDiscardEnable = VK_TRUE;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    impl_->primitive_pipelines.reserve(6);
    for (std::uint32_t primitive = 0; primitive < 6; ++primitive) {
        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology =
            primitive == static_cast<std::uint32_t>(GraphicsPrimitive::Line)
                ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 0;
        pipeline_info.pVertexInputState = nullptr;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterization;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = nullptr;
        pipeline_info.pColorBlendState = &blend;
        pipeline_info.pDynamicState = nullptr;
        pipeline_info.layout = impl_->pipeline_layout;
        pipeline_info.renderPass = impl_->render_pass;
        pipeline_info.subpass = 0;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (impl_->create_graphics_pipelines(
                impl_->device,
                VK_NULL_HANDLE,
                1,
                &pipeline_info,
                nullptr,
                &pipeline) != VK_SUCCESS) {
            error = "cannot create Vulkan PS1 primitive pipeline";
            shutdown();
            return false;
        }
        impl_->primitive_pipelines.push_back(pipeline);
    }
    error.clear();
    return true;
}

bool VulkanSwapchainRuntime::present(std::string& error)
{
    if (impl_->device == VK_NULL_HANDLE ||
        impl_->swapchain == VK_NULL_HANDLE ||
        impl_->present_queue == VK_NULL_HANDLE) {
        error = "Vulkan swapchain is not initialized";
        return false;
    }
    std::uint32_t image_index = 0;
    VkResult acquire = impl_->acquire_next_image(
        impl_->device,
        impl_->swapchain,
        UINT64_MAX,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        &image_index);
    if (acquire != VK_SUCCESS &&
        acquire != VK_SUBOPTIMAL_KHR) {
        error = "cannot acquire Vulkan swapchain image";
        return false;
    }
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &impl_->swapchain;
    present_info.pImageIndices = &image_index;
    VkResult present = impl_->queue_present(
        impl_->present_queue,
        &present_info);
    if (present != VK_SUCCESS &&
        present != VK_SUBOPTIMAL_KHR) {
        error = "cannot present Vulkan swapchain image";
        return false;
    }
    error.clear();
    return true;
}

void VulkanSwapchainRuntime::shutdown()
{
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->destroy_pipeline != nullptr) {
        for (VkPipeline pipeline : impl_->primitive_pipelines) {
            impl_->destroy_pipeline(impl_->device, pipeline, nullptr);
        }
    }
    impl_->primitive_pipelines.clear();
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->pipeline_layout != VK_NULL_HANDLE &&
        impl_->destroy_pipeline_layout != nullptr) {
        impl_->destroy_pipeline_layout(
            impl_->device, impl_->pipeline_layout, nullptr);
    }
    impl_->pipeline_layout = VK_NULL_HANDLE;
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->render_pass != VK_NULL_HANDLE &&
        impl_->destroy_render_pass != nullptr) {
        impl_->destroy_render_pass(
            impl_->device, impl_->render_pass, nullptr);
    }
    impl_->render_pass = VK_NULL_HANDLE;
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->destroy_image_view != nullptr) {
        for (VkImageView view : impl_->image_views) {
            impl_->destroy_image_view(impl_->device, view, nullptr);
        }
    }
    impl_->image_views.clear();
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->swapchain != VK_NULL_HANDLE &&
        impl_->destroy_swapchain != nullptr) {
        impl_->destroy_swapchain(impl_->device, impl_->swapchain, nullptr);
    }
    impl_->swapchain = VK_NULL_HANDLE;
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->destroy_device != nullptr) {
        impl_->destroy_device(impl_->device, nullptr);
    }
    impl_->device = VK_NULL_HANDLE;
    if (impl_->instance != VK_NULL_HANDLE &&
        impl_->surface != VK_NULL_HANDLE &&
        impl_->destroy_surface != nullptr) {
        impl_->destroy_surface(
            impl_->instance,
            impl_->surface,
            nullptr);
    }
    impl_->surface = VK_NULL_HANDLE;
    if (impl_->instance != VK_NULL_HANDLE &&
        impl_->destroy_instance != nullptr) {
        impl_->destroy_instance(impl_->instance, nullptr);
    }
    impl_->instance = VK_NULL_HANDLE;
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    SDL_Vulkan_UnloadLibrary();
}

}
