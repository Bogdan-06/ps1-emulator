#include "psx/cdrom.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace psx {

Cdrom::Cdrom(std::optional<Disc> disc)
    : disc_(std::move(disc)) {}

std::uint8_t Cdrom::read(const std::uint32_t address) {
    switch (address & 3U) {
    case 0:
        return status_register();
    case 1:
        if (responses_.empty()) {
            return 0;
        } else {
            const auto value = responses_.front();
            responses_.pop_front();
            return value;
        }
    case 2:
        if (!request_data_ || data_position_ >= data_size_) {
            return 0;
        } else {
            return data_[data_position_++];
        }
    case 3:
        if (index_ == 0 || index_ == 2) {
            return static_cast<std::uint8_t>(interrupt_enable_ | 0xe0U);
        }
        return static_cast<std::uint8_t>(interrupt_flag_ | 0xe0U);
    default:
        return 0;
    }
}

void Cdrom::write(const std::uint32_t address, const std::uint8_t value) {
    switch (address & 3U) {
    case 0:
        index_ = value & 3U;
        return;
    case 1:
        if (index_ == 0) {
            execute_command(value);
        }
        return;
    case 2:
        if (index_ == 0) {
            if (parameters_.size() < 16) {
                parameters_.push_back(value);
            }
        } else if (index_ == 1) {
            interrupt_enable_ = value & 0x1fU;
        }
        return;
    case 3:
        if (index_ == 0) {
            request_data_ = (value & 0x80U) != 0;
            if ((value & 0x40U) == 0) {
                data_position_ = 0;
            }
        } else if (index_ == 1) {
            acknowledge_interrupt(value);
        }
        return;
    default:
        return;
    }
}

std::uint32_t Cdrom::read_dma_word() {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(read(2)) << (byte * 8);
    }
    return value;
}

bool Cdrom::interrupt_requested() const noexcept {
    return (interrupt_enable_ & interrupt_flag_) != 0;
}

bool Cdrom::has_disc() const noexcept {
    return disc_.has_value();
}

