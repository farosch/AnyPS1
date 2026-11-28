#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace anyps1 {

struct ElfLinkResult {
    bool timed_out = false;
    int exit_code = -1;
    std::string stderr_text;
};

class ElfWriter {
public:
    static void write(
        const std::filesystem::path& assembly,
        const std::filesystem::path& output);
    static void write(
        const std::vector<std::filesystem::path>& assemblies,
        const std::filesystem::path& output);
    static void validate(const std::filesystem::path& path);

private:
    static ElfLinkResult link(
        const std::filesystem::path& assembly,
        const std::filesystem::path& output);
    static ElfLinkResult link(
        const std::vector<std::filesystem::path>& assemblies,
        const std::filesystem::path& output);
};

}
