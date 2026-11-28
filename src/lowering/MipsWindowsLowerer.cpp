#include "MipsWindowsLowerer.h"
#include "MipsLowerer.h"
#include "executable/PeWriter.h"
#include "format/PsExe.h"
#include "lowering/MipsLoweringSupport.h"
#include "lowering/NativeLoweringIr.h"
#include "lowering/UnsupportedInstructions.h"
#include "relinker/Relinker.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

void MipsWindowsLowerer::lower(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data)
{
    const MipsExecutablePlan plan = build_mips_executable_plan(options, data);
    const std::uint64_t estimated_source_bytes =
        static_cast<std::uint64_t>(plan.emittable_leaders.size()) *
        512u;
    if (estimated_source_bytes > 8u * 1024u * 1024u) {
        throw std::runtime_error(
            "unsupported native lowering: executable CFG exceeds "
            "the 8 MiB target-source emission limit");
    }
    const std::uint32_t file_size = plan.file_size;
    const std::uint32_t load_address = plan.load_address;
    const std::uint32_t initial_pc = plan.initial_pc;
    const std::uint32_t initial_gp = plan.initial_gp;
    const std::uint32_t unsupported_count = plan.unsupported_count;
    const std::uint32_t first_address = plan.first_unsupported_address;
    const std::uint32_t first_word = plan.first_unsupported_word;
    const std::string& first_name = plan.first_unsupported_name;

    std::ostringstream diagnostics;
    diagnostics
        << "AnyPS1 native executable diagnostics\n"
        << "target=windows\n"
        << "load_address=0x" << std::hex << load_address << "\n"
        << "entry_point=0x" << std::hex << initial_pc << "\n"
        << "initial_gp=0x" << std::hex << initial_gp << "\n"
        << "payload_size=" << std::dec << file_size << "\n"
        << "reachable_words=" << plan.reachable_count << "\n"
        << "supported_reachable_words=" << plan.supported_count << "\n"
        << "unsupported_instructions=" << unsupported_count << "\n";
    if (unsupported_count != 0) {
        diagnostics
            << "first_unsupported_gap=0x" << std::hex << first_address
            << " instruction=0x" << std::hex << first_word
            << " name=" << first_name << "\n";
    } else {
        diagnostics << "first_unsupported_gap=none\n";
    }
    if (unsupported_count == 0) {
        if (plan.native_host_loop_required) {
            diagnostics
                << "next_runtime_blocker=PS1 MMIO/host-device state progression\n";
        } else if (plan.supported_count != 0) {
            diagnostics
                << "next_runtime_blocker=Windows MIPS execution loop completion\n";
        }
    }
    std::ostringstream source;
    source << "#include <stddef.h>\n";
    source << "#include <stdint.h>\n";
    source << "#ifdef _WIN32\n";
    source << "extern \"C\" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);\n";
    source << "extern \"C\" __declspec(dllimport) int __stdcall WriteFile(void*, const void*, unsigned long, unsigned long*, void*);\n";
    source << "extern \"C\" __declspec(dllimport) void __stdcall ExitProcess(unsigned int);\n";
    source << "#endif\n";
    source << "extern \"C\" void* memcpy(void* destination, const void* source, size_t size) {\n";
    source << "unsigned char* dst=(unsigned char*)destination;\n";
    source << "const unsigned char* src=(const unsigned char*)source;\n";
    source << "for(size_t i=0;i<size;++i) dst[i]=src[i];\n";
    source << "return destination;\n}\n";
    source << "extern \"C\" void* memset(void* destination, int value, size_t size) {\n";
    source << "unsigned char* dst=(unsigned char*)destination;\n";
    source << "for(size_t i=0;i<size;++i) dst[i]=(unsigned char)value;\n";
    source << "return destination;\n}\n";
    source << "static const char diagnostics[] = {";
    for (unsigned char byte : diagnostics.str()) {
        source << static_cast<unsigned int>(byte) << ",";
    }
    source << "0};\n";
    source << "static uint32_t regs[32];\n";
    source << "static uint32_t ram[524288];\n";
    source << "static uint32_t scratchpad[256];\n";
    source << "static uint32_t hi_value;\n";
    source << "static uint32_t lo_value;\n";
    source << "static uint32_t payload[] = {";
    bool payload_first = true;
    for (std::uint32_t offset = 0; offset < plan.file_size; offset += 4u) {
        if (!payload_first) source << ",";
        payload_first = false;
        const std::uint32_t word =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        source << "0x" << std::hex << std::setfill('0') << std::setw(8)
               << word << std::dec;
    }
    source << "};\n";
    source << "int main(void) {\n";
    source << "#ifdef _WIN32\n";
    source << "void* handle = GetStdHandle(0xfffffff5u);\n";
    source << "unsigned long written = 0;\n";
    source << "WriteFile(handle, diagnostics, sizeof(diagnostics) - 1, &written, 0);\n";
    source << "#endif\n";
    source << "regs[29]=0x801FF000u;\n";
    source << "regs[28]=0x" << std::hex << initial_gp << std::dec << "u;\n";
    source << "for(uint32_t i=0;i<sizeof(payload)/sizeof(payload[0]);++i) "
               "ram[" << (plan.payload_ram_offset / 4u)
            << "+i]=payload[i];\n";
    source << "uint32_t pc=0x" << std::hex << initial_pc << std::dec << "u;\n";
    source << "uint32_t next_pc=pc+4u;\n";
    source << "uint32_t pending_target=0;\n";
    source << "uint32_t delay_remaining=0;\n";
    source << "for(uint32_t steps=0;steps<10000000u;++steps){\n";
    source << "if(delay_remaining==1u){pc=pending_target;delay_remaining=0u;}\n";
    source << "else if(delay_remaining==2u){pc=next_pc;delay_remaining=1u;}\n";
    source << "bool dispatched=false;\n";

    for (const auto& bucket_entry : plan.leader_dispatch) {
        source << "if(!dispatched && (pc>>8u)==0x"
               << std::hex << bucket_entry.first << std::dec << "u){\n";
        source << "dispatched=true;\n";
        source << "switch(pc){\n";
        for (std::uint32_t address : bucket_entry.second) {
        if (address < plan.load_address ||
            address >= plan.load_address + plan.file_size ||
            (address & 3u) != 0u) continue;
        const std::uint32_t offset = address - plan.load_address;
        if (offset + 4u > plan.file_size) continue;
        const std::uint32_t word =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        const MipsInstructionIr ir =
            build_mips_instruction_ir(address, word);
        if (!is_supported_mips_instruction(word)) {
            source << "case 0x" << std::hex << std::setfill('0')
                   << std::setw(8) << address << std::dec << "u:{"
                   << "ExitProcess(16u);break;}\n";
            continue;
        }
        source << "case 0x" << std::hex << std::setfill('0') << std::setw(8)
               << address << std::dec << "u:{\n";
        source << "next_pc=pc+4u;\n";
        switch (ir.kind) {
        case MipsInstructionKind::Nop:
            break;
        case MipsInstructionKind::Shift:
            if (ir.special == 0x00u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rt
                       << "]<<" << ir.shamt << "u;\n";
            else if (ir.special == 0x02u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rt
                       << "]>>" << ir.shamt << "u;\n";
            else if (ir.special == 0x03u)
                source << "regs[" << ir.rd << "]=(uint32_t)((int32_t)regs[" << ir.rt
                       << "]>>" << ir.shamt << "u);\n";
            else if (ir.special == 0x04u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rt
                       << "]<<(regs[" << ir.rs << "]&31u);\n";
            else if (ir.special == 0x06u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rt
                       << "]>>(regs[" << ir.rs << "]&31u);\n";
            else if (ir.special == 0x07u)
                source << "regs[" << ir.rd << "]=(uint32_t)((int32_t)regs[" << ir.rt
                       << "]>>(regs[" << ir.rs << "]&31u));\n";
            break;
        case MipsInstructionKind::Arithmetic:
            if (ir.special == 0x21u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]+regs[" << ir.rt << "];\n";
            else if (ir.special == 0x23u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]-regs[" << ir.rt << "];\n";
            else if (ir.special == 0x20u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]+regs[" << ir.rt << "];\n";
            else if (ir.special == 0x22u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]-regs[" << ir.rt << "];\n";
            break;
        case MipsInstructionKind::Logical:
            if (ir.special == 0x24u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]&regs[" << ir.rt << "];\n";
            else if (ir.special == 0x25u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]|regs[" << ir.rt << "];\n";
            else if (ir.special == 0x26u)
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]^regs[" << ir.rt << "];\n";
            else if (ir.special == 0x27u)
                source << "regs[" << ir.rd << "] =~(regs[" << ir.rs
                       << "]|regs[" << ir.rt << "]);\n";
            break;
        case MipsInstructionKind::Compare:
            if (ir.special == 0x2Au)
                source << "regs[" << ir.rd << "]=(int32_t)regs[" << ir.rs
                       << "]<(int32_t)regs[" << ir.rt << "]?1u:0u;\n";
            else
                source << "regs[" << ir.rd << "]=regs[" << ir.rs
                       << "]<regs[" << ir.rt << "]?1u:0u;\n";
            break;
        case MipsInstructionKind::Multiply:
            if (ir.special == 0x18u)
                source << "{int64_t v=(int64_t)(int32_t)regs[" << ir.rs
                       << "]*(int64_t)(int32_t)regs[" << ir.rt
                       << "];lo_value=(uint32_t)v;hi_value=(uint32_t)((uint64_t)v>>32u);}\n";
            else
                source << "{uint64_t v=(uint64_t)regs[" << ir.rs
                       << "]*(uint64_t)regs[" << ir.rt
                       << "];lo_value=(uint32_t)v;hi_value=(uint32_t)(v>>32u);}\n";
            break;
        case MipsInstructionKind::Divide:
            source << "if(regs[" << ir.rt << "]!=0){";
            if (ir.special == 0x1Au)
                source << "lo_value=(uint32_t)((int32_t)regs[" << ir.rs
                       << "]/(int32_t)regs[" << ir.rt
                       << "]);hi_value=(uint32_t)((int32_t)regs[" << ir.rs
                       << "]%(int32_t)regs[" << ir.rt << "]);";
            else
                source << "lo_value=regs[" << ir.rs << "]/regs[" << ir.rt
                       << "];hi_value=regs[" << ir.rs << "]%regs[" << ir.rt << "];";
            source << "}\n";
            break;
        case MipsInstructionKind::HiLo:
            if (ir.special == 0x10u)
                source << "regs[" << ir.rd << "]=hi_value;\n";
            else if (ir.special == 0x11u)
                source << "hi_value=regs[" << ir.rs << "];\n";
            else if (ir.special == 0x12u)
                source << "regs[" << ir.rd << "]=lo_value;\n";
            else if (ir.special == 0x13u)
                source << "lo_value=regs[" << ir.rs << "];\n";
            break;
        case MipsInstructionKind::Immediate:
            if (ir.opcode == 0x08u)
                source << "regs[" << ir.rt << "]=regs[" << ir.rs
                       << "]+0x" << std::hex << ir.immediate << std::dec << "u;\n";
            else if (ir.opcode == 0x09u)
                source << "regs[" << ir.rt << "]=regs[" << ir.rs
                       << "]+0x" << std::hex << ir.immediate << std::dec << "u;\n";
            else if (ir.opcode == 0x0Au)
                source << "regs[" << ir.rt << "]=(int32_t)regs[" << ir.rs
                       << "]<(int32_t)0x" << std::hex
                       << ir.immediate << std::dec << "u?1u:0u;\n";
            else if (ir.opcode == 0x0Bu)
                source << "regs[" << ir.rt << "]=regs[" << ir.rs
                       << "]<0x" << std::hex << ir.immediate << std::dec << "u?1u:0u;\n";
            else if (ir.opcode == 0x0Cu)
                source << "regs[" << ir.rt << "]=regs[" << ir.rs
                       << "]&0x" << std::hex << ir.immediate << std::dec << "u;\n";
            else if (ir.opcode == 0x0Du)
                source << "regs[" << ir.rt << "]=regs[" << ir.rs
                       << "]|0x" << std::hex << ir.immediate << std::dec << "u;\n";
            else if (ir.opcode == 0x0Eu)
                source << "regs[" << ir.rt << "]=regs[" << ir.rs
                       << "]^0x" << std::hex << ir.immediate << std::dec << "u;\n";
            else if (ir.opcode == 0x0Fu) {
                source << "regs[" << ir.rt << "]=";
                source << "0x" << std::hex << (ir.immediate << 16u)
                       << std::dec << "u;\n";
            }
            break;
        case MipsInstructionKind::Branch:
            source << "{uint32_t take=0u;";
            if (ir.opcode == 0x04u)
                source << "take=regs[" << ir.rs << "]==regs[" << ir.rt << "]?1u:0u;";
            else if (ir.opcode == 0x05u)
                source << "take=regs[" << ir.rs << "]!=regs[" << ir.rt << "]?1u:0u;";
            else if (ir.opcode == 0x06u)
                source << "take=(int32_t)regs[" << ir.rs << "]<=0?1u:0u;";
            else if (ir.opcode == 0x07u)
                source << "take=(int32_t)regs[" << ir.rs << "]>0?1u:0u;";
            else
                source << "take=0u;";
            source << "if(take){pending_target=0x" << std::hex << ir.target
                   << std::dec << "u;delay_remaining=2u;}}\n";
            break;
        case MipsInstructionKind::Jump:
            source << "pending_target=0x" << std::hex << ir.target << std::dec << "u;";
            if (ir.opcode == 0x03u)
                source << "regs[31]=pc+8u;";
            source << "delay_remaining=2u;\n";
            break;
        case MipsInstructionKind::Jr:
            source << "pending_target=regs[" << ir.rs
                   << "];delay_remaining=2u;\n";
            break;
        case MipsInstructionKind::Jalr:
            source << "regs[" << ir.rd << "]=pc+8u;"
                   << "pending_target=regs[" << ir.rs
                   << "];delay_remaining=2u;\n";
            break;
        case MipsInstructionKind::Load:
        case MipsInstructionKind::Store:
            source << "{uint32_t ea=(regs[" << ir.rs << "]+"
                   << static_cast<std::int32_t>(
                          static_cast<std::int16_t>(ir.immediate))
                   << "u)&0x1FFFFFFFu;";
            if (ir.kind == MipsInstructionKind::Load) {
                source << "if(ea<0x00200000u){";
                if (ir.size == 1u)
                    source << "regs[" << ir.rt << "]=(uint32_t)*((uint8_t*)ram+ea);";
                else if (ir.size == 2u)
                    source << "regs[" << ir.rt << "]=(uint32_t)*((uint16_t*)((uint8_t*)ram+ea));";
                else
                    source << "regs[" << ir.rt << "]=ram[ea>>2u];";
                source << "}else if(ea>=0x1F800000u&&ea<0x1F800400u){"
                           "regs[" << ir.rt << "]=scratchpad[(ea&0x3FFu)>>2u];"
                           "}else{ExitProcess(17u);}}\n";
            } else {
                source << "if(ea<0x00200000u){";
                if (ir.size == 1u)
                    source << "*((uint8_t*)ram+ea)=(uint8_t)regs[" << ir.rt << "];";
                else if (ir.size == 2u)
                    source << "*((uint16_t*)((uint8_t*)ram+ea))=(uint16_t)regs[" << ir.rt << "];";
                else
                    source << "ram[ea>>2u]=regs[" << ir.rt << "];";
                source << "}else if(ea>=0x1F800000u&&ea<0x1F800400u){"
                           "scratchpad[(ea&0x3FFu)>>2u]=regs[" << ir.rt << "];"
                           "}else{ExitProcess(17u);}}\n";
            }
            break;
        case MipsInstructionKind::System:
            if (ir.special == 0x0Cu)
                source << "ExitProcess(0u);\n";
            else
                source << "ExitProcess(9u);\n";
            break;
        default:
            source << "ExitProcess(16u);\n";
            break;
        }
        source << "regs[0]=0u;\n";
        source << "break;}\n";
    }
        source << "default:ExitProcess(17u);}\n";
        source << "}\n";
    }
    source << "if(!dispatched)ExitProcess(17u);\n";
    source << "pc=next_pc;\n";
    source << "}\n";
    source << "ExitProcess(17u);\n";
    source << "return 17;\n";
    source << "}\n";

    const std::uint32_t exit_status =
        unsupported_count != 0 ? 16 :
        plan.supported_count != 0 ? 17 : 17;

    NativeLoweringIr ir;
    ir.target = TargetPlatform::Windows;
    ir.entry_symbol = "main";
    ir.source = source.str();
    ir.diagnostics = diagnostics.str();
    ir.exit_status = exit_status;
    write_pe_ir(options, ir);
}

}
