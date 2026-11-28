#include "MipsLowerer.h"

#include "MipsLinuxLowerer.h"
#include "MipsWindowsLowerer.h"
#include "relinker/Relinker.h"
#include "format/PsExe.h"
#include "lowering/MipsLoweringSupport.h"
#include "lowering/PayloadRegion.h"
#include "lowering/UnsupportedInstructions.h"
#include "host/HostServices.h"

#include <memory>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

std::unique_ptr<MipsLowerer> create_mips_lowerer(TargetPlatform target)
{
    if (target == TargetPlatform::Linux) {
        return std::make_unique<MipsLinuxLowerer>();
    }
    if (target == TargetPlatform::Windows) {
        return std::make_unique<MipsWindowsLowerer>();
    }
    throw std::invalid_argument("unsupported native MIPS target platform");
}

MipsExecutablePlan build_mips_executable_plan(
    const RelinkOptions&,
    const std::vector<unsigned char>& data)
{
    MipsExecutablePlan plan;
    plan.file_size = read_psx32(data, 0x1C);
    plan.load_address = read_psx32(data, 0x18);
    plan.initial_pc = read_psx32(data, 0x10);
    plan.initial_gp = read_psx32(data, 0x14);
    const std::uint32_t initial_sp_base = read_psx32(data, 0x30);
    const std::uint32_t initial_sp_offset = read_psx32(data, 0x34);
    plan.initial_sp =
        initial_sp_base == 0 && initial_sp_offset == 0
            ? 0x801FF000u
            : initial_sp_base + initial_sp_offset;
    if (plan.file_size == 0 || plan.file_size % 4 != 0) {
        throw std::runtime_error(
            "unsupported native lowering: executable size is not a MIPS word sequence");
    }
    if (plan.initial_pc < plan.load_address ||
        plan.initial_pc >= plan.load_address + plan.file_size) {
        throw std::runtime_error(
            "unsupported native lowering: entry point outside executable payload");
    }
    plan.payload_ram_offset = plan.load_address & 0x1FFFFFu;
    if (plan.payload_ram_offset > 0x200000u ||
        plan.file_size > 0x200000u - plan.payload_ram_offset) {
        throw std::runtime_error(
            "unsupported native lowering: executable payload exceeds PS1 RAM");
    }

    std::set<std::uint32_t> processed;
    std::queue<std::uint32_t> work;
    plan.leaders.insert(plan.initial_pc);
    work.push(plan.initial_pc);
    for (std::uint32_t target : discover_payload_pointer_targets(
             data, plan.load_address, plan.file_size)) {
        plan.leaders.insert(target);
        work.push(target);
    }

    while (!work.empty()) {
        const std::uint32_t address = work.front();
        work.pop();
        if (!processed.insert(address).second) continue;
        if (address < plan.load_address ||
            address >= plan.load_address + plan.file_size ||
            (address & 3u) != 0) {
            continue;
        }
        const std::uint32_t offset = address - plan.load_address;
        if (offset + 4 > plan.file_size) continue;
        const std::uint32_t instruction =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        const MipsInstructionIr ir =
            build_mips_instruction_ir(address, instruction);
        if (classify_payload_region(address, instruction, true) ==
            PayloadRegionKind::Unknown) {
            continue;
        }
        if (ir.kind == MipsInstructionKind::Load ||
            ir.kind == MipsInstructionKind::Store ||
            ir.kind == MipsInstructionKind::CoprocessorLoad ||
            ir.kind == MipsInstructionKind::CoprocessorStore) {
            plan.native_host_loop_required = true;
        }
        const bool branch = ir.kind == MipsInstructionKind::Branch;
        const bool jump = ir.kind == MipsInstructionKind::Jump;
        const bool jr = ir.kind == MipsInstructionKind::Jr;
        const bool jalr = ir.kind == MipsInstructionKind::Jalr;
        const bool stop = ir.kind == MipsInstructionKind::System;

        if (branch) {
            const std::uint32_t target = ir.target;
            plan.leaders.insert(target);
            plan.leaders.insert(address + 8u);
            work.push(target);
            work.push(address + 8u);
        } else if (jump) {
            const std::uint32_t target = ir.target;
            if (ir.opcode == 0x03u) {
                ++plan.call_edges;
                if (target < plan.load_address ||
                    target >= plan.load_address + plan.file_size) {
                    ++plan.unresolved_indirect_transfers;
                }
            }
            if ((target & 0xFF000000u) == 0xA0000000u ||
                (target & 0xFF000000u) == 0xB0000000u ||
                (target & 0xFF000000u) == 0xC0000000u) {
                plan.leaders.insert(address + 8u);
                work.push(address + 8u);
            } else {
                plan.leaders.insert(target);
                work.push(target);
            }
            if (ir.opcode == 0x03u) {
                plan.leaders.insert(address + 8u);
                work.push(address + 8u);
            }
        } else if (!jr && !jalr && !stop) {
            plan.leaders.insert(address + 4u);
            work.push(address + 4u);
        }

        if (jr || jalr) {
            if (jalr) {
                ++plan.call_edges;
                plan.leaders.insert(address + 8u);
                work.push(address + 8u);
            }
            const std::uint32_t resolved_target =
                resolve_static_indirect_target(
                    data, plan.load_address, plan.file_size, address, ir.rs);
            if (resolved_target >= plan.load_address &&
                resolved_target < plan.load_address + plan.file_size &&
                (resolved_target & 3u) == 0u) {
                plan.leaders.insert(resolved_target);
                work.push(resolved_target);
            } else {
                const Ps1BiosCallPlan bios_call = map_ps1_bios_call(
                    resolved_target,
                    resolve_static_indirect_target(
                        data, plan.load_address, plan.file_size,
                        address + 4u, 9u));
                const std::uint32_t return_target =
                    bios_call.kind == Ps1BiosCallKind::Unsupported
                        ? 0u
                        : resolve_static_indirect_target(
                            data, plan.load_address, plan.file_size,
                            address, 31u);
                if (return_target >= plan.load_address &&
                    return_target < plan.load_address + plan.file_size &&
                    (return_target & 3u) == 0u) {
                    plan.leaders.insert(return_target);
                    work.push(return_target);
                } else {
                    const std::vector<std::uint32_t> table_targets =
                        discover_indirect_table_targets(
                            data, plan.load_address, plan.file_size,
                            address, ir.rs);
                    if (table_targets.empty()) {
                        ++plan.unresolved_indirect_transfers;
                    } else {
                        for (std::uint32_t target : table_targets) {
                            plan.leaders.insert(target);
                            work.push(target);
                        }
                    }
                }
            }
        }
        if (branch || jump || jr || jalr) {
            const std::uint32_t delay_address = address + 4u;
            if (delay_address >= plan.load_address &&
                delay_address < plan.load_address + plan.file_size) {
                plan.delay_slots.insert(delay_address);
                plan.leaders.insert(delay_address);
            }
        }
    }

    for (std::uint32_t address : plan.leaders) {
        if (address < plan.load_address ||
            address >= plan.load_address + plan.file_size) continue;
        const std::uint32_t offset = address - plan.load_address;
        if (offset + 4u > plan.file_size) continue;
        const std::uint32_t instruction =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        const PayloadRegionKind region =
            classify_payload_region(address, instruction, true);
        if (region == PayloadRegionKind::EmbeddedData) {
            ++plan.embedded_data_count;
            continue;
        }
        if (region == PayloadRegionKind::Instruction ||
            is_supported_mips_instruction(instruction)) {
            plan.emittable_leaders.insert(address);
        }
        if (region == PayloadRegionKind::Unknown) {
            ++plan.unknown_region_count;
            continue;
        }
        ++plan.reachable_count;
        if (is_supported_mips_instruction(instruction)) {
            ++plan.supported_count;
        } else {
            ++plan.unsupported_count;
            const UnsupportedInstruction unsupported =
                classify_unsupported_instruction(instruction);
            plan.unsupported_family_names.insert(unsupported.name);
            if (plan.first_unsupported_address == 0u) {
                plan.first_unsupported_address = address;
                plan.first_unsupported_word = instruction;
                plan.first_unsupported_name = unsupported.name;
            }
        }
    }
    return plan;
}

}
