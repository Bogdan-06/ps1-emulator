#include "psx/gpu.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace psx {
namespace {

constexpr std::uint32_t ready_for_command = 1U << 26;
constexpr std::uint32_t ready_for_vram_to_cpu = 1U << 27;
constexpr std::uint32_t ready_for_dma = 1U << 28;
constexpr std::uint32_t display_disable = 1U << 23;
constexpr std::uint32_t dma_direction_mask = 3U << 29;

[[nodiscard]] constexpr std::int32_t sign_extend_11(const std::uint32_t value) noexcept {
    const auto truncated = static_cast<std::int32_t>(value & 0x7ffU);
    return (truncated & 0x400) != 0 ? truncated - 0x800 : truncated;
}

[[nodiscard]] constexpr std::int32_t clamp_color(const std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, 0, 255));
}

}  // namespace

Gpu::Gpu() {
    reset();
}

void Gpu::write_gp0(const std::uint32_t value) {
    if (transfer_mode_ == TransferMode::cpu_to_vram) {
        write_transfer_word(value);
        return;
    }

    if (command_size_ == 0) {
        command_words_expected_ = packet_length(static_cast<std::uint8_t>(value >> 24));
    }

    if (command_size_ >= command_.size()) {
        throw GpuError{"GPU command packet exceeds internal buffer"};
    }

    command_[command_size_++] = value;
    if (command_size_ == command_words_expected_) {
        execute_packet();
        reset_command_buffer();
    }
}

void Gpu::write_gp1(const std::uint32_t value) {
    const auto command = static_cast<std::uint8_t>(value >> 24);

    switch (command) {
    case 0x00:
        reset();
        return;
    case 0x01:
        reset_command_buffer();
        transfer_mode_ = TransferMode::command;
        transfer_pixels_remaining_ = 0;
        status_ &= ~ready_for_vram_to_cpu;
        return;
    case 0x02:
        status_ &= ~(1U << 24);
        return;
    case 0x03:
        status_ = (status_ & ~display_disable) | ((value & 1U) << 23);
        return;
    case 0x04:
        status_ = (status_ & ~dma_direction_mask) | ((value & 3U) << 29);
        return;
    case 0x05:
        display_x_ = static_cast<std::uint16_t>(value & 0x3ffU);
        display_y_ = static_cast<std::uint16_t>((value >> 10) & 0x1ffU);
        return;
    case 0x06:
        horizontal_start_ = static_cast<std::uint16_t>(value & 0xfffU);
        horizontal_end_ = static_cast<std::uint16_t>((value >> 12) & 0xfffU);
        return;
    case 0x07:
        vertical_start_ = static_cast<std::uint16_t>(value & 0x3ffU);
        vertical_end_ = static_cast<std::uint16_t>((value >> 10) & 0x3ffU);
        return;
    case 0x08:
        status_ &= ~0x007f'4000U;
        status_ |= (value & 0x3fU) << 17;
        status_ |= (value & 0x40U) << 10;
        return;
    case 0x10:
        switch (value & 0xffU) {
        case 0x02:
            read_latch_ = (static_cast<std::uint32_t>(drawing_top_) << 10)
                | drawing_left_;
            return;
        case 0x03:
            read_latch_ = (static_cast<std::uint32_t>(drawing_bottom_) << 10)
                | drawing_right_;
            return;
        case 0x04:
            read_latch_ = (static_cast<std::uint32_t>(drawing_offset_y_ & 0x7ff) << 11)
                | static_cast<std::uint32_t>(drawing_offset_x_ & 0x7ff);
            return;
        case 0x07:
            read_latch_ = 2;
            return;
        case 0x08:
            read_latch_ = 0;
            return;
        default:
            read_latch_ = 0;
            return;
        }
    default:
        return;
    }
}

std::uint32_t Gpu::read_data() {
    if (transfer_mode_ == TransferMode::vram_to_cpu) {
        return read_transfer_word();
    }
    return read_latch_;
}

std::uint32_t Gpu::read_status() const noexcept {
    return status_;
}

std::uint16_t Gpu::pixel(const std::size_t x, const std::size_t y) const {
    if (x >= vram_width || y >= vram_height) {
        throw std::out_of_range{"GPU pixel coordinates out of range"};
    }
    return vram_[y * vram_width + x];
}

