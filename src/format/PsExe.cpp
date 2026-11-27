#include "PsExe.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

namespace {
void trim(std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { value.clear(); return; }
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
                if (boot.size() >= 7 && boot.compare(0, 7, "cdrom:\\") == 0) boot = boot.substr(7);
                const auto version = boot.find(';');
                if (version != std::string::npos) boot.erase(version);
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
    if (std::filesystem::is_regular_file(root / "SYSTEM.CNF")) system_cnf_files.push_back(root / "SYSTEM.CNF");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() == "SYSTEM.CNF") system_cnf_files.push_back(entry.path());
    }
    for (const auto& system_cnf : system_cnf_files) {
        const std::string boot = system_cnf_boot(system_cnf);
        if (!boot.empty()) {
            std::filesystem::path candidate = system_cnf.parent_path() / boot;
            if (std::filesystem::is_regular_file(candidate)) return candidate;
            candidate = system_cnf.parent_path() / boot.substr(boot.find_last_of("\\/") + 1);
            if (std::filesystem::is_regular_file(candidate)) return candidate;
        }
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && is_psx_exe(entry.path())) return entry.path();
    }
    throw std::runtime_error("extracted game has no boot PS-X EXE");
}
}

std::uint32_t read_psx32(const std::vector<unsigned char>& data, std::size_t offset)
{
    if (offset + 4 > data.size()) throw std::runtime_error("truncated PS-X EXE header");
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::string hex32(std::uint32_t value)
{
    char output[11];
    std::snprintf(output, sizeof(output), "0x%08X", static_cast<unsigned>(value));
    return output;
}

std::string escape_assembly_string(const std::string& value)
{
    std::string escaped;
    for (char character : value) {
        if (character == '\n') escaped += "\\n";
        else if (character == '\\') escaped += "\\\\";
        else if (character == '"') escaped += "\\\"";
        else escaped += character;
    }
    return escaped;
}

std::filesystem::path resolve_relink_input(const std::string& input)
{
    const std::filesystem::path path(input);
    if (std::filesystem::is_regular_file(path)) return path;
    if (std::filesystem::is_directory(path)) return find_extracted_boot(path);
    throw std::runtime_error("input extracted game path does not exist: " + input);
}

}