void Cdrom::execute_command(const std::uint8_t command) {
    const auto status = drive_status();

    switch (command) {
    case 0x01:  // GetStat
        queue_interrupt(3, {status});
        break;
    case 0x02: {  // Setloc
        const auto minute = from_bcd(pop_parameter());
        const auto second = from_bcd(pop_parameter());
        const auto frame = from_bcd(pop_parameter());
        const auto absolute_frame = static_cast<std::uint32_t>(minute) * 60U * 75U
            + static_cast<std::uint32_t>(second) * 75U
            + frame;
        current_lba_ = absolute_frame >= 150 ? absolute_frame - 150 : 0;
        queue_interrupt(3, {status});
        break;
    }
    case 0x06:  // ReadN
        reading_ = disc_.has_value();
        queue_interrupt(3, {status});
        if (reading_) {
            queue_interrupt(1, {status}, true);
        }
        break;
    case 0x08:  // Stop
    case 0x09:  // Pause
        reading_ = false;
        queue_interrupt(3, {status});
        queue_interrupt(2, {status});
        break;
    case 0x0a:  // Init
        reading_ = false;
        mode_ = 0;
        queue_interrupt(3, {status});
        queue_interrupt(2, {status});
        break;
    case 0x0e:  // Setmode
        mode_ = pop_parameter();
        queue_interrupt(3, {status});
        break;
    case 0x11: {  // GetlocP
        const auto absolute = current_lba_ + 150;
        const auto minute = absolute / (60U * 75U);
        const auto second = (absolute / 75U) % 60U;
        const auto frame = absolute % 75U;
        queue_interrupt(
            3,
            {1, 1, to_bcd(minute), to_bcd(second), to_bcd(frame),
             to_bcd(minute), to_bcd(second), to_bcd(frame)});
        break;
    }
    case 0x13:  // GetTN
        queue_interrupt(3, {status, 1, 1});
        break;
    case 0x14: {  // GetTD
        const auto track = pop_parameter();
        std::uint32_t absolute = 150;
        if (track == 0 && disc_.has_value()) {
            absolute += disc_->sector_count();
        }
        queue_interrupt(
            3,
            {status, to_bcd(absolute / (60U * 75U)), to_bcd((absolute / 75U) % 60U)});
        break;
    }
    case 0x15:  // SeekL
        queue_interrupt(3, {status});
        queue_interrupt(2, {status});
        break;
    case 0x19: {  // Test
        const auto subcommand = pop_parameter();
        if (subcommand == 0x20) {
            queue_interrupt(3, {0x94, 0x09, 0x19, 0xc0});
        } else {
            queue_interrupt(5, {static_cast<std::uint8_t>(status | 1U), 0x10});
        }
        break;
    }
    case 0x1a:  // GetID
        queue_interrupt(3, {status});
        if (disc_.has_value()) {
            queue_interrupt(2, {status, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'});
        } else {
            queue_interrupt(5, {0x11, 0x80});
        }
        break;
    case 0x1e:  // ReadTOC
        queue_interrupt(3, {status});
        queue_interrupt(2, {status});
        break;
    default:
        queue_interrupt(5, {static_cast<std::uint8_t>(status | 1U), 0x40});
        break;
    }

    parameters_.clear();
}

void Cdrom::queue_interrupt(
    const std::uint8_t flag,
    std::vector<std::uint8_t> response,
    const bool load_sector_on_activation) {
    pending_interrupts_.push_back(
        PendingInterrupt{flag, std::move(response), load_sector_on_activation});
    activate_next_interrupt();
}

void Cdrom::activate_next_interrupt() {
    if (interrupt_flag_ != 0 || pending_interrupts_.empty()) {
        return;
    }

    auto pending = std::move(pending_interrupts_.front());
    pending_interrupts_.pop_front();
    if (pending.load_sector) {
        load_sector();
    }
    responses_.assign(pending.response.begin(), pending.response.end());
    interrupt_flag_ = pending.flag;
}

void Cdrom::acknowledge_interrupt(const std::uint8_t value) {
    interrupt_flag_ &= static_cast<std::uint8_t>(~value & 0x1fU);
    if ((value & 0x40U) != 0) {
        parameters_.clear();
    }
    if (interrupt_flag_ == 0) {
        if (reading_ && pending_interrupts_.empty()) {
            queue_interrupt(1, {drive_status()}, true);
        } else {
            activate_next_interrupt();
        }
    }
}

void Cdrom::load_sector() {
    data_position_ = 0;
    data_size_ = 0;
    if (!disc_.has_value() || current_lba_ >= disc_->sector_count()) {
        reading_ = false;
        return;
    }

    if ((mode_ & 0x20U) != 0) {
        const auto sector = disc_->read_raw_sector(current_lba_);
        std::copy_n(sector.begin() + 12, 2340, data_.begin());
        data_size_ = 2340;
    } else {
        const auto sector = disc_->read_data_sector(current_lba_);
        std::copy(sector.begin(), sector.end(), data_.begin());
        data_size_ = sector.size();
    }
    ++current_lba_;
}

std::uint8_t Cdrom::status_register() const noexcept {
    std::uint8_t status = index_;
    if (parameters_.empty()) {
        status |= 1U << 3;
    }
    if (parameters_.size() < 16) {
        status |= 1U << 4;
    }
    if (!responses_.empty()) {
        status |= 1U << 5;
    }
    if (request_data_ && data_position_ < data_size_) {
        status |= 1U << 6;
    }
    return status;
}

std::uint8_t Cdrom::drive_status() const noexcept {
    return disc_.has_value() ? 0x02 : 0x10;
}

std::uint8_t Cdrom::pop_parameter() {
    if (parameters_.empty()) {
        return 0;
    }
    const auto value = parameters_.front();
    parameters_.pop_front();
    return value;
}

std::uint8_t Cdrom::from_bcd(const std::uint8_t value) noexcept {
    return static_cast<std::uint8_t>((value >> 4) * 10 + (value & 0x0fU));
}

std::uint8_t Cdrom::to_bcd(const std::uint32_t value) noexcept {
    return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
}

}  // namespace psx