std::span<const std::uint16_t> Gpu::vram() const noexcept {
    return vram_;
}

std::uint16_t Gpu::display_x() const noexcept {
    return display_x_;
}

std::uint16_t Gpu::display_y() const noexcept {
    return display_y_;
}

std::uint16_t Gpu::display_width() const noexcept {
    const auto horizontal_mode = ((status_ >> 17) & 3U) | ((status_ >> 14) & 4U);
    constexpr std::array<std::uint16_t, 8> widths{256, 320, 512, 640, 368, 320, 512, 640};
    return widths[horizontal_mode];
}

std::uint16_t Gpu::display_height() const noexcept {
    return (status_ & (1U << 19)) != 0 ? 480 : 240;
}

bool Gpu::display_disabled() const noexcept {
    return (status_ & display_disable) != 0;
}

void Gpu::reset() {
    reset_command_buffer();
    transfer_mode_ = TransferMode::command;
    transfer_pixels_remaining_ = 0;
    transfer_pixel_ = 0;
    status_ = 0x1480'2000U;
    read_latch_ = 0;
    drawing_offset_x_ = 0;
    drawing_offset_y_ = 0;
    drawing_left_ = 0;
    drawing_top_ = 0;
    drawing_right_ = 1023;
    drawing_bottom_ = 511;
    display_x_ = 0;
    display_y_ = 0;
    horizontal_start_ = 0x200;
    horizontal_end_ = 0xc00;
    vertical_start_ = 0x10;
    vertical_end_ = 0x100;
    force_mask_bit_ = false;
    preserve_masked_pixels_ = false;
}

void Gpu::reset_command_buffer() noexcept {
    command_size_ = 0;
    command_words_expected_ = 0;
}

void Gpu::execute_packet() {
    const auto opcode = static_cast<std::uint8_t>(command_[0] >> 24);

    if (opcode == 0x02) {
        fill_rectangle();
    } else if (opcode >= 0x20 && opcode <= 0x23) {
        draw_flat_polygon(false);
    } else if (opcode >= 0x28 && opcode <= 0x2b) {
        draw_flat_polygon(true);
    } else if (opcode >= 0x30 && opcode <= 0x33) {
        draw_shaded_polygon(false);
    } else if (opcode >= 0x38 && opcode <= 0x3b) {
        draw_shaded_polygon(true);
    } else if (opcode >= 0x60 && opcode <= 0x63) {
        const auto size = command_[2];
        draw_rectangle(size & 0xffffU, size >> 16);
    } else if (opcode >= 0x68 && opcode <= 0x6b) {
        draw_rectangle(1, 1);
    } else if (opcode >= 0x70 && opcode <= 0x73) {
        draw_rectangle(8, 8);
    } else if (opcode >= 0x78 && opcode <= 0x7b) {
        draw_rectangle(16, 16);
    } else if (opcode >= 0x80 && opcode <= 0x9f) {
        copy_vram();
    } else if (opcode >= 0xa0 && opcode <= 0xbf) {
        begin_cpu_to_vram();
    } else if (opcode >= 0xc0 && opcode <= 0xdf) {
        begin_vram_to_cpu();
    } else if (opcode == 0xe1) {
        status_ = (status_ & ~0x0000'07ffU) | (command_[0] & 0x0000'07ffU);
    } else if (opcode == 0xe3) {
        drawing_left_ = static_cast<std::uint16_t>(command_[0] & 0x3ffU);
        drawing_top_ = static_cast<std::uint16_t>((command_[0] >> 10) & 0x1ffU);
    } else if (opcode == 0xe4) {
        drawing_right_ = static_cast<std::uint16_t>(command_[0] & 0x3ffU);
        drawing_bottom_ = static_cast<std::uint16_t>((command_[0] >> 10) & 0x1ffU);
    } else if (opcode == 0xe5) {
        drawing_offset_x_ = sign_extend_11(command_[0]);
        drawing_offset_y_ = sign_extend_11(command_[0] >> 11);
    } else if (opcode == 0xe6) {
        force_mask_bit_ = (command_[0] & 1U) != 0;
        preserve_masked_pixels_ = (command_[0] & 2U) != 0;
        status_ = (status_ & ~0x1800U) | ((command_[0] & 3U) << 11);
    }
}

