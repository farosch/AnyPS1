#include "MipsLinuxLowerer.h"
#include "MipsLowerer.h"
#include "executable/ElfWriter.h"
#include "relinker/Relinker.h"
#include "format/PsExe.h"
#include "UnsupportedInstructions.h"
#include "PayloadRegion.h"
#include "memory/MemoryMap.h"
#include "host/HostServices.h"
#include "MipsLoweringSupport.h"
#include "diagnostics/DiagnosticReport.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>


namespace anyps1 {

namespace {

bool is_leader_line(const std::string& line)
{
    if (line.size() < 3u ||
        line[0] != 'p') return false;
    if (line.find(':') != line.size() - 1u) return false;
    for (std::size_t index = 1u; index + 1u < line.size(); ++index) {
        const char character = line[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool is_shared_runtime_symbol(const std::string& line)
{
    static const char* symbols[] = {
        "regs:", "ram:", "scratchpad:", "cop0:", "cop3_data:",
        "mmio_state:", "gte_data:", "gte_ctrl:", "fregs:", "fpcsr:",
        "pending_load_valid:", "pending_load_register:",
        "pending_load_value:", "previous_load_valid:",
        "previous_load_register:", "previous_load_value:",
        "host_frame_counter:",
        "overflow_exit:", "divide_zero_exit:", "memory_fault_exit:",
        "mmio_exit:", "unsupported_jump_exit:",
        "unsupported_bios_exit:", "unsupported_instruction_exit:",
        "host_runtime_exit:", "host_shutdown_exit:",
        "indirect_jump:", "print_diag:",
        "native_completion_exit:"
    };
    for (const char* symbol : symbols) {
        if (line == symbol) return true;
    }
    if (line.size() > 12u &&
        line.compare(0u, 11u, "unsupported") == 0 &&
        line.back() == ':') {
        return true;
    }
    if (line.size() > 2u && line.front() == 'p' &&
        line.back() == ':' &&
        is_leader_line(line)) {
        return true;
    }
    return false;
}

void replace_unresolved_leader_targets(
    std::string& line,
    const std::set<std::string>& leader_labels)
{
    const std::string::size_type stub_position =
        line.find("unsupported_");
    if (stub_position != std::string::npos &&
        line.back() != ':' &&
        line.find("_stub", stub_position) != std::string::npos) {
        const std::string::size_type stub_end =
            line.find_first_of(" \t\r\n:", stub_position);
        if (stub_end != std::string::npos) {
            line.replace(
                stub_position,
                stub_end - stub_position,
                "unsupported_instruction_exit");
        } else {
            line.replace(
                stub_position,
                line.size() - stub_position,
                "unsupported_instruction_exit");
        }
    }
    for (std::size_t position = 0;
         position < line.size();
         ) {
        if (line[position] != 'p') {
            ++position;
            continue;
        }
        if (position + 9u > line.size()) break;
        bool hexadecimal = true;
        for (std::size_t index = position + 1u;
             index < position + 9u;
             ++index) {
            const char character = line[index];
            if (!((character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f') ||
                  (character >= 'A' && character <= 'F'))) {
                hexadecimal = false;
                break;
            }
        }
        if (!hexadecimal) {
            ++position;
            continue;
        }
        const std::string label =
            line.substr(position, 9u);
        if (leader_labels.find(label) == leader_labels.end()) {
            line.replace(position, 9u,
                         "unsupported_jump_exit");
            position += 22u;
        } else {
            position += 9u;
        }
    }
}

void split_leader_assembly(
    const std::filesystem::path& assembly,
    const std::filesystem::path& output,
    std::vector<std::filesystem::path>& chunk_assemblies)
{
    std::ifstream input(assembly);
    if (!input) {
        throw std::runtime_error(
            "cannot open Linux lowering assembly for chunking");
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);

    std::size_t leader_start = lines.size();
    std::size_t leader_end = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (is_leader_line(lines[index])) {
            leader_start = index;
            break;
        }
    }
    for (std::size_t index = leader_start;
         index < lines.size();
         ++index) {
        if (lines[index].find("leaq diag_completed") !=
            std::string::npos) {
            leader_end = index;
            break;
        }
    }
    std::set<std::string> leader_labels;
    std::set<std::string> unsupported_stub_names;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (is_leader_line(lines[index])) {
            leader_labels.insert(lines[index].substr(
                0u, lines[index].size() - 1u));
        }
    }
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string::size_type jmp_position =
            lines[index].find("jmp unsupported_");
        if (jmp_position != std::string::npos) {
            std::string stub =
                lines[index].substr(jmp_position + 4u);
            const std::string::size_type stub_end =
                stub.find_first_of(" \t");
            if (stub_end != std::string::npos) {
                stub.resize(stub_end);
            }
            if (!stub.empty()) {
                if (!is_shared_runtime_symbol(stub + ":")) {
                    unsupported_stub_names.insert(stub);
                }
            }
        }
        replace_unresolved_leader_targets(
            lines[index], leader_labels);
    }
    if (leader_start == lines.size() ||
        leader_end == lines.size()) {
        return;
    }

    std::ofstream base(assembly, std::ios::trunc);
    if (!base) {
        throw std::runtime_error(
            "cannot rewrite Linux lowering assembly chunk");
    }
    for (std::size_t index = 0; index < leader_start; ++index) {
        if (is_shared_runtime_symbol(lines[index])) {
            base << ".globl " << lines[index].substr(
                0u, lines[index].size() - 1u) << "\n";
        }
        base << lines[index] << "\n";
    }
    for (std::size_t index = leader_start; index < leader_end; ++index) {
        if (lines[index].size() > 12u &&
            lines[index].compare(0u, 11u, "unsupported") == 0 &&
            lines[index].back() == ':') {
            base << ".globl " << lines[index].substr(
                0u, lines[index].size() - 1u) << "\n";
            base << lines[index] << "\n";
            if (index + 1u < leader_end) {
                base << lines[index + 1u] << "\n";
            }
        }
    }
    for (std::size_t index = leader_end; index < lines.size(); ++index) {
        if (index == leader_end) {
            base << ".globl native_completion_exit\n";
            base << "native_completion_exit:\n";
        }
        base << lines[index] << "\n";
    }
    base.close();

    std::size_t leader_count = 0;
    std::vector<std::string> chunk_lines;
    std::size_t chunk_index = 0;
    std::string last_leader_address;
    for (std::size_t index = leader_start; index < leader_end;) {
        if (!is_leader_line(lines[index])) {
            ++index;
            continue;
        }
        if (!chunk_lines.empty() &&
            leader_count >= 512u) {
            if (!last_leader_address.empty()) {
                if (index < leader_end && is_leader_line(lines[index])) {
                    const std::string next_address =
                        lines[index].substr(0u, lines[index].size() - 1u);
                    chunk_lines.push_back(
                        "jmp " + next_address);
                } else {
                    chunk_lines.push_back(
                        "jmp native_completion_exit");
                }
            }
            const std::filesystem::path chunk =
                std::filesystem::path(
                    output.string() + ".chunk." +
                    std::to_string(chunk_index++) + ".s");
            std::ofstream chunk_output(chunk, std::ios::trunc);
            if (!chunk_output) {
                throw std::runtime_error(
                    "cannot create Linux lowering assembly chunk");
            }
            chunk_output << ".text\n";
            chunk_output << ".globl native_completion_exit\n";
            for (const std::string& chunk_line : chunk_lines) {
                chunk_output << chunk_line << "\n";
            }
            chunk_output.close();
            chunk_assemblies.push_back(chunk);
            chunk_lines.clear();
            leader_count = 0u;
        }
        const std::size_t block_start = index;
        std::size_t block_end = index + 1u;
        while (block_end < leader_end &&
               !is_leader_line(lines[block_end])) {
            ++block_end;
        }
        chunk_lines.push_back(".globl " + lines[index].substr(
            0u, lines[index].size() - 1u));
        for (std::size_t block_index = block_start;
             block_index < block_end;
             ++block_index) {
            chunk_lines.push_back(lines[block_index]);
        }
        last_leader_address =
            lines[block_start].substr(
                0u, lines[block_start].size() - 1u);
        ++leader_count;
        index = block_end;
    }
    if (!chunk_lines.empty()) {
        chunk_lines.push_back("jmp native_completion_exit");
        const std::filesystem::path chunk =
            std::filesystem::path(
                output.string() + ".chunk." +
                std::to_string(chunk_index) + ".s");
        std::ofstream chunk_output(chunk, std::ios::trunc);
        if (!chunk_output) {
            throw std::runtime_error(
                "cannot create Linux lowering assembly chunk");
        }
        chunk_output << ".text\n";
        chunk_output << ".globl native_completion_exit\n";
        for (const std::string& chunk_line : chunk_lines) {
            chunk_output << chunk_line << "\n";
        }
        chunk_output.close();
        chunk_assemblies.push_back(chunk);
    }
}

}

void MipsLinuxLowerer::lower(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data
)
{
    const MipsExecutablePlan plan = build_mips_executable_plan(options, data);
    const std::uint32_t file_size = plan.file_size;
    const std::uint32_t load_address = plan.load_address;
    const std::uint32_t initial_pc = plan.initial_pc;
    const std::uint32_t initial_gp = plan.initial_gp;
    const std::uint32_t initial_sp = plan.initial_sp;
    const std::uint32_t payload_ram_offset = plan.payload_ram_offset;
    const std::set<std::uint32_t>& leaders = plan.leaders;
    const std::set<std::uint32_t>& delay_slots = plan.delay_slots;
    const std::uint32_t& unsupported_count = plan.unsupported_count;
    const std::uint32_t& reachable_count = plan.reachable_count;
    const std::uint32_t& supported_count = plan.supported_count;
    const std::uint32_t& embedded_data_count = plan.embedded_data_count;
    const std::uint32_t& unknown_region_count = plan.unknown_region_count;
    const std::uint32_t& first_unsupported_address = plan.first_unsupported_address;
    const std::uint32_t& first_unsupported_word = plan.first_unsupported_word;
    const std::string& first_unsupported_name = plan.first_unsupported_name;
    const std::uint32_t& call_edges = plan.call_edges;
    const std::uint32_t& unresolved_indirect_transfers =
        plan.unresolved_indirect_transfers;
    const std::set<std::string>& unsupported_family_names =
        plan.unsupported_family_names;
    const std::set<std::uint32_t>& emittable_leaders =
        plan.emittable_leaders;
    const bool native_host_loop_required = plan.native_host_loop_required;

    const std::filesystem::path assembly = std::filesystem::path(options.output + ".native.s");
    const std::filesystem::path payload = std::filesystem::path(options.output + ".native.bin");
    {
        std::ofstream output(payload, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(data.data() + 2048),
            static_cast<std::streamsize>(file_size));
        if (!output) throw std::runtime_error("cannot write native lowering payload");
    }

    std::ofstream source(assembly, std::ios::trunc);
    const std::string banner = "AnyPS1 native executable diagnostics\n";
    const std::string target = "target=linux\n";
    const std::string load = "load_address=" + hex32(load_address) + "\n";
    const std::string entry = "entry_point=" + hex32(initial_pc) + "\n";
    const std::string gp = "initial_gp=" + hex32(initial_gp) + "\n";
    const std::string sp = "initial_sp=" + hex32(initial_sp) + "\n";
    const std::string payload_size = "payload_size=" + std::to_string(file_size) + "\n";
    const std::string lowered = "lowered_words=" + std::to_string(leaders.size()) + "\n";
    const std::uint32_t payload_words = file_size / 4u;
    const std::uint32_t coverage_percent =
        payload_words == 0
            ? 0u
            : static_cast<std::uint32_t>(
                  leaders.size() * 100u / payload_words);
    const std::string coverage =
        "lowered_coverage_percent=" + std::to_string(coverage_percent) + "\n";
    const std::string edges =
        "call_edges=" + std::to_string(call_edges) + "\n";
    const std::string unresolved =
        "unresolved_indirect_transfers=" +
        std::to_string(unresolved_indirect_transfers) + "\n";
    const std::string reachable =
        "reachable_words=" + std::to_string(reachable_count) + "\n";
    const std::string supported =
        "supported_reachable_words=" + std::to_string(supported_count) + "\n";
    const std::string embedded =
        "embedded_data_words=" + std::to_string(embedded_data_count) + "\n";
    const std::string unknown =
        "unknown_region_words=" + std::to_string(unknown_region_count) + "\n";
    const std::string executable_range =
        "executable_range=" + hex32(load_address) + "-" +
        hex32(static_cast<std::uint32_t>(load_address + file_size)) + "\n";
    const std::string ram_range =
        "ram_range=0x00000000-0x00200000\n";
    const std::string scratchpad_range =
        "scratchpad_range=0x1F800000-0x1F800400\n";
    const std::string unsupported =
        "unsupported_instructions=" + std::to_string(unsupported_count) + "\n";
    const std::string first_gap =
        first_unsupported_address == 0
            ? "first_unsupported_gap=none\n"
            : "first_unsupported_gap=" + hex32(first_unsupported_address) +
              " instruction=" + hex32(first_unsupported_word) +
              " name=" + first_unsupported_name + "\n";
    const HostLoopPlan host_loop = plan_native_host_loop();
    const std::string host_loop_diag =
        "host_loop input=" + std::to_string(host_loop.input_polling) +
        " window=" + std::to_string(host_loop.window_events) +
        " audio=" + std::to_string(host_loop.audio) +
        " framebuffer=" + std::to_string(host_loop.framebuffer) +
        " vulkan=" + std::to_string(host_loop.vulkan) +
        " memory_card=" + std::to_string(host_loop.memory_card) +
        " disc=" + std::to_string(host_loop.disc) + "\n";
    const std::string mode = get_diagnostic_mode();
    DiagnosticReport report{
        TargetPlatform::Linux,
        plan,
        {},
    };
    const std::string formatted_report =
        format_diagnostic_report(report, mode);
    const std::string completed = "native lowering complete\n";
    const std::string host_title = "AnyPS1";
    source << ".text\n.section .bss\n.p2align 4\n";
    source << "regs:\n.skip 136\nram:\n.skip 2097152\nscratchpad:\n.skip 1024\n";
    source << "cop0:\n.skip 128\n";
    source << "cop3_data:\n.skip 128\n";
    source << "mmio_state:\n.skip 8192\n";
    source << "gte_data:\n.skip 128\ngte_ctrl:\n.skip 128\n";
    source << "fregs:\n.skip 256\nfpcsr:\n.skip 4\n";
    source << "pending_load_valid:\n.skip 1\npending_load_register:\n.skip 1\n";
    source << "pending_load_value:\n.skip 4\n";
    source << "previous_load_valid:\n.skip 1\nprevious_load_register:\n.skip 1\n";
    source << "previous_load_value:\n.skip 4\n";
    source << "host_frame_counter:\n.skip 4\n";
    source << ".section .rodata\n";
    source << "diag_banner:.asciz \"" << escape_assembly_string(banner) << "\"\n";
    source << "diag_target:.asciz \"" << escape_assembly_string(target) << "\"\n";
    source << "diag_load:.asciz \"" << escape_assembly_string(load) << "\"\n";
    source << "diag_entry:.asciz \"" << escape_assembly_string(entry) << "\"\n";
    source << "diag_gp:.asciz \"" << escape_assembly_string(gp) << "\"\n";
    source << "diag_sp:.asciz \"" << escape_assembly_string(sp) << "\"\n";
    source << "diag_payload:.asciz \"" << escape_assembly_string(payload_size) << "\"\n";
    source << "diag_lowered:.asciz \"" << escape_assembly_string(lowered) << "\"\n";
    source << "diag_coverage:.asciz \"" << escape_assembly_string(coverage) << "\"\n";
    source << "diag_edges:.asciz \"" << escape_assembly_string(edges) << "\"\n";
    source << "diag_unresolved:.asciz \"" << escape_assembly_string(unresolved) << "\"\n";
    source << "diag_reachable:.asciz \"" << escape_assembly_string(reachable) << "\"\n";
    source << "diag_supported:.asciz \"" << escape_assembly_string(supported) << "\"\n";
    source << "diag_embedded:.asciz \"" << escape_assembly_string(embedded) << "\"\n";
    source << "diag_unknown:.asciz \"" << escape_assembly_string(unknown) << "\"\n";
    source << "diag_executable_range:.asciz \"" << escape_assembly_string(executable_range) << "\"\n";
    source << "diag_ram_range:.asciz \"" << escape_assembly_string(ram_range) << "\"\n";
    source << "diag_scratchpad_range:.asciz \"" << escape_assembly_string(scratchpad_range) << "\"\n";
    source << "diag_unsupported:.asciz \"" << escape_assembly_string(unsupported) << "\"\n";
    source << "diag_first_gap:.asciz \"" << escape_assembly_string(first_gap) << "\"\n";
    source << "diag_host_loop:.asciz \"" << escape_assembly_string(host_loop_diag) << "\"\n";
    source << "diag_report:.asciz \"" << escape_assembly_string(formatted_report) << "\"\n";
    source << "diag_completed:.asciz \"" << escape_assembly_string(completed) << "\"\n";
    source << "diag_host_title:.asciz \"" << escape_assembly_string(host_title) << "\"\n";
    source << ".text\n.globl _start\n_start:\n";
    source << "leaq diag_banner(%rip), %rsi\nmov $" << banner.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_target(%rip), %rsi\nmov $" << target.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_load(%rip), %rsi\nmov $" << load.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_entry(%rip), %rsi\nmov $" << entry.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_gp(%rip), %rsi\nmov $" << gp.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_sp(%rip), %rsi\nmov $" << sp.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_payload(%rip), %rsi\nmov $" << payload_size.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_lowered(%rip), %rsi\nmov $" << lowered.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_coverage(%rip), %rsi\nmov $" << coverage.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_edges(%rip), %rsi\nmov $" << edges.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_unresolved(%rip), %rsi\nmov $" << unresolved.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_reachable(%rip), %rsi\nmov $" << reachable.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_supported(%rip), %rsi\nmov $" << supported.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_embedded(%rip), %rsi\nmov $" << embedded.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_unknown(%rip), %rsi\nmov $" << unknown.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_executable_range(%rip), %rsi\nmov $" << executable_range.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_ram_range(%rip), %rsi\nmov $" << ram_range.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_scratchpad_range(%rip), %rsi\nmov $" << scratchpad_range.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_unsupported(%rip), %rsi\nmov $" << unsupported.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_first_gap(%rip), %rsi\nmov $" << first_gap.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_host_loop(%rip), %rsi\nmov $" << host_loop_diag.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_report(%rip), %rsi\nmov $" << formatted_report.size() << ", %rdx\ncall print_diag\n";
    source << "leaq ram(%rip), %rdi\n";
    source << "add $" << payload_ram_offset << ", %rdi\n";
    source << "leaq payload(%rip), %rsi\n";
    source << "mov $" << file_size << ", %ecx\n";
    source << "rep movsb\n";
    source << "leaq regs(%rip), %r12\n";
    source << "movl $0, 0(%r12)\n";
    source << "movl $0x" << std::hex << initial_sp << std::dec << ", 116(%r12)\n";
    source << "movl $0x" << std::hex << initial_gp << std::dec << ", 112(%r12)\n";
    if (native_host_loop_required) {
        source << "leaq diag_host_title(%rip), %rdi\n";
        source << "call anyps1_host_loop_init\n";
        source << "testl %eax, %eax\n";
        source << "jne host_runtime_exit\n";
        source << "leaq ram(%rip), %rdi\n";
        source << "call anyps1_host_loop_attach_ram\n";
        source << "testl %eax, %eax\n";
        source << "jne host_runtime_exit\n";
        source << "movl $0, host_frame_counter(%rip)\n";
    }
    if (emittable_leaders.find(initial_pc) != emittable_leaders.end()) {
        source << "jmp p" << std::hex << initial_pc << std::dec << "\n";
    } else {
        source << "jmp unsupported_instruction_exit\n";
    }
    source << ".section .rodata\npayload:\n.incbin \"" << payload.string() << "\"\n";
    source << ".text\n";
    source << "overflow_exit:\nmov $12, %edi\nmov $60, %eax\nsyscall\n";
    source << "divide_zero_exit:\nmov $11, %edi\nmov $60, %eax\nsyscall\n";
    source << "memory_fault_exit:\nmov $13, %edi\nmov $60, %eax\nsyscall\n";
    source << "mmio_exit:\nmov $14, %edi\nmov $60, %eax\nsyscall\n";
    source << "unsupported_jump_exit:\nmov $15, %edi\nmov $60, %eax\nsyscall\n";
    source << "unsupported_bios_exit:\nmov $17, %edi\nmov $60, %eax\nsyscall\n";
    source << "unsupported_instruction_exit:\n";
    source << "leaq diag_first_gap(%rip), %rsi\n";
    source << "mov $" << first_gap.size() << ", %rdx\n";
    source << "call print_diag\n";
    source << "leaq diag_host_loop(%rip), %rsi\n";
    source << "mov $" << host_loop_diag.size() << ", %rdx\n";
    source << "call print_diag\n";
    source << "mov $16, %edi\nmov $60, %eax\nsyscall\n";
    source << "host_runtime_exit:\n";
    source << "call anyps1_host_loop_shutdown\n";
    source << "mov $18, %edi\nmov $60, %eax\nsyscall\n";
    source << "host_shutdown_exit:\n";
    source << "call anyps1_host_loop_shutdown\n";
    source << "xor %edi, %edi\nmov $60, %eax\nsyscall\n";
    for (const std::string& family : unsupported_family_names) {
        source << instruction_family_source_name(family)
               << ":\njmp unsupported_instruction_exit\n";
    }
    source << ".text\n";
    source << "print_diag:\n";
    source << "mov $1, %edi\n";
    source << "mov $1, %eax\n";
    source << "syscall\n";
    source << "ret\n";
    source << ".extern anyps1_gte_execute\n";
    source << "indirect_jump:\n";
    const Ps1BiosCallPlan init_heap_call =
        map_ps1_bios_call(0xA0u, 0x39u);
    source << "cmpl $" << init_heap_call.vector << ", %eax\n";
    source << "jne bios_b0_c0_dispatch\n";
    source << "cmpl $" << init_heap_call.function << ", 36(%r12)\n";
    source << "jne unsupported_bios_exit\n";
    source << "movl $0, 8(%r12)\n";
    source << "movl 124(%r12), %eax\n";
    source << "jmp indirect_jump\n";
    source << "bios_b0_c0_dispatch:\n";
    const std::uint32_t bios_vectors[] = {
        0xB0u, 0xC0u
    };
    const std::uint32_t b0_functions[] = {
        0x00u, 0x01u, 0x02u, 0x07u, 0x08u, 0x09u,
        0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x12u, 0x13u,
        0x14u, 0x15u, 0x16u, 0x17u, 0x18u, 0x32u,
        0x33u, 0x35u
    };
    const std::uint32_t c0_functions[] = {
        0x00u, 0x01u, 0x02u, 0x03u, 0x08u, 0x09u, 0x0Au, 0x0Bu,
        0x10u, 0x13u
    };
    for (std::uint32_t vector : bios_vectors) {
        const std::uint32_t* functions =
            vector == 0xB0u ? b0_functions : c0_functions;
        const std::size_t function_count =
            vector == 0xB0u ? sizeof(b0_functions) / sizeof(b0_functions[0])
                             : sizeof(c0_functions) / sizeof(c0_functions[0]);
        for (std::size_t index = 0; index < function_count; ++index) {
            const std::uint32_t function = functions[index];
            const std::string label =
                "bios_" + (vector == 0xB0u ? std::string("b0") : std::string("c0")) +
                "_" + std::to_string(function);
            source << "cmpl $" << vector << ", %eax\n";
            source << "je " << label << "_vector_ok\n";
            source << "cmpl $" << (vector << 24) << ", %eax\n";
            source << "jne " << label << "_next_vector\n";
            source << label << "_vector_ok:\n";
            source << "cmpl $" << function << ", 36(%r12)\n";
            source << "jne " << label << "_next_vector\n";
            if (vector == 0xB0u && function == 0x00u) {
                source << "movl $0x80100000, 8(%r12)\n";
            } else {
                source << "movl $1, 8(%r12)\n";
            }
            source << "movl 124(%r12), %eax\n";
            source << "jmp indirect_jump\n";
            source << label << "_next_vector:\n";
        }
    }
    source << "indirect_leader_dispatch:\n";
    for (std::uint32_t indirect_target : emittable_leaders) {
        source << "cmpl $" << indirect_target << ", %eax\n";
        source << "je p" << std::hex << indirect_target << std::dec << "\n";
    }
    source << "jmp unsupported_jump_exit\n";
    auto emit_unsupported_stub = [&](std::uint32_t instruction) {
        const UnsupportedInstruction unsupported =
            classify_unsupported_instruction(instruction);
        source << "jmp "
               << instruction_family_source_name(unsupported.name)
               << "\n";
    };

    auto emit_schedule_load = [&](std::uint32_t rt) {
        source << "movl %edx, pending_load_value(%rip)\n";
        source << "movb $" << rt << ", pending_load_register(%rip)\n";
        source << "movb $1, pending_load_valid(%rip)\n";
    };

    auto emit_normal_body = [&](std::uint32_t address, std::uint32_t instruction) {
        const MipsInstructionIr ir =
            build_mips_instruction_ir(address, instruction);
        if (ir.kind == MipsInstructionKind::Nop) return;
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t rs = (instruction >> 21) & 31u;
        const std::uint32_t rt = (instruction >> 16) & 31u;
        const std::uint32_t rd = (instruction >> 11) & 31u;
        const std::uint32_t shamt = (instruction >> 6) & 31u;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        const std::uint32_t special = instruction & 63u;
        if (opcode == 0) {
            if (instruction == 0) return;
            if (special == 0x00) {
                source << "movl " << rt * 4 << "(%r12), %eax\nshll $" << shamt << ", %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x02) {
                source << "movl " << rt * 4 << "(%r12), %eax\nshrl $" << shamt << ", %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x03) {
                source << "movl " << rt * 4 << "(%r12), %eax\nsarl $" << shamt << ", %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x04) {
                source << "movl " << rs * 4 << "(%r12), %ecx\nmovl " << rt * 4 << "(%r12), %eax\nshll %cl, %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x06) {
                source << "movl " << rs * 4 << "(%r12), %ecx\nmovl " << rt * 4 << "(%r12), %eax\nshrl %cl, %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x07) {
                source << "movl " << rs * 4 << "(%r12), %ecx\nmovl " << rt * 4 << "(%r12), %eax\nsarl %cl, %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x20) {
                source << "movl " << rs * 4 << "(%r12), %eax\naddl " << rt * 4 << "(%r12), %eax\njo overflow_exit\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x21) {
                source << "movl " << rs * 4 << "(%r12), %eax\naddl " << rt * 4 << "(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x22) {
                source << "movl " << rs * 4 << "(%r12), %eax\nsubl " << rt * 4 << "(%r12), %eax\njo overflow_exit\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x23) {
                source << "movl " << rs * 4 << "(%r12), %eax\nsubl " << rt * 4 << "(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x24) {
                source << "movl " << rs * 4 << "(%r12), %eax\nandl " << rt * 4 << "(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x25) {
                source << "movl " << rs * 4 << "(%r12), %eax\norl " << rt * 4 << "(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x26) {
                source << "movl " << rs * 4 << "(%r12), %eax\nxorl " << rt * 4 << "(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x27) {
                source << "movl " << rs * 4 << "(%r12), %eax\norl " << rt * 4 << "(%r12), %eax\nnotl %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x2A) {
                source << "movl " << rs * 4 << "(%r12), %eax\ncmpl " << rt * 4 << "(%r12), %eax\nsetl %al\nmovzbl %al, %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x2B) {
                source << "movl " << rs * 4 << "(%r12), %eax\ncmpl " << rt * 4 << "(%r12), %eax\nsetb %al\nmovzbl %al, %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x18) {
                source << "movl " << rs * 4 << "(%r12), %eax\nmovl " << rt * 4 << "(%r12), %ecx\ncltd\nimull %ecx\nmovl %eax, 128(%r12)\nmovl %edx, 132(%r12)\n";
            } else if (special == 0x19) {
                source << "movl " << rs * 4 << "(%r12), %eax\nmovl " << rt * 4 << "(%r12), %ecx\nxorl %edx, %edx\nmull %ecx\nmovl %eax, 128(%r12)\nmovl %edx, 132(%r12)\n";
            } else if (special == 0x1A || special == 0x1B) {
                source << "movl " << rs * 4 << "(%r12), %eax\nmovl " << rt * 4 << "(%r12), %ecx\ntestl %ecx, %ecx\nje divide_zero_exit\n";
                if (special == 0x1B) source << "xorl %edx, %edx\ndivl %ecx\n";
                else source << "cltd\nidivl %ecx\n";
                source << "movl %eax, 128(%r12)\nmovl %edx, 132(%r12)\n";
            } else if (special == 0x10) {
                source << "movl 132(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x11) {
                source << "movl " << rs * 4 << "(%r12), %eax\nmovl %eax, 132(%r12)\n";
            } else if (special == 0x12) {
                source << "movl 128(%r12), %eax\nmovl %eax, " << rd * 4 << "(%r12)\n";
            } else if (special == 0x13) {
                source << "movl " << rs * 4 << "(%r12), %eax\nmovl %eax, 128(%r12)\n";
            } else if (special == 0x08 || special == 0x09) {
                if (special == 0x08) {
                    source << "movl " << rs * 4 << "(%r12), %eax\n";
                } else {
                    source << "movl " << rs * 4 << "(%r12), %eax\n";
                    source << "movl $" << address + 8 << ", " << rd * 4 << "(%r12)\n";
                }
            } else if (special == 0x0C) {
                source << "xor %edi, %edi\nmov $60, %eax\nsyscall\n";
            } else if (special == 0x0D) {
                source << "jmp p" << std::hex << address + 4u << std::dec << "\n";
            } else {
                emit_unsupported_stub(instruction);
            }
        } else if (opcode == 0x10) {
            const std::uint32_t cop0_register = rd;
            if (rs == 0) {
                source << "movl cop0+" << cop0_register * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (rs == 2) {
                source << "movl cop0+" << cop0_register * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (rs == 4) {
                source << "movl " << rt * 4 << "(%r12), %eax\n"
                       << "movl %eax, cop0+" << cop0_register * 4 << "(%rip)\n";
            } else if (rs == 6) {
                source << "movl " << rt * 4 << "(%r12), %eax\n"
                       << "movl %eax, cop0+" << cop0_register * 4 << "(%rip)\n";
            } else if (rs == 16 && (instruction & 0x3Fu) == 0x10) {
                source << "movl cop0+48(%rip), %eax\n"
                       << "andl $~3, %eax\n"
                       << "movl %eax, cop0+48(%rip)\n";
            } else {
                emit_unsupported_stub(instruction);
            }
        } else if (opcode == 0x12) {
            const std::uint32_t cop2_rs = rs;
            const std::uint32_t cop2_reg = rd;
            if (cop2_rs == 0) {
                source << "movl gte_data+" << cop2_reg * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (cop2_rs == 2) {
                source << "movl gte_ctrl+" << cop2_reg * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (cop2_rs == 4) {
                source << "movl " << rt * 4 << "(%r12), %eax\n";
                source << "movl %eax, gte_data+" << cop2_reg * 4 << "(%rip)\n";
            } else if (cop2_rs == 6) {
                source << "movl " << rt * 4 << "(%r12), %eax\n";
                source << "movl %eax, gte_ctrl+" << cop2_reg * 4 << "(%rip)\n";
            } else if (cop2_rs >= 16) {
                source << "movl $" << instruction << ", %edi\n";
                source << "leaq gte_data(%rip), %rsi\n";
                source << "leaq gte_ctrl(%rip), %rdx\n";
                source << "call anyps1_gte_execute\n";
                source << "testl %eax, %eax\n";
                source << "je unsupported_instruction_exit\n";
            } else if (cop2_rs == 16) {
                switch (instruction) {
                case 0x48C0C000u: // NCLIP
                    source << "movl gte_data+0(%rip), %eax\n";
                    source << "movl gte_data+20(%rip), %ecx\n";
                    source << "imull %ecx\n";
                    source << "movl %eax, %esi\n";
                    source << "movl gte_data+4(%rip), %eax\n";
                    source << "movl gte_data+16(%rip), %ecx\n";
                    source << "imull %ecx\n";
                    source << "subl %eax, %esi\n";
                    source << "movl %esi, gte_data+12(%rip)\n";
                    break;
                case 0x48C0C800u: // OP
                    source << "movl gte_data+0(%rip), %eax\n";
                    source << "movl gte_data+4(%rip), %ecx\n";
                    source << "addl %ecx, %eax\n";
                    source << "movl %eax, gte_data+36(%rip)\n";
                    source << "movl gte_data+8(%rip), %eax\n";
                    source << "movl %eax, gte_data+40(%rip)\n";
                    source << "xorl %eax, %eax\n";
                    source << "movl %eax, gte_data+44(%rip)\n";
                    break;
                case 0x48C8D000u: // DPCS
                    source << "movl gte_data+16(%rip), %eax\n";
                    source << "movl gte_data+20(%rip), %ecx\n";
                    source << "movl gte_data+24(%rip), %edx\n";
                    source << "movl %eax, gte_data+36(%rip)\n";
                    source << "movl %ecx, gte_data+40(%rip)\n";
                    source << "movl %edx, gte_data+44(%rip)\n";
                    break;
                case 0x48C8D800u: // INTPL
                    source << "movl gte_data+16(%rip), %eax\n";
                    source << "addl gte_data+36(%rip), %eax\n";
                    source << "movl %eax, gte_data+36(%rip)\n";
                    source << "movl gte_data+20(%rip), %eax\n";
                    source << "addl gte_data+40(%rip), %eax\n";
                    source << "movl %eax, gte_data+40(%rip)\n";
                    source << "movl gte_data+24(%rip), %eax\n";
                    source << "addl gte_data+44(%rip), %eax\n";
                    source << "movl %eax, gte_data+44(%rip)\n";
                    break;
                case 0x48C8E000u: // MVMVA
                    source << "movl gte_data+0(%rip), %eax\n";
                    source << "movl gte_data+4(%rip), %ecx\n";
                    source << "movl gte_data+8(%rip), %edx\n";
                    source << "sarl $12, %eax\n";
                    source << "sarl $12, %ecx\n";
                    source << "sarl $12, %edx\n";
                    source << "addl %ecx, %eax\n";
                    source << "addl %edx, %eax\n";
                    source << "shll $12, %eax\n";
                    source << "movl %eax, gte_data+36(%rip)\n";
                    source << "movl %eax, gte_data+40(%rip)\n";
                    source << "movl %eax, gte_data+44(%rip)\n";
                    break;
                case 0x48C8E800u: // SQR
                    source << "movl gte_data+0(%rip), %eax\n";
                    source << "imull %eax\n";
                    source << "movl %eax, gte_data+36(%rip)\n";
                    source << "movl gte_data+4(%rip), %eax\n";
                    source << "imull %eax\n";
                    source << "movl %eax, gte_data+40(%rip)\n";
                    source << "movl gte_data+8(%rip), %eax\n";
                    source << "imull %eax\n";
                    source << "movl %eax, gte_data+44(%rip)\n";
                    break;
                case 0x48C8F000u: // RTV0
                    source << "movl gte_data+0(%rip), %eax\n";
                    source << "movl gte_data+4(%rip), %ecx\n";
                    source << "movl gte_data+8(%rip), %edx\n";
                    source << "sarl $12, %eax\n";
                    source << "sarl $12, %ecx\n";
                    source << "sarl $12, %edx\n";
                    source << "addl gte_ctrl+48(%rip), %eax\n";
                    source << "addl gte_ctrl+52(%rip), %ecx\n";
                    source << "addl gte_ctrl+56(%rip), %edx\n";
                    source << "shll $12, %eax\n";
                    source << "shll $12, %ecx\n";
                    source << "shll $12, %edx\n";
                    source << "movl %eax, gte_data+36(%rip)\n";
                    source << "movl %ecx, gte_data+40(%rip)\n";
                    source << "movl %edx, gte_data+44(%rip)\n";
                    break;
                default:
                    emit_unsupported_stub(instruction);
                }
            } else {
                emit_unsupported_stub(instruction);
            }
        } else if (opcode == 0x11) {
            const std::uint32_t cop1_rs = rs;
            const std::uint32_t cop1_fd = rd;
            if (cop1_rs == 0) {
                source << "movl fregs+" << cop1_fd * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (cop1_rs == 2) {
                source << "movl fpcsr(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (cop1_rs == 4) {
                source << "movl " << rt * 4 << "(%r12), %eax\n";
                source << "movl %eax, fregs+" << cop1_fd * 4 << "(%rip)\n";
            } else if (cop1_rs == 6) {
                source << "movl " << rt * 4 << "(%r12), %eax\n";
                source << "movl %eax, fpcsr(%rip)\n";
            } else if (cop1_rs == 16) {
                const std::uint32_t ft = rt;
                const std::uint32_t fs = rd;
                const std::uint32_t fd = shamt;
                if (special == 0) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "addss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 1) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "subss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 2) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "mulss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 3) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "divss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 4) {
                    source << "sqrtss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 5) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "andl $0x7FFFFFFF, %eax\n";
                    source << "movd %xmm0, %eax\n";
                    source << "andl $0x7FFFFFFF, %eax\n";
                    source << "movd %eax, %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 6) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 7) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "movd %xmm0, %eax\n";
                    source << "xorl $0x80000000, %eax\n";
                    source << "movd %eax, %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 12) {
                    source << "roundss $0, fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "cvttss2si %xmm0, %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 13) {
                    source << "cvttss2si fregs+" << fs * 4 << "(%rip), %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 14) {
                    source << "roundss $2, fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "cvttss2si %xmm0, %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 15) {
                    source << "roundss $1, fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "cvttss2si %xmm0, %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 5) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "andl $0x7FFFFFFF, %eax\n";
                    source << "movd %xmm0, %eax\n";
                    source << "andl $0x7FFFFFFF, %eax\n";
                    source << "movd %eax, %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 6) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 7) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "movd %xmm0, %eax\n";
                    source << "xorl $0x80000000, %eax\n";
                    source << "movd %eax, %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 12) {
                    source << "roundss $0, fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "cvttss2si %xmm0, %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 13) {
                    source << "cvttss2si fregs+" << fs * 4 << "(%rip), %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 14) {
                    source << "roundss $2, fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "cvttss2si %xmm0, %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 15) {
                    source << "roundss $1, fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "cvttss2si %xmm0, %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 0x20) {
                    source << "cvttss2si fregs+" << fs * 4 << "(%rip), %eax\n";
                    source << "movl %eax, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 0x24) {
                    source << "movl fregs+" << fs * 4 << "(%rip), %eax\n";
                    source << "cvtsi2ss %eax, %xmm0\n";
                    source << "movss %xmm0, fregs+" << fd * 4 << "(%rip)\n";
                } else if (special == 0x30) {
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x31) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "comiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "setp %dl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x32) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "comiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "setz %dl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x33) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "comiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "setz %dl\n";
                    source << "setp %cl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "movzbl %cl, %ecx\n";
                    source << "orl %ecx, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x34) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "comiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "setb %dl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x36) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "comiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "setbe %dl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x35) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "ucomiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "sete %dl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special == 0x37) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "ucomiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    source << "setbe %dl\n";
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else if (special >= 0x38) {
                    source << "movss fregs+" << fs * 4 << "(%rip), %xmm0\n";
                    source << "ucomiss fregs+" << ft * 4 << "(%rip), %xmm0\n";
                    source << "movl fpcsr(%rip), %eax\n";
                    source << "andl $~1, %eax\n";
                    if (special == 0x38) {
                        source << "seto %dl\n";
                    } else if (special == 0x39) {
                        source << "setne %dl\n";
                    } else if (special == 0x3A) {
                        source << "sete %dl\n";
                    } else if (special == 0x3B) {
                        source << "setne %dl\n";
                    } else if (special == 0x3C) {
                        source << "setl %dl\n";
                    } else if (special == 0x3D) {
                        source << "setge %dl\n";
                    } else if (special == 0x3E) {
                        source << "setle %dl\n";
                    } else if (special == 0x3F) {
                        source << "setg %dl\n";
                    } else {
                        source << "movzbl %dl, %edx\n";
                        source << "movl %eax, fpcsr(%rip)\n";
                        emit_unsupported_stub(instruction);
                        return;
                    }
                    source << "movzbl %dl, %edx\n";
                    source << "orl %edx, %eax\n";
                    source << "movl %eax, fpcsr(%rip)\n";
                } else {
                    emit_unsupported_stub(instruction);
                }
            } else {
                emit_unsupported_stub(instruction);
            }
        } else if (opcode == 0x13) {
            const std::uint32_t cop3_register = rd;
            if (rs == 0) {
                source << "movl cop3_data+" << cop3_register * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (rs == 2) {
                source << "movl cop3_data+" << cop3_register * 4 << "(%rip), %eax\n";
                source << "movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (rs == 4) {
                source << "movl " << rt * 4 << "(%r12), %eax\n";
                source << "movl %eax, cop3_data+" << cop3_register * 4 << "(%rip)\n";
            } else if (rs == 6) {
                source << "movl " << rt * 4 << "(%r12), %eax\n";
                source << "movl %eax, cop3_data+" << cop3_register * 4 << "(%rip)\n";
            } else {
                emit_unsupported_stub(instruction);
            }
        } else if (opcode == 0x0F) {
            source << "movl $" << (immediate << 16) << ", " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x08) {
            source << "movl " << rs * 4 << "(%r12), %eax\naddl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\njo overflow_exit\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x09) {
            source << "movl " << rs * 4 << "(%r12), %eax\naddl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x0C) {
            source << "movl " << rs * 4 << "(%r12), %eax\nandl $" << immediate << ", %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x0D) {
            source << "movl " << rs * 4 << "(%r12), %eax\norl $" << immediate << ", %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x0E) {
            source << "movl " << rs * 4 << "(%r12), %eax\nxorl $" << immediate << ", %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x0A) {
            source << "movl " << rs * 4 << "(%r12), %eax\ncmpl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\nsetl %al\nmovzbl %al, %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x0B) {
            source << "movl " << rs * 4 << "(%r12), %eax\ncmpl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\nsetb %al\nmovzbl %al, %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x14 || opcode == 0x15 ||
                   opcode == 0x16 || opcode == 0x17) {
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            if (opcode == 0x14 || opcode == 0x15) {
                source << "cmpl " << rt * 4 << "(%r12), %eax\n";
                source << (opcode == 0x14 ? "je " : "jne ");
            } else {
                source << "testl %eax, %eax\n";
                source << (opcode == 0x16 ? "jle " : "jg ");
            }
            const std::int32_t displacement =
                static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t target =
                static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(address) + 4 + displacement);
            source << "p" << std::hex << target << std::dec << "\n";
        } else if (opcode == 0x2F) {
            // Host cache operations are folded into native memory protection/state.
        } else if (opcode == 0x20 || opcode == 0x21 || opcode == 0x23 || opcode == 0x24 || opcode == 0x25) {
            const std::uint32_t access_size =
                opcode == 0x20 || opcode == 0x24 ? 1u :
                opcode == 0x21 || opcode == 0x25 ? 2u : 4u;
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "movl %eax, %ecx\n";
            source << "andl $0x1FFFFFFF, %ecx\n";
            source << "cmpl $0x1F800000, %ecx\n";
            source << "jb ram_load_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1F800400, %ecx\n";
            source << "jae mmio_load_" << std::hex << address << std::dec << "\n";
            source << "subl $0x1F800000, %ecx\n";
            source << "cmpl $" << (0x401u - access_size) << ", %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "leaq scratchpad(%rip), %rbx\n";
            source << "jmp scratch_load_" << std::hex << address << std::dec << "\n";
            source << "mmio_load_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x1F802000, %ecx\n";
            source << "jae mmio_exit\n";
            source << "cmpl $" << (0x1F802001u - access_size) << ", %ecx\n";
            source << "jae mmio_exit\n";
            source << "subl $0x1F800000, %ecx\n";
            source << "movl %ecx, %edi\n";
            source << "addl $0x1F800000, %edi\n";
            source << "call anyps1_host_loop_mmio_read\n";
            source << "movl %eax, %edx\n";
            source << "movl %edx, pending_load_value(%rip)\n";
            source << "movb $" << rt << ", pending_load_register(%rip)\n";
            source << "movb $1, pending_load_valid(%rip)\n";
            source << "jmp mmio_load_done_" << std::hex << address << std::dec << "\n";
            source << "leaq mmio_state(%rip), %rbx\n";
            source << "andl $0x1FFF, %ecx\n";
            source << "mmio_load_controller_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x1040, %ecx\n";
            source << "jb mmio_load_default_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1080, %ecx\n";
            source << "jae mmio_load_spu_" << std::hex << address << std::dec << "\n";
            source << "call anyps1_host_loop_controller_state\n";
            source << "movl %eax, %edx\n";
            source << "movl %edx, pending_load_value(%rip)\n";
            source << "movb $" << rt << ", pending_load_register(%rip)\n";
            source << "movb $1, pending_load_valid(%rip)\n";
            source << "jmp mmio_load_done_" << std::hex << address << std::dec << "\n";
            source << "mmio_load_spu_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x1C00, %ecx\n";
            source << "jb mmio_load_default_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1D80, %ecx\n";
            source << "jae mmio_load_default_" << std::hex << address << std::dec << "\n";
            source << "movl %ecx, %edi\n";
            source << "addl $0x1F800000, %edi\n";
            source << "call anyps1_host_loop_spu_read\n";
            source << "movl %eax, %edx\n";
            source << "movl %edx, pending_load_value(%rip)\n";
            source << "movb $" << rt << ", pending_load_register(%rip)\n";
            source << "movb $1, pending_load_valid(%rip)\n";
            source << "jmp mmio_load_done_" << std::hex << address << std::dec << "\n";
            source << "mmio_load_default_" << std::hex << address << std::dec << ": \n";
            if (opcode == 0x20) source << "movsbl (%rbx,%rcx), %edx\n";
            else if (opcode == 0x21) source << "movswl (%rbx,%rcx), %edx\n";
            else if (opcode == 0x23) source << "movl (%rbx,%rcx), %edx\n";
            else if (opcode == 0x24) source << "movzbl (%rbx,%rcx), %edx\n";
            else source << "movzwl (%rbx,%rcx), %edx\n";
            emit_schedule_load(rt);
            source << "jmp mmio_load_done_" << std::hex << address << std::dec << "\n";
            source << "ram_load_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x800000, %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "andl $0x1FFFFF, %ecx\n";
            source << "cmpl $" << (0x200001u - access_size) << ", %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "scratch_load_" << std::hex << address << std::dec << ": \n";
            if (opcode == 0x20) source << "movsbl (%rbx,%rcx), %edx\n";
            else if (opcode == 0x21) source << "movswl (%rbx,%rcx), %edx\n";
            else if (opcode == 0x23) source << "movl (%rbx,%rcx), %edx\n";
            else if (opcode == 0x24) source << "movzbl (%rbx,%rcx), %edx\n";
            else source << "movzwl (%rbx,%rcx), %edx\n";
            emit_schedule_load(rt);
            source << "mmio_load_done_" << std::hex << address << std::dec << ": \n";
        } else if (opcode == 0x28 || opcode == 0x29 || opcode == 0x2B) {
            const std::uint32_t access_size =
                opcode == 0x28 ? 1u : opcode == 0x29 ? 2u : 4u;
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "movl %eax, %ecx\n";
            source << "andl $0x1FFFFFFF, %ecx\n";
            source << "cmpl $0x1F800000, %ecx\n";
            source << "jb ram_store_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1F800400, %ecx\n";
            source << "jae mmio_store_" << std::hex << address << std::dec << "\n";
            source << "subl $0x1F800000, %ecx\n";
            source << "cmpl $" << (0x401u - access_size) << ", %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "leaq scratchpad(%rip), %rbx\n";
            source << "jmp scratch_store_" << std::hex << address << std::dec << "\n";
            source << "mmio_store_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x1F802000, %ecx\n";
            source << "jae mmio_exit\n";
            source << "cmpl $" << (0x1F802001u - access_size) << ", %ecx\n";
            source << "jae mmio_exit\n";
            source << "subl $0x1F800000, %ecx\n";
            source << "movl %ecx, %edi\n";
            source << "addl $0x1F800000, %edi\n";
            source << "movl " << rt * 4 << "(%r12), %esi\n";
            source << "call anyps1_host_loop_mmio_write\n";
            source << "cmpl $0, %eax\n";
            source << "jne host_runtime_exit\n";
            source << "jmp mmio_store_done_" << std::hex << address << std::dec << "\n";
            source << "leaq mmio_state(%rip), %rbx\n";
            source << "andl $0x1FFF, %ecx\n";
            source << "movl " << rt * 4 << "(%r12), %edx\n";
            if (opcode == 0x2B) {
                source << "cmpl $0x810, %ecx\n";
                source << "jb mmio_native_spu_" << std::hex << address << std::dec << "\n";
                source << "cmpl $0x815, %ecx\n";
                source << "jae mmio_native_spu_" << std::hex << address << std::dec << "\n";
                source << "movl %ecx, %edi\n";
                source << "addl $0x1F800000, %edi\n";
                source << "movl %edx, %esi\n";
                source << "call anyps1_host_loop_gpu_write\n";
                source << "jmp mmio_native_done_" << std::hex << address << std::dec << "\n";
                source << "mmio_native_spu_" << std::hex << address << std::dec << ": \n";
                source << "cmpl $0xC00, %ecx\n";
                source << "jb mmio_native_done_" << std::hex << address << std::dec << "\n";
                source << "cmpl $0xD80, %ecx\n";
                source << "jae mmio_native_done_" << std::hex << address << std::dec << "\n";
                source << "movl %ecx, %edi\n";
                source << "addl $0x1F800000, %edi\n";
                source << "movl %edx, %esi\n";
                source << "call anyps1_host_loop_spu_write\n";
                source << "mmio_native_done_" << std::hex << address << std::dec << ": \n";
            }
            if (opcode == 0x28) source << "movb %dl, (%rbx,%rcx)\n";
            else if (opcode == 0x29) source << "movw %dx, (%rbx,%rcx)\n";
            else source << "movl %edx, (%rbx,%rcx)\n";
            source << "mmio_store_done_" << std::hex << address << std::dec << ": \n";
            source << "ram_store_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x800000, %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "andl $0x1FFFFF, %ecx\n";
            source << "cmpl $" << (0x200001u - access_size) << ", %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "scratch_store_" << std::hex << address << std::dec << ": \n";
            source << "movl " << rt * 4 << "(%r12), %edx\n";
            if (opcode == 0x28) source << "movb %dl, (%rbx,%rcx)\n";
            else if (opcode == 0x29) source << "movw %dx, (%rbx,%rcx)\n";
            else source << "movl %edx, (%rbx,%rcx)\n";
        } else if (opcode == 0x32 || opcode == 0x3A) {
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "movl %eax, %ecx\n";
            source << "andl $0x1FFFFFFF, %ecx\n";
            source << "cmpl $0x1F800000, %ecx\n";
            source << "jb gte_ram_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1F800400, %ecx\n";
            source << "jae mmio_exit\n";
            source << "subl $0x1F800000, %ecx\n";
            source << "leaq scratchpad(%rip), %rbx\n";
            source << "jmp gte_finish_" << std::hex << address << std::dec << "\n";
            source << "gte_ram_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x800000, %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "andl $0x1FFFFF, %ecx\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "gte_finish_" << std::hex << address << std::dec << ": \n";
            if (opcode == 0x32) {
                source << "movl (%rbx,%rcx), %eax\n";
                source << "movl %eax, gte_data+" << rt * 4 << "(%rip)\n";
            } else {
                source << "movl gte_data+" << rt * 4 << "(%rip), %eax\n";
                source << "movl %eax, (%rbx,%rcx)\n";
            }
        } else if (opcode == 0x31 || opcode == 0x39) {
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "movl %eax, %ecx\n";
            source << "andl $0x1FFFFFFF, %ecx\n";
            source << "cmpl $0x1F800000, %ecx\n";
            source << "jb cop1_ram_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1F800400, %ecx\n";
            source << "jae mmio_exit\n";
            source << "subl $0x1F800000, %ecx\n";
            source << "leaq scratchpad(%rip), %rbx\n";
            source << "jmp cop1_finish_" << std::hex << address << std::dec << "\n";
            source << "cop1_ram_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x800000, %ecx\n";
            source << "jae memory_fault_exit\n";
            source << "andl $0x1FFFFF, %ecx\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "cop1_finish_" << std::hex << address << std::dec << ": \n";
            if (opcode == 0x31) {
                source << "movss (%rbx,%rcx), %xmm0\n";
                source << "movss %xmm0, fregs+" << rt * 4 << "(%rip)\n";
            } else {
                source << "movss fregs+" << rt * 4 << "(%rip), %xmm0\n";
                source << "movss %xmm0, (%rbx,%rcx)\n";
            }
        } else if (opcode == 0x30 || opcode == 0x33 ||
                   opcode == 0x38 || opcode == 0x3B ||
                   opcode == 0x35 || opcode == 0x36 ||
                   opcode == 0x37 || opcode == 0x3D ||
                   opcode == 0x3E || opcode == 0x3F) {
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "andl $0x1FFFFFFF, %eax\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "cmpl $0x200000, %eax\n";
            source << "jae mmio_exit\n";
            if (opcode == 0x30) {
                source << "movl (%rbx,%rax), %eax\n";
                source << "movl %eax, cop0+" << rt * 4 << "(%rip)\n";
            } else if (opcode == 0x35) {
                source << "movq (%rbx,%rax), %xmm0\n";
                source << "movq %xmm0, fregs+" << rt * 4 << "(%rip)\n";
            } else if (opcode == 0x36) {
                source << "movq (%rbx,%rax), %xmm0\n";
                source << "movq %xmm0, gte_data+" << rt * 4 << "(%rip)\n";
            } else if (opcode == 0x37) {
                source << "movq (%rbx,%rax), %xmm0\n";
                source << "movq %xmm0, cop3_data+" << rt * 4 << "(%rip)\n";
            } else if (opcode == 0x33) {
                source << "movl (%rbx,%rax), %eax\n";
                source << "movl %eax, cop3_data+" << rt * 4 << "(%rip)\n";
            } else if (opcode == 0x38) {
                source << "movl cop0+" << rt * 4 << "(%rip), %edx\n";
                source << "movl %edx, (%rbx,%rax)\n";
            } else if (opcode == 0x3B) {
                source << "movl cop3_data+" << rt * 4 << "(%rip), %edx\n";
                source << "movl %edx, (%rbx,%rax)\n";
            } else if (opcode == 0x3D) {
                source << "movq fregs+" << rt * 4 << "(%rip), %xmm0\n";
                source << "movq %xmm0, (%rbx,%rax)\n";
            } else if (opcode == 0x3E) {
                source << "movq gte_data+" << rt * 4 << "(%rip), %xmm0\n";
                source << "movq %xmm0, (%rbx,%rax)\n";
            } else if (opcode == 0x3F) {
                source << "movq cop3_data+" << rt * 4 << "(%rip), %xmm0\n";
                source << "movq %xmm0, (%rbx,%rax)\n";
            } else {
                source << "movl gte_data+" << rt * 4 << "(%rip), %edx\n";
                source << "movl %edx, (%rbx,%rax)\n";
                source << "movl gte_data+" << ((rt + 1u) & 31u) * 4 << "(%rip), %edx\n";
                source << "movl %edx, 4(%rbx,%rax)\n";
            }
        } else if (opcode == 0x22 || opcode == 0x26) {
            // Little-endian PS1 LWL/LWR: merge a word-aligned load
            // according to the effective address alignment.
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "movl %eax, %ecx\n";
            source << "andl $0x1FFFFFFF, %ecx\n";
            source << "movl %ecx, %edx\n";
            source << "andl $3, %edx\n";
            source << "andl $~3, %ecx\n";
            source << "movl %ecx, %edi\n";
            source << "cmpl $0x1F800000, %ecx\n";
            source << "jb unaligned_ram_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1F800400, %ecx\n";
            source << "jae mmio_exit\n";
            source << "subl $0x1F800000, %edi\n";
            source << "leaq scratchpad(%rip), %rbx\n";
            source << "jmp unaligned_loaded_" << std::hex << address << std::dec << "\n";
            source << "unaligned_ram_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x800000, %edi\n";
            source << "jae memory_fault_exit\n";
            source << "andl $0x1FFFFF, %edi\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "unaligned_loaded_" << std::hex << address << std::dec << ": \n";
            source << "movl " << rt * 4 << "(%r12), %esi\n";
            source << "movl (%rbx,%rdi), %eax\n";
            if (opcode == 0x22) {
                source << "cmpl $0, %edx\n";
                source << "je lwl0_" << std::hex << address << std::dec << "\n";
                source << "cmpl $1, %edx\n";
                source << "je lwl1_" << std::hex << address << std::dec << "\n";
                source << "cmpl $2, %edx\n";
                source << "je lwl2_" << std::hex << address << std::dec << "\n";
                source << "movl %eax, %esi\n";
                source << "jmp lwl_done_" << std::hex << address << std::dec << "\n";
                source << "lwl0_" << std::hex << address << std::dec << ": \n";
                source << "shll $24, %eax\n";
                source << "andl $0x00FFFFFF, %esi\n";
                source << "orl %eax, %esi\n";
                source << "jmp lwl_done_" << std::hex << address << std::dec << "\n";
                source << "lwl1_" << std::hex << address << std::dec << ": \n";
                source << "shll $16, %eax\n";
                source << "andl $0x0000FFFF, %esi\n";
                source << "orl %eax, %esi\n";
                source << "jmp lwl_done_" << std::hex << address << std::dec << "\n";
                source << "lwl2_" << std::hex << address << std::dec << ": \n";
                source << "shll $8, %eax\n";
                source << "andl $0x000000FF, %esi\n";
                source << "orl %eax, %esi\n";
                source << "lwl_done_" << std::hex << address << std::dec << ": \n";
            } else {
                source << "cmpl $0, %edx\n";
                source << "je lwr0_" << std::hex << address << std::dec << "\n";
                source << "cmpl $1, %edx\n";
                source << "je lwr1_" << std::hex << address << std::dec << "\n";
                source << "cmpl $2, %edx\n";
                source << "je lwr2_" << std::hex << address << std::dec << "\n";
                source << "shrl $24, %eax\n";
                source << "andl $0xFFFFFF00, %esi\n";
                source << "orl %eax, %esi\n";
                source << "jmp lwr_done_" << std::hex << address << std::dec << "\n";
                source << "lwr0_" << std::hex << address << std::dec << ": \n";
                source << "movl %eax, %esi\n";
                source << "jmp lwr_done_" << std::hex << address << std::dec << "\n";
                source << "lwr1_" << std::hex << address << std::dec << ": \n";
                source << "shrl $8, %eax\n";
                source << "andl $0xFF000000, %esi\n";
                source << "orl %eax, %esi\n";
                source << "jmp lwr_done_" << std::hex << address << std::dec << "\n";
                source << "lwr2_" << std::hex << address << std::dec << ": \n";
                source << "shrl $16, %eax\n";
                source << "andl $0xFFFF0000, %esi\n";
                source << "orl %eax, %esi\n";
                source << "lwr_done_" << std::hex << address << std::dec << ": \n";
            }
            source << "movl %esi, %edx\n";
            emit_schedule_load(rt);
        } else if (opcode == 0x2A || opcode == 0x2E) {
            // Little-endian PS1 SWL/SWR: merge a register into a
            // word-aligned store according to the effective address.
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\n";
            source << "movl %eax, %ecx\n";
            source << "andl $0x1FFFFFFF, %ecx\n";
            source << "movl %ecx, %edx\n";
            source << "andl $3, %edx\n";
            source << "andl $~3, %ecx\n";
            source << "movl %ecx, %edi\n";
            source << "cmpl $0x1F800000, %ecx\n";
            source << "jb unaligned_store_ram_" << std::hex << address << std::dec << "\n";
            source << "cmpl $0x1F800400, %ecx\n";
            source << "jae mmio_exit\n";
            source << "subl $0x1F800000, %edi\n";
            source << "leaq scratchpad(%rip), %rbx\n";
            source << "jmp unaligned_store_loaded_" << std::hex << address << std::dec << "\n";
            source << "unaligned_store_ram_" << std::hex << address << std::dec << ": \n";
            source << "cmpl $0x800000, %edi\n";
            source << "jae memory_fault_exit\n";
            source << "andl $0x1FFFFF, %edi\n";
            source << "leaq ram(%rip), %rbx\n";
            source << "unaligned_store_loaded_" << std::hex << address << std::dec << ": \n";
            source << "movl " << rt * 4 << "(%r12), %esi\n";
            source << "movl (%rbx,%rdi), %eax\n";
            if (opcode == 0x2A) {
                source << "cmpl $0, %edx\n";
                source << "je swl0_" << std::hex << address << std::dec << "\n";
                source << "cmpl $1, %edx\n";
                source << "je swl1_" << std::hex << address << std::dec << "\n";
                source << "cmpl $2, %edx\n";
                source << "je swl2_" << std::hex << address << std::dec << "\n";
                source << "movl %esi, %eax\n";
                source << "jmp swl_done_" << std::hex << address << std::dec << "\n";
                source << "swl0_" << std::hex << address << std::dec << ": \n";
                source << "andl $0xFFFFFF00, %eax\n";
                source << "movl %esi, %ecx\n";
                source << "shrl $24, %ecx\n";
                source << "orl %ecx, %eax\n";
                source << "jmp swl_done_" << std::hex << address << std::dec << "\n";
                source << "swl1_" << std::hex << address << std::dec << ": \n";
                source << "andl $0xFFFF0000, %eax\n";
                source << "movl %esi, %ecx\n";
                source << "shrl $16, %ecx\n";
                source << "orl %ecx, %eax\n";
                source << "jmp swl_done_" << std::hex << address << std::dec << "\n";
                source << "swl2_" << std::hex << address << std::dec << ": \n";
                source << "andl $0xFF000000, %eax\n";
                source << "movl %esi, %ecx\n";
                source << "shrl $8, %ecx\n";
                source << "orl %ecx, %eax\n";
                source << "swl_done_" << std::hex << address << std::dec << ": \n";
            } else {
                source << "cmpl $0, %edx\n";
                source << "je swr0_" << std::hex << address << std::dec << "\n";
                source << "cmpl $1, %edx\n";
                source << "je swr1_" << std::hex << address << std::dec << "\n";
                source << "cmpl $2, %edx\n";
                source << "je swr2_" << std::hex << address << std::dec << "\n";
                source << "andl $0x00FFFFFF, %eax\n";
                source << "movl %esi, %ecx\n";
                source << "shll $24, %ecx\n";
                source << "orl %ecx, %eax\n";
                source << "jmp swr_done_" << std::hex << address << std::dec << "\n";
                source << "swr0_" << std::hex << address << std::dec << ": \n";
                source << "movl %esi, %eax\n";
                source << "jmp swr_done_" << std::hex << address << std::dec << "\n";
                source << "swr1_" << std::hex << address << std::dec << ": \n";
                source << "andl $0x000000FF, %eax\n";
                source << "movl %esi, %ecx\n";
                source << "shll $8, %ecx\n";
                source << "orl %ecx, %eax\n";
                source << "jmp swr_done_" << std::hex << address << std::dec << "\n";
                source << "swr2_" << std::hex << address << std::dec << ": \n";
                source << "andl $0x0000FFFF, %eax\n";
                source << "movl %esi, %ecx\n";
                source << "shll $16, %ecx\n";
                source << "orl %ecx, %eax\n";
                source << "swr_done_" << std::hex << address << std::dec << ": \n";
            }
            source << "movl %eax, (%rbx,%rdi)\n";
        } else if ((opcode >= 0x30 && opcode <= 0x33) ||
                   (opcode >= 0x38 && opcode <= 0x3B)) {
            emit_unsupported_stub(instruction);
        } else {
            emit_unsupported_stub(instruction);
        }
    };

    std::set<std::uint32_t> emitted_addresses;
    std::uint32_t emitted_instruction_serial = 0;
    auto emit_normal = [&](std::uint32_t address, std::uint32_t instruction) {
        const std::string serial = std::to_string(emitted_instruction_serial++);
        source << "movzbl pending_load_valid(%rip), %eax\n";
        source << "movb %al, previous_load_valid(%rip)\n";
        source << "movzbl pending_load_register(%rip), %eax\n";
        source << "movb %al, previous_load_register(%rip)\n";
        source << "movl pending_load_value(%rip), %eax\n";
        source << "movl %eax, previous_load_value(%rip)\n";
        source << "movb $0, pending_load_valid(%rip)\n";
        emit_normal_body(address, instruction);
        source << "cmpb $0, previous_load_valid(%rip)\n";
        const std::string no_commit =
            "previous_load_skip_" + serial;
        source << "je " << no_commit << "\n";
        source << "movzbl previous_load_register(%rip), %eax\n";
        source << "shll $2, %eax\n";
        source << "movl previous_load_value(%rip), %edx\n";
        source << "movl %edx, (%r12,%rax)\n";
        source << no_commit << ":\n";
        source << "movl $0, 0(%r12)\n";
    };
    std::set<std::uint32_t> emitted_delay_slots;
    auto emit_label_jump = [&](std::uint32_t target) {
        if (emittable_leaders.find(target) != emittable_leaders.end()) {
            source << "jmp p" << std::hex << target << std::dec << "\n";
        } else {
            source << "jmp unsupported_jump_exit\n";
        }
    };
    for (std::uint32_t address : leaders) {
        if (emittable_leaders.find(address) == emittable_leaders.end()) continue;
        if (address < load_address || address >= load_address + file_size) continue;
        const std::uint32_t offset = address - load_address;
        if (offset + 4 > file_size) continue;
        const std::uint32_t instruction = read_psx32(data, 2048 + offset);
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t rs = (instruction >> 21) & 31u;
        const std::uint32_t rt = (instruction >> 16) & 31u;
        const std::uint32_t rd = (instruction >> 11) & 31u;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        source << "p" << std::hex << address << std::dec << ":\n";
        if (native_host_loop_required) {
            source << "incl host_frame_counter(%rip)\n";
            source << "cmpl $1024, host_frame_counter(%rip)\n";
            source << "jb host_frame_tick_done_" << std::hex << address << std::dec << "\n";
            source << "movl $0, host_frame_counter(%rip)\n";
            source << "movl $1024, %edi\n";
            source << "call anyps1_host_loop_tick\n";
            source << "testl %eax, %eax\n";
            source << "jne host_shutdown_exit\n";
            source << "host_frame_tick_done_" << std::hex << address << std::dec << ":\n";
        }

        if (delay_slots.find(address) != delay_slots.end() &&
            emitted_delay_slots.find(address) == emitted_delay_slots.end()) {
            const MipsInstructionIr ir =
                build_mips_instruction_ir(address, instruction);
            const bool address_is_control =
                ir.kind == MipsInstructionKind::Branch ||
                ir.kind == MipsInstructionKind::Jump ||
                ir.kind == MipsInstructionKind::Jr ||
                ir.kind == MipsInstructionKind::Jalr;
            if (!address_is_control) {
                emit_normal(address, instruction);
                emitted_addresses.insert(address);
            }
            emitted_delay_slots.insert(address);
            if (!address_is_control) {
                emit_label_jump(address + 4);
                continue;
            }
        }
        const MipsInstructionIr ir =
            build_mips_instruction_ir(address, instruction);
        const bool branch = ir.kind == MipsInstructionKind::Branch;
        const bool jump = ir.kind == MipsInstructionKind::Jump;
        const bool jr = ir.kind == MipsInstructionKind::Jr;
        const bool jalr = ir.kind == MipsInstructionKind::Jalr;
        if (branch || jump || jr || jalr) {
            const std::uint32_t delay_address = address + 4;
            if (delay_address >= load_address && delay_address < load_address + file_size) {
                const std::uint32_t delay_offset = delay_address - load_address;
                if (delay_offset + 4 <= file_size) {
                    if (emitted_addresses.insert(delay_address).second) {
                        emit_normal(delay_address,
                                    read_psx32(data, 2048 + delay_offset));
                    }
                    emitted_delay_slots.insert(delay_address);
                }
            }
        }
        if (branch) {
            const std::int32_t displacement =
                static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t branch_target =
                static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
            if (emittable_leaders.find(branch_target) ==
                emittable_leaders.end()) {
                source << "jmp unsupported_jump_exit\n";
            } else if (opcode == 0x11 && (rs == 8 || rs == 12)) {
                source << "movl fpcsr(%rip), %eax\n";
                source << "testl $1, %eax\n";
                if (rt == 0 || rt == 2) {
                    source << "jz p" << std::hex << branch_target << std::dec << "\n";
                } else {
                    source << "jnz p" << std::hex << branch_target << std::dec << "\n";
                }
            } else if (opcode == 0x04) {
                source << "movl " << rs * 4 << "(%r12), %eax\n";
                source << "cmpl " << rt * 4 << "(%r12), %eax\n";
                source << "je p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x05) {
                source << "movl " << rs * 4 << "(%r12), %eax\n";
                source << "cmpl " << rt * 4 << "(%r12), %eax\n";
                source << "jne p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x06) {
                source << "cmpl $0, " << rs * 4 << "(%r12)\njle p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x07) {
                source << "cmpl $0, " << rs * 4 << "(%r12)\njg p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x14) {
                source << "movl " << rs * 4 << "(%r12), %eax\n";
                source << "cmpl " << rt * 4 << "(%r12), %eax\n";
                source << "je p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x15) {
                source << "movl " << rs * 4 << "(%r12), %eax\n";
                source << "cmpl " << rt * 4 << "(%r12), %eax\n";
                source << "jne p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x16) {
                source << "cmpl $0, " << rs * 4 << "(%r12)\njle p" << std::hex << branch_target << std::dec << "\n";
            } else if (opcode == 0x17) {
                source << "cmpl $0, " << rs * 4 << "(%r12)\njg p" << std::hex << branch_target << std::dec << "\n";
            } else {
                source << "cmpl $0, " << rs * 4 << "(%r12)\n";
                if (rt == 0) {
                    source << "jl p" << std::hex << branch_target << std::dec << "\n";
                } else if (rt == 1) {
                    source << "jge p" << std::hex << branch_target << std::dec << "\n";
                } else if (rt == 16) {
                    source << "movl $" << address + 8 << ", " << 31 * 4 << "(%r12)\n";
                    source << "jl p" << std::hex << branch_target << std::dec << "\n";
                } else if (rt == 17) {
                    source << "movl $" << address + 8 << ", " << 31 * 4 << "(%r12)\n";
                    source << "jge p" << std::hex << branch_target << std::dec << "\n";
                } else {
                    emit_unsupported_stub(instruction);
                }
            }
            emit_label_jump(address + 8);
        } else if (jump) {
            const std::uint32_t jump_target =
                ((address + 4) & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2);
            if (opcode == 0x03) {
                source << "movl $" << address + 8 << ", " << 31 * 4 << "(%r12)\n";
                const Ps1FileCallPlan file_call =
                    map_ps1_file_call(jump_target);
                if (file_call.kind != Ps1FileCallKind::Unsupported) {
                    if (file_call.kind == Ps1FileCallKind::Open) {
                        source << "movl 16(%r12), %eax\n";
                        source << "cmpl $0x200000, %eax\n";
                        source << "jae memory_fault_exit\n";
                        source << "leaq ram(%rip), %rdi\n";
                        source << "addq %rax, %rdi\n";
                        source << "movl 20(%r12), %esi\n";
                        source << "movl 24(%r12), %edx\n";
                        source << "mov $" << file_call.linux_syscall << ", %eax\n";
                        source << "syscall\n";
                        source << "movl %eax, 8(%r12)\n";
                    } else if (file_call.kind == Ps1FileCallKind::Read) {
                        source << "movl 16(%r12), %edi\n";
                        source << "movl 20(%r12), %eax\n";
                        source << "cmpl $0x200000, %eax\n";
                        source << "jae memory_fault_exit\n";
                        source << "movl 24(%r12), %ecx\n";
                        source << "addl %ecx, %eax\n";
                        source << "jb memory_fault_exit\n";
                        source << "cmpl $0x200000, %eax\n";
                        source << "ja memory_fault_exit\n";
                        source << "movl 20(%r12), %eax\n";
                        source << "leaq ram(%rip), %rbx\n";
                        source << "addq %rax, %rbx\n";
                        source << "movq %rbx, %rsi\n";
                        source << "movl 24(%r12), %edx\n";
                        source << "mov $" << file_call.linux_syscall << ", %eax\n";
                        source << "syscall\n";
                        source << "movl %eax, 8(%r12)\n";
                    } else if (file_call.kind == Ps1FileCallKind::Write) {
                        source << "movl 16(%r12), %edi\n";
                        source << "movl 20(%r12), %eax\n";
                        source << "cmpl $0x200000, %eax\n";
                        source << "jae memory_fault_exit\n";
                        source << "movl 24(%r12), %ecx\n";
                        source << "addl %ecx, %eax\n";
                        source << "jb memory_fault_exit\n";
                        source << "cmpl $0x200000, %eax\n";
                        source << "ja memory_fault_exit\n";
                        source << "movl 20(%r12), %eax\n";
                        source << "leaq ram(%rip), %rbx\n";
                        source << "addq %rax, %rbx\n";
                        source << "movq %rbx, %rsi\n";
                        source << "movl 24(%r12), %edx\n";
                        source << "mov $" << file_call.linux_syscall << ", %eax\n";
                        source << "syscall\n";
                        source << "movl %eax, 8(%r12)\n";
                    } else if (file_call.kind == Ps1FileCallKind::Seek) {
                        source << "movl 16(%r12), %edi\n";
                        source << "movl 20(%r12), %esi\n";
                        source << "movl 24(%r12), %edx\n";
                        source << "mov $" << file_call.linux_syscall << ", %eax\n";
                        source << "syscall\n";
                        source << "movl %eax, 8(%r12)\n";
                    } else if (file_call.kind == Ps1FileCallKind::Close) {
                        source << "movl 16(%r12), %edi\n";
                        source << "mov $" << file_call.linux_syscall << ", %eax\n";
                        source << "syscall\n";
                        source << "movl %eax, 8(%r12)\n";
                    }
                } else if (emittable_leaders.find(jump_target) !=
                           emittable_leaders.end()) {
                    source << "call p" << std::hex << jump_target << std::dec << "\n";
                } else if (leaders.find(jump_target) != leaders.end()) {
                    source << "call p" << std::hex << jump_target << std::dec << "\n";
                } else {
                    const std::uint32_t target_offset =
                        jump_target >= load_address &&
                        jump_target < load_address + file_size &&
                        (jump_target & 3u) == 0u
                            ? jump_target - load_address
                            : file_size;
                    if (target_offset + 4u <= file_size) {
                        const std::uint32_t target_instruction =
                            read_psx32(data, PSX_HEADER_SIZE + target_offset);
                        if (build_mips_instruction_ir(
                                jump_target, target_instruction).supported) {
                            source << "call p" << std::hex << jump_target << std::dec << "\n";
                        } else {
                            source << "jmp unsupported_jump_exit\n";
                        }
                    } else {
                        source << "jmp unsupported_jump_exit\n";
                    }
                }
            } else {
                if (emittable_leaders.find(jump_target) !=
                    emittable_leaders.end()) {
                    source << "jmp p" << std::hex << jump_target << std::dec << "\n";
                } else {
                    source << "jmp unsupported_jump_exit\n";
                }
            }
        } else if (jr) {
            if (rs == 31) {
                source << "ret\n";
            } else {
                source << "movl " << rs * 4 << "(%r12), %eax\n";
                source << "jmp indirect_jump\n";
            }
        } else if (jalr) {
            source << "movl " << rs * 4 << "(%r12), %eax\n";
            source << "movl $" << address + 8 << ", " << rd * 4 << "(%r12)\n";
            const std::uint32_t resolved_target =
                resolve_static_indirect_target(
                    data,
                    load_address,
                    file_size,
                    address,
                    rs);
            if (emittable_leaders.find(resolved_target) !=
                emittable_leaders.end()) {
                source << "call p" << std::hex << resolved_target << std::dec << "\n";
                emit_label_jump(address + 8);
            } else {
                source << "call indirect_jump\n";
                emit_label_jump(address + 8);
            }
        } else {
            if (emitted_addresses.insert(address).second) {
                emit_normal(address, instruction);
            }
        }
    }

    source << "leaq diag_completed(%rip), %rsi\n";
    source << "mov $25, %rdx\n";
    source << "call print_diag\n";
    source << "xor %edi, %edi\nmov $60, %eax\nsyscall\n";
    if (!source) throw std::runtime_error("cannot create native lowering assembly");

    source.flush();
    source.close();
    std::vector<std::filesystem::path> assembly_chunks;
    split_leader_assembly(
        assembly,
        std::filesystem::path(options.output),
        assembly_chunks);
    assembly_chunks.insert(assembly_chunks.begin(), assembly);
    ElfWriter::write(assembly_chunks, options.output);
    bool keep_assembly = false;
#ifdef _WIN32
    char* keep_environment = nullptr;
    size_t keep_environment_length = 0;
    if (_dupenv_s(&keep_environment,
                  &keep_environment_length,
                  "ANYPS1_KEEP_ASSEMBLY") == 0 &&
        keep_environment != nullptr) {
        keep_assembly = true;
        std::free(keep_environment);
    }
#else
    keep_assembly = std::getenv("ANYPS1_KEEP_ASSEMBLY") != nullptr;
#endif
    if (!keep_assembly) {
        for (std::size_t index = 1u;
             index < assembly_chunks.size();
             ++index) {
            std::filesystem::remove(assembly_chunks[index]);
        }
    }
    std::filesystem::remove(payload);
}

}
