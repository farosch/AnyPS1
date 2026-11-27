#include "Relinker.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <queue>
#include <set>
#include <queue>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

namespace {

constexpr std::uint32_t PSX_HEADER_SIZE = 2048;

std::uint32_t read32(const std::vector<unsigned char>& data, std::size_t offset);
std::string hex32(std::uint32_t value);
std::string escape_assembly_string(const std::string& value);

void trim(std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        value.clear();
        return;
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
}

void write_linux_native_cfg(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data
)
{
    const std::uint32_t file_size = read32(data, 0x1C);
    const std::uint32_t load_address = read32(data, 0x18);
    const std::uint32_t initial_pc = read32(data, 0x10);
    const std::uint32_t initial_gp = read32(data, 0x14);
    const std::uint32_t initial_sp = 0x801FF000u;
    if (file_size == 0 || file_size % 4 != 0) {
        throw std::runtime_error("unsupported native lowering: executable size is not a MIPS word sequence");
    }
    if (initial_pc < load_address || initial_pc >= load_address + file_size) {
        throw std::runtime_error("unsupported native lowering: entry point outside executable payload");
    }

    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> delay_slots;
    std::queue<std::uint32_t> work;
    leaders.insert(initial_pc);
    work.push(initial_pc);
    while (!work.empty()) {
        const std::uint32_t address = work.front();
        work.pop();
        if (address < load_address || address >= load_address + file_size
            || (address & 3u) != 0) {
            continue;
        }
        const std::uint32_t offset = address - load_address;
        if (offset + 4 > file_size) continue;
        const std::uint32_t instruction = read32(data, 2048 + offset);
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        const std::uint32_t special = instruction & 63u;
        const bool branch = opcode == 0x01 || opcode == 0x04 || opcode == 0x05
            || opcode == 0x06 || opcode == 0x07;
        const bool jump = opcode == 0x02 || opcode == 0x03;
        const bool jr = opcode == 0 && special == 0x08;
        const bool stop = opcode == 0 && (special == 0x0C || special == 0x0D);
        if (branch) {
            const std::int32_t displacement =
                static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t target =
                static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
            leaders.insert(target);
            leaders.insert(address + 8);
            work.push(target);
            work.push(address + 8);
        } else if (jump) {
            const std::uint32_t target =
                ((address + 4) & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2);
            leaders.insert(target);
            work.push(target);
            if (opcode == 0x03) {
                leaders.insert(address + 8);
                work.push(address + 8);
            }
        } else if (!jr && !stop) {
            leaders.insert(address + 4);
            work.push(address + 4);
        }
        if (branch || jump || jr) {
            const std::uint32_t delay_address = address + 4;
            if (delay_address >= load_address && delay_address < load_address + file_size) {
                delay_slots.insert(delay_address);
                leaders.insert(delay_address);
                work.push(delay_address);
            }
        }
    }

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
    const std::string completed = "native lowering complete\n";
    source << ".text\n.section .bss\n.p2align 4\n";
    source << "regs:\n.skip 136\nram:\n.skip 2097152\n";
    source << ".section .rodata\n";
    source << "diag_banner:.asciz \"" << escape_assembly_string(banner) << "\"\n";
    source << "diag_target:.asciz \"" << escape_assembly_string(target) << "\"\n";
    source << "diag_load:.asciz \"" << escape_assembly_string(load) << "\"\n";
    source << "diag_entry:.asciz \"" << escape_assembly_string(entry) << "\"\n";
    source << "diag_gp:.asciz \"" << escape_assembly_string(gp) << "\"\n";
    source << "diag_sp:.asciz \"" << escape_assembly_string(sp) << "\"\n";
    source << "diag_payload:.asciz \"" << escape_assembly_string(payload_size) << "\"\n";
    source << "diag_lowered:.asciz \"" << escape_assembly_string(lowered) << "\"\n";
    source << "diag_completed:.asciz \"" << escape_assembly_string(completed) << "\"\n";
    source << ".text\n.globl _start\n_start:\n";
    source << "leaq diag_banner(%rip), %rsi\nmov $" << banner.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_target(%rip), %rsi\nmov $" << target.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_load(%rip), %rsi\nmov $" << load.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_entry(%rip), %rsi\nmov $" << entry.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_gp(%rip), %rsi\nmov $" << gp.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_sp(%rip), %rsi\nmov $" << sp.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_payload(%rip), %rsi\nmov $" << payload_size.size() << ", %rdx\ncall print_diag\n";
    source << "leaq diag_lowered(%rip), %rsi\nmov $" << lowered.size() << ", %rdx\ncall print_diag\n";
    source << "leaq ram(%rip), %rdi\n";
    source << "add $" << (load_address & 0x1FFFFFu) << ", %rdi\n";
    source << "leaq payload(%rip), %rsi\n";
    source << "mov $" << file_size << ", %ecx\n";
    source << "rep movsb\n";
    source << "leaq regs(%rip), %r12\n";
    source << "movl $0, 0(%r12)\n";
    source << "movl $0x" << std::hex << initial_sp << std::dec << ", 116(%r12)\n";
    source << "movl $0x" << std::hex << initial_gp << std::dec << ", 112(%r12)\n";
    source << "jmp p" << std::hex << initial_pc << std::dec << "\n";
    source << ".section .rodata\npayload:\n.incbin \"" << payload.string() << "\"\n";
    source << ".text\n";
    source << "overflow_exit:\nmov $12, %edi\nmov $60, %eax\nsyscall\n";
    source << "divide_zero_exit:\nmov $11, %edi\nmov $60, %eax\nsyscall\n";
    source << ".text\n";
    source << "print_diag:\n";
    source << "mov $1, %edi\n";
    source << "mov $1, %eax\n";
    source << "syscall\n";
    source << "ret\n";

    auto emit_normal = [&](std::uint32_t address, std::uint32_t instruction) {
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
                throw std::runtime_error("control instruction must be emitted by CFG lowering");
            } else if (special == 0x0C || special == 0x0D) {
                source << "xor %edi, %edi\nmov $60, %eax\nsyscall\n";
            } else {
                throw std::runtime_error("unsupported native MIPS instruction at " + hex32(address));
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
            source << "movl " << rs * 4 << "(%r12), %eax\ncmpl $" << immediate << ", %eax\nsetb %al\nmovzbl %al, %eax\nmovl %eax, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x20 || opcode == 0x21 || opcode == 0x23 || opcode == 0x24 || opcode == 0x25) {
            source << "movl " << rs * 4 << "(%r12), %eax\naddl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\nandl $0x1FFFFF, %eax\nleaq ram(%rip), %rbx\n";
            if (opcode == 0x20) source << "movsbl (%rbx,%rax), %edx\n";
            else if (opcode == 0x21) source << "movswl (%rbx,%rax), %edx\n";
            else if (opcode == 0x23) source << "movl (%rbx,%rax), %edx\n";
            else if (opcode == 0x24) source << "movzbl (%rbx,%rax), %edx\n";
            else source << "movzwl (%rbx,%rax), %edx\n";
            source << "movl %edx, " << rt * 4 << "(%r12)\n";
        } else if (opcode == 0x28 || opcode == 0x29 || opcode == 0x2B) {
            source << "movl " << rs * 4 << "(%r12), %eax\naddl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) << ", %eax\nandl $0x1FFFFF, %eax\nmovl " << rt * 4 << "(%r12), %edx\nleaq ram(%rip), %rbx\n";
            if (opcode == 0x28) source << "movb %dl, (%rbx,%rax)\n";
            else if (opcode == 0x29) source << "movw %dx, (%rbx,%rax)\n";
            else source << "movl %edx, (%rbx,%rax)\n";
        } else if (opcode == 0x22 || opcode == 0x26 || opcode == 0x2A || opcode == 0x2E) {
            throw std::runtime_error("unsupported native MIPS unaligned load/store at " + hex32(address));
        } else {
            throw std::runtime_error("unsupported native MIPS opcode at " + hex32(address));
        }
    };

