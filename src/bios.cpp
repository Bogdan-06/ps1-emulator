#include "psx/bios.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

namespace psx {

Bios::Bios(std::vector<std::uint8_t> bytes)
    : bytes_(std::move(bytes)) {
    if (bytes_.size() != size) {
        throw std::invalid_argument(
            "PlayStation BIOS must be exactly " + std::to_string(size) + " bytes");
    }
}

Bios Bios::load(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        throw std::runtime_error("unable to open BIOS: " + path.string());
    }

    const auto end = stream.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) != size) {
        throw std::runtime_error(
            "invalid BIOS size; expected " + std::to_string(size) + " bytes");
    }

    std::vector<std::uint8_t> bytes(size);
    stream.seekg(0);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    if (!stream) {
        throw std::runtime_error("failed while reading BIOS: " + path.string());
    }

    return Bios{std::move(bytes)};
}

std::span<const std::uint8_t> Bios::bytes() const noexcept {
    return bytes_;
}

}  // namespace psx
