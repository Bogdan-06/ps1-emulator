#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace psx {

struct AppConfig {
    std::filesystem::path bios_path;
    std::uint64_t instruction_limit = 1'000'000;
    bool trace = false;
    bool show_help = false;
};

struct ParseResult {
    AppConfig config;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] ParseResult parse_arguments(std::span<const std::string_view> arguments);
[[nodiscard]] std::string usage(std::string_view program_name);

}  // namespace psx
