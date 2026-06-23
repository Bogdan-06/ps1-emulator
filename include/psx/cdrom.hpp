#pragma once

#include "psx/disc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace psx {

class Cdrom {
public:
    explicit Cdrom(std::optional<Disc> disc = std::nullopt);

    [[nodiscard]] std::uint8_t read(std::uint32_t address);
    void write(std::uint32_t address, std::uint8_t value);
    [[nodiscard]] std::uint32_t read_dma_word();
    [[nodiscard]] bool interrupt_requested() const noexcept;
    [[nodiscard]] bool has_disc() const noexcept;

private:
    struct PendingInterrupt {
        std::uint8_t flag;
        std::vector<std::uint8_t> response;
        bool load_sector = false;
    };

    void execute_command(std::uint8_t command);
    void queue_interrupt(
        std::uint8_t flag,
        std::vector<std::uint8_t> response,
        bool load_sector = false);
    void activate_next_interrupt();
    void acknowledge_interrupt(std::uint8_t value);
    void load_sector();

    [[nodiscard]] std::uint8_t status_register() const noexcept;
    [[nodiscard]] std::uint8_t drive_status() const noexcept;
    [[nodiscard]] std::uint8_t pop_parameter();
    [[nodiscard]] static std::uint8_t from_bcd(std::uint8_t value) noexcept;
    [[nodiscard]] static std::uint8_t to_bcd(std::uint32_t value) noexcept;

    std::optional<Disc> disc_;
    std::uint8_t index_ = 0;
    std::uint8_t interrupt_enable_ = 0;
    std::uint8_t interrupt_flag_ = 0;
    std::uint8_t mode_ = 0;
    std::deque<std::uint8_t> parameters_;
    std::deque<std::uint8_t> responses_;
    std::deque<PendingInterrupt> pending_interrupts_;
    std::array<std::uint8_t, Disc::raw_sector_size> data_{};
    std::size_t data_size_ = 0;
    std::size_t data_position_ = 0;
    std::uint32_t current_lba_ = 0;
    bool request_data_ = false;
    bool reading_ = false;
};

}  // namespace psx
