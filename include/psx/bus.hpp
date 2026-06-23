#pragma once

#include "psx/bios.hpp"
#include "psx/cdrom.hpp"
#include "psx/disc.hpp"
#include "psx/gpu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

    explicit Bus(Bios bios, std::optional<Disc> disc = std::nullopt);

    [[nodiscard]] std::uint8_t load8(std::uint32_t address) const;
    [[nodiscard]] std::uint16_t load16(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t load32(std::uint32_t address) const;

    void store8(std::uint32_t address, std::uint8_t value);
    void store16(std::uint32_t address, std::uint16_t value);
    void store32(std::uint32_t address, std::uint32_t value);
    void tick(std::uint32_t cpu_cycles);

    [[nodiscard]] const Gpu& gpu() const noexcept;
    [[nodiscard]] Gpu& gpu() noexcept;
    [[nodiscard]] bool interrupt_pending() const noexcept;

private:
    struct DmaChannel {
        std::uint32_t base = 0;
        std::uint32_t block = 0;
        std::uint32_t control = 0;
    };

    [[nodiscard]] static std::uint32_t physical_address(std::uint32_t address) noexcept;
    static void require_alignment(std::uint32_t address, std::uint32_t alignment);
    [[nodiscard]] std::uint32_t load_dma(std::uint32_t address) const;
    void store_dma(std::uint32_t address, std::uint32_t value);
    void execute_dma(std::size_t channel);
    void execute_gpu_dma(DmaChannel& channel);
    void execute_otc_dma(DmaChannel& channel);
    void complete_dma(std::size_t channel);
    [[nodiscard]] std::uint32_t read_ram_word(std::uint32_t address) const;
    void write_ram_word(std::uint32_t address, std::uint32_t value);

    Bios bios_;
    std::array<std::uint8_t, ram_size> ram_{};
    std::array<std::uint8_t, scratchpad_size> scratchpad_{};
    std::array<std::uint8_t, io_size> io_{};
    mutable Gpu gpu_;
    mutable Cdrom cdrom_;
    std::array<DmaChannel, 7> dma_channels_{};
    std::uint32_t dma_control_ = 0x0765'4321;
    std::uint32_t dma_interrupt_ = 0;
    std::uint16_t interrupt_status_ = 0;
    std::uint16_t interrupt_mask_ = 0;
    std::uint32_t cache_control_ = 0;
};

}  // namespace psx
