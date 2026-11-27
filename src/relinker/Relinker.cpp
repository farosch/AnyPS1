#include "Relinker.h"
#include "format/PsExe.h"
#include "executable/PeWriter.h"
#include "lowering/MipsLowerer.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace anyps1 {

namespace {
void create_output_parent(const std::string& output)
{
    const std::filesystem::path output_path(output);
    const std::filesystem::path parent = output_path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
}
}

bool relink(RelinkOptions& options)
{
    if (options.input.empty()) throw std::invalid_argument("input path is empty");
    if (!options.target.has_value()) {
        throw std::invalid_argument("target platform must be set explicitly");
    }
    if (options.architecture != TargetArchitecture::X64) {
        throw std::invalid_argument(
            "native architecture lowering is not implemented for this target");
    }
    RelinkOptions resolved = options;
    const std::filesystem::path input_path(options.input);
    resolved.input = resolve_relink_input(options.input).string();
    if (resolved.output.empty()) {
        std::filesystem::path output_root = std::filesystem::is_directory(input_path) ? input_path : input_path.parent_path();
        if (output_root.empty()) output_root = ".";
        resolved.output = (output_root / (*options.target == TargetPlatform::Windows ? "game.exe" : "game.elf")).string();
    }
    options.output = resolved.output;
    create_output_parent(resolved.output);

    std::ifstream input(resolved.input, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input PS-X EXE: " + resolved.input);
    const std::vector<unsigned char> data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (data.size() < PSX_HEADER_SIZE) throw std::runtime_error("PS-X EXE is shorter than its header");
    if (data.size() < 8 || std::memcmp(data.data(), "PS-X EXE", 8) != 0) throw std::runtime_error("input is not a PS-X EXE");

    const std::uint32_t load_address = read_psx32(data, 0x18);
    const std::uint32_t file_size = read_psx32(data, 0x1C);
    const std::uint32_t initial_pc = read_psx32(data, 0x10);
    const std::uint64_t code_end = static_cast<std::uint64_t>(PSX_HEADER_SIZE) + file_size;
    if (code_end > data.size()) throw std::runtime_error("PS-X EXE declared size exceeds available input");
    if (load_address < 0x80000000 || load_address >= 0x80200000) throw std::runtime_error("PS-X EXE load address is outside PS1 KSEG0");
    if (initial_pc != 0 && (initial_pc < load_address || initial_pc >= load_address + file_size)) throw std::runtime_error("PS-X EXE entry point is outside the executable image");

    if (*resolved.target == TargetPlatform::Windows) {
        write_windows_native_cfg(resolved, data);
        return true;
    }
    try { write_linux_native_cfg(resolved, data); }
    catch (...) {
        std::filesystem::remove(resolved.output + ".native.s");
        std::filesystem::remove(resolved.output + ".native.bin");
        throw;
    }
    return true;
}

}
