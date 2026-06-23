#include "psx/cli.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    {
        const std::vector<std::string_view> arguments{
            "--bios", "bios.bin", "--steps", "42", "--trace"};
        const auto result = psx::parse_arguments(arguments);

        expect(result.ok(), "valid arguments should parse");
        expect(result.config.bios_path == "bios.bin", "BIOS path should be retained");
        expect(result.config.instruction_limit == 42, "instruction limit should be parsed");
        expect(result.config.trace, "trace flag should be enabled");
    }

    {
        const std::vector<std::string_view> arguments{"--help"};
        const auto result = psx::parse_arguments(arguments);

        expect(result.ok(), "help should not require a BIOS");
        expect(result.config.show_help, "help flag should be retained");
    }

    {
        const std::vector<std::string_view> arguments;
        const auto result = psx::parse_arguments(arguments);

        expect(!result.ok(), "BIOS should be required");
    }

    {
        const std::vector<std::string_view> arguments{"--bios", "bios.bin", "--steps", "invalid"};
        const auto result = psx::parse_arguments(arguments);

        expect(!result.ok(), "invalid instruction limit should fail");
    }

    std::cout << "CLI tests passed\n";
    return 0;
}
