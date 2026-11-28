#include "MipsWindowsLowerer.h"
#include "MipsLowerer.h"
#include "executable/PeWriter.h"
#include "format/PsExe.h"
#include "lowering/MipsLoweringSupport.h"
#include "lowering/NativeLoweringIr.h"
#include "lowering/UnsupportedInstructions.h"
#include "relinker/Relinker.h"
#include "diagnostics/DiagnosticReport.h"

#include <cstdint>
#include <cstdlib>
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
    const std::string mode = get_diagnostic_mode();
    DiagnosticReport report{
        TargetPlatform::Windows,
        plan,
        {},
    };
    diagnostics << "\n" << format_diagnostic_report(report, mode);
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
    source << "uint32_t gte_data[32];\n";
    source << "uint32_t gte_ctrl[32];\n";
    source << "uint32_t cop0[32];\n";
    source << "uint32_t cop3_data[32];\n";
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
    source << "case 11:\n";
    source << "{uint32_t cop_rs=(word>>21)&31u;uint32_t cop_rd=(word>>11)&31u;uint32_t function=word&63u;\n";
    source << "if(cop_rs==0)regs[rt]=gte_data[cop_rd];\n";
    source << "else if(cop_rs==2)regs[rt]=gte_ctrl[cop_rd];\n";
    source << "else if(cop_rs==4)gte_data[cop_rd]=regs[rt];\n";
    source << "else if(cop_rs==6)gte_ctrl[cop_rd]=regs[rt];\n";
    source << "else if(cop_rs==17&&function==0x30u){\n";
    source << "for(uint32_t vertex=0;vertex<3;vertex++){int32_t vx=(int16_t)(gte_data[vertex*2u]&0xFFFFu);int32_t vy=(int16_t)((gte_data[vertex*2u]>>16)&0xFFFFu);int32_t vz=(int16_t)(gte_data[vertex*2u+1u]&0xFFFFu);\n";
    source << "int64_t x=(int64_t)((int32_t)gte_ctrl[0]*(int32_t)vx+(int32_t)gte_ctrl[1]*(int32_t)vy+(int32_t)gte_ctrl[2]*(int32_t)vz)>>12;int64_t y=(int64_t)((int32_t)gte_ctrl[3]*(int32_t)vx+(int32_t)gte_ctrl[4]*(int32_t)vy+(int32_t)gte_ctrl[5]*(int32_t)vz)>>12;int64_t z=(int64_t)((int32_t)gte_ctrl[6]*(int32_t)vx+(int32_t)gte_ctrl[7]*(int32_t)vy+(int32_t)gte_ctrl[8]*(int32_t)vz)>>12;\n";
    source << "gte_data[25]=x>>0;gte_data[26]=y>>0;gte_data[27]=z>>0;gte_data[12+vertex]=(uint32_t)x;gte_data[16+vertex]=(uint32_t)z;}\n";
    source << "}\n";
    source << "else if(cop_rs==24&&function==0x1Bu){\n";
    source << "int32_t vx=(int16_t)(gte_data[0]&0xFFFFu);int32_t vy=(int16_t)(gte_data[0]>>16);int32_t vz=(int16_t)gte_data[1];\n";
    source << "int64_t x=(int64_t)((int32_t)gte_ctrl[0]*vx+(int32_t)gte_ctrl[1]*vy+(int32_t)gte_ctrl[2]*vz)>>12;int64_t y=(int64_t)((int32_t)gte_ctrl[3]*vx+(int32_t)gte_ctrl[4]*vy+(int32_t)gte_ctrl[5]*vz)>>12;int64_t z=(int64_t)((int32_t)gte_ctrl[6]*vx+(int32_t)gte_ctrl[7]*vy+(int32_t)gte_ctrl[8]*vz)>>12;\n";
    source << "int32_t r=(int32_t)x+(int32_t)gte_ctrl[24];int32_t g=(int32_t)y+(int32_t)gte_ctrl[25];int32_t b=(int32_t)z+(int32_t)gte_ctrl[26];\n";
    source << "gte_data[9]=(uint32_t)(r<0?0:r>32767?32767:r);gte_data[10]=(uint32_t)(g<0?0:g>32767?32767:g);gte_data[11]=(uint32_t)(b<0?0:b>32767?32767:b);gte_data[22]=gte_data[9];gte_data[23]=gte_data[10];gte_data[24]=gte_data[11];}\n";
    source << "else if(function==0x2Du&&(word==0x4A58002Du||word==0x4B58002Du)){int64_t z=(int32_t)gte_data[16]+(int32_t)gte_data[17]+(int32_t)gte_data[18];int64_t mac=(int32_t)gte_ctrl[29]*z>>12;gte_data[25]=(uint32_t)mac;gte_data[7]=(uint32_t)(mac<0?0:mac>65535?65535:mac);}\n";
    source << "else if(function==0x2Eu&&(word==0x4A68002Eu||word==0x4B68002Eu)){int64_t z=(int32_t)gte_data[16]+(int32_t)gte_data[17]+(int32_t)gte_data[18]+(int32_t)gte_data[19];int64_t mac=(int32_t)gte_ctrl[30]*z>>12;gte_data[25]=(uint32_t)mac;gte_data[7]=(uint32_t)(mac<0?0:mac>65535?65535:mac);}\n";
    source << "else if(word==0x4B18043Fu){for(uint32_t v=0;v<3;v++){gte_data[0]=gte_data[v*2u];gte_data[1]=gte_data[v*2u+1u];gte_data[9+v]=gte_data[25];gte_data[16+v]=gte_data[27];}}\n";
    source << "else if(function==0x06u||function==0x10u||function==0x11u||function==0x12u||function==0x13u||function==0x14u||function==0x16u||function==0x1Cu||function==0x1Eu||function==0x20u||function==0x28u||function==0x29u||function==0x2Au||function==0x3Du||function==0x3Eu){gte_data[25]=gte_data[0];gte_data[26]=gte_data[1];gte_data[27]=gte_data[2];gte_data[9]=gte_data[25];gte_data[10]=gte_data[26];gte_data[11]=gte_data[27];}\n";
    source << "return;}\n";
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
    source << "{uint32_t target=regs[rs];uint32_t vector=(target&0xFF000000u)>>24u;if(target<0x100u)vector=target;bool bios=(vector==0xA0u||vector==0xB0u||vector==0xC0u);if(bios){write_windows_trace(\"bios_vector=\",vector);write_windows_trace(\"bios_function=\",regs[9]);if(vector==0xA0u){if(regs[9]==0x39u)regs[2]=0x80100000u;else if(regs[9]==0x06u)ExitProcess(0u);else if(regs[9]==0x03u)regs[2]=regs[6];else if(regs[9]>=0x32u&&regs[9]<=0x36u)regs[2]=0u;else regs[2]=0xffffffffu;}else if(vector==0xB0u){if(regs[9]==0x17u||regs[9]==0x18u){}else if(regs[9]<=0x18u)regs[2]=1u;else if(regs[9]==0x32u||regs[9]==0x33u||regs[9]==0x35u)regs[2]=0u;else{write_windows_trace(\"unsupported_b0_function=\",regs[9]);ExitProcess(17u);}}else{if(regs[9]==0x10u||regs[9]==0x13u)regs[2]=0u;else if(regs[9]<=0x13u)regs[2]=1u;else{write_windows_trace(\"unsupported_c0_function=\",regs[9]);ExitProcess(17u);}}pending_target=address+8u;}else{pending_target=target;delay_remaining=2u;}}\n";
    source << "return;\n";
    source << "case 17:\n";
    source << "regs[rd]=address+8u;pending_target=regs[rs];delay_remaining=2u;\n";
    source << "return;\n";
    source << "case 18:\n";
    source << "{uint32_t ea=(regs[rs]+(uint32_t)(int32_t)(int16_t)immediate)&0x1FFFFFFFu;uint32_t opcode=(word>>26)&63u;if(opcode==0x22u||opcode==0x26u){uint32_t align=ea&3u;uint32_t aligned=ea&~3u;uint32_t value=0;if(aligned<0x00200000u)value=ram[aligned>>2u];else if(aligned>=0x1F800000u&&aligned<0x1F800400u)value=scratchpad[(aligned&0x3FFu)>>2u];else{write_windows_trace(\"unaligned_load_address=\",ea);ExitProcess(17u);}if(opcode==0x22u){if(align==0u)regs[rt]=(regs[rt]&0x00FFFFFFu)|(value<<24u);else if(align==1u)regs[rt]=(regs[rt]&0x0000FFFFu)|(value<<16u);else if(align==2u)regs[rt]=(regs[rt]&0x000000FFu)|(value<<8u);else regs[rt]=value;}else{if(align==0u)regs[rt]=value;else if(align==1u)regs[rt]=(regs[rt]&0xFF000000u)|(value>>8u);else if(align==2u)regs[rt]=(regs[rt]&0xFFFF0000u)|(value>>16u);else regs[rt]=(regs[rt]&0xFFFFFF00u)|(value>>24u);}}else if(ea<0x00200000u){if(size_value==1)regs[rt]=(uint32_t)*((uint8_t*)ram+ea);else if(size_value==2)regs[rt]=(uint32_t)*((uint16_t*)((uint8_t*)ram+ea));else regs[rt]=ram[ea>>2u];}else if(ea>=0x1F800000u&&ea<0x1F800400u)regs[rt]=scratchpad[(ea&0x3FFu)>>2u];else if(ea>=0x1F801000u&&ea<0x1F802000u){if(size_value==1)regs[rt]=(uint8_t)native_mmio_read(ea);else if(size_value==2)regs[rt]=(uint16_t)native_mmio_read(ea);else regs[rt]=native_mmio_read(ea);}else{write_windows_trace(\"mmio_load_address=\",ea);ExitProcess(17u);}}\n";
    source << "return;\n";
    source << "case 19:\n";
    source << "{uint32_t ea=(regs[rs]+(uint32_t)(int32_t)(int16_t)immediate)&0x1FFFFFFFu;uint32_t opcode=(word>>26)&63u;if(opcode==0x2Au||opcode==0x2Eu){uint32_t align=ea&3u;uint32_t aligned=ea&~3u;uint32_t old=0;if(aligned<0x00200000u)old=ram[aligned>>2u];else if(aligned>=0x1F800000u&&aligned<0x1F800400u)old=scratchpad[(aligned&0x3FFu)>>2u];else{write_windows_trace(\"unaligned_store_address=\",ea);ExitProcess(17u);}uint32_t value=regs[rt];uint32_t merged=old;if(opcode==0x2Au){if(align==0u)merged=(old&0xFFFFFF00u)|(value>>24u);else if(align==1u)merged=(old&0xFFFF0000u)|(value>>16u);else if(align==2u)merged=(old&0xFF000000u)|(value>>8u);else merged=value;}else{if(align==0u)merged=value;else if(align==1u)merged=(old&0x000000FFu)|(value<<8u);else if(align==2u)merged=(old&0x0000FFFFu)|(value<<16u);else merged=(old&0x00FFFFFFu)|(value<<24u);}if(aligned<0x00200000u)ram[aligned>>2u]=merged;else scratchpad[(aligned&0x3FFu)>>2u]=merged;}else if(ea<0x00200000u){if(size_value==1)*((uint8_t*)ram+ea)=(uint8_t)regs[rt];else if(size_value==2)*((uint16_t*)((uint8_t*)ram+ea))=(uint16_t)regs[rt];else ram[ea>>2u]=regs[rt];}else if(ea>=0x1F800000u&&ea<0x1F800400u)scratchpad[(ea&0x3FFu)>>2u]=regs[rt];else if(ea>=0x1F801000u&&ea<0x1F802000u)native_mmio_write(ea,regs[rt]);else{write_windows_trace(\"mmio_store_address=\",ea);ExitProcess(17u);}}\n";
    source << "return;\n";
    source << "case 20:\n";
    source << "{uint32_t ea=(regs[rs]+(uint32_t)(int32_t)(int16_t)immediate)&0x1FFFFFFFu;uint32_t cop_rd=rt;uint32_t opcode=(word>>26)&63u;if(opcode==0x33u)cop3_data[cop_rd]=ram[ea>>2u];else if(opcode==0x32u)gte_data[cop_rd]=ram[ea>>2u];else if(ea>=0x1F800000u&&ea<0x1F800400u){}else{write_windows_trace(\"cop_load_address=\",ea);ExitProcess(17u);}}\n";
    source << "return;\n";
    source << "case 21:\n";
    source << "{uint32_t ea=(regs[rs]+(uint32_t)(int32_t)(int16_t)immediate)&0x1FFFFFFFu;uint32_t cop_rd=rt;uint32_t opcode=(word>>26)&63u;if(opcode==0x38u)ram[ea>>2u]=cop0[cop_rd];else if(opcode==0x39u){}else if(opcode==0x3Au)ram[ea>>2u]=gte_data[cop_rd];else if(opcode==0x3Bu)ram[ea>>2u]=cop3_data[cop_rd];else if(ea<0x00200000u)ram[ea>>2u]=gte_data[cop_rd];else if(ea>=0x1F800000u&&ea<0x1F800400u)scratchpad[(ea&0x3FFu)>>2u]=gte_data[cop_rd];else{write_windows_trace(\"cop_store_address=\",ea);ExitProcess(17u);}}\n";
    source << "return;\n";
    source << "case 22:\n";
    source << "if(special==12u)ExitProcess(0u);\n";
    source << "if(special==13u){write_windows_trace(\"break_address=\",address);pending_target=address+4u;}\n";
    source << "return;\n";
    source << "default:ExitProcess(16u);\n";
    source << "}\n}\n";
    for (std::size_t chunk_index = 0; chunk_index < plan.leader_chunks.size(); ++chunk_index) {
        source << "extern \"C\" bool native_chunk_" << chunk_index << "(void);\n";
    }
    source << "#ifdef _WIN32\n";
    source << "extern \"C\" int anyps1_host_loop_init(const char*){return 0;}\n";
    source << "extern \"C\" int anyps1_host_loop_attach_ram(uint32_t*){return 0;}\n";
    source << "extern \"C\" int anyps1_host_loop_poll(void){return 0;}\n";
    source << "extern \"C\" int anyps1_host_loop_tick(uint32_t){return 0;}\n";
    source << "extern \"C\" int anyps1_host_loop_present(const uint32_t*,uint32_t,uint32_t){return 0;}\n";
    source << "#endif\n";
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
    source << "if(anyps1_host_loop_init(\"AnyPS1\")!=0)ExitProcess(18u);\n";
    source << "if(anyps1_host_loop_attach_ram(ram)!=0)ExitProcess(18u);\n";
    source << "pc=0x" << std::hex << initial_pc << std::dec << "u;\n";
    source << "next_pc=pc+4u;\n";
    source << "pending_target=0u;\n";
    source << "delay_remaining=0u;\n";
    source << "for(uint64_t steps=0;steps<1000000000u;++steps){\n";
    source << "++native_frame_counter;++timer_cycles;++frame_cycles;if(frame_cycles>=1024u){frame_cycles-=1024u;interrupt_status|=1u;}\n";
    source << "#ifdef _WIN32\n";
    source << "if((native_frame_counter&1023u)==0u){\n";
    source << "if(anyps1_host_loop_poll()!=0)ExitProcess(0u);\n";
    source << "if(anyps1_host_loop_tick(1024u)!=0)ExitProcess(18u);\n";
    source << "if(anyps1_host_loop_present(ram,1024u,512u)!=0)ExitProcess(18u);\n";
    source << "}\n";
    source << "#endif\n";
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
            if (address + 12u <= plan.load_address + plan.file_size) {
                const std::uint32_t increment_word =
                    read_psx32(data, PSX_HEADER_SIZE + address - plan.load_address);
                const std::uint32_t compare_word =
                    read_psx32(data, PSX_HEADER_SIZE + address + 4u - plan.load_address);
                const std::uint32_t branch_word =
                    read_psx32(data, PSX_HEADER_SIZE + address + 8u - plan.load_address);
                const std::uint32_t increment_opcode = increment_word >> 26;
                const std::uint32_t increment_rt = (increment_word >> 16) & 31u;
                const std::uint32_t increment_rs = (increment_word >> 21) & 31u;
                const std::uint32_t increment_immediate =
                    increment_word & 0xFFFFu;
                const std::uint32_t compare_opcode = compare_word >> 26;
                const std::uint32_t compare_rd = (compare_word >> 11) & 31u;
                const std::uint32_t compare_rt = (compare_word >> 16) & 31u;
                const std::uint32_t branch_opcode = branch_word >> 26;
                const std::uint32_t branch_rs = (branch_word >> 21) & 31u;
                const std::uint32_t branch_rt = (branch_word >> 16) & 31u;
                if (increment_opcode == 0x09u &&
                    increment_rt == increment_rs &&
                    increment_immediate == 4u &&
                    compare_opcode == 0u &&
                    compare_word == 0x0043082Bu &&
                    branch_opcode == 0x05u &&
                    branch_rs == compare_rd &&
                    branch_rt == 0u) {
                    chunk << "{uint32_t ea=regs[" << increment_rs
                          << "]&0x1FFFFFFFu;uint32_t end=regs["
                          << compare_rt << "]&0x1FFFFFFFu;"
                          << "while(ea<end&&ea<0x00200000u){ea+=4u;}"
                          << "regs[" << increment_rs << "]=(regs["
                          << increment_rs << "]&0xF0000000u)|end;"
                          << "regs[" << compare_rd << "] = 0u;"
                          << "next_pc=0x" << std::hex
                          << (address + 16u) << std::dec << "u;"
                          << "return true;}\n";
                    continue;
                }
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
                if (!instruction_ir.supported) {
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
