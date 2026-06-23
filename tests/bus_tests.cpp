#include "psx/bios.hpp"
#include "psx/bus.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

void expect(bool condition, std::string_view message);

void run_bus_tests() {
    std::vector<std::uint8_t> bios_bytes(psx::Bios::size);
    bios_bytes[0] = 0x78;
    bios_bytes[1] = 0x56;
    bios_bytes[2] = 0x34;
    bios_bytes[3] = 0x12;
    psx::Bus bus{psx::Bios{std::move(bios_bytes)}};

    expect(bus.load32(0xbfc0'0000) == 0x1234'5678, "reset vector should map to BIOS");

    bus.store32(0x0000'0100, 0xdead'beef);
    expect(bus.load32(0x8000'0100) == 0xdead'beef, "KSEG0 should map to RAM");
    expect(bus.load32(0xa000'0100) == 0xdead'beef, "KSEG1 should map to RAM");
    expect(bus.load32(0x0020'0100) == 0xdead'beef, "RAM mirrors should wrap");

    bus.store16(0x1f80'0000, 0xabcd);
    expect(bus.load16(0x1f80'0000) == 0xabcd, "scratchpad should retain values");

    bus.store32(0x1f80'1810, 0x0200'00ff);
    bus.store32(0x1f80'1810, 0x0014'0010);
    bus.store32(0x1f80'1810, 0x0002'0003);
    expect(bus.gpu().pixel(16, 20) == 0x001f, "GP0 writes should reach the GPU");

    bus.store32(0x1f80'1814, 0x0300'0000);
    expect(
        (bus.load32(0x1f80'1814) & (1U << 23)) == 0,
        "GP1 writes and status reads should reach the GPU");

    bus.store32(0xfffe'0130, 0x0000'0804);
    expect(bus.load32(0xfffe'0130) == 0x0000'0804, "cache control should retain values");

    expect(bus.load8(0x1f00'0084) == 0xff, "absent expansion should return an open byte bus");
    expect(bus.load16(0x1f00'0084) == 0xffff, "absent expansion should return an open halfword bus");
    expect(bus.load32(0x1f00'0084) == 0xffff'ffff, "absent expansion should return an open word bus");
    bus.store32(0x1f00'0084, 0x1234'5678);
    expect(bus.load32(0x1f00'0084) == 0xffff'ffff, "absent expansion should ignore writes");

    bus.store32(0x1f80'10e0, 0x0000'010c);
    bus.store32(0x1f80'10e4, 4);
    bus.store32(0x1f80'10e8, 0x1100'0002);
    expect(bus.load32(0x0000'010c) == 0x0000'0108, "OTC should link to the previous word");
    expect(bus.load32(0x0000'0108) == 0x0000'0104, "OTC should build a descending list");
    expect(bus.load32(0x0000'0100) == 0x00ff'ffff, "OTC should terminate the list");
    expect(
        (bus.load32(0x1f80'10e8) & (1U << 24)) == 0,
        "OTC completion should clear the DMA start bit");

    bus.store32(0x0000'0200, 0x0200'00ff);
    bus.store32(0x0000'0204, 0x003c'0030);
    bus.store32(0x0000'0208, 0x0001'0001);
    bus.store32(0x1f80'10a0, 0x0000'0200);
    bus.store32(0x1f80'10a4, 3);
    bus.store32(0x1f80'10a8, 0x1100'0001);
    expect(bus.gpu().pixel(48, 60) == 0x001f, "GPU DMA should deliver GP0 command words");
    expect(
        (bus.load32(0x1f80'10a8) & (1U << 24)) == 0,
        "GPU DMA completion should clear the start bit");

    bus.store16(0x1f80'1074, 1);
    bus.tick(psx::Gpu::cpu_cycles_per_frame);
    expect((bus.load16(0x1f80'1070) & 1U) != 0, "VBlank should set interrupt status");
    expect(bus.interrupt_pending(), "enabled VBlank should request a CPU interrupt");
    bus.store16(0x1f80'1070, 0);
    expect(!bus.interrupt_pending(), "interrupt acknowledgement should clear the request");

    bool rejected_unaligned = false;
    try {
        static_cast<void>(bus.load32(0x0000'0002));
    } catch (const psx::BusError&) {
        rejected_unaligned = true;
    }
    expect(rejected_unaligned, "unaligned word access should fail");

    bool rejected_bios_write = false;
    try {
        bus.store8(0xbfc0'0000, 0);
    } catch (const psx::BusError&) {
        rejected_bios_write = true;
    }
    expect(rejected_bios_write, "BIOS should be read-only");
}
