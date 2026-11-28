#include "ElfWriter.h"
#include "NativeProcess.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace anyps1 {

ElfLinkResult ElfWriter::link(
    const std::filesystem::path& assembly,
    const std::filesystem::path& output)
{
    return link(std::vector<std::filesystem::path>{assembly}, output);
}

ElfLinkResult ElfWriter::link(
    const std::vector<std::filesystem::path>& assemblies,
    const std::filesystem::path& output)
{
#ifdef _WIN32
    (void)assemblies;
    (void)output;
    throw std::runtime_error(
        "native Linux lowering toolchain is not available on Windows");
#else
    std::vector<std::filesystem::path> objects;
    objects.reserve(assemblies.size());
    for (std::size_t index = 0; index < assemblies.size(); ++index) {
        const std::filesystem::path object =
            std::filesystem::path(
                output.string() + ".native." +
                std::to_string(index) + ".o");
        std::vector<std::string> assembler_args = {
            "-x",
            "assembler",
            "-c",
            assemblies[index].string(),
            "-o",
            object.string(),
        };
        const NativeProcessResult assembler =
            run_native_process(
                "clang",
                assembler_args,
                std::chrono::minutes(2));
        if (!assembler.started) {
            return {false, -1,
                    assembler.stderr_text.empty()
                        ? "cannot start native Linux assembler"
                        : assembler.stderr_text};
        }
        if (assembler.timed_out) {
            return {true, 124, assembler.stderr_text};
        }
        if (assembler.exit_code != 0) {
            return {false, assembler.exit_code, assembler.stderr_text};
        }
        objects.push_back(object);
    }
    std::vector<std::string> linker_args = {
        "-nostdlib",
        "-no-pie",
        "-fuse-ld=lld",
        "-Wl,-e,_start,-z,noexecstack,-z,relro,-z,now,-z,notext,--build-id=none",
        "-o",
        output.string(),
    };
    for (const std::filesystem::path& object : objects) {
        linker_args.push_back(object.string());
    }
    const std::string build_dir = ANYPS1_BUILD_DIR;
    linker_args.push_back(build_dir + "/src/libanyps1_gte.a");
    linker_args.push_back(build_dir + "/src/libanyps1_device.a");
    linker_args.push_back(build_dir + "/src/libanyps1_host.a");
    linker_args.push_back(build_dir + "/src/libanyps1_input.a");
    linker_args.push_back(build_dir + "/src/libanyps1_gpu.a");
    linker_args.push_back("-lstdc++");
    linker_args.push_back(build_dir + "/3rdparty/SDL/libSDL3.so.0.5.0");
    linker_args.push_back("-Wl,-rpath," + build_dir + "/3rdparty/SDL");
    linker_args.push_back("-lc");
    linker_args.push_back("-lgcc_s");
    linker_args.push_back("-lgcc");
    linker_args.push_back("-lm");
    linker_args.push_back("-ldl");
    linker_args.push_back("-lpthread");
    const NativeProcessResult linker =
        run_native_process(
            "clang++",
            linker_args,
            std::chrono::minutes(2));
    if (!linker.started) {
        return {false, -1,
                linker.stderr_text.empty()
                    ? "cannot start native Linux linker"
                    : linker.stderr_text};
    }
    if (linker.timed_out) {
        return {true, 124, linker.stderr_text};
    }
    if (linker.exit_code != 0) {
        return {false, linker.exit_code, linker.stderr_text};
    }
    const char* keep_environment = std::getenv("ANYPS1_KEEP_ASSEMBLY");
    if (keep_environment == nullptr) {
        for (const std::filesystem::path& object : objects) {
            std::error_code remove_error;
            std::filesystem::remove(object, remove_error);
            if (remove_error) {
                return {false, -1,
                        "cannot remove temporary ELF object: " +
                            object.string() + ": " +
                            remove_error.message()};
            }
        }
    }
    return {false, 0, {}};
#endif
}

