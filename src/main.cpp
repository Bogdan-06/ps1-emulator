#include "psx/bios.hpp"
#include "psx/bus.hpp"
#include "psx/cli.hpp"
#include "psx/cpu.hpp"

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

int main(const int argc, const char* const argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto parsed = psx::parse_arguments(arguments);
    const std::string_view program_name = argc > 0 ? argv[0] : "ps1-emulator";

    if (!parsed.ok()) {
        std::cerr << "error: " << parsed.error << "\n\n"
                  << psx::usage(program_name);
        return 2;
    }

    if (parsed.config.show_help) {
        std::cout << psx::usage(program_name);
        return 0;
    }

    try {
        psx::Cpu cpu{psx::Bus{psx::Bios::load(parsed.config.bios_path)}};

        std::cout << "PS1 emulator core initialized\n"
                  << "BIOS: " << parsed.config.bios_path << '\n'
                  << "Instruction limit: " << parsed.config.instruction_limit << '\n'
                  << "Trace: " << (parsed.config.trace ? "enabled" : "disabled") << '\n';

        std::uint64_t executed = 0;
        for (; executed < parsed.config.instruction_limit; ++executed) {
            const auto result = cpu.step();
            if (parsed.config.trace) {
                std::cout << std::hex << std::setfill('0')
                          << std::setw(8) << result.pc << "  "
                          << std::setw(8) << result.instruction << '\n';
            }
        }

        std::cout << std::dec << "Stopped after " << executed
                  << " instructions at PC 0x"
                  << std::hex << std::setfill('0') << std::setw(8) << cpu.pc()
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
