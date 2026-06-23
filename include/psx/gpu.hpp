#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace psx {

class GpuError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Gpu {
public:
    static constexpr std::size_t vram_width = 1024;
    static constexpr std::size_t vram_height = 512;
    static constexpr std::size_t vram_size = vram_width * vram_height;

    Gpu();

    void write_gp0(std::uint32_t value);
    void write_gp1(std::uint32_t value);
    [[nodiscard]] std::uint32_t read_data();
    [[nodiscard]] std::uint32_t read_status() const noexcept;

    [[nodiscard]] std::uint16_t pixel(std::size_t x, std::size_t y) const;
    [[nodiscard]] std::span<const std::uint16_t> vram() const noexcept;
    [[nodiscard]] std::uint16_t display_x() const noexcept;
    [[nodiscard]] std::uint16_t display_y() const noexcept;
    [[nodiscard]] std::uint16_t display_width() const noexcept;
    [[nodiscard]] std::uint16_t display_height() const noexcept;
    [[nodiscard]] bool display_disabled() const noexcept;

private:
    struct Position {
        std::int32_t x;
        std::int32_t y;
    };

    struct Color {
        std::int32_t r;
        std::int32_t g;
        std::int32_t b;
    };

    enum class TransferMode {
        command,
        cpu_to_vram,
        vram_to_cpu,
    };

    void reset();
    void reset_command_buffer() noexcept;
    void execute_packet();

    void fill_rectangle();
    void copy_vram();
    void draw_rectangle(std::uint32_t width, std::uint32_t height);
    void draw_flat_polygon(bool quad);
    void draw_shaded_polygon(bool quad);
    void begin_cpu_to_vram();
    void begin_vram_to_cpu();
    void write_transfer_word(std::uint32_t value);
    [[nodiscard]] std::uint32_t read_transfer_word();

    void draw_triangle(
        Position a,
        Position b,
        Position c,
        Color color_a,
        Color color_b,
        Color color_c,
        bool shaded);
    void draw_pixel(std::int32_t x, std::int32_t y, Color color);

    [[nodiscard]] static std::size_t packet_length(std::uint8_t opcode);
    [[nodiscard]] static Position decode_position(std::uint32_t value);
    [[nodiscard]] static Color decode_color(std::uint32_t value);
    [[nodiscard]] static std::uint16_t encode_color(Color color);
    [[nodiscard]] static std::int64_t edge(Position a, Position b, Position p) noexcept;
    [[nodiscard]] std::size_t transfer_index() const noexcept;

    std::array<std::uint16_t, vram_size> vram_{};
    std::array<std::uint32_t, 16> command_{};
    std::size_t command_size_ = 0;
    std::size_t command_words_expected_ = 0;
    TransferMode transfer_mode_ = TransferMode::command;
    std::uint32_t transfer_x_ = 0;
    std::uint32_t transfer_y_ = 0;
    std::uint32_t transfer_width_ = 0;
    std::uint32_t transfer_height_ = 0;
    std::size_t transfer_pixels_remaining_ = 0;
    std::size_t transfer_pixel_ = 0;

    std::uint32_t status_ = 0;
    std::uint32_t read_latch_ = 0;
    std::int32_t drawing_offset_x_ = 0;
    std::int32_t drawing_offset_y_ = 0;
    std::uint16_t drawing_left_ = 0;
    std::uint16_t drawing_top_ = 0;
    std::uint16_t drawing_right_ = 1023;
    std::uint16_t drawing_bottom_ = 511;
    std::uint16_t display_x_ = 0;
    std::uint16_t display_y_ = 0;
    std::uint16_t horizontal_start_ = 0x200;
    std::uint16_t horizontal_end_ = 0xc00;
    std::uint16_t vertical_start_ = 0x10;
    std::uint16_t vertical_end_ = 0x100;
    bool force_mask_bit_ = false;
    bool preserve_masked_pixels_ = false;
};

}  // namespace psx
