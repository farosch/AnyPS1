#include "gpu/Graphics.h"
#include "host/HostServices.h"
#include "input/Input.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        anyps1::Ps1InputState input;
        anyps1::apply_input_binding(input, anyps1::Ps1Button::Cross, true);
        if (!input.is_pressed(anyps1::Ps1Button::Cross)) {
            throw std::runtime_error("host input mapping did not lower PS1 input");
        }

        const anyps1::HostServiceMapping console =
            anyps1::map_host_service(anyps1::HostServiceKind::Console);
        const anyps1::HostServiceMapping file =
            anyps1::map_host_service(anyps1::HostServiceKind::File);
        const anyps1::HostServiceMapping host_input =
            anyps1::map_host_service(anyps1::HostServiceKind::Input);
        if (!console.implemented || !file.implemented || !host_input.implemented) {
            throw std::runtime_error("required host mapping is not implemented");
        }

        const std::vector<std::uint32_t> gp0{
            0x20FFFFFFu,
            0x00000010u,
            0x00100000u,
            0x00100010u
        };
        const std::vector<std::uint32_t> gp1{
            0x03000000u,
            0x07000000u
        };
        const anyps1::GraphicsIr graphics =
            anyps1::lower_gp0_gp1(gp0, gp1);
        const anyps1::VulkanResourcePlan plan =
            anyps1::lower_graphics_ir_to_vulkan(
                graphics, anyps1::VulkanBackendPlatform::Linux);
        if (plan.pipeline_primitives.empty()) {
            throw std::runtime_error("host graphics lowering produced no pipeline");
        }
        const anyps1::HostServiceMapping dma =
            anyps1::map_host_service(anyps1::HostServiceKind::Dma);
        const anyps1::HostServiceMapping cdrom =
            anyps1::map_host_service(anyps1::HostServiceKind::CdRom);
        const anyps1::HostServiceMapping spu =
            anyps1::map_host_service(anyps1::HostServiceKind::Spu);
        if (dma.kind != anyps1::HostServiceKind::Dma ||
            cdrom.kind != anyps1::HostServiceKind::CdRom ||
            spu.kind != anyps1::HostServiceKind::Spu) {
            throw std::runtime_error("peripheral host mappings are incomplete");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Host integration test failed: " << error.what() << "\n";
        return 1;
    }
}
