#include "psx/launcher_settings.hpp"

#include <filesystem>
#include <string_view>

void expect(bool condition, std::string_view message);

void run_launcher_settings_tests() {
    const auto path = std::filesystem::temp_directory_path()
        / "ps1-emulator-launcher-settings.conf";

    psx::LauncherSettings settings;
    settings.bios_path = "/tmp/My BIOS.bin";
    settings.disc_path = "/tmp/My Game.cue";
    settings.trace = true;
    settings.remember_files = true;
    psx::save_launcher_settings(settings, path);

    const auto loaded = psx::load_launcher_settings(path);
    expect(loaded.bios_path == settings.bios_path, "launcher should persist BIOS path");
    expect(loaded.disc_path == settings.disc_path, "launcher should persist disc path");
    expect(loaded.trace, "launcher should persist trace setting");
    expect(loaded.remember_files, "launcher should persist remember-files setting");

    settings.remember_files = false;
    psx::save_launcher_settings(settings, path);
    const auto private_settings = psx::load_launcher_settings(path);
    expect(private_settings.bios_path.empty(), "disabled persistence should clear saved BIOS");
    expect(private_settings.disc_path.empty(), "disabled persistence should clear saved disc");

    std::filesystem::remove(path);
}
