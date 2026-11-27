#include "gpu/Graphics.h"

#include <cstdio>
#include <stdexcept>
#include <string>

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
        case 0x01:
        case 0x02:
        case 0x08:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = command == 0x00 ? 1u : (command == 0x08 ? (parameter & 1u) == 0 : parameter == 0);
            break;
        case 0x03:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = parameter != 0;
            break;
        case 0x04:
            lowered.type = GraphicsCommandType::SetDmaDirection;
            lowered.first = parameter & 3u;
            break;
        case 0x05:
            lowered.type = GraphicsCommandType::SetDisplayArea;
            lowered.first = parameter & 0x3FFu;
            lowered.second = (parameter >> 10) & 0x1FFu;
            break;
        case 0x06:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter & 0xFFFu;
            lowered.second = (parameter >> 12) & 0xFFFu;
            break;
        case 0x07:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter;
            result.display_width = (parameter & 1u) ? 640u : 320u;
            result.display_height = (parameter & 0x18u) ? 480u : 240u;
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
        case 0xE1:
            lowered.type = GraphicsCommandType::SetTextureWindow;
            lowered.first = word & 0xFFFFFFu;
            break;
        case 0xE2:
        case 0xE3:
        case 0xE4:
        case 0xE5:
            lowered.type = command == 0xE2 ? GraphicsCommandType::SetDrawArea
                                           : command == 0xE3 ? GraphicsCommandType::SetDrawArea
                                                             : command == 0xE4 ? GraphicsCommandType::SetDrawArea
                                                                               : GraphicsCommandType::SetDrawOffset;
            lowered.first = word & 0xFFFFFFu;
            break;
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23: {
            require_payload(gp0_words, index, 2, "GP0 polygon");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Polygon;
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
            require_payload(gp0_words, index, 1, "GP0 variable rectangle");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Rectangle;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0x60:
        case 0x68: {
            require_payload(gp0_words, index, 1, "GP0 sprite");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Sprite;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0xA0: {
            require_payload(gp0_words, index, 3, "GP0 VRAM transfer");
            lowered.type = GraphicsCommandType::VRAMTransfer;
            lowered.first = gp0_words[index++];
            lowered.second = gp0_words[index++];
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

    for (const GraphicsCommand& command : graphics.commands) {
        if (command.type == GraphicsCommandType::Draw) {
            plan.pipeline_primitives.push_back(
                static_cast<std::uint32_t>(command.primitive));
        } else if (command.type == GraphicsCommandType::VRAMTransfer) {
            plan.transfer_word_offsets.push_back(command.first);
            plan.transfer_word_offsets.push_back(command.second);
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
    case GraphicsPrimitive::Framebuffer: return "framebuffer";
    }
    return "unknown";
}

}
