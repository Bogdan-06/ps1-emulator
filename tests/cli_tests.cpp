#include "psx/cli.hpp"

#include <string_view>
#include <vector>

void expect(bool condition, std::string_view message);

void run_cli_tests() {
    {
        const std::vector<std::string_view> arguments{
            "--bios", "bios.bin", "--disc", "game.cue", "--steps", "42", "--trace"};
        const auto result = psx::parse_arguments(arguments);

        expect(result.ok(), "valid arguments should parse");
        expect(result.config.bios_path == "bios.bin", "BIOS path should be retained");
        expect(result.config.disc_path == "game.cue", "disc path should be retained");
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
        const std::vector<std::string_view> arguments{"--bios", "bios.bin", "--display"};
        const auto result = psx::parse_arguments(arguments);

        expect(result.ok(), "display arguments should parse");
        expect(result.config.display, "display flag should be enabled");
        expect(result.config.instruction_limit == 0, "display should run continuously by default");
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

}