void Gpu::fill_rectangle() {
    const auto color = encode_color(decode_color(command_[0]));
    const auto position = decode_position(command_[1]);
    const auto size = command_[2];
    const auto x = static_cast<std::uint32_t>(position.x) & 0x3f0U;
    const auto y = static_cast<std::uint32_t>(position.y) & 0x1ffU;
    const auto width = ((size & 0x3ffU) + 15U) & ~15U;
    const auto height = (size >> 16) & 0x1ffU;

    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            vram_[((y + row) % vram_height) * vram_width
                + ((x + column) % vram_width)] = color;
        }
    }
}

void Gpu::copy_vram() {
    const auto source = decode_position(command_[1]);
    const auto destination = decode_position(command_[2]);
    auto width = command_[3] & 0x3ffU;
    auto height = (command_[3] >> 16) & 0x1ffU;
    width = width == 0 ? 0x400U : width;
    height = height == 0 ? 0x200U : height;

    std::array<std::uint16_t, vram_size> copy{};
    const auto pixel_count = static_cast<std::size_t>(width) * height;
    if (pixel_count > copy.size()) {
        throw GpuError{"GPU VRAM copy is larger than VRAM"};
    }

    for (std::size_t index = 0; index < pixel_count; ++index) {
        const auto x = (static_cast<std::uint32_t>(source.x) + index % width) % vram_width;
        const auto y = (static_cast<std::uint32_t>(source.y) + index / width) % vram_height;
        copy[index] = vram_[y * vram_width + x];
    }

    for (std::size_t index = 0; index < pixel_count; ++index) {
        const auto x = (static_cast<std::uint32_t>(destination.x) + index % width) % vram_width;
        const auto y = (static_cast<std::uint32_t>(destination.y) + index / width) % vram_height;
        const auto destination_index = y * vram_width + x;
        if (!preserve_masked_pixels_ || (vram_[destination_index] & 0x8000U) == 0) {
            vram_[destination_index] = copy[index] | (force_mask_bit_ ? 0x8000U : 0U);
        }
    }
}

void Gpu::draw_rectangle(const std::uint32_t width, const std::uint32_t height) {
    const auto color = decode_color(command_[0]);
    const auto position = decode_position(command_[1]);

    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            draw_pixel(
                position.x + static_cast<std::int32_t>(column),
                position.y + static_cast<std::int32_t>(row),
                color);
        }
    }
}

void Gpu::draw_flat_polygon(const bool quad) {
    const auto color = decode_color(command_[0]);
    const auto a = decode_position(command_[1]);
    const auto b = decode_position(command_[2]);
    const auto c = decode_position(command_[3]);
    draw_triangle(a, b, c, color, color, color, false);

    if (quad) {
        const auto d = decode_position(command_[4]);
        draw_triangle(b, c, d, color, color, color, false);
    }
}

void Gpu::draw_shaded_polygon(const bool quad) {
    const auto color_a = decode_color(command_[0]);
    const auto a = decode_position(command_[1]);
    const auto color_b = decode_color(command_[2]);
    const auto b = decode_position(command_[3]);
    const auto color_c = decode_color(command_[4]);
    const auto c = decode_position(command_[5]);
    draw_triangle(a, b, c, color_a, color_b, color_c, true);

    if (quad) {
        const auto color_d = decode_color(command_[6]);
        const auto d = decode_position(command_[7]);
        draw_triangle(b, c, d, color_b, color_c, color_d, true);
    }
}

void Gpu::begin_cpu_to_vram() {
    const auto destination = decode_position(command_[1]);
    transfer_x_ = static_cast<std::uint32_t>(destination.x) & 0x3ffU;
    transfer_y_ = static_cast<std::uint32_t>(destination.y) & 0x1ffU;
    transfer_width_ = command_[2] & 0x3ffU;
    transfer_height_ = (command_[2] >> 16) & 0x1ffU;
    transfer_width_ = transfer_width_ == 0 ? 0x400U : transfer_width_;
    transfer_height_ = transfer_height_ == 0 ? 0x200U : transfer_height_;
    transfer_pixels_remaining_ = static_cast<std::size_t>(transfer_width_) * transfer_height_;
    transfer_pixel_ = 0;
    transfer_mode_ = TransferMode::cpu_to_vram;
}