void ElfWriter::validate(const std::filesystem::path& path)
{
    std::ifstream elf(path, std::ios::binary);
    if (!elf) throw std::runtime_error("cannot open generated Linux ELF");
    std::array<unsigned char, 64> header{};
    elf.read(reinterpret_cast<char*>(header.data()),
             static_cast<std::streamsize>(header.size()));
    if (elf.gcount() != static_cast<std::streamsize>(header.size())) {
        throw std::runtime_error(
            "generated Linux ELF has a truncated ELF header");
    }

    if (header[0] != 0x7F || header[1] != 'E'
        || header[2] != 'L' || header[3] != 'F') {
        throw std::runtime_error(
            "generated output does not contain an ELF signature");
    }
    if (header[4] != 2) {
        throw std::runtime_error("generated Linux ELF is not ELF64");
    }
    if (header[5] != 1) {
        throw std::runtime_error("generated Linux ELF is not little-endian");
    }

    const auto read16 = [&](std::size_t offset) {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(header[offset])
            | static_cast<std::uint16_t>(
                  static_cast<std::uint16_t>(header[offset + 1]) << 8));
    };
    const auto read64 = [&](std::size_t offset) {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(header[offset + index])
                << (index * 8);
        }
        return value;
    };
    if (read16(18) != 62) {
        throw std::runtime_error(
            "generated Linux ELF is not an x86-64 executable");
    }
    if (read16(16) != 2) {
        throw std::runtime_error(
            "generated Linux ELF is not executable type ET_EXEC");
    }
    if (read64(24) == 0) {
        throw std::runtime_error("generated Linux ELF entrypoint is zero");
    }
    if (read64(32) == 0) {
        throw std::runtime_error(
            "generated Linux ELF has no program-header table");
    }
    if (read64(40) == 0) {
        throw std::runtime_error(
            "generated Linux ELF has no section-header table");
    }

    elf.clear();
    elf.seekg(0, std::ios::beg);
    const std::size_t program_header_offset =
        static_cast<std::size_t>(read64(32));
    const std::uint16_t program_header_count = read16(56);
    const std::uint16_t program_header_size = read16(54);
    if (program_header_count == 0 || program_header_size < 56) {
        throw std::runtime_error(
            "generated Linux ELF has invalid program-header metadata");
    }
    if (program_header_offset == 0 ||
        program_header_offset +
            static_cast<std::size_t>(program_header_count) *
                program_header_size >
            static_cast<std::size_t>(
                std::filesystem::file_size(path))) {
        throw std::runtime_error(
            "generated Linux ELF program-header table is out of bounds");
    }

    elf.seekg(static_cast<std::streamoff>(program_header_offset),
              std::ios::beg);
    bool executable_segment = false;
    bool writable_segment = false;
    for (std::uint16_t index = 0; index < program_header_count; ++index) {
        std::array<unsigned char, 56> program_header{};
        elf.read(reinterpret_cast<char*>(program_header.data()),
                 static_cast<std::streamsize>(program_header.size()));
        if (elf.gcount() !=
            static_cast<std::streamsize>(program_header.size())) {
            throw std::runtime_error(
                "generated Linux ELF has a truncated program-header entry");
        }
        const std::uint32_t segment_type =
            static_cast<std::uint32_t>(program_header[0])
            | (static_cast<std::uint32_t>(program_header[1]) << 8)
            | (static_cast<std::uint32_t>(program_header[2]) << 16)
            | (static_cast<std::uint32_t>(program_header[3]) << 24);
        const std::uint32_t segment_flags =
            static_cast<std::uint32_t>(program_header[4])
            | (static_cast<std::uint32_t>(program_header[5]) << 8)
            | (static_cast<std::uint32_t>(program_header[6]) << 16)
            | (static_cast<std::uint32_t>(program_header[7]) << 24);
        if (segment_type == 1) {
            if ((segment_flags & 1u) != 0) executable_segment = true;
            if ((segment_flags & 2u) != 0) writable_segment = true;
        }
    }
    if (!executable_segment || !writable_segment) {
        throw std::runtime_error(
            "generated Linux ELF lacks expected executable/writable load segments");
    }
}

void ElfWriter::write(
    const std::filesystem::path& assembly,
    const std::filesystem::path& output)
{
    const ElfLinkResult result = link(assembly, output);
    if (result.timed_out) {
        throw std::runtime_error(
            "native Linux lowering toolchain timed out");
    }
    if (result.exit_code != 0) {
        throw std::runtime_error(
            "native Linux lowering toolchain failed" +
                (result.stderr_text.empty()
                     ? std::string{}
                     : std::string(": ") + result.stderr_text));
    }
    validate(output);
    std::filesystem::permissions(
        output,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
}

void ElfWriter::write(
    const std::vector<std::filesystem::path>& assemblies,
    const std::filesystem::path& output)
{
    const ElfLinkResult result = link(assemblies, output);
    if (result.timed_out) {
        throw std::runtime_error(
            "native Linux lowering toolchain timed out");
    }
    if (result.exit_code != 0) {
        throw std::runtime_error(
            "native Linux lowering toolchain failed" +
                (result.stderr_text.empty()
                     ? std::string{}
                     : std::string(": ") + result.stderr_text));
    }
    validate(output);
    std::filesystem::permissions(
        output,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
}

}
