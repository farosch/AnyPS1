#include "gpu/Graphics.h"
#include "host/HostServices.h"
#include "input/Input.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        anyps1::NativeFileRuntime files;
        std::string host_error;
        const std::filesystem::path temp =
            std::filesystem::temp_directory_path() / "anyps1-host-runtime.txt";
        const int handle =
            files.open(temp.string(), true, false, host_error);
        if (handle < 0) throw std::runtime_error(host_error);
        const char payload[] = "AnyPS1 host file";
        if (files.write(handle, payload, sizeof(payload) - 1, host_error) !=
            static_cast<int>(sizeof(payload) - 1)) {
            throw std::runtime_error(host_error);
        }
        if (!files.close(handle, host_error)) throw std::runtime_error(host_error);
        const int read_handle =
            files.open(temp.string(), false, false, host_error);
        if (read_handle < 0) throw std::runtime_error(host_error);
        char read_back[64]{};
        if (files.read(read_handle, read_back, sizeof(read_back) - 1, host_error) <= 0) {
            throw std::runtime_error(host_error);
        }
        if (!files.close(read_handle, host_error)) throw std::runtime_error(host_error);
        std::filesystem::remove(temp);

        if (anyps1::host_exit_log(anyps1::HostExitCode::UnsupportedHostService)
                != "host_exit=17 unsupported_host_service") {
            throw std::runtime_error("structured host exit diagnostics are invalid");
        }

        anyps1::SdlHostRuntime sdl;
        if (sdl.should_close()) {
            throw std::runtime_error("uninitialized SDL host runtime reported closed");
        }
        if (sdl.poll_events(host_error)) {
            throw std::runtime_error("uninitialized SDL host runtime unexpectedly polled");
        }

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
        const anyps1::HostServiceMapping audio =
            anyps1::map_host_service(anyps1::HostServiceKind::Audio);
        const anyps1::HostServiceMapping video =
            anyps1::map_host_service(anyps1::HostServiceKind::Video);
        const anyps1::HostServiceMapping vulkan =
            anyps1::map_host_service(anyps1::HostServiceKind::VulkanFramebuffer);
        if (!audio.implemented || !video.implemented || !vulkan.implemented) {
            throw std::runtime_error("required media host mappings are not implemented");
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
