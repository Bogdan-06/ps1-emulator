#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace psx {

class Bios {
public:
    static constexpr std::size_t size = 512 * 1024;

    explicit Bios(std::vector<std::uint8_t> bytes);

    [[nodiscard]] static Bios load(const std::filesystem::path& path);
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

private:
    std::vector<std::uint8_t> bytes_;
};

}  // namespace psx
