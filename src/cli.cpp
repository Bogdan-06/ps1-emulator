#include "psx/cli.hpp"

#include <charconv>
#include <system_error>

namespace psx {
namespace {

[[nodiscard]] bool parse_instruction_limit(
    const std::string_view value,
    std::uint64_t& destination) {
    if (value.empty()) {
        return false;
    }

    const auto* const begin = value.data();
    const auto* const end = begin + value.size();
    const auto [position, error] = std::from_chars(begin, end, destination);
    return error == std::errc{} && position == end;
}

}  // namespace

ParseResult parse_arguments(const std::span<const std::string_view> arguments) {
    ParseResult result;
    bool steps_provided = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];

        if (argument == "--help" || argument == "-h") {
            result.config.show_help = true;
            continue;
        }

        if (argument == "--trace") {
            result.config.trace = true;
            continue;
        }

        if (argument == "--display") {
            result.config.display = true;
            continue;
        }

        if (argument == "--bios") {
            if (++index >= arguments.size()) {
                result.error = "--bios requires a file path";
                return result;
            }
            result.config.bios_path = arguments[index];
            continue;
        }

        if (argument == "--steps") {
            steps_provided = true;
            if (++index >= arguments.size()) {
                result.error = "--steps requires an integer";
                return result;
            }

            if (!parse_instruction_limit(arguments[index], result.config.instruction_limit)) {
                result.error = "--steps must be a non-negative integer";
                return result;
            }
            continue;
        }

        result.error = "unknown argument: " + std::string{argument};
        return result;
    }

    if (!result.config.show_help && result.config.bios_path.empty()) {
        result.error = "a BIOS path is required; pass --bios <file>";
    }
    if (result.config.display && !steps_provided) {
        result.config.instruction_limit = 0;
    }

    return result;
}

std::string usage(const std::string_view program_name) {
    return "Usage: " + std::string{program_name}
        + " --bios <file> [--steps <count>] [--trace] [--display]\n"
          "\n"
          "Options:\n"
          "  --bios <file>    Path to a 512 KiB PlayStation BIOS dump\n"
          "  --steps <count>  Stop after this many instructions; 0 runs continuously\n"
          "  --trace          Print each executed instruction\n"
          "  --display        Open a window; runs continuously unless --steps is set\n"
          "  -h, --help       Show this help text\n";
}

}  // namespace psx