void Gpu::begin_vram_to_cpu() {
    const auto source = decode_position(command_[1]);
    transfer_x_ = static_cast<std::uint32_t>(source.x) & 0x3ffU;
    transfer_y_ = static_cast<std::uint32_t>(source.y) & 0x1ffU;
    transfer_width_ = command_[2] & 0x3ffU;
    transfer_height_ = (command_[2] >> 16) & 0x1ffU;
    transfer_width_ = transfer_width_ == 0 ? 0x400U : transfer_width_;
    transfer_height_ = transfer_height_ == 0 ? 0x200U : transfer_height_;
    transfer_pixels_remaining_ = static_cast<std::size_t>(transfer_width_) * transfer_height_;
    transfer_pixel_ = 0;
    transfer_mode_ = TransferMode::vram_to_cpu;
    status_ |= ready_for_vram_to_cpu;
}

void Gpu::write_transfer_word(const std::uint32_t value) {
    for (unsigned half = 0; half < 2 && transfer_pixels_remaining_ > 0; ++half) {
        const auto index = transfer_index();
        const auto pixel_value = static_cast<std::uint16_t>(value >> (half * 16));
        if (!preserve_masked_pixels_ || (vram_[index] & 0x8000U) == 0) {
            vram_[index] = pixel_value | (force_mask_bit_ ? 0x8000U : 0U);
        }
        ++transfer_pixel_;
        --transfer_pixels_remaining_;
    }

    if (transfer_pixels_remaining_ == 0) {
        transfer_mode_ = TransferMode::command;
    }
}

std::uint32_t Gpu::read_transfer_word() {
    std::uint32_t value = 0;
    for (unsigned half = 0; half < 2 && transfer_pixels_remaining_ > 0; ++half) {
        value |= static_cast<std::uint32_t>(vram_[transfer_index()]) << (half * 16);
        ++transfer_pixel_;
        --transfer_pixels_remaining_;
    }

    if (transfer_pixels_remaining_ == 0) {
        transfer_mode_ = TransferMode::command;
        status_ &= ~ready_for_vram_to_cpu;
    }
    read_latch_ = value;
    return value;
}

void Gpu::draw_triangle(
    Position a,
    Position b,
    Position c,
    Color color_a,
    Color color_b,
    Color color_c,
    const bool shaded) {
    a.x += drawing_offset_x_;
    a.y += drawing_offset_y_;
    b.x += drawing_offset_x_;
    b.y += drawing_offset_y_;
    c.x += drawing_offset_x_;
    c.y += drawing_offset_y_;

    auto area = edge(a, b, c);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        std::swap(b, c);
        std::swap(color_b, color_c);
        area = -area;
    }

    const auto minimum_x = std::max<std::int32_t>(
        drawing_left_,
        std::min({a.x, b.x, c.x}));
    const auto maximum_x = std::min<std::int32_t>(
        drawing_right_,
        std::max({a.x, b.x, c.x}));
    const auto minimum_y = std::max<std::int32_t>(
        drawing_top_,
        std::min({a.y, b.y, c.y}));
    const auto maximum_y = std::min<std::int32_t>(
        drawing_bottom_,
        std::max({a.y, b.y, c.y}));

    for (auto y = minimum_y; y <= maximum_y; ++y) {
        for (auto x = minimum_x; x <= maximum_x; ++x) {
            const Position point{x, y};
            const auto weight_a = edge(b, c, point);
            const auto weight_b = edge(c, a, point);
            const auto weight_c = edge(a, b, point);
            if (weight_a < 0 || weight_b < 0 || weight_c < 0) {
                continue;
            }

            Color color = color_a;
            if (shaded) {
                color.r = clamp_color(
                    (weight_a * color_a.r + weight_b * color_b.r + weight_c * color_c.r)
                    / area);
                color.g = clamp_color(
                    (weight_a * color_a.g + weight_b * color_b.g + weight_c * color_c.g)
                    / area);
                color.b = clamp_color(
                    (weight_a * color_a.b + weight_b * color_b.b + weight_c * color_c.b)
                    / area);
            }
            draw_pixel(x - drawing_offset_x_, y - drawing_offset_y_, color);
        }
    }
}

