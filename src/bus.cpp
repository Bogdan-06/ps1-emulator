#include "psx/bus.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace psx {
namespace {

constexpr std::uint32_t ram_mirror_end = 0x0080'0000;
constexpr std::uint32_t scratchpad_start = 0x1f80'0000;
constexpr std::uint32_t io_start = 0x1f80'1000;
constexpr std::uint32_t bios_start = 0x1fc0'0000;
constexpr std::uint32_t cache_control_address = 0xfffe'0130;

[[nodiscard]] std::string hexadecimal_address(const std::uint32_t address) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << address;
    return stream.str();
}

template <typename Container>
[[nodiscard]] std::uint16_t read16(const Container& data, const std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

template <typename Container>
[[nodiscard]] std::uint32_t read32(const Container& data, const std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

template <typename Container>
void write16(Container& data, const std::size_t offset, const std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

template <typename Container>
void write32(Container& data, const std::size_t offset, const std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    data[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    data[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

}  // namespace

Bus::Bus(Bios bios)
    : bios_(std::move(bios)) {}

std::uint8_t Bus::load8(const std::uint32_t address) const {
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end) {
        return ram_[physical % ram_size];
    }
    if (physical >= scratchpad_start && physical < scratchpad_start + scratchpad_size) {
        return scratchpad_[physical - scratchpad_start];
    }
    if (physical >= io_start && physical < io_start + io_size) {
        return io_[physical - io_start];
    }
    if (physical >= bios_start && physical < bios_start + Bios::size) {
        return bios_.bytes()[physical - bios_start];
    }
    if (address >= cache_control_address && address < cache_control_address + 4) {
        const auto shift = static_cast<unsigned>((address - cache_control_address) * 8);
        return static_cast<std::uint8_t>(cache_control_ >> shift);
    }

    throw BusError{"unmapped 8-bit read at " + hexadecimal_address(address)};
}

std::uint16_t Bus::load16(const std::uint32_t address) const {
    require_alignment(address, 2);
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end - 1) {
        return read16(ram_, physical % ram_size);
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 1) {
        return read16(scratchpad_, physical - scratchpad_start);
    }
    if (physical >= io_start && physical < io_start + io_size - 1) {
        return read16(io_, physical - io_start);
    }
    if (physical >= bios_start && physical < bios_start + Bios::size - 1) {
        return read16(bios_.bytes(), physical - bios_start);
    }
    if (address == cache_control_address || address == cache_control_address + 2) {
        const auto shift = static_cast<unsigned>((address - cache_control_address) * 8);
        return static_cast<std::uint16_t>(cache_control_ >> shift);
    }

    throw BusError{"unmapped 16-bit read at " + hexadecimal_address(address)};
}

std::uint32_t Bus::load32(const std::uint32_t address) const {
    require_alignment(address, 4);
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end - 3) {
        return read32(ram_, physical % ram_size);
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 3) {
        return read32(scratchpad_, physical - scratchpad_start);
    }
    if (physical >= io_start && physical < io_start + io_size - 3) {
        return read32(io_, physical - io_start);
    }
    if (physical >= bios_start && physical < bios_start + Bios::size - 3) {
        return read32(bios_.bytes(), physical - bios_start);
    }
    if (address == cache_control_address) {
        return cache_control_;
    }

    throw BusError{"unmapped 32-bit read at " + hexadecimal_address(address)};
}

void Bus::store8(const std::uint32_t address, const std::uint8_t value) {
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end) {
        ram_[physical % ram_size] = value;
        return;
    }
    if (physical >= scratchpad_start && physical < scratchpad_start + scratchpad_size) {
        scratchpad_[physical - scratchpad_start] = value;
        return;
    }
    if (physical >= io_start && physical < io_start + io_size) {
        io_[physical - io_start] = value;
        return;
    }
    if (address >= cache_control_address && address < cache_control_address + 4) {
        const auto shift = static_cast<unsigned>((address - cache_control_address) * 8);
        const auto mask = static_cast<std::uint32_t>(0xffU << shift);
        cache_control_ = (cache_control_ & ~mask) | (static_cast<std::uint32_t>(value) << shift);
        return;
    }

    throw BusError{"unmapped 8-bit write at " + hexadecimal_address(address)};
}

void Bus::store16(const std::uint32_t address, const std::uint16_t value) {
    require_alignment(address, 2);
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end - 1) {
        write16(ram_, physical % ram_size, value);
        return;
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 1) {
        write16(scratchpad_, physical - scratchpad_start, value);
        return;
    }
    if (physical >= io_start && physical < io_start + io_size - 1) {
        write16(io_, physical - io_start, value);
        return;
    }
    if (address == cache_control_address || address == cache_control_address + 2) {
        const auto shift = static_cast<unsigned>((address - cache_control_address) * 8);
        const auto mask = static_cast<std::uint32_t>(0xffffU << shift);
        cache_control_ = (cache_control_ & ~mask) | (static_cast<std::uint32_t>(value) << shift);
        return;
    }

    throw BusError{"unmapped 16-bit write at " + hexadecimal_address(address)};
}

void Bus::store32(const std::uint32_t address, const std::uint32_t value) {
    require_alignment(address, 4);
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end - 3) {
        write32(ram_, physical % ram_size, value);
        return;
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 3) {
        write32(scratchpad_, physical - scratchpad_start, value);
        return;
    }
    if (physical >= io_start && physical < io_start + io_size - 3) {
        write32(io_, physical - io_start, value);
        return;
    }
    if (address == cache_control_address) {
        cache_control_ = value;
        return;
    }

    throw BusError{"unmapped 32-bit write at " + hexadecimal_address(address)};
}

std::uint32_t Bus::physical_address(const std::uint32_t address) noexcept {
    constexpr std::array<std::uint32_t, 8> region_masks{
        0xffff'ffff,
        0xffff'ffff,
        0xffff'ffff,
        0xffff'ffff,
        0x7fff'ffff,
        0x1fff'ffff,
        0xffff'ffff,
        0xffff'ffff,
    };

    return address & region_masks[address >> 29];
}

void Bus::require_alignment(const std::uint32_t address, const std::uint32_t alignment) {
    if (address % alignment != 0) {
        throw BusError{
            "unaligned access at " + hexadecimal_address(address)
            + " (alignment " + std::to_string(alignment) + ")"};
    }
}

}  // namespace psx
