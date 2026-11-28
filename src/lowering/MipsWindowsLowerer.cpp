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
    if (plan.leader_chunks.size() > 512u) {
        throw std::runtime_error(
            "unsupported native Windows lowering: executable requires "
            "more than 512 emission chunks");
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
    source << "uint32_t regs[32];\n";
    source << "uint32_t ram[524288];\n";
    source << "uint32_t scratchpad[256];\n";
    source << "uint32_t hi_value;\n";
    source << "uint32_t lo_value;\n";
    source << "uint32_t mmio[2048];\n";
    source << "uint32_t timer_cycles;\n";
    source << "uint32_t frame_cycles;\n";
    source << "uint32_t interrupt_status;\n";
    source << "uint32_t interrupt_mask;\n";
    source << "uint32_t native_frame_counter;\n";
    source << "uint32_t gpu_status_native;\n";
    source << "uint32_t spu_native[384];\n";
    source << "uint32_t cd_parameters[16];\n";
    source << "uint32_t cd_responses[16];\n";
    source << "uint32_t cd_parameter_count;\n";
    source << "uint32_t cd_response_count;\n";
    source << "uint32_t cd_response_read;\n";
    source << "uint32_t cd_status_index;\n";
    source << "uint32_t cd_irq;\n";
    source << "uint32_t controller_rx[8];\n";
    source << "uint32_t controller_rx_read;\n";
    source << "uint32_t controller_rx_count;\n";
    source << "uint32_t pc;\n";
    source << "uint32_t next_pc;\n";
    source << "uint32_t pending_target;\n";
    source << "uint32_t delay_remaining;\n";
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
    source << "static uint32_t native_mmio_read(uint32_t ea){\n";
    source << "uint32_t n=ea&0x1FFFFFFFu;if(n>=0x1F801100u&&n<0x1F801130u){uint32_t base=n&~15u;uint32_t off=n-base;if(off==0u){uint32_t timer=(base-0x1F801100u)/16u;return timer_cycles+timer*0x400u;}return mmio[(n-0x1F801000u)>>2u];}if(n==0x1F801070u)return interrupt_status;if(n==0x1F801074u)return interrupt_mask;if(n==0x1F801040u){uint32_t value=controller_rx_read<controller_rx_count?controller_rx[controller_rx_read++]:0xFFu;return value;}if(n==0x1F801044u)return controller_rx_read<controller_rx_count?0x02u:0u;if(n==0x1F801810u||n==0x1F801814u)return gpu_status_native;if(n>=0x1F801800u&&n<0x1F801804u){if(n==0x1F801800u)return cd_status_index|(cd_parameter_count<16u?0x08u:0u)|(cd_response_read<cd_response_count?0x20u:0u)|0x02u;if(n==0x1F801801u&&cd_status_index==1u)return cd_response_read<cd_response_count?cd_responses[cd_response_read++]:0u;if(n==0x1F801803u)return cd_irq|0x10u;return 0u;}if(n>=0x1F801C00u&&n<0x1F801D80u)return spu_native[(n-0x1F801C00u)>>2u];if(n>=0x1F801000u&&n<0x1F802000u)return mmio[(n-0x1F801000u)>>2u];return 0u;}\n";
    source << "static void native_dma_write(uint32_t n,uint32_t value){\n";
    source << "uint32_t channel=(n-0x1F801080u)/16u;if(channel>6u)return;uint32_t control=(n&15u)==8u?value:mmio[((n-0x1F801000u)>>2u)];if((n&15u)==8u&&((value&0x01000000u)==0u))return;if((n&15u)==8u&&((value&0x01000000u)!=0u)){uint32_t base=0x1F801080u+channel*16u;uint32_t address=mmio[(base-0x1F801000u)>>2u]&0x00FFFFFFu;uint32_t sync=(value>>9u)&3u;if(sync==2u){uint32_t limit=65536u;while(limit--!=0u&&address<524288u){uint32_t header=ram[address];uint32_t next=header&0x00FFFFFFu;uint32_t size=header>>24u;if(address+1u+size>524288u)break;for(uint32_t index=0u;index<size;++index){uint32_t packet=ram[address+1u+index];uint32_t command=packet>>24u;if(command>=0x20u&&command<=0x7Fu)gpu_status_native|=1u<<26;}if(next==0x00FFFFFFu||next>=524288u)break;address=next;}mmio[(base-0x1F801000u)>>2u]=address;}else{uint32_t block=mmio[((base+4u)-0x1F801000u)>>2u];uint32_t words=block&0xFFFFu;if(words==0u)words=1u;uint32_t blocks=(block>>16u)&0xFFFFu;if(blocks==0u)blocks=1u;uint64_t count=(uint64_t)words*blocks;if(count<=524288u-address/4u){for(uint32_t index=0u;index<count;++index)ram[address/4u+index]=0u;}}mmio[((base+8u)-0x1F801000u)>>2u]=value&~0x01000000u;interrupt_status|=0x8u;}}\n";
    source << "static void native_mmio_write(uint32_t ea,uint32_t value){\n";
    source << "uint32_t n=ea&0x1FFFFFFFu;if(n==0x1F801070u)interrupt_status &= ~value;if(n==0x1F801074u)interrupt_mask=value;if(n==0x1F801040u){controller_rx_read=0u;if(value==0x81u){controller_rx_count=5u;controller_rx[0]=0xFFu;controller_rx[1]=0xFFu;controller_rx[2]=0x5Au;controller_rx[3]=0x5Du;controller_rx[4]=0u;}else{controller_rx_count=5u;controller_rx[0]=0u;controller_rx[1]=0x41u;controller_rx[2]=0x5Au;controller_rx[3]=(uint8_t)value;controller_rx[4]=(uint8_t)(value>>8u);}}if(n>=0x1F801C00u&&n<0x1F801D80u)spu_native[(n-0x1F801C00u)>>2u]=value;if(n==0x1F801810u){uint32_t command=value>>24u;if(command>=0x20u&&command<=0x7Fu)gpu_status_native|=1u<<26u;}if(n==0x1F801814u){if((value>>24u)==0u)gpu_status_native=0x14802000u;if((value>>24u)==0x02u)interrupt_status &= ~0x1u;}if(n>=0x1F801800u&&n<0x1F801804u){if(n==0x1F801800u)cd_status_index=value&3u;else if(n==0x1F801801u){cd_response_read=0u;cd_response_count=1u;if(value==0x01u||value==0x02u||value==0x06u||value==0x08u){cd_responses[0]=0x02u;cd_irq=value==0x06u?1u:3u;}else{cd_responses[0]=0x11u;cd_irq=5u;}interrupt_status|=0x4u;}else if(n==0x1F801802u&&cd_status_index==2u&&cd_parameter_count<16u){cd_parameters[cd_parameter_count++]=value&0xFFu;}else if(n==0x1F801803u){cd_irq=0u;if((value&0x40u)!=0u)cd_parameter_count=0u;}}if(n>=0x1F801080u&&n<0x1F801100u)native_dma_write(n,value);if(n>=0x1F801000u&&n<0x1F802000u)mmio[(n-0x1F801000u)>>2u]=value;}\n";
    source << "static void write_windows_trace(const char* prefix,uint32_t value){\n";
    source << "char text[48];uint32_t position=0;while(prefix[position]){text[position]=prefix[position];++position;}text[position++]='0';text[position++]='x';for(int shift=28;shift>=0;shift-=4){uint32_t digit=(value>>shift)&15u;text[position++]=(char)(digit<10u?'0'+digit:'a'+digit-10u);}text[position++]='\\n';void* handle=GetStdHandle(0xfffffff5u);unsigned long written=0;WriteFile(handle,text,position,&written,0);}\n";
    source << "extern \"C\" void native_execute_ir(uint32_t kind,uint32_t address,uint32_t word,uint32_t rs,uint32_t rt,uint32_t rd,uint32_t shamt,uint32_t immediate,uint32_t target,uint32_t size_value,uint32_t special){\n";
    source << "switch(kind){\n";
    source << "case 1:return;\n";
    source << "case 2:\n";
    source << "if(special==0)regs[rd]=regs[rt]<<shamt;\n";
    source << "else if(special==2)regs[rd]=regs[rt]>>shamt;\n";
    source << "else if(special==3)regs[rd]=(uint32_t)((int32_t)regs[rt]>>shamt);\n";
    source << "else if(special==4)regs[rd]=regs[rt]<<(regs[rs]&31u);\n";
    source << "else if(special==6)regs[rd]=regs[rt]>>(regs[rs]&31u);\n";
    source << "else regs[rd]=(uint32_t)((int32_t)regs[rt]>>(regs[rs]&31u));\n";
    source << "return;\n";
    source << "case 3:\n";
    source << "if(special==33||special==32)regs[rd]=regs[rs]+regs[rt];\n";
    source << "else regs[rd]=regs[rs]-regs[rt];\n";
    source << "return;\n";
    source << "case 4:\n";
    source << "if(special==36)regs[rd]=regs[rs]&regs[rt];\n";
    source << "else if(special==37)regs[rd]=regs[rs]|regs[rt];\n";
    source << "else if(special==38)regs[rd]=regs[rs]^regs[rt];\n";
    source << "else regs[rd]=~(regs[rs]|regs[rt]);\n";
    source << "return;\n";
    source << "case 5:\n";
    source << "if(special==42)regs[rd]=(int32_t)regs[rs]<(int32_t)regs[rt]?1u:0u;\n";
    source << "else regs[rd]=regs[rs]<regs[rt]?1u:0u;\n";
    source << "return;\n";
    source << "case 6:\n";
    source << "{uint64_t value=special==24?(uint64_t)(int64_t)((int32_t)regs[rs]*(int32_t)regs[rt]):(uint64_t)regs[rs]*(uint64_t)regs[rt];lo_value=(uint32_t)value;hi_value=(uint32_t)(value>>32u);}\n";
    source << "return;\n";
    source << "case 7:\n";
    source << "if(regs[rt]!=0){if(special==26){lo_value=(uint32_t)((int32_t)regs[rs]/(int32_t)regs[rt]);hi_value=(uint32_t)((int32_t)regs[rs]%(int32_t)regs[rt]);}else{lo_value=regs[rs]/regs[rt];hi_value=regs[rs]%regs[rt];}}\n";
    source << "return;\n";
    source << "case 8:\n";
    source << "if(special==16)regs[rd]=hi_value;else if(special==17)hi_value=regs[rs];else if(special==18)regs[rd]=lo_value;else lo_value=regs[rs];\n";
    source << "return;\n";
    source << "case 13:\n";
    source << "if((word>>26)==8||((word>>26)==9))regs[rt]=regs[rs]+(uint32_t)(int32_t)(int16_t)immediate;\n";
    source << "else if((word>>26)==10)regs[rt]=(int32_t)regs[rs]<(int32_t)(int16_t)immediate?1u:0u;\n";
    source << "else if((word>>26)==11)regs[rt]=regs[rs]<((uint32_t)(uint16_t)immediate)?1u:0u;\n";
    source << "else if((word>>26)==12)regs[rt]=regs[rs]&immediate;\n";
    source << "else if((word>>26)==13)regs[rt]=regs[rs]|immediate;\n";
    source << "else if((word>>26)==14)regs[rt]=regs[rs]^immediate;\n";
    source << "else if((word>>26)==15)regs[rt]=immediate<<16;\n";
    source << "return;\n";
    source << "case 14:\n";
    source << "{uint32_t take=0;if((word>>26)==4)take=regs[rs]==regs[rt];else if((word>>26)==5)take=regs[rs]!=regs[rt];else if((word>>26)==6)take=(int32_t)regs[rs]<=0;else if((word>>26)==7)take=(int32_t)regs[rs]>0;if(take){pending_target=target;delay_remaining=2u;}}\n";
    source << "return;\n";
    source << "case 15:\n";
    source << "pending_target=target;if((word>>26)==3)regs[31]=address+8u;delay_remaining=2u;\n";
    source << "return;\n";
    source << "case 16:\n";
    source << "{uint32_t target=regs[rs];if(target==0xA0u||target==0xB0u||target==0xC0u||((target&0xFF000000u)==0xA0000000u)||((target&0xFF000000u)==0xB0000000u)||((target&0xFF000000u)==0xC0000000u)){write_windows_trace(\"bios_vector=\",target);write_windows_trace(\"bios_function=\",regs[9]);if(target==0xA0u){if(regs[9]==0x39u)regs[2]=0x80100000u;else if(regs[9]==0x06u)ExitProcess(0u);else if(regs[9]==0x03u)regs[2]=regs[6];else regs[2]=0xffffffffu;}else if(target==0xB0u){if(regs[9]!=0x17u){write_windows_trace(\"unsupported_b0_function=\",regs[9]);ExitProcess(17u);}}else if(target==0xC0u){if(regs[9]==0x10u||regs[9]==0x13u)regs[2]=0u;else{write_windows_trace(\"unsupported_c0_function=\",regs[9]);ExitProcess(17u);}}pending_target=address+8u;}else pending_target=target;delay_remaining=2u;}\n";
    source << "return;\n";
    source << "case 17:\n";
    source << "regs[rd]=address+8u;pending_target=regs[rs];delay_remaining=2u;\n";
    source << "return;\n";
    source << "case 18:\n";
    source << "{uint32_t ea=(regs[rs]+(uint32_t)(int32_t)(int16_t)immediate)&0x1FFFFFFFu;if(ea<0x00200000u){if(size_value==1)regs[rt]=(uint32_t)*((uint8_t*)ram+ea);else if(size_value==2)regs[rt]=(uint32_t)*((uint16_t*)((uint8_t*)ram+ea));else regs[rt]=ram[ea>>2u];}else if(ea>=0x1F800000u&&ea<0x1F800400u)regs[rt]=scratchpad[(ea&0x3FFu)>>2u];else if(ea>=0x1F801000u&&ea<0x1F802000u){if(size_value==1)regs[rt]=(uint8_t)native_mmio_read(ea);else if(size_value==2)regs[rt]=(uint16_t)native_mmio_read(ea);else regs[rt]=native_mmio_read(ea);}else{write_windows_trace(\"mmio_load_address=\",ea);ExitProcess(17u);}}\n";
    source << "return;\n";
    source << "case 19:\n";
    source << "{uint32_t ea=(regs[rs]+(uint32_t)(int32_t)(int16_t)immediate)&0x1FFFFFFFu;if(ea<0x00200000u){if(size_value==1)*((uint8_t*)ram+ea)=(uint8_t)regs[rt];else if(size_value==2)*((uint16_t*)((uint8_t*)ram+ea))=(uint16_t)regs[rt];else ram[ea>>2u]=regs[rt];}else if(ea>=0x1F800000u&&ea<0x1F800400u)scratchpad[(ea&0x3FFu)>>2u]=regs[rt];else if(ea>=0x1F801000u&&ea<0x1F802000u)native_mmio_write(ea,regs[rt]);else{write_windows_trace(\"mmio_store_address=\",ea);ExitProcess(17u);}}\n";
    source << "return;\n";
    source << "case 22:\n";
    source << "if(special==12u)ExitProcess(0u);\n";
    source << "if(special==13u){write_windows_trace(\"break_address=\",address);ExitProcess(9u);}\n";
    source << "return;\n";
    source << "default:ExitProcess(16u);\n";
    source << "}\n}\n";
    for (std::size_t chunk_index = 0; chunk_index < plan.leader_chunks.size(); ++chunk_index) {
        source << "extern \"C\" bool native_chunk_" << chunk_index << "(void);\n";
    }
    source << "int main(void) {\n";
    source << "#ifdef _WIN32\n";
    source << "void* handle = GetStdHandle(0xfffffff5u);\n";
    source << "unsigned long written = 0;\n";
    source << "WriteFile(handle, diagnostics, sizeof(diagnostics) - 1, &written, 0);\n";
    source << "#endif\n";
    source << "regs[29]=0x801FF000u;\n";
    source << "regs[28]=0x" << std::hex << initial_gp << std::dec << "u;\n";
    source << "gpu_status_native=0x14802000u;\n";
    source << "controller_rx_count=0u;controller_rx_read=0u;\n";
    source << "cd_parameter_count=0u;cd_response_count=0u;cd_response_read=0u;cd_status_index=0u;cd_irq=0u;\n";
    source << "for(uint32_t i=0;i<sizeof(payload)/sizeof(payload[0]);++i) "
               "ram[" << (plan.payload_ram_offset / 4u)
            << "+i]=payload[i];\n";
    source << "pc=0x" << std::hex << initial_pc << std::dec << "u;\n";
    source << "next_pc=pc+4u;\n";
    source << "pending_target=0u;\n";
    source << "delay_remaining=0u;\n";
    source << "for(uint32_t steps=0;steps<100000000u;++steps){\n";
    source << "++native_frame_counter;++timer_cycles;++frame_cycles;if(frame_cycles>=1024u){frame_cycles-=1024u;interrupt_status|=1u;}\n";
    source << "if(delay_remaining==1u){pc=pending_target;delay_remaining=0u;}\n";
    source << "else if(delay_remaining==2u){pc=next_pc;delay_remaining=1u;}\n";
    source << "bool dispatched=false;\n";
    for (std::size_t chunk_index = 0; chunk_index < plan.leader_chunks.size(); ++chunk_index) {
        source << "if(!dispatched && native_chunk_" << chunk_index << "())dispatched=true;\n";
    }
    source << "if(!dispatched){write_windows_trace(\"undispatched_pc=\",pc);ExitProcess(17u);}\n";
    source << "pc=next_pc;\n";
    source << "}\n";
    source << "write_windows_trace(\"step_limit_pc=\",pc);\n";
    source << "ExitProcess(17u);\n";
    source << "return 17;\n";
    source << "}\n";

    std::vector<std::string> chunks;

    const std::uint32_t exit_status =
        unsupported_count != 0 ? 16 :
        plan.supported_count != 0 ? 17 : 17;

    NativeLoweringIr ir;
    ir.target = TargetPlatform::Windows;
    ir.entry_symbol = "main";
    ir.source = source.str();
    ir.chunks.push_back(source.str());
    for (std::size_t chunk_index = 0;
         chunk_index < plan.leader_chunks.size();
         ++chunk_index) {
        std::ostringstream chunk;
        chunk << "#include <stddef.h>\n";
        chunk << "#include <stdint.h>\n";
        chunk << "#ifdef _WIN32\n";
        chunk << "extern \"C\" __declspec(dllimport) void __stdcall ExitProcess(unsigned int);\n";
        chunk << "#endif\n";
        chunk << "extern uint32_t regs[32];\n";
        chunk << "extern uint32_t ram[524288];\n";
        chunk << "extern uint32_t scratchpad[256];\n";
        chunk << "extern uint32_t hi_value;\n";
        chunk << "extern uint32_t lo_value;\n";
        chunk << "extern uint32_t pc;\n";
        chunk << "extern uint32_t next_pc;\n";
        chunk << "extern uint32_t pending_target;\n";
        chunk << "extern uint32_t delay_remaining;\n";
        chunk << "extern \"C\" void native_execute_ir(uint32_t kind,uint32_t address,uint32_t word,uint32_t rs,uint32_t rt,uint32_t rd,uint32_t shamt,uint32_t immediate,uint32_t target,uint32_t size_value,uint32_t special);\n";
        chunk << "extern \"C\" bool native_chunk_" << chunk_index << "(void){\n";
        chunk << "switch(pc){\n";
        for (std::uint32_t address : plan.leader_chunks[chunk_index]) {
            chunk << "case 0x" << std::hex << std::setfill('0')
                  << std::setw(8) << address << std::dec << "u:\n";
            std::uint32_t block_end = plan.load_address + plan.file_size;
            const auto next_leader = plan.leaders.upper_bound(address);
            if (next_leader != plan.leaders.end()) {
                block_end = *next_leader;
            }
            for (std::uint32_t block_address = address;
                 block_address <= block_end;
                 block_address += 4u) {
                const std::uint32_t offset =
                    block_address - plan.load_address;
                if (offset + 4u > plan.file_size) break;
                const std::uint32_t word =
                    read_psx32(data, PSX_HEADER_SIZE + offset);
                const MipsInstructionIr instruction_ir =
                    build_mips_instruction_ir(block_address, word);
                if (!is_supported_mips_instruction(word)) {
                    chunk << "ExitProcess(16u);return true;\n";
                    break;
                }
                chunk << "next_pc=0x" << std::hex << block_address
                      << std::dec << "u+4u;\n";
                chunk << "native_execute_ir("
                      << static_cast<std::uint32_t>(instruction_ir.kind)
                      << ",0x" << std::hex << block_address << std::dec << "u,"
                      << "0x" << std::hex << word << std::dec << "u,"
                      << instruction_ir.rs << "u,"
                      << instruction_ir.rt << "u,"
                      << instruction_ir.rd << "u,"
                      << instruction_ir.shamt << "u,"
                      << instruction_ir.immediate << "u,"
                      << "0x" << std::hex << instruction_ir.target << std::dec << "u,"
                      << instruction_ir.size << "u,"
                      << instruction_ir.special << "u);\n";
                chunk << "regs[0]=0u;\n";
                const std::uint32_t instruction_kind =
                    static_cast<std::uint32_t>(instruction_ir.kind);
                if (instruction_kind >= 14u &&
                    instruction_kind <= 17u) {
                    chunk << "break;\n";
                } else if (instruction_kind == 22u) {
                    chunk << "break;\n";
                }
            }
            chunk << "return true;\n";
        }
        chunk << "default:return false;\n";
        chunk << "}\n";
        chunk << "return true;\n}\n";
        ir.chunks.push_back(chunk.str());
    }
    ir.diagnostics = diagnostics.str();
    ir.exit_status = exit_status;
    write_pe_ir(options, ir);
}

}
