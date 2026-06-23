#include "psx/bios.hpp"
#include "psx/bus.hpp"
#include "psx/cli.hpp"
#include "psx/cpu.hpp"
#include "psx/display.hpp"

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
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
        std::unique_ptr<psx::Display> display;
        if (parsed.config.display) {
            display = std::make_unique<psx::Display>();
        }

        std::cout << "PS1 emulator core initialized\n"
                  << "BIOS: " << parsed.config.bios_path << '\n'
                  << "Instruction limit: ";
        if (parsed.config.instruction_limit == 0) {
            std::cout << "unlimited\n";
        } else {
            std::cout << parsed.config.instruction_limit << '\n';
        }
        std::cout
                  << "Trace: " << (parsed.config.trace ? "enabled" : "disabled") << '\n';

        std::uint64_t executed = 0;
        bool running = true;
        while (running
            && (parsed.config.instruction_limit == 0
                || executed < parsed.config.instruction_limit)) {
            const auto result = cpu.step();
            ++executed;
            if (parsed.config.trace) {
                std::cout << std::hex << std::setfill('0')
                          << std::setw(8) << result.pc << "  "
                          << std::setw(8) << result.instruction << '\n';
            }
            if (display != nullptr && executed % 50'000 == 0) {
                running = display->present(cpu.bus().gpu());
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
