#pragma once

#include <filesystem>
#include <vector>

namespace anyps1 {

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
    static int link(
        const std::filesystem::path& assembly,
        const std::filesystem::path& output);
    static int link(
        const std::vector<std::filesystem::path>& assemblies,
        const std::filesystem::path& output);
};

}