    std::set<std::uint32_t> emitted_delay_slots;
    for (std::uint32_t address : leaders) {
        if (address < load_address || address >= load_address + file_size) continue;
        const std::uint32_t offset = address - load_address;
        if (offset + 4 > file_size) continue;
        const std::uint32_t instruction = read32(data, 2048 + offset);
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t rs = (instruction >> 21) & 31u;
        const std::uint32_t rt = (instruction >> 16) & 31u;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        const std::uint32_t special = instruction & 63u;
        source << "p" << std::hex << address << std::dec << ":\n";

        if (delay_slots.find(address) != delay_slots.end() && emitted_delay_slots.find(address) == emitted_delay_slots.end()) {
            emit_normal(address, instruction);
            emitted_delay_slots.insert(address);
            continue;
        }
        if (delay_slots.find(address) != delay_slots.end()) continue;

        const bool branch = opcode == 0x01 || opcode == 0x04 || opcode == 0x05
            || opcode == 0x06 || opcode == 0x07;
        const bool jump = opcode == 0x02 || opcode == 0x03;
        const bool jr = opcode == 0 && special == 0x08;
        const bool jalr = opcode == 0 && special == 0x09;
        if (branch || jump || jr || jalr) {
            const std::uint32_t delay_address = address + 4;
            if (delay_address >= load_address && delay_address < load_address + file_size) {
                const std::uint32_t delay_offset = delay_address - load_address;
                if (delay_offset + 4 <= file_size) {
                    source << "p" << std::hex << delay_address << std::dec << ":\n";
                    emit_normal(delay_address, read32(data, 2048 + delay_offset));
                    emitted_delay_slots.insert(delay_address);
                }
            }
        }
        if (branch) {
            const std::int32_t displacement =
                static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t target =
                static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
            if (opcode == 0x04) {
                source << "cmpl " << rt * 4 << "(%r12), " << rs * 4 << "(%r12)\nje p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x05) {
                source << "cmpl " << rt * 4 << "(%r12), " << rs * 4 << "(%r12)\njne p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x06) {
                source << "cmpl $0, " << rs * 4 << "(%r12)\njle p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x07) {
                source << "cmpl $0, " << rs * 4 << "(%r12)\njg p" << std::hex << target << std::dec << "\n";
            } else {
                source << "cmpl $0, " << rs * 4 << "(%r12)\n";
                if ((instruction >> 16 & 31u) == 0) source << "jl p" << std::hex << target << std::dec << "\n";
                else source << "jge p" << std::hex << target << std::dec << "\n";
            }
        } else if (jump) {
            const std::uint32_t target =
                ((address + 4) & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2);
            if (opcode == 0x03) {
                source << "movl $" << address + 8 << ", " << 31 * 4 << "(%r12)\n";
                source << "call p" << std::hex << target << std::dec << "\n";
            } else {
                source << "jmp p" << std::hex << target << std::dec << "\n";
            }
        } else if (jr) {
            if (rs == 31) source << "ret\n";
            else throw std::runtime_error("unsupported native indirect JR at " + hex32(address));
        } else if (jalr) {
            throw std::runtime_error("unsupported native JALR at " + hex32(address));
        } else {
            emit_normal(address, instruction);
        }
    }

