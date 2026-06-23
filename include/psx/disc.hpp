#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace psx {

class Disc {
public:
    static constexpr std::size_t raw_sector_size = 2352;
    static constexpr std::size_t data_sector_size = 2048;

    [[nodiscard]] static Disc open(const std::filesystem::path& path);

    Disc(Disc&&) noexcept = default;
    Disc& operator=(Disc&&) noexcept = default;
    Disc(const Disc&) = delete;
    Disc& operator=(const Disc&) = delete;

    [[nodiscard]] std::uint32_t sector_count() const noexcept;
    [[nodiscard]] std::array<std::uint8_t, raw_sector_size> read_raw_sector(
        std::uint32_t lba);
    [[nodiscard]] std::array<std::uint8_t, data_sector_size> read_data_sector(
        std::uint32_t lba);
    [[nodiscard]] const std::filesystem::path& image_path() const noexcept;

private:
    Disc(
        std::filesystem::path image_path,
        std::ifstream stream,
        std::uint32_t sector_count);

    std::filesystem::path image_path_;
    std::ifstream stream_;
    std::uint32_t sector_count_ = 0;
};

}  // namespace psx
