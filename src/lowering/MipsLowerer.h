#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace anyps1 {

struct RelinkOptions;
enum class TargetPlatform;

struct MipsExecutablePlan {
    std::uint32_t file_size = 0;
    std::uint32_t load_address = 0;
    std::uint32_t initial_pc = 0;
    std::uint32_t initial_gp = 0;
    std::uint32_t initial_sp = 0;
    std::uint32_t payload_ram_offset = 0;
    std::uint32_t unsupported_count = 0;
    std::uint32_t reachable_count = 0;
    std::uint32_t supported_count = 0;
    std::uint32_t embedded_data_count = 0;
    std::uint32_t unknown_region_count = 0;
    std::uint32_t first_unsupported_address = 0;
    std::uint32_t first_unsupported_word = 0;
    std::string first_unsupported_name;
    std::uint32_t call_edges = 0;
    std::uint32_t unresolved_indirect_transfers = 0;
    bool native_host_loop_required = false;
    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> delay_slots;
    std::set<std::uint32_t> emittable_leaders;
    std::set<std::string> unsupported_family_names;
};

class MipsLowerer {
public:
    virtual ~MipsLowerer() = default;
    virtual void lower(
        const RelinkOptions& options,
        const std::vector<unsigned char>& data) = 0;
};

std::unique_ptr<MipsLowerer> create_mips_lowerer(TargetPlatform target);
MipsExecutablePlan build_mips_executable_plan(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data);

}