    source << "leaq diag_completed(%rip), %rsi\n";
    source << "mov $25, %rdx\n";
    source << "call print_diag\n";
    source << "xor %edi, %edi\nmov $60, %eax\nsyscall\n";
    if (!source) throw std::runtime_error("cannot create native lowering assembly");

    std::string command = "gcc -nostdlib -no-pie -Wl,-e,_start -o \"" +
        options.output + "\" \"" + assembly.string() + "\"";
    // Flush the assembly before invoking the native assembler/linker.
    source.flush();
    source.close();
    const int status = std::system(command.c_str());
    std::filesystem::remove(payload);
    if (status != 0) throw std::runtime_error("native Linux lowering toolchain failed");
    std::filesystem::permissions(
        options.output,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
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
                if (boot.size() >= 7
                    && boot.compare(0, 7, "cdrom:\\") == 0) {
                    boot = boot.substr(7);
                }
                const auto version = boot.find(';');
                if (version != std::string::npos) {
                    boot.erase(version);
                }
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
    if (std::filesystem::is_regular_file(root / "SYSTEM.CNF")) {
        system_cnf_files.push_back(root / "SYSTEM.CNF");
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() == "SYSTEM.CNF") {
            system_cnf_files.push_back(entry.path());
        }
    }
    for (const auto& system_cnf : system_cnf_files) {
        const std::string boot = system_cnf_boot(system_cnf);
        if (!boot.empty()) {
            std::filesystem::path candidate = system_cnf.parent_path() / boot;
            if (std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
            candidate = system_cnf.parent_path() / boot.substr(boot.find_last_of("/\\") + 1);
            if (std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
        }
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && is_psx_exe(entry.path())) {
            return entry.path();
        }
    }
    throw std::runtime_error("extracted game has no boot PS-X EXE");
}

std::filesystem::path resolve_relink_input(const RelinkOptions& options)
{
    const std::filesystem::path input(options.input);
    if (std::filesystem::is_regular_file(input)) {
        return input;
    }
    if (std::filesystem::is_directory(input)) {
        return find_extracted_boot(input);
    }
    throw std::runtime_error("input extracted game path does not exist: " + options.input);
}

void create_output_parent(const std::string& output)
{
    const std::filesystem::path output_path(output);
    const std::filesystem::path parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write32(std::ofstream& output, std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write16(std::ofstream& output, std::uint16_t value)
{
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write64(std::ofstream& output, std::uint64_t value)
{
    const std::array<unsigned char, 8> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
        static_cast<unsigned char>(value >> 32),
        static_cast<unsigned char>(value >> 40),
        static_cast<unsigned char>(value >> 48),
        static_cast<unsigned char>(value >> 56),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::uint32_t read32(const std::vector<unsigned char>& data, std::size_t offset)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("truncated PS-X EXE header");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::string hex32(std::uint32_t value)
{
    char output[11]{};
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

void write_pe_layout(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size
)
{
    throw std::runtime_error(
        "native Windows MIPS lowering is not implemented");
}

void write_linux_native(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data
)
{
    const std::uint32_t file_size = read32(data, 0x1C);
    const std::uint32_t load_address = read32(data, 0x18);
    const std::uint32_t initial_pc = read32(data, 0x10);
    const std::uint32_t initial_gp = read32(data, 0x14);
    const std::uint32_t initial_sp = 0x801FF000u;
    if (file_size == 0 || file_size % 4 != 0) {
        throw std::runtime_error("unsupported native lowering: executable size is not a MIPS word sequence");
    }
    if (initial_pc < load_address || initial_pc >= load_address + file_size) {
        throw std::runtime_error("unsupported native lowering: entry point outside executable payload");
    }
    const std::uint32_t entry_offset = initial_pc - load_address;
    const std::uint32_t block_words = std::min<std::uint32_t>(1024u, (file_size - entry_offset) / 4u);
    const std::filesystem::path assembly = std::filesystem::path(options.output + ".native.s");
    const std::filesystem::path payload = std::filesystem::path(options.output + ".native.bin");
    {
        std::ofstream output(payload, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(data.data() + PSX_HEADER_SIZE),
            static_cast<std::streamsize>(file_size));
        if (!output) {
            throw std::runtime_error("cannot write native lowering payload");
        }
    }
    {
        std::ofstream source(assembly, std::ios::trunc);
        const std::string banner = "AnyPS1 native executable diagnostics\n";
        const std::string target = "target=linux\n";
        const std::string load = "load_address=" + hex32(load_address) + "\n";
        const std::string entry = "entry_point=" + hex32(initial_pc) + "\n";
        const std::string gp = "initial_gp=" + hex32(initial_gp) + "\n";
        const std::string sp = "initial_sp=" + hex32(initial_sp) + "\n";
        const std::string payload_size = "payload_size=" + std::to_string(file_size) + "\n";
        const std::string lowered = "lowered_words=" + std::to_string(block_words) + "\n";
        const std::string completed = "native lowering complete\n";
        source << ".text\n";
        source << ".section .bss\n";
        source << ".p2align 4\n";
        source << "regs:\n";
        source << ".skip 136\n";
        source << "ram:\n";
        source << ".skip 2097152\n";
        source << ".text\n";
        source << ".section .rodata\n";
        source << "diag_banner:.asciz \"" << escape_assembly_string(banner) << "\"\n";
        source << "diag_target:.asciz \"" << escape_assembly_string(target) << "\"\n";
        source << "diag_load:.asciz \"" << escape_assembly_string(load) << "\"\n";
        source << "diag_entry:.asciz \"" << escape_assembly_string(entry) << "\"\n";
        source << "diag_gp:.asciz \"" << escape_assembly_string(gp) << "\"\n";
        source << "diag_sp:.asciz \"" << escape_assembly_string(sp) << "\"\n";
        source << "diag_payload:.asciz \"" << escape_assembly_string(payload_size) << "\"\n";
        source << "diag_lowered:.asciz \"" << escape_assembly_string(lowered) << "\"\n";
        source << "diag_completed:.asciz \"" << escape_assembly_string(completed) << "\"\n";
        source << ".text\n";
        source << "print_diag:\n";
        source << "    mov $1, %edi\n";
        source << "    mov $1, %eax\n";
        source << "    syscall\n";
        source << "    ret\n";
        source << ".globl _start\n";
        source << "_start:\n";
        source << "    leaq diag_banner(%rip), %rsi\n";
        source << "    mov $" << banner.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_target(%rip), %rsi\n";
        source << "    mov $" << target.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_load(%rip), %rsi\n";
        source << "    mov $" << load.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_entry(%rip), %rsi\n";
        source << "    mov $" << entry.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_gp(%rip), %rsi\n";
        source << "    mov $" << gp.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_sp(%rip), %rsi\n";
        source << "    mov $" << sp.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_payload(%rip), %rsi\n";
        source << "    mov $" << payload_size.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq diag_lowered(%rip), %rsi\n";
        source << "    mov $" << lowered.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    leaq ram(%rip), %rdi\n";
        source << "    add $" << (load_address & 0x1FFFFFu) << ", %rdi\n";
        source << "    leaq payload(%rip), %rsi\n";
        source << "    mov $" << file_size << ", %ecx\n";
        source << "    rep movsb\n";
        source << "    leaq regs(%rip), %r12\n";
        source << "    movl $0, 0(%r12)\n";
        source << "    movl $0x" << std::hex << initial_sp << std::dec << ", 116(%r12)\n";
        source << "    movl $0x" << std::hex << initial_gp << std::dec << ", 112(%r12)\n";
        source << "    jmp p" << std::hex << initial_pc << std::dec << "\n";
        source << "\n";
        source << ".section .rodata\n";
        source << "payload:\n";
        source << ".incbin \"" << payload.string() << "\"\n";
        source << "\n";
        source << ".text\n";

        std::uint32_t first_external_jal_target = 0;
        for (std::uint32_t index = 0; index < block_words; ++index) {
            const std::uint32_t pc = entry_offset + index * 4;
            const std::uint32_t address = load_address + pc;
            const std::uint32_t instruction = read32(data, PSX_HEADER_SIZE + pc);
            const std::uint32_t opcode = instruction >> 26;
            const std::uint32_t rs = (instruction >> 21) & 31u;
            const std::uint32_t rt = (instruction >> 16) & 31u;
            const std::uint32_t rd = (instruction >> 11) & 31u;
            const std::uint32_t shamt = (instruction >> 6) & 31u;
            const std::uint32_t immediate = instruction & 0xFFFFu;
            const std::uint32_t special = instruction & 63u;
            source << "p" << std::hex << address << std::dec << ":\n";

            if (opcode == 0) {
                if (instruction == 0) {
                    continue;
                }
                if (special == 0x00) {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shll $" << shamt << ", %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x04) {
                    source << "    movl " << rs * 4 << "(%r12), %ecx\n";
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shll %cl, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x06) {
                    source << "    movl " << rs * 4 << "(%r12), %ecx\n";
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shrl %cl, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x07) {
                    source << "    movl " << rs * 4 << "(%r12), %ecx\n";
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    sarl %cl, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x02) {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shrl $" << shamt << ", %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x03) {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    sarl $" << shamt << ", %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x21) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    addl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x23) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    subl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x24) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    andl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x25) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    orl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x26) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    xorl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x27) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    orl " << rt * 4 << "(%r12), %eax\n";
                    source << "    notl %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x2A) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    cmpl " << rt * 4 << "(%r12), %eax\n";
                    source << "    setl %al\n";
                    source << "    movzbl %al, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x2B) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    cmpl " << rt * 4 << "(%r12), %eax\n";
                    source << "    setb %al\n";
                    source << "    movzbl %al, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x08) {
                    source << "    ret\n";
                } else if (special == 0x09) {
                    source << "    movl $" << address + 8 << ", " << rd * 4 << "(%r12)\n";
                    source << "    ret\n";
                } else if (special == 0x19) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %ecx\n";
                    source << "    xorl %edx, %edx\n";
                    source << "    mull %ecx\n";
                    source << "    movl %eax, 128(%r12)\n";
                    source << "    movl %edx, 132(%r12)\n";
                } else if (special == 0x18) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %ecx\n";
                    source << "    cltd\n";
                    source << "    imull %ecx\n";
                    source << "    movl %eax, 128(%r12)\n";
                    source << "    movl %edx, 132(%r12)\n";
                } else if (special == 0x1A || special == 0x1B) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %ecx\n";
                    if (special == 0x1B) {
                        source << "    xorl %edx, %edx\n";
                        source << "    divl %ecx\n";
                    } else {
                        source << "    cltd\n";
                        source << "    idivl %ecx\n";
                    }
                    source << "    movl %eax, 128(%r12)\n";
                    source << "    movl %edx, 132(%r12)\n";
                } else if (special == 0x12) {
                    source << "    movl 128(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x10) {
                    source << "    movl 132(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (special == 0x11) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, 132(%r12)\n";
                } else if (special == 0x13) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, 128(%r12)\n";
                } else if (special == 0x0D || special == 0x0C) {
                    source << "    xor %edi, %edi\n";
                    source << "    mov $60, %eax\n";
                    source << "    syscall\n";
                } else {
                    throw std::runtime_error(
                        "unsupported native MIPS instruction at 0x"
                        + [] (std::uint32_t value) {
                            char output[9]{};
                            std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                            return std::string(output);
                        }(address)
                        + ": special 0x"
                        + [] (std::uint32_t value) {
                            char output[9]{};
                            std::snprintf(output, sizeof(output), "%02X", static_cast<unsigned>(value));
                            return std::string(output);
                        }(special)
                        + "; native lowering is unsupported");
                }
            } else if (opcode == 0x0F) {
                source << "    movl $" << (immediate << 16) << ", " << rt * 4 << "(%r12)\n";
            } else if (opcode == 0x10) {
                const std::uint32_t cop_rs = rs;
                if (cop_rs == 0) {
                    source << "    movl $0, " << rt * 4 << "(%r12)\n";
                } else {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                }
            } else if (opcode == 0x01) {
                const std::int32_t displacement =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
                const std::uint32_t target =
                    static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
                if (target < initial_pc || target >= initial_pc + block_words * 4) {
                    throw std::runtime_error("unsupported native MIPS Bxx branch outside lowered block");
                }
                source << "    cmpl $0, " << rs * 4 << "(%r12)\n";
                if (rt == 0) source << "    jl p" << std::hex << target << std::dec << "\n";
                if (rt == 1) source << "    jge p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x09 || opcode == 0x08) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                       << ", %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (opcode == 0x0D) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    orl $" << immediate << ", %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (opcode == 0x0C) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    andl $" << immediate << ", %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (opcode == 0x0E) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    xorl $" << immediate << ", %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (opcode == 0x01) {
                const std::int32_t displacement =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
                const std::uint32_t target =
                    static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
                source << "    cmpl $0, " << rs * 4 << "(%r12)\n";
                if (rt == 0) source << "    jl p" << std::hex << target << std::dec << "\n";
                if (rt == 1) source << "    jge p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x0A) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    cmpl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                       << ", %eax\n";
                source << "    setl %al\n";
                source << "    movzbl %al, %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
            } else if (opcode == 0x0B) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    cmpl $" << static_cast<std::uint32_t>(static_cast<std::uint16_t>(immediate))
                       << ", %eax\n";
                source << "    setb %al\n";
                source << "    movzbl %al, %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x06 || opcode == 0x07) {
                const std::int32_t displacement =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
                const std::uint32_t target =
                    static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
                source << "    cmpl $0, " << rs * 4 << "(%r12)\n";
                if (opcode == 0x06) source << "    jle p" << std::hex << target << std::dec << "\n";
                else source << "    jg p" << std::hex << target << std::dec << "\n";
                } else if (opcode == 0x0E) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    xorl $" << immediate << ", %eax\n";
                source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x10) {
                if (rs == 0) {
                    source << "    movl $0, " << rt * 4 << "(%r12)\n";
                } else {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                }
            } else if (opcode == 0x23 || opcode == 0x24 || opcode == 0x25
                || opcode == 0x20 || opcode == 0x21) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                       << ", %eax\n";
                source << "    andl $0x1FFFFF, %eax\n";
                source << "    leaq ram(%rip), %rbx\n";
                if (opcode == 0x23) {
                    source << "    movl (%rbx,%rax), %edx\n";
                    source << "    movl %edx, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x24) {
                    source << "    movzbl (%rbx,%rax), %edx\n";
                    source << "    movl %edx, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x25) {
                    source << "    movzwl (%rbx,%rax), %edx\n";
                    source << "    movl %edx, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x20) {
                    source << "    movsbl (%rbx,%rax), %edx\n";
                    source << "    movl %edx, " << rt * 4 << "(%r12)\n";
                } else {
                    source << "    movswl (%rbx,%rax), %edx\n";
                    source << "    movl %edx, " << rt * 4 << "(%r12)\n";
                }
            } else if (opcode == 0x2B || opcode == 0x28 || opcode == 0x29) {
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                       << ", %eax\n";
                source << "    andl $0x1FFFFF, %eax\n";
                source << "    movl " << rt * 4 << "(%r12), %edx\n";
                source << "    leaq ram(%rip), %rbx\n";
                if (opcode == 0x2B) source << "    movl %edx, (%rbx,%rax)\n";
                if (opcode == 0x29) source << "    movw %dx, (%rbx,%rax)\n";
                if (opcode == 0x28) source << "    movb %dl, (%rbx,%rax)\n";
            } else if (opcode == 0x04 || opcode == 0x05) {
                const std::int32_t displacement =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
                const std::uint32_t target =
                    static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
                const std::uint32_t block_end = initial_pc + block_words * 4;
                if (target < initial_pc || target >= block_end) {
                    throw std::runtime_error(
                        "unsupported native MIPS branch outside lowered block at 0x"
                        + [] (std::uint32_t value) {
                            char output[9]{};
                            std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                            return std::string(output);
                        }(address));
                }
                source << "    movl " << rs * 4 << "(%r12), %eax\n";
                source << "    cmpl " << rt * 4 << "(%r12), %eax\n";
                if (opcode == 0x04) source << "    je p" << std::hex << target << std::dec << "\n";
                else source << "    jne p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x06 || opcode == 0x07) {
                const std::int32_t displacement =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
                const std::uint32_t target =
                    static_cast<std::uint32_t>(static_cast<std::int64_t>(address) + 4 + displacement);
                const std::uint32_t block_end = initial_pc + block_words * 4;
                if (target < initial_pc || target >= block_end) {
                    throw std::runtime_error("unsupported native MIPS branch outside lowered block");
                }
                source << "    cmpl $0, " << rs * 4 << "(%r12)\n";
                if (opcode == 0x06) source << "    jle p" << std::hex << target << std::dec << "\n";
                else source << "    jg p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x02 || opcode == 0x03) {
                const std::uint32_t target = ((address + 4) & 0xF0000000u)
                    | ((instruction & 0x03FFFFFFu) << 2);
                if (target < load_address || target >= load_address + file_size) {
                    throw std::runtime_error(
                        "unsupported native MIPS jump outside executable payload at 0x"
                        + [] (std::uint32_t value) {
                            char output[9]{};
                            std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                            return std::string(output);
                        }(address));
                }
                if (opcode == 0x03) {
                    source << "    movl $" << address + 8 << ", " << 31 * 4 << "(%r12)\n";
                    if (first_external_jal_target == 0) {
                        first_external_jal_target = target;
                    }
                    source << "    call p" << std::hex << target << std::dec << "\n";
                } else {
                    source << "    jmp p" << std::hex << target << std::dec << "\n";
                }
            } else {
                throw std::runtime_error(
                    "unsupported native MIPS opcode at 0x"
                    + [] (std::uint32_t value) {
                        char output[9]{};
                        std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                        return std::string(output);
                    }(address)
                    + ": 0x"
                    + [] (std::uint32_t value) {
                        char output[3]{};
                        std::snprintf(output, sizeof(output), "%02X", static_cast<unsigned>(value));
                        return std::string(output);
                    }(opcode)
                    + "; native lowering is unsupported");
            }
        }
        if (first_external_jal_target != 0) {
            const std::uint32_t target_offset = first_external_jal_target - load_address;
            const std::uint32_t target_words =
                std::min<std::uint32_t>(256u, (file_size - target_offset) / 4u);
            for (std::uint32_t index = 0; index < target_words; ++index) {
                const std::uint32_t address = first_external_jal_target + index * 4;
                const std::uint32_t instruction =
                    read32(data, PSX_HEADER_SIZE + target_offset + index * 4);
                const std::uint32_t opcode = instruction >> 26;
                const std::uint32_t rs = (instruction >> 21) & 31u;
                const std::uint32_t rt = (instruction >> 16) & 31u;
                const std::uint32_t rd = (instruction >> 11) & 31u;
                const std::uint32_t shamt = (instruction >> 6) & 31u;
                const std::uint32_t immediate = instruction & 0xFFFFu;
                const std::uint32_t special = instruction & 63u;
                source << "p" << std::hex << address << std::dec << ":\n";
                if (instruction == 0) continue;
                if (opcode == 0 && special == 0x00) {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shll $" << shamt << ", %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x04) {
                    source << "    movl " << rs * 4 << "(%r12), %ecx\n";
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shll %cl, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x06) {
                    source << "    movl " << rs * 4 << "(%r12), %ecx\n";
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    shrl %cl, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x07) {
                    source << "    movl " << rs * 4 << "(%r12), %ecx\n";
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    sarl %cl, %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x21) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    addl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x23) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    subl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x27) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    orl " << rt * 4 << "(%r12), %eax\n";
                    source << "    notl %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x03) {
                    source << "    movl " << rt * 4 << "(%r12), %eax\n";
                    source << "    sarl $" << shamt << ", %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x25) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    orl " << rt * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x08) {
                    source << "    ret\n";
                } else if (opcode == 0 && special == 0x09) {
                    source << "    movl $" << address + 8 << ", " << rd * 4 << "(%r12)\n";
                    source << "    ret\n";
                } else if (opcode == 0 && special == 0x19) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %ecx\n";
                    source << "    xorl %edx, %edx\n";
                    source << "    mull %ecx\n";
                    source << "    movl %eax, 128(%r12)\n";
                    source << "    movl %edx, 132(%r12)\n";
                } else if (opcode == 0 && special == 0x18) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %ecx\n";
                    source << "    cltd\n";
                    source << "    imull %ecx\n";
                    source << "    movl %eax, 128(%r12)\n";
                    source << "    movl %edx, 132(%r12)\n";
                } else if (opcode == 0 && special == 0x11) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, 132(%r12)\n";
                } else if (opcode == 0 && special == 0x13) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl %eax, 128(%r12)\n";
                } else if (opcode == 0 && (special == 0x1A || special == 0x1B)) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %ecx\n";
                    if (special == 0x1B) {
                        source << "    xorl %edx, %edx\n";
                        source << "    divl %ecx\n";
                    } else {
                        source << "    cltd\n";
                        source << "    idivl %ecx\n";
                    }
                    source << "    movl %eax, 128(%r12)\n";
                    source << "    movl %edx, 132(%r12)\n";
                } else if (opcode == 0 && special == 0x12) {
                    source << "    movl 128(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && special == 0x10) {
                    source << "    movl 132(%r12), %eax\n";
                    source << "    movl %eax, " << rd * 4 << "(%r12)\n";
                } else if (opcode == 0 && (special == 0x0D || special == 0x0C)) {
                    source << "    xor %edi, %edi\n";
                    source << "    mov $60, %eax\n";
                    source << "    syscall\n";
                } else if (opcode == 0x0F) {
                    source << "    movl $" << (immediate << 16) << ", " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x09 || opcode == 0x08) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                           << ", %eax\n";
                    source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x0D) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    orl $" << immediate << ", %eax\n";
                    source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x23) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                           << ", %eax\n";
                    source << "    andl $0x1FFFFF, %eax\n";
                    source << "    leaq ram(%rip), %rbx\n";
                    source << "    movl (%rbx,%rax), %edx\n";
                    source << "    movl %edx, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x2B) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    addl $" << static_cast<std::int32_t>(static_cast<std::int16_t>(immediate))
                           << ", %eax\n";
                    source << "    andl $0x1FFFFF, %eax\n";
                    source << "    movl " << rt * 4 << "(%r12), %edx\n";
                    source << "    leaq ram(%rip), %rbx\n";
                    source << "    movl %edx, (%rbx,%rax)\n";
                } else if (opcode == 0x0B) {
                    source << "    movl " << rs * 4 << "(%r12), %eax\n";
                    source << "    cmpl $" << static_cast<std::uint32_t>(static_cast<std::uint16_t>(immediate))
                           << ", %eax\n";
                    source << "    setb %al\n";
                    source << "    movzbl %al, %eax\n";
                    source << "    movl %eax, " << rt * 4 << "(%r12)\n";
                } else if (opcode == 0x03) {
                    source << "    xor %edi, %edi\n";
                    source << "    mov $60, %eax\n";
                    source << "    syscall\n";
                } else if (opcode == 0x02) {
                    source << "    xor %edi, %edi\n";
                    source << "    mov $60, %eax\n";
                    source << "    syscall\n";
                } else {
                    throw std::runtime_error(
                        "unsupported native MIPS instruction in JAL target at 0x"
                        + [] (std::uint32_t value) {
                            char output[9]{};
                            std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                            return std::string(output);
                        }(address));
                }
            }
        }
        source << "    leaq diag_completed(%rip), %rsi\n";
        source << "    mov $" << completed.size() << ", %rdx\n";
        source << "    call print_diag\n";
        source << "    xor %edi, %edi\n";
        source << "    mov $60, %eax\n";
        source << "    syscall\n";
        if (!source) {
            throw std::runtime_error("cannot create native lowering assembly");
        }
    }

    std::string command = "gcc -nostdlib -no-pie -Wl,-e,_start -o \""
        + options.output + "\" \"" + assembly.string() + "\"";
    const int status = std::system(command.c_str());
    std::filesystem::remove(payload);
    if (status != 0) {
        throw std::runtime_error("native Linux lowering toolchain failed");
    }
    std::filesystem::permissions(
        options.output,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add
    );
}

}

