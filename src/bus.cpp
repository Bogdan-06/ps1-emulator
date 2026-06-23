#include "psx/bus.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace psx {
namespace {

constexpr std::uint32_t ram_mirror_end = 0x0080'0000;
constexpr std::uint32_t expansion1_start = 0x1f00'0000;
constexpr std::uint32_t expansion1_size = 8 * 1024 * 1024;
constexpr std::uint32_t scratchpad_start = 0x1f80'0000;
constexpr std::uint32_t io_start = 0x1f80'1000;
constexpr std::uint32_t interrupt_status_address = 0x1f80'1070;
constexpr std::uint32_t interrupt_mask_address = 0x1f80'1074;
constexpr std::uint32_t dma_start = 0x1f80'1080;
constexpr std::uint32_t dma_end = 0x1f80'10f8;
constexpr std::uint32_t timer_start = 0x1f80'1100;
constexpr std::uint32_t timer_end = 0x1f80'1130;
constexpr std::uint32_t gpu_data_address = 0x1f80'1810;
constexpr std::uint32_t gpu_status_address = 0x1f80'1814;
constexpr std::uint32_t cdrom_start = 0x1f80'1800;
constexpr std::uint32_t cdrom_end = 0x1f80'1804;
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

Bus::Bus(Bios bios, std::optional<Disc> disc)
    : bios_(std::move(bios)),
      cdrom_(std::move(disc)) {}

std::uint8_t Bus::load8(const std::uint32_t address) const {
    const auto physical = physical_address(address);

    if (physical < ram_mirror_end) {
        return ram_[physical % ram_size];
    }
    if (physical >= expansion1_start
        && physical < expansion1_start + expansion1_size) {
        return 0xff;
    }
    if (physical >= scratchpad_start && physical < scratchpad_start + scratchpad_size) {
        return scratchpad_[physical - scratchpad_start];
    }
    if (physical >= cdrom_start && physical < cdrom_end) {
        return cdrom_.read(physical);
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
    if (physical >= expansion1_start
        && physical < expansion1_start + expansion1_size - 1) {
        return 0xffff;
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 1) {
        return read16(scratchpad_, physical - scratchpad_start);
    }
    if (physical == interrupt_status_address) {
        return interrupt_status_;
    }
    if (physical == interrupt_mask_address) {
        return interrupt_mask_;
    }
    if (physical >= timer_start && physical < timer_end) {
        return load_timer(physical);
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
    if (physical >= expansion1_start
        && physical < expansion1_start + expansion1_size - 3) {
        return 0xffff'ffff;
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 3) {
        return read32(scratchpad_, physical - scratchpad_start);
    }
    if (physical == interrupt_status_address) {
        return interrupt_status_;
    }
    if (physical == interrupt_mask_address) {
        return interrupt_mask_;
    }
    if (physical >= timer_start && physical < timer_end) {
        return load_timer(physical);
    }
    if (physical == gpu_data_address) {
        return gpu_.read_data();
    }
    if (physical == gpu_status_address) {
        return gpu_.read_status();
    }
    if (physical >= dma_start && physical < dma_end) {
        return load_dma(physical);
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
    if (physical >= expansion1_start
        && physical < expansion1_start + expansion1_size) {
        return;
    }
    if (physical >= scratchpad_start && physical < scratchpad_start + scratchpad_size) {
        scratchpad_[physical - scratchpad_start] = value;
        return;
    }
    if (physical >= cdrom_start && physical < cdrom_end) {
        cdrom_.write(physical, value);
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
    if (physical >= expansion1_start
        && physical < expansion1_start + expansion1_size - 1) {
        return;
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 1) {
        write16(scratchpad_, physical - scratchpad_start, value);
        return;
    }
    if (physical == interrupt_status_address) {
        interrupt_status_ &= value;
        return;
    }
    if (physical == interrupt_mask_address) {
        interrupt_mask_ = static_cast<std::uint16_t>(value & 0x07ffU);
        return;
    }
    if (physical >= timer_start && physical < timer_end) {
        store_timer(physical, value);
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
    if (physical >= expansion1_start
        && physical < expansion1_start + expansion1_size - 3) {
        return;
    }
    if (physical >= scratchpad_start
        && physical < scratchpad_start + scratchpad_size - 3) {
        write32(scratchpad_, physical - scratchpad_start, value);
        return;
    }
    if (physical == interrupt_status_address) {
        interrupt_status_ &= static_cast<std::uint16_t>(value);
        return;
    }
    if (physical == interrupt_mask_address) {
        interrupt_mask_ = static_cast<std::uint16_t>(value & 0x07ffU);
        return;
    }
    if (physical >= timer_start && physical < timer_end) {
        store_timer(physical, static_cast<std::uint16_t>(value));
        return;
    }
    if (physical == gpu_data_address) {
        gpu_.write_gp0(value);
        return;
    }
    if (physical == gpu_status_address) {
        gpu_.write_gp1(value);
        return;
    }
    if (physical >= dma_start && physical < dma_end) {
        store_dma(physical, value);
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

void Bus::tick(const std::uint32_t cpu_cycles) {
    if (gpu_.tick(cpu_cycles)) {
        interrupt_status_ |= 1U << 0;
    }
    if (cdrom_.interrupt_requested()) {
        interrupt_status_ |= 1U << 2;
    }

    tick_timer(0, cpu_cycles);

    auto& timer1 = timers_[1];
    timer1.divider_cycles += cpu_cycles;
    if (timer1.divider_cycles >= 2'146) {
        const auto ticks = timer1.divider_cycles / 2'146;
        timer1.divider_cycles %= 2'146;
        tick_timer(1, ticks);
    }

    auto& timer2 = timers_[2];
    if ((timer2.mode & (1U << 9)) != 0) {
        timer2.divider_cycles += cpu_cycles;
        const auto ticks = timer2.divider_cycles / 8;
        timer2.divider_cycles %= 8;
        tick_timer(2, ticks);
    } else {
        tick_timer(2, cpu_cycles);
    }
}

const Gpu& Bus::gpu() const noexcept {
    return gpu_;
}

Gpu& Bus::gpu() noexcept {
    return gpu_;
}

bool Bus::interrupt_pending() const noexcept {
    return (interrupt_status_ & interrupt_mask_) != 0;
}

std::uint32_t Bus::load_dma(const std::uint32_t address) const {
    if (address == 0x1f80'10f0) {
        return dma_control_;
    }
    if (address == 0x1f80'10f4) {
        return dma_interrupt_;
    }

    const auto channel_index = static_cast<std::size_t>((address - dma_start) >> 4);
    if (channel_index >= dma_channels_.size()) {
        return 0;
    }

    const auto register_index = (address >> 2) & 3U;
    const auto& channel = dma_channels_[channel_index];
    switch (register_index) {
    case 0:
        return channel.base;
    case 1:
        return channel.block;
    case 2:
        return channel.control;
    default:
        return 0;
    }
}

void Bus::store_dma(const std::uint32_t address, const std::uint32_t value) {
    if (address == 0x1f80'10f0) {
        dma_control_ = value;
        return;
    }
    if (address == 0x1f80'10f4) {
        const auto acknowledge = (value >> 24) & 0x7fU;
        dma_interrupt_ &= ~(acknowledge << 24);
        dma_interrupt_ = (dma_interrupt_ & 0xff00'0000U) | (value & 0x00ff'803fU);
        const bool force_irq = (dma_interrupt_ & (1U << 15)) != 0;
        const bool master_enabled = (dma_interrupt_ & (1U << 23)) != 0;
        const auto enabled = (dma_interrupt_ >> 16) & 0x7fU;
        const auto flags = (dma_interrupt_ >> 24) & 0x7fU;
        if (force_irq || (master_enabled && (enabled & flags) != 0)) {
            dma_interrupt_ |= 1U << 31;
        } else {
            dma_interrupt_ &= ~(1U << 31);
        }
        return;
    }

    const auto channel_index = static_cast<std::size_t>((address - dma_start) >> 4);
    if (channel_index >= dma_channels_.size()) {
        return;
    }

    auto& channel = dma_channels_[channel_index];
    switch ((address >> 2) & 3U) {
    case 0:
        channel.base = value & 0x00ff'ffffU;
        return;
    case 1:
        channel.block = value;
        return;
    case 2:
        channel.control = value & 0x7177'0703U;
        execute_dma(channel_index);
        return;
    default:
        return;
    }
}

void Bus::execute_dma(const std::size_t channel_index) {
    auto& channel = dma_channels_[channel_index];
    const auto synchronization = (channel.control >> 9) & 3U;
    const bool started = (channel.control & (1U << 24)) != 0;
    const bool triggered = (channel.control & (1U << 28)) != 0;
    if (!started || (synchronization == 0 && !triggered)) {
        return;
    }

    switch (channel_index) {
    case 2:
        execute_gpu_dma(channel);
        break;
    case 3: {
        auto words = channel.block & 0xffffU;
        if (synchronization == 1) {
            const auto blocks = std::max(1U, channel.block >> 16);
            words = std::max(1U, words) * blocks;
        } else {
            words = std::max(1U, words);
        }
        auto address = channel.base & 0x001f'fffcU;
        for (std::uint32_t word = 0; word < words; ++word) {
            write_ram_word(address, cdrom_.read_dma_word());
            address = (address + 4) & 0x001f'fffcU;
        }
        channel.base = address;
        break;
    }
    case 6:
        execute_otc_dma(channel);
        break;
    default:
        break;
    }

    channel.control &= ~((1U << 24) | (1U << 28));
    complete_dma(channel_index);
}

void Bus::execute_gpu_dma(DmaChannel& channel) {
    const bool from_ram = (channel.control & 1U) != 0;
    const bool decrement = (channel.control & 2U) != 0;
    const auto synchronization = (channel.control >> 9) & 3U;
    auto address = channel.base & 0x001f'fffcU;

    if (synchronization == 2) {
        if (!from_ram) {
            return;
        }

        std::size_t nodes = 0;
        while (nodes++ < 1'000'000) {
            const auto header = read_ram_word(address);
            const auto word_count = header >> 24;
            for (std::uint32_t word = 0; word < word_count; ++word) {
                address = (address + 4) & 0x001f'fffcU;
                gpu_.write_gp0(read_ram_word(address));
            }

            if ((header & 0x00ff'ffffU) == 0x00ff'ffffU) {
                break;
            }
            address = header & 0x001f'fffcU;
        }
        channel.base = address;
        return;
    }

    std::uint32_t word_count = channel.block & 0xffffU;
    if (synchronization == 1) {
        auto block_count = channel.block >> 16;
        block_count = block_count == 0 ? 0x1'0000U : block_count;
        const auto block_size = word_count == 0 ? 0x1'0000U : word_count;
        word_count = block_size * block_count;
    } else if (word_count == 0) {
        word_count = 0x1'0000U;
    }

    const auto step = decrement ? static_cast<std::uint32_t>(-4) : 4U;
    for (std::uint32_t word = 0; word < word_count; ++word) {
        if (from_ram) {
            gpu_.write_gp0(read_ram_word(address));
        } else {
            write_ram_word(address, gpu_.read_data());
        }
        address = (address + step) & 0x001f'fffcU;
    }
    channel.base = address;
}

void Bus::execute_otc_dma(DmaChannel& channel) {
    auto address = channel.base & 0x001f'fffcU;
    auto word_count = channel.block & 0xffffU;
    word_count = word_count == 0 ? 0x1'0000U : word_count;

    for (std::uint32_t word = 0; word < word_count; ++word) {
        const auto value = word + 1 == word_count
            ? 0x00ff'ffffU
            : (address - 4) & 0x001f'ffffU;
        write_ram_word(address, value);
        address = (address - 4) & 0x001f'fffcU;
    }
    channel.base = address;
}

void Bus::complete_dma(const std::size_t channel) {
    dma_interrupt_ |= 1U << (24 + channel);
    const bool master_enabled = (dma_interrupt_ & (1U << 23)) != 0;
    const auto enabled = (dma_interrupt_ >> 16) & 0x7fU;
    const auto flags = (dma_interrupt_ >> 24) & 0x7fU;
    if (master_enabled && (enabled & flags) != 0) {
        dma_interrupt_ |= 1U << 31;
        interrupt_status_ |= 1U << 3;
    }
}

std::uint16_t Bus::load_timer(const std::uint32_t address) const {
    const auto timer_index = static_cast<std::size_t>((address - timer_start) >> 4);
    if (timer_index >= timers_.size()) {
        return 0;
    }

    const auto register_index = (address >> 2) & 3U;
    auto& timer = timers_[timer_index];
    switch (register_index) {
    case 0:
        return static_cast<std::uint16_t>(timer.counter);
    case 1: {
        const auto value = timer.mode;
        timer.mode &= static_cast<std::uint16_t>(~0x1800U);
        return value;
    }
    case 2:
        return timer.target;
    default:
        return 0;
    }
}

void Bus::store_timer(const std::uint32_t address, const std::uint16_t value) {
    const auto timer_index = static_cast<std::size_t>((address - timer_start) >> 4);
    if (timer_index >= timers_.size()) {
        return;
    }

    auto& timer = timers_[timer_index];
    switch ((address >> 2) & 3U) {
    case 0:
        timer.counter = value;
        return;
    case 1:
        timer.mode = static_cast<std::uint16_t>(value & 0x03ffU);
        timer.counter = 0;
        timer.divider_cycles = 0;
        return;
    case 2:
        timer.target = value;
        return;
    default:
        return;
    }
}

void Bus::tick_timer(const std::size_t timer_index, const std::uint32_t ticks) {
    if (ticks == 0) {
        return;
    }

    auto& timer = timers_[timer_index];
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        ++timer.counter;
        bool interrupt = false;
        if (static_cast<std::uint16_t>(timer.counter) == timer.target) {
            timer.mode |= 1U << 11;
            interrupt = (timer.mode & (1U << 4)) != 0;
            if ((timer.mode & (1U << 3)) != 0) {
                timer.counter = 0;
            }
        }
        if (timer.counter > 0xffffU) {
            timer.counter &= 0xffffU;
            timer.mode |= 1U << 12;
            interrupt = interrupt || (timer.mode & (1U << 5)) != 0;
        }
        if (interrupt) {
            interrupt_status_ |= static_cast<std::uint16_t>(1U << (4 + timer_index));
        }
    }
}

std::uint32_t Bus::read_ram_word(const std::uint32_t address) const {
    return read32(ram_, address & (ram_size - 1));
}

void Bus::write_ram_word(const std::uint32_t address, const std::uint32_t value) {
    write32(ram_, address & (ram_size - 1), value);
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
