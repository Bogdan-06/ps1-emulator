#include "psx/bios.hpp"
#include "psx/bus.hpp"
#include "psx/cpu.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

void expect(bool condition, std::string_view message);

namespace {

[[nodiscard]] constexpr std::uint32_t encode_i(
    const std::uint32_t opcode,
    const std::uint32_t rs,
    const std::uint32_t rt,
    const std::uint16_t immediate) {
    return (opcode << 26) | (rs << 21) | (rt << 16) | immediate;
}

[[nodiscard]] constexpr std::uint32_t encode_r(
    const std::uint32_t rs,
    const std::uint32_t rt,
    const std::uint32_t rd,
    const std::uint32_t function) {
    return (rs << 21) | (rt << 16) | (rd << 11) | function;
}

[[nodiscard]] constexpr std::uint32_t encode_j(
    const std::uint32_t opcode,
    const std::uint32_t address) {
    return (opcode << 26) | ((address >> 2) & 0x03ff'ffff);
}

void write_instruction(
    std::vector<std::uint8_t>& bios,
    const std::size_t index,
    const std::uint32_t instruction) {
    const auto offset = index * 4;
    bios[offset] = static_cast<std::uint8_t>(instruction);
    bios[offset + 1] = static_cast<std::uint8_t>(instruction >> 8);
    bios[offset + 2] = static_cast<std::uint8_t>(instruction >> 16);
    bios[offset + 3] = static_cast<std::uint8_t>(instruction >> 24);
}

}  // namespace

void run_cpu_tests() {
    {
        std::vector<std::uint8_t> bios(psx::Bios::size);

        write_instruction(bios, 0, encode_i(0x0f, 0, 8, 0x1234));  // lui t0, 0x1234
        write_instruction(bios, 1, encode_i(0x0d, 8, 8, 0x5678));  // ori t0, t0, 0x5678
        write_instruction(bios, 2, encode_i(0x09, 0, 9, 0x0100));  // addiu t1, zero, 0x100
        write_instruction(bios, 3, encode_i(0x2b, 9, 8, 0));       // sw t0, 0(t1)
        write_instruction(bios, 4, encode_i(0x23, 9, 10, 0));      // lw t2, 0(t1)
        write_instruction(bios, 5, encode_i(0x09, 10, 11, 1));     // addiu t3, t2, 1
        write_instruction(bios, 6, encode_r(10, 0, 12, 0x21));     // addu t4, t2, zero
        write_instruction(bios, 7, encode_i(0x04, 12, 8, 2));      // beq t4, t0, +2
        write_instruction(bios, 8, encode_i(0x09, 0, 13, 7));      // delay slot
        write_instruction(bios, 9, encode_i(0x09, 0, 13, 9));      // skipped
        write_instruction(bios, 10, encode_i(0x09, 0, 14, 11));    // branch target

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};

        for (int step = 0; step < 10; ++step) {
            static_cast<void>(cpu.step());
        }

        expect(cpu.register_value(8) == 0x1234'5678, "LUI and ORI should compose a word");
        expect(cpu.bus().load32(0x100) == 0x1234'5678, "SW should write RAM");
        expect(cpu.register_value(10) == 0x1234'5678, "LW should complete after one delay slot");
        expect(cpu.register_value(11) == 1, "load delay slot should observe the old value");
        expect(cpu.register_value(12) == 0x1234'5678, "instruction after delay slot should see load");
        expect(cpu.register_value(13) == 7, "taken branch should execute its delay slot");
        expect(cpu.register_value(14) == 11, "taken branch should continue at its target");
        expect(cpu.pc() == psx::Cpu::reset_vector + 44, "PC should advance through branch target");
        expect(cpu.register_value(0) == 0, "zero register must remain immutable");
    }

    {
        std::vector<std::uint8_t> bios(psx::Bios::size);

        write_instruction(bios, 0, encode_i(0x09, 0, 8, 0xfffa));  // addiu t0, zero, -6
        write_instruction(bios, 1, encode_i(0x09, 0, 9, 3));       // addiu t1, zero, 3
        write_instruction(bios, 2, encode_r(8, 9, 0, 0x18));       // mult t0, t1
        write_instruction(bios, 3, encode_r(0, 0, 10, 0x12));      // mflo t2
        write_instruction(bios, 4, encode_r(8, 9, 0, 0x1a));       // div t0, t1
        write_instruction(bios, 5, encode_r(0, 0, 11, 0x12));      // mflo t3
        write_instruction(bios, 6, encode_r(0, 0, 12, 0x10));      // mfhi t4
        write_instruction(
            bios,
            7,
            encode_j(0x03, psx::Cpu::reset_vector + 40));          // jal target
        write_instruction(bios, 8, encode_i(0x09, 0, 13, 1));      // delay slot
        write_instruction(bios, 9, encode_i(0x09, 0, 13, 9));      // skipped
        write_instruction(bios, 10, encode_i(0x09, 0, 14, 2));     // target

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};
        for (int step = 0; step < 10; ++step) {
            static_cast<void>(cpu.step());
        }

        expect(cpu.register_value(10) == 0xffff'ffee, "MULT should write signed product");
        expect(cpu.register_value(11) == 0xffff'fffe, "DIV should write signed quotient");
        expect(cpu.register_value(12) == 0, "DIV should write signed remainder");
        expect(cpu.register_value(13) == 1, "JAL should execute its delay slot");
        expect(cpu.register_value(14) == 2, "JAL should jump to its target");
        expect(
            cpu.register_value(31) == psx::Cpu::reset_vector + 36,
            "JAL should store the return address");
    }

    {
        std::vector<std::uint8_t> bios(psx::Bios::size);

        write_instruction(bios, 0, encode_i(0x0f, 0, 8, 1));       // lui t0, 1
        write_instruction(bios, 1, encode_i(0x10, 4, 8, 12 << 11));// mtc0 t0, status
        write_instruction(bios, 2, encode_i(0x09, 0, 9, 0x100));   // addiu t1, zero, 0x100
        write_instruction(bios, 3, encode_i(0x09, 0, 10, 0x55));   // addiu t2, zero, 0x55
        write_instruction(bios, 4, encode_i(0x2b, 9, 10, 0));      // sw t2, 0(t1)
        write_instruction(bios, 5, encode_i(0x10, 4, 0, 12 << 11));// mtc0 zero, status
        write_instruction(bios, 6, encode_i(0x2b, 9, 10, 0));      // sw t2, 0(t1)

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};
        for (int step = 0; step < 5; ++step) {
            static_cast<void>(cpu.step());
        }
        expect(cpu.bus().load32(0x100) == 0, "isolated cache should suppress RAM stores");

        static_cast<void>(cpu.step());
        static_cast<void>(cpu.step());
        expect(cpu.bus().load32(0x100) == 0x55, "RAM stores should resume after cache isolation");
    }

    {
        std::vector<std::uint8_t> bios(psx::Bios::size);

        write_instruction(bios, 0, encode_i(0x09, 0, 8, 0x101));   // addiu t0, zero, 0x101
        write_instruction(bios, 1, encode_i(0x0f, 0, 9, 0xaabb));   // lui t1, 0xaabb
        write_instruction(bios, 2, encode_i(0x0d, 9, 9, 0xccdd));   // ori t1, t1, 0xccdd
        write_instruction(bios, 3, encode_i(0x22, 8, 9, 3));        // lwl t1, 3(t0)
        write_instruction(bios, 4, encode_i(0x26, 8, 9, 0));        // lwr t1, 0(t0)
        write_instruction(bios, 5, 0);                              // load delay slot
        write_instruction(bios, 6, encode_i(0x2a, 8, 9, 7));        // swl t1, 7(t0)
        write_instruction(bios, 7, encode_i(0x2e, 8, 9, 4));        // swr t1, 4(t0)

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};
        cpu.bus().store8(0x101, 0x11);
        cpu.bus().store8(0x102, 0x22);
        cpu.bus().store8(0x103, 0x33);
        cpu.bus().store8(0x104, 0x44);

        for (int step = 0; step < 8; ++step) {
            static_cast<void>(cpu.step());
        }

        expect(
            cpu.register_value(9) == 0x4433'2211,
            "consecutive LWL/LWR should merge through the load delay");
        expect(cpu.bus().load8(0x105) == 0x11, "SWR should store the low byte");
        expect(cpu.bus().load8(0x106) == 0x22, "SWR should store the second byte");
        expect(cpu.bus().load8(0x107) == 0x33, "SWR should store the third byte");
        expect(cpu.bus().load8(0x108) == 0x44, "SWL should store the high byte");
    }

    {
        std::vector<std::uint8_t> bios(psx::Bios::size);
        write_instruction(bios, 0, 0x0000'000c);                   // syscall
        write_instruction(bios, 0x180 / 4, 0);                     // exception handler

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};
        static_cast<void>(cpu.step());

        expect(
            cpu.pc() == 0xbfc0'0180,
            "SYSCALL should enter the bootstrap exception vector");
        expect(
            cpu.cop0_register_value(14) == psx::Cpu::reset_vector,
            "SYSCALL should store its address in EPC");
        expect(
            ((cpu.cop0_register_value(13) >> 2) & 0x1fU) == 8,
            "SYSCALL should set the exception code");
    }

    {
        std::vector<std::uint8_t> bios(psx::Bios::size);
        write_instruction(bios, 0, encode_i(0x04, 0, 0, 1));       // beq zero, zero, +1
        write_instruction(bios, 1, 0x0000'000c);                   // syscall in delay slot
        write_instruction(bios, 0x180 / 4, 0);

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};
        static_cast<void>(cpu.step());
        static_cast<void>(cpu.step());

        expect(
            cpu.cop0_register_value(14) == psx::Cpu::reset_vector,
            "delay-slot exception EPC should point to the branch");
        expect(
            (cpu.cop0_register_value(13) & 0x8000'0000U) != 0,
            "delay-slot exception should set the Cause BD bit");
    }

    {
        std::vector<std::uint8_t> bios(psx::Bios::size);
        write_instruction(bios, 0, encode_i(0x0f, 0, 8, 0x0040));  // lui t0, 0x0040
        write_instruction(bios, 1, encode_i(0x0d, 8, 8, 0x0401));  // ori t0, t0, 0x0401
        write_instruction(bios, 2, encode_i(0x10, 4, 8, 12 << 11));// mtc0 t0, status
        write_instruction(bios, 3, 0);
        write_instruction(bios, 0x180 / 4, 0);

        psx::Cpu cpu{psx::Bus{psx::Bios{std::move(bios)}}};
        for (int step = 0; step < 4; ++step) {
            static_cast<void>(cpu.step());
        }
        cpu.bus().store16(0x1f80'1074, 1);
        cpu.bus().tick(psx::Gpu::cpu_cycles_per_frame);
        static_cast<void>(cpu.step());

        expect(
            cpu.pc() == 0xbfc0'0180,
            "enabled hardware interrupt should enter the exception vector");
        expect(
            ((cpu.cop0_register_value(13) >> 2) & 0x1fU) == 0,
            "hardware interrupt should set exception code zero");
    }
}
