#include "Relinker.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

namespace {

constexpr std::uint32_t PSX_HEADER_SIZE = 2048;

void trim(std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        value.clear();
        return;
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
}

std::string system_cnf_boot(const std::filesystem::path& system_cnf)
{
    std::ifstream input(system_cnf);
    std::string line;
    while (std::getline(input, line)) {
        trim(line);
        if (line.rfind("BOOT", 0) == 0) {
            const auto separator = line.find('=');
            if (separator != std::string::npos) {
                std::string boot = line.substr(separator + 1);
                trim(boot);
                if (boot.size() >= 7
                    && boot.compare(0, 7, "cdrom:\\") == 0) {
                    boot = boot.substr(7);
                }
                const auto version = boot.find(';');
                if (version != std::string::npos) {
                    boot.erase(version);
                }
                return boot;
            }
        }
    }
    return {};
}

bool is_psx_exe(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 8> magic{};
    input.read(reinterpret_cast<char*>(magic.data()), magic.size());
    return input.gcount() == static_cast<std::streamsize>(magic.size())
        && std::memcmp(magic.data(), "PS-X EXE", magic.size()) == 0;
}

std::filesystem::path find_extracted_boot(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> system_cnf_files;
    if (std::filesystem::is_regular_file(root / "SYSTEM.CNF")) {
        system_cnf_files.push_back(root / "SYSTEM.CNF");
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() == "SYSTEM.CNF") {
            system_cnf_files.push_back(entry.path());
        }
    }
    for (const auto& system_cnf : system_cnf_files) {
        const std::string boot = system_cnf_boot(system_cnf);
        if (!boot.empty()) {
            std::filesystem::path candidate = system_cnf.parent_path() / boot;
            if (std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
            candidate = system_cnf.parent_path() / boot.substr(boot.find_last_of("/\\") + 1);
            if (std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
        }
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && is_psx_exe(entry.path())) {
            return entry.path();
        }
    }
    throw std::runtime_error("extracted game has no boot PS-X EXE");
}

std::filesystem::path resolve_relink_input(const RelinkOptions& options)
{
    const std::filesystem::path input(options.input);
    if (std::filesystem::is_regular_file(input)) {
        return input;
    }
    if (std::filesystem::is_directory(input)) {
        return find_extracted_boot(input);
    }
    throw std::runtime_error("input extracted game path does not exist: " + options.input);
}

void create_output_parent(const std::string& output)
{
    const std::filesystem::path output_path(output);
    const std::filesystem::path parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write32(std::ofstream& output, std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write16(std::ofstream& output, std::uint16_t value)
{
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write64(std::ofstream& output, std::uint64_t value)
{
    const std::array<unsigned char, 8> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
        static_cast<unsigned char>(value >> 32),
        static_cast<unsigned char>(value >> 40),
        static_cast<unsigned char>(value >> 48),
        static_cast<unsigned char>(value >> 56),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::uint32_t read32(const std::vector<unsigned char>& data, std::size_t offset)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("truncated PS-X EXE header");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void write_pe_layout(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size
)
{
    throw std::runtime_error(
        "native Windows MIPS lowering is not implemented; refusing emulator-style relinking");
}

void write_linux_native(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data
)
{
    const std::uint32_t file_size = read32(data, 0x1C);
    const std::uint32_t load_address = read32(data, 0x18);
    if (file_size == 0 || file_size % 4 != 0) {
        throw std::runtime_error("unsupported native lowering: executable size is not a MIPS word sequence");
    }
    for (std::uint32_t offset = 0; offset < file_size; offset += 4) {
        const std::uint32_t instruction = read32(data, PSX_HEADER_SIZE + offset);
        if (instruction != 0) {
            throw std::runtime_error(
                "unsupported native MIPS lowering at 0x"
                + [] (std::uint32_t value) {
                    char output[9]{};
                    std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                    return std::string(output);
                }(load_address + offset)
                + ": opcode 0x"
                + [] (std::uint32_t value) {
                    char output[9]{};
                    std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                    return std::string(output);
                }(instruction)
                + "; emulator-style relinking is forbidden");
        }
    }

    const std::filesystem::path assembly =
        std::filesystem::temp_directory_path()
        / (options.output + ".native.s");
    {
        std::ofstream source(assembly, std::ios::trunc);
        source << ".text\n";
        source << ".globl _start\n";
        source << "_start:\n";
        source << "    xor %edi, %edi\n";
        source << "    mov $60, %eax\n";
        source << "    syscall\n";
        if (!source) {
            throw std::runtime_error("cannot create native lowering assembly");
        }
    }

    std::string command = "gcc -nostdlib -no-pie -Wl,-e,_start -o \""
        + options.output + "\" \"" + assembly.string() + "\"";
    const int status = std::system(command.c_str());
    std::filesystem::remove(assembly);
    if (status != 0) {
        throw std::runtime_error("native Linux lowering toolchain failed");
    }
    std::filesystem::permissions(
        options.output,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add
    );
}

}

bool relink(const RelinkOptions& options)
{
    if (options.input.empty()) {
        throw std::invalid_argument("input path is empty");
    }
    RelinkOptions resolved = options;
    const std::filesystem::path input_path(options.input);
    resolved.input = resolve_relink_input(options).string();
    if (resolved.output.empty()) {
        std::filesystem::path output_root;
        if (std::filesystem::is_directory(input_path)) {
            output_root = input_path;
        } else {
            output_root = input_path.parent_path();
            if (output_root.empty()) {
                output_root = ".";
            }
        }
        resolved.output = (output_root
            / (options.target == TargetPlatform::Windows ? "game.exe" : "game.elf")).string();
    }

    create_output_parent(resolved.output);

    std::ifstream input(resolved.input, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input PS-X EXE: " + resolved.input);
    }
    const std::vector<unsigned char> data{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (data.size() < PSX_HEADER_SIZE) {
        throw std::runtime_error("PS-X EXE is shorter than its header");
    }
    if (data.size() < 8 || std::memcmp(data.data(), "PS-X EXE", 8) != 0) {
        throw std::runtime_error("input is not a PS-X EXE");
    }

    const std::uint32_t initial_pc = read32(data, 0x10);
    const std::uint32_t initial_gp = read32(data, 0x14);
    const std::uint32_t load_address = read32(data, 0x18);
    const std::uint32_t file_size = read32(data, 0x1C);
    const std::uint64_t code_end = static_cast<std::uint64_t>(PSX_HEADER_SIZE) + file_size;
    if (code_end > data.size()) {
        throw std::runtime_error("PS-X EXE declared size exceeds available input");
    }
    if (load_address < 0x80000000 || load_address >= 0x80200000) {
        throw std::runtime_error("PS-X EXE load address is outside PS1 KSEG0");
    }
    if (initial_pc != 0 && (initial_pc < load_address || initial_pc >= load_address + file_size)) {
        throw std::runtime_error("PS-X EXE entry point is outside the executable image");
    }

    if (resolved.target == TargetPlatform::Windows) {
        write_pe_layout(resolved, data, load_address, file_size);
        return true;
    }

    write_linux_native(resolved, data);
    return true;
}

}
