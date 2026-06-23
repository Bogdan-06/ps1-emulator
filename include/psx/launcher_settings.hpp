#pragma once

#include <filesystem>

namespace psx {

struct LauncherSettings {
    std::filesystem::path bios_path;
    std::filesystem::path disc_path;
    bool trace = false;
    bool remember_files = true;
};

[[nodiscard]] std::filesystem::path launcher_settings_path();
[[nodiscard]] LauncherSettings load_launcher_settings(
    const std::filesystem::path& path);
void save_launcher_settings(
    const LauncherSettings& settings,
    const std::filesystem::path& path);

}  // namespace psx
