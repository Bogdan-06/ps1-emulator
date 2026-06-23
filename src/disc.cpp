#include "psx/disc.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>

namespace psx {
namespace {

[[nodiscard]] std::filesystem::path resolve_image_path(
    const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });

    if (extension != ".cue") {
        return path;
    }

    std::ifstream cue{path};
    if (!cue) {
        throw std::runtime_error{"unable to open CUE file: " + path.string()};
    }

    const std::string contents{
        std::istreambuf_iterator<char>{cue},
        std::istreambuf_iterator<char>{}};
    const std::regex file_pattern{
        R"re(FILE\s+"([^"]+)"\s+BINARY)re",
        std::regex::icase};
    std::smatch match;
    if (!std::regex_search(contents, match, file_pattern)) {
        throw std::runtime_error{
            "CUE file must reference one binary track: " + path.string()};
    }

    if (contents.find("MODE2/2352") == std::string::npos
        && contents.find("mode2/2352") == std::string::npos) {
        throw std::runtime_error{"only MODE2/2352 CUE tracks are currently supported"};
    }

    return path.parent_path() / match[1].str();
}

}  // namespace

Disc Disc::open(const std::filesystem::path& path) {
    auto image_path = resolve_image_path(path);
    std::ifstream stream{image_path, std::ios::binary | std::ios::ate};
    if (!stream) {
        throw std::runtime_error{"unable to open disc image: " + image_path.string()};
    }

    const auto end = stream.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) % raw_sector_size != 0) {
        throw std::runtime_error{
            "disc image size must be a positive multiple of 2352 bytes"};
    }

    const auto sectors = static_cast<std::uint64_t>(end) / raw_sector_size;
    if (sectors > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error{"disc image contains too many sectors"};
    }

    stream.seekg(0);
    return Disc{
        std::move(image_path),
        std::move(stream),
        static_cast<std::uint32_t>(sectors)};
}

std::uint32_t Disc::sector_count() const noexcept {
    return sector_count_;
}

std::array<std::uint8_t, Disc::raw_sector_size> Disc::read_raw_sector(
    const std::uint32_t lba) {
    if (lba >= sector_count_) {
        throw std::out_of_range{"disc sector is outside the image"};
    }

    std::array<std::uint8_t, raw_sector_size> sector{};
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(lba) * raw_sector_size);
    stream_.read(
        reinterpret_cast<char*>(sector.data()),
        static_cast<std::streamsize>(sector.size()));
    if (!stream_) {
        throw std::runtime_error{"failed while reading disc sector"};
    }
    return sector;
}

std::array<std::uint8_t, Disc::data_sector_size> Disc::read_data_sector(
    const std::uint32_t lba) {
    const auto raw = read_raw_sector(lba);
    if (raw[15] != 2) {
        throw std::runtime_error{"disc sector is not Mode 2"};
    }
    if ((raw[18] & 0x20U) != 0) {
        throw std::runtime_error{"Mode 2 Form 2 sectors are not supported as 2048-byte data"};
    }

    std::array<std::uint8_t, data_sector_size> data{};
    std::copy_n(raw.begin() + 24, data.size(), data.begin());
    return data;
}

const std::filesystem::path& Disc::image_path() const noexcept {
    return image_path_;
}

Disc::Disc(
    std::filesystem::path image_path,
    std::ifstream stream,
    const std::uint32_t sector_count)
    : image_path_(std::move(image_path)),
      stream_(std::move(stream)),
      sector_count_(sector_count) {}

}  // namespace psx
