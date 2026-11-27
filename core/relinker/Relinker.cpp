#include "Relinker.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

namespace {

constexpr std::uint32_t PSX_HEADER_SIZE = 2048;

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
        source << ".text\n";
        source << ".section .bss\n";
        source << ".p2align 4\n";
        source << "regs:\n";
        source << ".skip 136\n";
        source << "ram:\n";
        source << ".skip 2097152\n";
        source << ".text\n";
        source << ".globl _start\n";
        source << "_start:\n";
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
    std::filesystem::remove(assembly);
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

    write_linux_native(resolved, data);
    return true;
}

}
