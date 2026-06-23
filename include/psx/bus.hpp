#pragma once

#include "psx/bios.hpp"
#include "psx/gpu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace psx {

class BusError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Bus {
public:
    static constexpr std::size_t ram_size = 2 * 1024 * 1024;
    static constexpr std::size_t scratchpad_size = 1024;
    static constexpr std::size_t io_size = 8 * 1024;

    explicit Bus(Bios bios);

    [[nodiscard]] std::uint8_t load8(std::uint32_t address) const;
    [[nodiscard]] std::uint16_t load16(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t load32(std::uint32_t address) const;

    void store8(std::uint32_t address, std::uint8_t value);
    void store16(std::uint32_t address, std::uint16_t value);
    void store32(std::uint32_t address, std::uint32_t value);

    [[nodiscard]] const Gpu& gpu() const noexcept;
    [[nodiscard]] Gpu& gpu() noexcept;

private:
    [[nodiscard]] static std::uint32_t physical_address(std::uint32_t address) noexcept;
    static void require_alignment(std::uint32_t address, std::uint32_t alignment);

    Bios bios_;
    std::array<std::uint8_t, ram_size> ram_{};
    std::array<std::uint8_t, scratchpad_size> scratchpad_{};
    std::array<std::uint8_t, io_size> io_{};
    mutable Gpu gpu_;
    std::uint32_t cache_control_ = 0;
};

}  // namespace psx