void Gpu::draw_pixel(std::int32_t x, std::int32_t y, const Color color) {
    x += drawing_offset_x_;
    y += drawing_offset_y_;
    if (x < drawing_left_ || x > drawing_right_ || y < drawing_top_ || y > drawing_bottom_) {
        return;
    }
    if (x < 0 || x >= static_cast<std::int32_t>(vram_width)
        || y < 0 || y >= static_cast<std::int32_t>(vram_height)) {
        return;
    }

    const auto index = static_cast<std::size_t>(y) * vram_width
        + static_cast<std::size_t>(x);
    if (preserve_masked_pixels_ && (vram_[index] & 0x8000U) != 0) {
        return;
    }
    vram_[index] = encode_color(color) | (force_mask_bit_ ? 0x8000U : 0U);
}

std::size_t Gpu::packet_length(const std::uint8_t opcode) {
    if (opcode <= 0x01 || opcode >= 0xe0) {
        return 1;
    }
    if (opcode == 0x02) {
        return 3;
    }
    if (opcode >= 0x20 && opcode <= 0x23) {
        return 4;
    }
    if (opcode >= 0x24 && opcode <= 0x27) {
        return 7;
    }
    if (opcode >= 0x28 && opcode <= 0x2b) {
        return 5;
    }
    if (opcode >= 0x2c && opcode <= 0x2f) {
        return 9;
    }
    if (opcode >= 0x30 && opcode <= 0x33) {
        return 6;
    }
    if (opcode >= 0x34 && opcode <= 0x37) {
        return 9;
    }
    if (opcode >= 0x38 && opcode <= 0x3b) {
        return 8;
    }
    if (opcode >= 0x3c && opcode <= 0x3f) {
        return 12;
    }
    if (opcode >= 0x40 && opcode <= 0x47) {
        return 3;
    }
    if (opcode >= 0x50 && opcode <= 0x57) {
        return 4;
    }
    if (opcode >= 0x60 && opcode <= 0x63) {
        return 3;
    }
    if (opcode >= 0x64 && opcode <= 0x67) {
        return 4;
    }
    if (opcode >= 0x68 && opcode <= 0x6b) {
        return 2;
    }
    if (opcode >= 0x6c && opcode <= 0x6f) {
        return 3;
    }
    if (opcode >= 0x70 && opcode <= 0x73) {
        return 2;
    }
    if (opcode >= 0x74 && opcode <= 0x77) {
        return 3;
    }
    if (opcode >= 0x78 && opcode <= 0x7b) {
        return 2;
    }
    if (opcode >= 0x7c && opcode <= 0x7f) {
        return 3;
    }
    if (opcode >= 0x80 && opcode <= 0xdf) {
        return opcode < 0xa0 ? 4 : 3;
    }
    return 1;
}

Gpu::Position Gpu::decode_position(const std::uint32_t value) {
    return Position{
        static_cast<std::int16_t>(value),
        static_cast<std::int16_t>(value >> 16),
    };
}

Gpu::Color Gpu::decode_color(const std::uint32_t value) {
    return Color{
        static_cast<std::int32_t>(value & 0xffU),
        static_cast<std::int32_t>((value >> 8) & 0xffU),
        static_cast<std::int32_t>((value >> 16) & 0xffU),
    };
}

std::uint16_t Gpu::encode_color(const Color color) {
    const auto red = static_cast<std::uint16_t>(std::clamp(color.r, 0, 255) >> 3);
    const auto green = static_cast<std::uint16_t>(std::clamp(color.g, 0, 255) >> 3);
    const auto blue = static_cast<std::uint16_t>(std::clamp(color.b, 0, 255) >> 3);
    return static_cast<std::uint16_t>(red | (green << 5) | (blue << 10));
}

std::int64_t Gpu::edge(const Position a, const Position b, const Position p) noexcept {
    return static_cast<std::int64_t>(p.x - a.x) * (b.y - a.y)
        - static_cast<std::int64_t>(p.y - a.y) * (b.x - a.x);
}

std::size_t Gpu::transfer_index() const noexcept {
    const auto x = (transfer_x_ + transfer_pixel_ % transfer_width_) % vram_width;
    const auto y = (transfer_y_ + transfer_pixel_ / transfer_width_) % vram_height;
    return y * vram_width + x;
}

}  // namespace psx
