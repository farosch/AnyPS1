#include "ElfWriter.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace anyps1 {

int ElfWriter::link(
    const std::filesystem::path& assembly,
    const std::filesystem::path& output)
{
#ifdef _WIN32
    (void)assembly;
    (void)output;
    throw std::runtime_error(
        "native Linux lowering toolchain is not available on Windows");
#else
    const std::filesystem::path object =
        std::filesystem::path(output.string() + ".native.o");

    const pid_t assembler = fork();
    if (assembler < 0) {
        throw std::runtime_error(
            "cannot start native Linux assembler: " +
            std::string(std::strerror(errno)));
    }
    if (assembler == 0) {
        std::vector<const char*> assembler_args = {
            "clang",
            "-x",
            "assembler",
            "-c",
        };
        assembler_args.push_back(assembly.c_str());
        assembler_args.push_back("-o");
        assembler_args.push_back(object.c_str());
        assembler_args.push_back(nullptr);
        execvp(assembler_args[0],
               const_cast<char* const*>(assembler_args.data()));
        _exit(127);
    }

    int assembler_status = 0;
    if (waitpid(assembler, &assembler_status, 0) < 0) {
        throw std::runtime_error(
            "cannot wait for native Linux assembler: " +
            std::string(std::strerror(errno)));
    }
    if (!WIFEXITED(assembler_status) ||
        WEXITSTATUS(assembler_status) != 0) {
        return 1;
    }

    const pid_t child = fork();
    if (child < 0) {
        throw std::runtime_error(
            "cannot start native Linux linker: " +
            std::string(std::strerror(errno)));
    }
    if (child == 0) {
        std::vector<const char*> linker_args = {
            "clang++",
            "-nostdlib",
            "-no-pie",
            "-fuse-ld=lld",
            "-Wl,-e,_start,-z,noexecstack,-z,relro,-z,now,-z,notext,--build-id=none",
        };
        linker_args.push_back("-o");
        linker_args.push_back(output.c_str());
        linker_args.push_back(object.c_str());
        const std::string build_dir = ANYPS1_BUILD_DIR;
        const std::string gte_library = build_dir + "/src/libanyps1_gte.a";
        const std::string device_library =
            build_dir + "/src/libanyps1_device.a";
        const std::string host_library = build_dir + "/src/libanyps1_host.a";
        const std::string input_library = build_dir + "/src/libanyps1_input.a";
        const std::string gpu_library = build_dir + "/src/libanyps1_gpu.a";
        linker_args.push_back(gte_library.c_str());
        linker_args.push_back(host_library.c_str());
        linker_args.push_back(device_library.c_str());
        linker_args.push_back(input_library.c_str());
        linker_args.push_back(gpu_library.c_str());
        linker_args.push_back("-lstdc++");
        const std::string sdl_library =
            build_dir + "/3rdparty/SDL/libSDL3.so.0.5.0";
        linker_args.push_back(sdl_library.c_str());
        const std::string sdl_rpath =
            "-Wl,-rpath," + build_dir + "/3rdparty/SDL";
        linker_args.push_back(sdl_rpath.c_str());
        linker_args.push_back("-lc");
        linker_args.push_back("-lm");
        linker_args.push_back("-ldl");
        linker_args.push_back("-lpthread");
        linker_args.push_back(nullptr);
        execvp(linker_args[0], const_cast<char* const*>(linker_args.data()));
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        throw std::runtime_error(
            "cannot wait for native Linux linker: " +
            std::string(std::strerror(errno)));
    }
    if (!WIFEXITED(status)) return -1;
    if (WEXITSTATUS(status) != 0) return WEXITSTATUS(status);
    std::error_code remove_error;
    std::filesystem::remove(object, remove_error);
    return 0;
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
    try {
        const int status = link(assembly, output);
        if (status != 0) {
            throw std::runtime_error(
                "native Linux lowering toolchain failed (assembly retained: " +
                assembly.string() + ")");
        }
        validate(output);
        std::filesystem::permissions(
            output,
            std::filesystem::perms::owner_exec
                | std::filesystem::perms::group_exec
                | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    } catch (...) {
        throw;
    }
}

}
