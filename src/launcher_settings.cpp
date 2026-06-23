#include "psx/launcher_settings.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>

namespace psx {

std::filesystem::path launcher_settings_path() {
    if (const char* const configured = std::getenv("XDG_CONFIG_HOME");
        configured != nullptr && configured[0] != '\0') {
        return std::filesystem::path{configured}
            / "ps1-emulator"
            / "settings.conf";
    }
    if (const char* const home = std::getenv("HOME");
        home != nullptr && home[0] != '\0') {
        return std::filesystem::path{home}
            / ".config"
            / "ps1-emulator"
            / "settings.conf";
    }
    return "ps1-emulator-settings.conf";
}

LauncherSettings load_launcher_settings(const std::filesystem::path& path) {
    LauncherSettings settings;
    std::ifstream stream{path};
    if (!stream) {
        return settings;
    }

    std::string key;
    while (stream >> key) {
        if (key == "bios") {
            std::string value;
            stream >> std::quoted(value);
            settings.bios_path = value;
        } else if (key == "disc") {
            std::string value;
            stream >> std::quoted(value);
            settings.disc_path = value;
        } else if (key == "trace") {
            stream >> settings.trace;
        } else if (key == "remember_files") {
            stream >> settings.remember_files;
        } else {
            std::string ignored;
            std::getline(stream, ignored);
        }
    }
    return settings;
}

void save_launcher_settings(
    const LauncherSettings& settings,
    const std::filesystem::path& path) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream stream{path, std::ios::trunc};
    if (!stream) {
        return;
    }

    const auto bios = settings.remember_files ? settings.bios_path.string() : std::string{};
    const auto disc = settings.remember_files ? settings.disc_path.string() : std::string{};
    stream << "bios " << std::quoted(bios) << '\n';
    stream << "disc " << std::quoted(disc) << '\n';
    stream << "trace " << settings.trace << '\n';
    stream << "remember_files " << settings.remember_files << '\n';
}

}  // namespace psx