bool relink(const RelinkOptions& options)
{
    if (options.input.empty()) {
        throw std::invalid_argument("input path is empty");
    }
    RelinkOptions resolved = options;
    const std::filesystem::path input_path(options.input);
    resolved.input = resolve_relink_input(options).string();
    if (resolved.output.empty()) {
        std::filesystem::path output_root;
        if (std::filesystem::is_directory(input_path)) {
            output_root = input_path;
        } else {
            output_root = input_path.parent_path();
            if (output_root.empty()) {
                output_root = ".";
            }
        }
        resolved.output = (output_root
            / (options.target == TargetPlatform::Windows ? "game.exe" : "game.elf")).string();
    }

    create_output_parent(resolved.output);

    std::ifstream input(resolved.input, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input PS-X EXE: " + resolved.input);
    }
    const std::vector<unsigned char> data{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (data.size() < PSX_HEADER_SIZE) {
        throw std::runtime_error("PS-X EXE is shorter than its header");
    }
    if (data.size() < 8 || std::memcmp(data.data(), "PS-X EXE", 8) != 0) {
        throw std::runtime_error("input is not a PS-X EXE");
    }

    const std::uint32_t initial_pc = read32(data, 0x10);
    const std::uint32_t initial_gp = read32(data, 0x14);
    const std::uint32_t load_address = read32(data, 0x18);
    const std::uint32_t file_size = read32(data, 0x1C);
    const std::uint64_t code_end = static_cast<std::uint64_t>(PSX_HEADER_SIZE) + file_size;
    if (code_end > data.size()) {
        throw std::runtime_error("PS-X EXE declared size exceeds available input");
    }
    if (load_address < 0x80000000 || load_address >= 0x80200000) {
        throw std::runtime_error("PS-X EXE load address is outside PS1 KSEG0");
    }
    if (initial_pc != 0 && (initial_pc < load_address || initial_pc >= load_address + file_size)) {
        throw std::runtime_error("PS-X EXE entry point is outside the executable image");
    }

    if (resolved.target == TargetPlatform::Windows) {
        write_pe_layout(resolved, data, load_address, file_size);
        return true;
    }

    try {
        write_linux_native_cfg(resolved, data);
    } catch (...) {
        std::filesystem::remove(resolved.output + ".native.s");
        std::filesystem::remove(resolved.output + ".native.bin");
        throw;
    }
    return true;
}

}
