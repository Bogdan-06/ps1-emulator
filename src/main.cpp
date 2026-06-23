#include "psx/bios.hpp"
#include "psx/bus.hpp"
#include "psx/cli.hpp"
#include "psx/cpu.hpp"
#include "psx/display.hpp"
#include "psx/disc.hpp"
#include "psx/launcher.hpp"

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

int main(const int argc, const char* const argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const std::string_view program_name = argc > 0 ? argv[0] : "ps1-emulator";
    psx::AppConfig config;

    if (arguments.empty()) {
        try {
            const auto launched = psx::Launcher::run();
            if (!launched.has_value()) {
                return 0;
            }
            config = *launched;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 1;
        }
    } else {
        const auto parsed = psx::parse_arguments(arguments);

        if (!parsed.ok()) {
            std::cerr << "error: " << parsed.error << "\n\n"
                      << psx::usage(program_name);
            return 2;
        }

        if (parsed.config.show_help) {
            std::cout << psx::usage(program_name);
            return 0;
        }
        config = parsed.config;
    }

    try {
        std::optional<psx::Disc> disc;
        if (!config.disc_path.empty()) {
            disc.emplace(psx::Disc::open(config.disc_path));
        }
        psx::Cpu cpu{
            psx::Bus{psx::Bios::load(config.bios_path), std::move(disc)}};
        std::unique_ptr<psx::Display> display;
        if (config.display) {
            display = std::make_unique<psx::Display>();
        }

        std::cout << "PS1 emulator core initialized\n"
                  << "BIOS: " << config.bios_path << '\n'
                  << "Disc: "
                  << (config.disc_path.empty()
                        ? std::string{"none"}
                        : config.disc_path.string())
                  << '\n'
                  << "Instruction limit: ";
        if (config.instruction_limit == 0) {
            std::cout << "unlimited\n";
        } else {
            std::cout << config.instruction_limit << '\n';
        }
        std::cout
                  << "Trace: " << (config.trace ? "enabled" : "disabled") << '\n';

        std::uint64_t executed = 0;
        std::uint64_t presented_frame = cpu.bus().gpu().frame_counter();
        bool running = true;
        while (running
            && (config.instruction_limit == 0
                || executed < config.instruction_limit)) {
            const auto result = cpu.step();
            ++executed;
            if (config.trace) {
                std::cout << std::hex << std::setfill('0')
                          << std::setw(8) << result.pc << "  "
                          << std::setw(8) << result.instruction << '\n';
            }
            if (display != nullptr) {
                const auto current_frame = cpu.bus().gpu().frame_counter();
                if (current_frame != presented_frame) {
                    running = display->present(cpu.bus().gpu());
                    presented_frame = current_frame;
                }
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
