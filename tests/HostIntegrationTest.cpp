#include "gpu/Graphics.h"
#include "host/HostServices.h"
#include "input/Input.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <vector>

int main()
{
    try {
        const std::uint32_t open_vector = 0xA0000032u;
        const anyps1::Ps1FileCallPlan open_call =
            anyps1::map_ps1_file_call(open_vector);
        if (open_call.kind != anyps1::Ps1FileCallKind::Open ||
            open_call.linux_syscall != 2 ||
            open_call.windows_function == nullptr ||
            std::string(open_call.windows_function) != "CreateFileA") {
            throw std::runtime_error("A0 open file call mapping failed");
        }
        const anyps1::Ps1FileCallPlan close_call =
            anyps1::map_ps1_file_call(0xA0000036u);
        if (close_call.kind != anyps1::Ps1FileCallKind::Close ||
            close_call.linux_syscall != 3 ||
            std::string(close_call.windows_function) != "CloseHandle") {
            throw std::runtime_error("A0 close file call mapping failed");
        }
        const anyps1::Ps1BiosCallPlan memcpy_call =
            anyps1::map_ps1_bios_call(0xA0u, 0x39u);
        if (memcpy_call.kind != anyps1::Ps1BiosCallKind::Memcpy) {
            throw std::runtime_error("A0 memcpy BIOS call mapping failed");
        }
        const anyps1::Ps1BiosCallPlan b0_event =
            anyps1::map_ps1_bios_call(0xB0u, 0x07u);
        if (b0_event.kind != anyps1::Ps1BiosCallKind::B0DeliverEvent) {
            throw std::runtime_error("B0 event BIOS call mapping failed");
        }
        const anyps1::Ps1BiosCallPlan c0_timer =
            anyps1::map_ps1_bios_call(0xC0u, 0x00u);
        if (c0_timer.kind != anyps1::Ps1BiosCallKind::C0SysEnqIntRP) {
            throw std::runtime_error("C0 interrupt BIOS call mapping failed");
        }
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
        anyps1::NativeHostLoopRuntime loop;
        if (loop.should_close()) {
            throw std::runtime_error("uninitialized native host loop reported closed");
        }
        if (loop.poll(input, host_error)) {
            throw std::runtime_error("uninitialized native host loop unexpectedly polled");
        }
        const anyps1::HostLoopPlan host_loop =
            anyps1::plan_native_host_loop();
        if (!host_loop.input_polling ||
            !host_loop.window_events ||
            !host_loop.audio ||
            !host_loop.framebuffer ||
            !host_loop.vulkan ||
            !host_loop.memory_card ||
            !host_loop.disc ||
            !host_loop.vblank_scheduling) {
            throw std::runtime_error("native host loop plan is incomplete");
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
        if (anyps1::anyps1_host_loop_controller_state() != 0xFFFFu) {
            throw std::runtime_error("uninitialized controller state is invalid");
        }
        {
            anyps1::Ps1DeviceRuntime devices;
            devices.set_controller_state(0x5F7Fu);
            if (devices.write(0x1F801040u, 0x01u) != 0 ||
                devices.read(0x1F801040u) != 0x00u ||
                devices.read(0x1F801040u) != 0x41u ||
                devices.read(0x1F801040u) != 0x5Au ||
                static_cast<std::uint8_t>(
                    devices.read(0x1F801040u)) != 0x7Fu ||
                static_cast<std::uint8_t>(
                    devices.read(0x1F801040u)) != 0x5Fu) {
                throw std::runtime_error("controller protocol FIFO failed");
            }
            devices.write(0x1F801040u, 0x81u);
            if (devices.read(0x1F801040u) != 0xFFu ||
                devices.read(0x1F801040u) != 0xFFu ||
                devices.read(0x1F801040u) != 0x5Au ||
                devices.read(0x1F801040u) != 0x5Du) {
                throw std::runtime_error("memory-card JOY protocol failed");
            }
            devices.write(0x1F801800u, 0u);
            if (devices.write(0x1F801801u, 0x01u) != 0 ||
                devices.write(0x1F801800u, 1u) != 0 ||
                devices.read(0x1F801801u) != 0x02u) {
                throw std::runtime_error("CD-ROM Getstat protocol failed");
            }
            devices.write(0x1F801800u, 0u);
            if (devices.write(0x1F801801u, 0x06u) != 0 ||
                devices.write(0x1F801800u, 3u) != 0 ||
                devices.read(0x1F801803u) != 0x11u) {
                throw std::runtime_error("CD-ROM Read protocol failed");
            }
        }
        if (anyps1::anyps1_host_loop_spu_read(0x1F801C00u) != 0u) {
            throw std::runtime_error("uninitialized SPU state is invalid");
        }
        if (anyps1::anyps1_host_loop_spu_write(0x1F801C00u, 0x1234u) != 18) {
            throw std::runtime_error("uninitialized SPU write did not report host runtime error");
        }
        if (anyps1::anyps1_host_loop_gpu_write(0x1F801810u, 0u) != 18) {
            throw std::runtime_error("uninitialized GPU write did not report host runtime error");
        }
        if (anyps1::anyps1_host_loop_memory_card_open(
                0,
                "card-test.mcd") != 18) {
            throw std::runtime_error("uninitialized memory-card open did not report host runtime error");
        }
        if (anyps1::anyps1_host_loop_disc_open(
                "disc-test.bin",
                0) != 18) {
            throw std::runtime_error("uninitialized disc open did not report host runtime error");
        }
        if (anyps1::anyps1_host_loop_tick(1024) != 18) {
            throw std::runtime_error("uninitialized host tick did not report runtime error");
        }
        {
            anyps1::NativeHostLoopRuntime runtime;
            if (!runtime.initialize(
                    "AnyPS1 headless",
                    320,
                    240,
                    "",
                    host_error)) {
                throw std::runtime_error(host_error);
            }
            if (runtime.mmio_write(0x1F801074u, 0x4u) != 0 ||
                runtime.mmio_read(0x1F801074u) != 0x4u) {
                throw std::runtime_error("interrupt mask MMIO round trip failed");
            }
            if (runtime.tick(1024u) != 0 ||
                runtime.mmio_read(0x1F801070u) != 0x4u) {
                throw std::runtime_error("timer tick did not assert VBlank status");
            }
            if (runtime.mmio_write(0x1F801070u, 0x4u) != 0 ||
                runtime.mmio_read(0x1F801070u) != 0u) {
                throw std::runtime_error("interrupt acknowledge did not clear status");
            }
            if (runtime.mmio_write(0x1F801040u, 0x01u) != 0 ||
                runtime.mmio_read(0x1F801040u) != 0u ||
                runtime.mmio_read(0x1F801040u) != 0x41u ||
                runtime.mmio_read(0x1F801040u) != 0x5Au) {
                throw std::runtime_error("controller MMIO FIFO did not progress");
            }
            if (runtime.mmio_write(0x1F801800u, 0u) != 0 ||
                runtime.mmio_write(0x1F801801u, 0x01u) != 0 ||
                runtime.mmio_write(0x1F801800u, 1u) != 0 ||
                runtime.mmio_read(0x1F801801u) != 0x02u) {
                throw std::runtime_error("CD-ROM MMIO response did not progress");
            }
            anyps1::Ps1InputState polled_input;
            if (!runtime.poll(polled_input, host_error) ||
                runtime.frame_count() == 0 ||
                runtime.presented_frame_count() == 0 ||
                runtime.vblank_count() != 1) {
                throw std::runtime_error(
                    "host frame polling, presentation, or VBlank progression failed");
            }
            std::vector<std::uint32_t> framebuffer(
                1024u * 512u, 0u);
            if (!runtime.present_framebuffer(
                    framebuffer.data(), 1024u, 512u, host_error) ||
                runtime.presented_frame_count() < 2) {
                throw std::runtime_error(host_error);
            }
            const std::int16_t audio[32]{};
            if (!runtime.queue_audio(audio, 32u, host_error) ||
                runtime.queued_audio_samples() != 32u) {
                throw std::runtime_error(host_error);
            }
        }
        {
            anyps1::Ps1DeviceRuntime devices;
            std::uint32_t ram[2]{};
            devices.attach_ram(ram, 2u);
            devices.set_controller_state(0x1234u);
            devices.write(0x1F801048u, 1u);
            if (devices.read(0x1F801040u) != 0u ||
                devices.read(0x1F801040u) != 0x41u ||
                devices.read(0x1F801040u) != 0x5Au ||
                devices.read(0x1F801040u) != 0x34u) {
                throw std::runtime_error(
                    "controller state did not progress through serial data");
            }
            devices.write(0x1F801800u, 0u);
            devices.write(0x1F801801u, 0x01u);
            devices.tick(1u);
            devices.write(0x1F801800u, 1u);
            if ((devices.read(0x1F801070u) & 0x4u) == 0u ||
                devices.read(0x1F801801u) != 0x02u) {
                throw std::runtime_error(
                    "CD-ROM command did not produce response and interrupt");
            }
            devices.write(0x1F801C00u, 0x4567u);
            devices.tick(1u);
            if (devices.read(0x1F801C00u) != 0x4567u) {
                throw std::runtime_error("SPU register did not persist across ticks");
            }
            ram[0] = 0x20FFFFFFu;
            devices.write(0x1F8010A0u, 0u);
            devices.write(0x1F8010A4u, 1u);
            devices.write(0x1F8010A8u, 0x01000001u);
            if ((devices.read(0x1F801814u) & (1u << 26u)) == 0u) {
                throw std::runtime_error(
                    "GPU DMA did not queue native graphics work");
            }
            devices.tick(1u);
            if ((devices.read(0x1F801070u) & 0x8u) == 0u) {
                throw std::runtime_error(
                    "GPU DMA progression did not complete");
            }
            devices.write(0x1F801104u, 1u);
            devices.write(0x1F801108u, 1u);
            devices.write(0x1F801070u, 0xFFFFFFFFu);
            devices.tick(1024u);
            if ((devices.read(0x1F801070u) & 0x1u) == 0u) {
                throw std::runtime_error("device tick did not assert VBlank");
            }
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

        const std::filesystem::path card =
            std::filesystem::temp_directory_path() /
            "anyps1-memory-card-test.bin";
        std::filesystem::remove(card);
        anyps1::MemoryCardRuntime memory_card;
        std::uint8_t write_block[128]{};
        const char text[] = "memory-card";
        for (std::size_t index = 0;
             index < sizeof(text) - 1;
             ++index) {
            write_block[index] =
                static_cast<std::uint8_t>(text[index]);
        }
        if (!memory_card.open_slot(0, card.string(), host_error) ||
            !memory_card.write_block(
                0,
                0,
                write_block,
                128,
                host_error)) {
            throw std::runtime_error(host_error);
        }
        std::uint8_t block[128]{};
        if (!memory_card.read_block(0, 0, block, 128, host_error) ||
            block[0] != 'm') {
            throw std::runtime_error("memory card runtime read/write failed");
        }
        memory_card.close_slot(0);
        std::filesystem::remove(card);

        const std::filesystem::path disc =
            std::filesystem::temp_directory_path() /
            "anyps1-disc-test.bin";
        {
            std::ofstream output(disc, std::ios::binary);
            output.write(std::string(2048, 'D').c_str(), 2048);
        }
        anyps1::DiscImageRuntime disc_runtime;
        if (!disc_runtime.open_disc(disc.string(), false, host_error)) {
            throw std::runtime_error(host_error);
        }
        std::uint8_t sector[2048]{};
        if (!disc_runtime.read_sector(0, sector, sizeof(sector), host_error) ||
            sector[0] != 'D') {
            throw std::runtime_error("disc runtime sector read failed");
        }
        disc_runtime.close_disc();
        std::filesystem::remove(disc);

        if (anyps1::is_supported_bios_vector(0xA0000040u)) {
            throw std::runtime_error("unsupported BIOS vector was accepted");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Host integration test failed: " << error.what() << "\n";
        return 1;
    }
}
