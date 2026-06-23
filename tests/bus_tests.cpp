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

    bus.store32(0x1f80'1810, 0x0300'0000);
    expect(bus.load32(0x1f80'1810) == 0x0300'0000, "I/O registers should retain values");

    bus.store32(0xfffe'0130, 0x0000'0804);
    expect(bus.load32(0xfffe'0130) == 0x0000'0804, "cache control should retain values");

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
