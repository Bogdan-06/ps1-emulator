#include "psx/launcher.hpp"

#include "psx/bios.hpp"
#include "psx/launcher_settings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace psx {
namespace {

constexpr std::uint32_t sdl_init_video = 0x0000'0020;
constexpr std::uint32_t sdl_window_shown = 0x0000'0004;
constexpr std::int32_t sdl_window_position_centered = 0x2fff'0000;
constexpr std::uint32_t sdl_renderer_accelerated = 0x0000'0002;
constexpr std::uint32_t sdl_renderer_present_vsync = 0x0000'0004;
constexpr std::uint32_t sdl_quit = 0x0000'0100;
constexpr std::uint32_t sdl_mouse_button_down = 0x0000'0401;

struct SdlWindow;
struct SdlRenderer;

struct SdlRect {
    int x;
    int y;
    int width;
    int height;
};

struct Color {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha = 255;
};

enum class Tab {
    game,
    settings,
    about,
};

constexpr Color background{11, 17, 28};
constexpr Color sidebar{17, 25, 40};
constexpr Color panel{24, 35, 54};
constexpr Color panel_light{31, 45, 68};
constexpr Color text_primary{239, 244, 252};
constexpr Color text_secondary{148, 163, 184};
constexpr Color accent{62, 179, 255};
constexpr Color accent_dark{24, 117, 184};
constexpr Color success{66, 211, 146};
constexpr Color warning{255, 190, 74};
constexpr Color danger{255, 98, 110};

template <typename Function>
[[nodiscard]] Function load_symbol(void* const library, const char* const name) {
    void* symbol = dlsym(library, name);
    if (symbol == nullptr) {
        throw std::runtime_error{"SDL2 is missing required symbol " + std::string{name}};
    }
    static_assert(sizeof(Function) == sizeof(symbol));
    Function function{};
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

[[nodiscard]] bool contains(const SdlRect rectangle, const int x, const int y) noexcept {
    return x >= rectangle.x
        && x < rectangle.x + rectangle.width
        && y >= rectangle.y
        && y < rectangle.y + rectangle.height;
}

[[nodiscard]] std::array<std::uint8_t, 7> glyph(const char input) {
    const auto character = static_cast<char>(
        std::toupper(static_cast<unsigned char>(input)));
    switch (character) {
    case 'A': return {14, 17, 17, 31, 17, 17, 17};
    case 'B': return {30, 17, 17, 30, 17, 17, 30};
    case 'C': return {14, 17, 16, 16, 16, 17, 14};
    case 'D': return {30, 17, 17, 17, 17, 17, 30};
    case 'E': return {31, 16, 16, 30, 16, 16, 31};
    case 'F': return {31, 16, 16, 30, 16, 16, 16};
    case 'G': return {14, 17, 16, 23, 17, 17, 15};
    case 'H': return {17, 17, 17, 31, 17, 17, 17};
    case 'I': return {31, 4, 4, 4, 4, 4, 31};
    case 'J': return {7, 2, 2, 2, 18, 18, 12};
    case 'K': return {17, 18, 20, 24, 20, 18, 17};
    case 'L': return {16, 16, 16, 16, 16, 16, 31};
    case 'M': return {17, 27, 21, 21, 17, 17, 17};
    case 'N': return {17, 25, 21, 19, 17, 17, 17};
    case 'O': return {14, 17, 17, 17, 17, 17, 14};
    case 'P': return {30, 17, 17, 30, 16, 16, 16};
    case 'Q': return {14, 17, 17, 17, 21, 18, 13};
    case 'R': return {30, 17, 17, 30, 20, 18, 17};
    case 'S': return {15, 16, 16, 14, 1, 1, 30};
    case 'T': return {31, 4, 4, 4, 4, 4, 4};
    case 'U': return {17, 17, 17, 17, 17, 17, 14};
    case 'V': return {17, 17, 17, 17, 17, 10, 4};
    case 'W': return {17, 17, 17, 21, 21, 21, 10};
    case 'X': return {17, 17, 10, 4, 10, 17, 17};
    case 'Y': return {17, 17, 10, 4, 4, 4, 4};
    case 'Z': return {31, 1, 2, 4, 8, 16, 31};
    case '0': return {14, 17, 19, 21, 25, 17, 14};
    case '1': return {4, 12, 4, 4, 4, 4, 14};
    case '2': return {14, 17, 1, 2, 4, 8, 31};
    case '3': return {30, 1, 1, 14, 1, 1, 30};
    case '4': return {2, 6, 10, 18, 31, 2, 2};
    case '5': return {31, 16, 16, 30, 1, 1, 30};
    case '6': return {14, 16, 16, 30, 17, 17, 14};
    case '7': return {31, 1, 2, 4, 8, 8, 8};
    case '8': return {14, 17, 17, 14, 17, 17, 14};
    case '9': return {14, 17, 17, 15, 1, 1, 14};
    case '.': return {0, 0, 0, 0, 0, 12, 12};
    case ':': return {0, 12, 12, 0, 12, 12, 0};
    case '-': return {0, 0, 0, 31, 0, 0, 0};
    case '_': return {0, 0, 0, 0, 0, 0, 31};
    case '/': return {1, 2, 2, 4, 8, 8, 16};
    case '(': return {2, 4, 8, 8, 8, 4, 2};
    case ')': return {8, 4, 2, 2, 2, 4, 8};
    case '!': return {4, 4, 4, 4, 4, 0, 4};
    case '?': return {14, 17, 1, 2, 4, 0, 4};
    case '+': return {0, 4, 4, 31, 4, 4, 0};
    case ' ': return {0, 0, 0, 0, 0, 0, 0};
    default: return {31, 17, 5, 4, 4, 0, 4};
    }
}

[[nodiscard]] std::string shorten(std::string value, const std::size_t limit) {
    if (value.size() <= limit) {
        return value;
    }
    if (limit <= 3) {
        return value.substr(0, limit);
    }
    return value.substr(0, limit - 3) + "...";
}

[[nodiscard]] std::string selected_name(
    const std::filesystem::path& path,
    const std::string_view fallback) {
    return path.empty()
        ? std::string{fallback}
        : shorten(path.filename().string(), 42);
}

[[nodiscard]] std::optional<std::filesystem::path> choose_file(const bool bios) {
    const char* const command = bios
        ? "kdialog --title \"Select PlayStation BIOS\" "
          "--getopenfilename \"$HOME/Downloads\" "
          "\"*.bin *.BIN|PlayStation BIOS (*.bin)\" 2>/dev/null"
        : "kdialog --title \"Select PlayStation Game\" "
          "--getopenfilename \"$HOME/Downloads\" "
          "\"*.cue *.CUE *.bin *.BIN|PlayStation disc (*.cue *.bin)\" 2>/dev/null";

    std::unique_ptr<FILE, int (*)(FILE*)> pipe{popen(command, "r"), pclose};
    if (!pipe) {
        return std::nullopt;
    }

    std::array<char, 4096> buffer{};
    std::string result;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result.empty()
        ? std::nullopt
        : std::optional<std::filesystem::path>{result};
}

[[nodiscard]] bool valid_bios(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty()
        && std::filesystem::is_regular_file(path, error)
        && !error
        && std::filesystem::file_size(path, error) == Bios::size
        && !error;
}

[[nodiscard]] bool valid_disc(const std::filesystem::path& path) {
    if (path.empty()) {
        return true;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

class LauncherWindow {
public:
    using Init = int (*)(std::uint32_t);
    using Quit = void (*)();
    using GetError = const char* (*)();
    using CreateWindow = SdlWindow* (*)(
        const char*, int, int, int, int, std::uint32_t);
    using DestroyWindow = void (*)(SdlWindow*);
    using CreateRenderer = SdlRenderer* (*)(SdlWindow*, int, std::uint32_t);
    using DestroyRenderer = void (*)(SdlRenderer*);
    using SetColor = int (*)(SdlRenderer*, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t);
    using RenderClear = int (*)(SdlRenderer*);
    using FillRect = int (*)(SdlRenderer*, const SdlRect*);
    using DrawRect = int (*)(SdlRenderer*, const SdlRect*);
    using RenderPresent = void (*)(SdlRenderer*);
    using PollEvent = int (*)(void*);
    using Delay = void (*)(std::uint32_t);

    LauncherWindow() {
        library_ = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
        if (library_ == nullptr) {
            throw std::runtime_error{
                "unable to load SDL2 runtime: " + std::string{dlerror()}};
        }

        try {
            init_ = load_symbol<Init>(library_, "SDL_Init");
            quit_ = load_symbol<Quit>(library_, "SDL_Quit");
            get_error_ = load_symbol<GetError>(library_, "SDL_GetError");
            create_window_ = load_symbol<CreateWindow>(library_, "SDL_CreateWindow");
            destroy_window_ = load_symbol<DestroyWindow>(library_, "SDL_DestroyWindow");
            create_renderer_ = load_symbol<CreateRenderer>(library_, "SDL_CreateRenderer");
            destroy_renderer_ = load_symbol<DestroyRenderer>(library_, "SDL_DestroyRenderer");
            set_color_ = load_symbol<SetColor>(library_, "SDL_SetRenderDrawColor");
            render_clear_ = load_symbol<RenderClear>(library_, "SDL_RenderClear");
            fill_rect_ = load_symbol<FillRect>(library_, "SDL_RenderFillRect");
            draw_rect_ = load_symbol<DrawRect>(library_, "SDL_RenderDrawRect");
            render_present_ = load_symbol<RenderPresent>(library_, "SDL_RenderPresent");
            poll_event_ = load_symbol<PollEvent>(library_, "SDL_PollEvent");
            delay_ = load_symbol<Delay>(library_, "SDL_Delay");

            if (init_(sdl_init_video) != 0) {
                throw_sdl_error("unable to initialize launcher video");
            }
            initialized_ = true;
            window_ = create_window_(
                "PS1 Emulator",
                sdl_window_position_centered,
                sdl_window_position_centered,
                980,
                650,
                sdl_window_shown);
            if (window_ == nullptr) {
                throw_sdl_error("unable to create launcher window");
            }
            renderer_ = create_renderer_(
                window_,
                -1,
                sdl_renderer_accelerated | sdl_renderer_present_vsync);
            if (renderer_ == nullptr) {
                renderer_ = create_renderer_(window_, -1, 0);
            }
            if (renderer_ == nullptr) {
                throw_sdl_error("unable to create launcher renderer");
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~LauncherWindow() {
        cleanup();
    }

    [[nodiscard]] std::optional<AppConfig> run() {
        auto settings = load_launcher_settings(launcher_settings_path());
        detect_default_bios(settings);
        Tab tab = Tab::game;
        std::string status = valid_bios(settings.bios_path)
            ? "BIOS READY - SELECT A GAME OR BOOT THE BIOS"
            : "SELECT A BIOS AND OPTIONAL GAME DISC";
        Color status_color = valid_bios(settings.bios_path) ? success : text_secondary;
        bool running = true;

        while (running) {
            render(tab, settings, status, status_color);

            alignas(std::max_align_t) std::array<std::byte, 128> event{};
            while (poll_event_(event.data()) != 0) {
                std::uint32_t type = 0;
                std::memcpy(&type, event.data(), sizeof(type));
                if (type == sdl_quit) {
                    running = false;
                    break;
                }
                if (type != sdl_mouse_button_down) {
                    continue;
                }

                int x = 0;
                int y = 0;
                std::memcpy(&x, event.data() + 20, sizeof(x));
                std::memcpy(&y, event.data() + 24, sizeof(y));

                if (contains({24, 174, 164, 50}, x, y)) {
                    tab = Tab::game;
                } else if (contains({24, 236, 164, 50}, x, y)) {
                    tab = Tab::settings;
                } else if (contains({24, 298, 164, 50}, x, y)) {
                    tab = Tab::about;
                } else if (tab == Tab::game) {
                    if (contains({770, 203, 130, 42}, x, y)) {
                        if (const auto selected = choose_file(true); selected.has_value()) {
                            settings.bios_path = *selected;
                            status = valid_bios(settings.bios_path)
                                ? "BIOS READY"
                                : "THE BIOS MUST BE EXACTLY 512 KIB";
                            status_color = valid_bios(settings.bios_path) ? success : danger;
                            persist(settings);
                        }
                    } else if (contains({770, 348, 130, 42}, x, y)) {
                        if (const auto selected = choose_file(false); selected.has_value()) {
                            settings.disc_path = *selected;
                            status = "GAME DISC SELECTED";
                            status_color = success;
                            persist(settings);
                        }
                    } else if (contains({625, 348, 130, 42}, x, y)) {
                        settings.disc_path.clear();
                        status = "DISC REMOVED - BIOS ONLY";
                        status_color = text_secondary;
                        persist(settings);
                    } else if (contains({660, 525, 240, 60}, x, y)) {
                        if (!valid_bios(settings.bios_path)) {
                            status = "SELECT A VALID 512 KIB BIOS FIRST";
                            status_color = danger;
                        } else if (!valid_disc(settings.disc_path)) {
                            status = "THE SELECTED GAME FILE NO LONGER EXISTS";
                            status_color = danger;
                        } else {
                            persist(settings);
                            AppConfig config;
                            config.bios_path = settings.bios_path;
                            config.disc_path = settings.disc_path;
                            config.instruction_limit = 0;
                            config.trace = settings.trace;
                            config.display = true;
                            return config;
                        }
                    }
                } else if (tab == Tab::settings) {
                    if (contains({770, 204, 110, 42}, x, y)) {
                        settings.trace = !settings.trace;
                        persist(settings);
                    } else if (contains({770, 298, 110, 42}, x, y)) {
                        settings.remember_files = !settings.remember_files;
                        persist(settings);
                    } else if (contains({660, 470, 220, 46}, x, y)) {
                        settings.bios_path.clear();
                        settings.disc_path.clear();
                        persist(settings);
                        status = "SAVED FILE SELECTIONS CLEARED";
                        status_color = text_secondary;
                    }
                }
            }
            delay_(16);
        }

        persist(settings);
        return std::nullopt;
    }

private:
    void detect_default_bios(LauncherSettings& settings) {
        if (!settings.bios_path.empty()) {
            return;
        }
        const char* const home = std::getenv("HOME");
        if (home == nullptr) {
            return;
        }
        const auto candidate = std::filesystem::path{home}
            / "Downloads"
            / "SCPH1001.BIN";
        if (valid_bios(candidate)) {
            settings.bios_path = candidate;
        }
    }

    void persist(const LauncherSettings& settings) {
        save_launcher_settings(settings, launcher_settings_path());
    }

    void set_color(const Color color) {
        set_color_(renderer_, color.red, color.green, color.blue, color.alpha);
    }

    void rectangle(const SdlRect rectangle, const Color color, const bool outline = false) {
        set_color(color);
        if (outline) {
            draw_rect_(renderer_, &rectangle);
        } else {
            fill_rect_(renderer_, &rectangle);
        }
    }

    void text(
        const int x,
        const int y,
        const std::string_view value,
        const int scale,
        const Color color) {
        set_color(color);
        int cursor = x;
        for (const char character : value) {
            const auto bitmap = glyph(character);
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((bitmap[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0) {
                        continue;
                    }
                    const SdlRect pixel{
                        cursor + column * scale,
                        y + row * scale,
                        scale,
                        scale};
                    fill_rect_(renderer_, &pixel);
                }
            }
            cursor += 6 * scale;
        }
    }

    void button(
        const SdlRect area,
        const std::string_view label,
        const Color fill,
        const Color label_color = text_primary) {
        rectangle(area, fill);
        rectangle(area, panel_light, true);
        const auto label_width = static_cast<int>(label.size()) * 12;
        text(
            area.x + std::max(10, (area.width - label_width) / 2),
            area.y + 14,
            label,
            2,
            label_color);
    }

    void tab_button(
        const SdlRect area,
        const std::string_view label,
        const bool selected) {
        rectangle(area, selected ? panel_light : sidebar);
        if (selected) {
            rectangle({area.x, area.y, 4, area.height}, accent);
        }
        text(area.x + 22, area.y + 17, label, 2, selected ? text_primary : text_secondary);
    }

    void file_card(
        const int y,
        const std::string_view title,
        const std::filesystem::path& path,
        const bool valid,
        const bool optional) {
        rectangle({248, y, 652, 112}, panel);
        text(274, y + 20, title, 2, text_primary);
        text(
            274,
            y + 52,
            selected_name(path, optional ? "NO DISC - BIOS ONLY" : "NOT SELECTED"),
            2,
            path.empty() ? text_secondary : text_primary);
        const bool empty_optional = optional && path.empty();
        const auto badge = empty_optional ? text_secondary : (valid ? success : danger);
        rectangle({274, y + 83, 10, 10}, badge);
        text(
            294,
            y + 81,
            empty_optional ? "OPTIONAL" : (valid ? "READY" : "INVALID"),
            1,
            badge);
    }

    void render(
        const Tab tab,
        const LauncherSettings& settings,
        const std::string_view status,
        const Color status_color) {
        set_color(background);
        render_clear_(renderer_);
        rectangle({0, 0, 214, 650}, sidebar);

        rectangle({24, 30, 44, 44}, accent_dark);
        text(35, 43, "PS", 2, text_primary);
        text(82, 34, "PS1", 3, text_primary);
        text(82, 60, "EMULATOR", 1, text_secondary);

        tab_button({24, 174, 164, 50}, "GAME", tab == Tab::game);
        tab_button({24, 236, 164, 50}, "SETTINGS", tab == Tab::settings);
        tab_button({24, 298, 164, 50}, "ABOUT", tab == Tab::about);
        text(28, 610, "EARLY DEVELOPMENT BUILD", 1, text_secondary);

        if (tab == Tab::game) {
            text(248, 52, "START A GAME", 4, text_primary);
            text(250, 95, "CHOOSE YOUR BIOS AND AN OPTIONAL DISC IMAGE", 1, text_secondary);

            file_card(160, "PLAYSTATION BIOS", settings.bios_path, valid_bios(settings.bios_path), false);
            button({770, 203, 130, 42}, "BROWSE", accent_dark);

            file_card(305, "GAME DISC", settings.disc_path, valid_disc(settings.disc_path), true);
            button({625, 348, 130, 42}, "CLEAR", panel_light);
            button({770, 348, 130, 42}, "BROWSE", accent_dark);

            rectangle({248, 445, 652, 50}, panel);
            rectangle({266, 461, 8, 18}, warning);
            text(290, 460, "COMMERCIAL GAME BOOT IS STILL EXPERIMENTAL", 1, warning);

            text(250, 550, status, 1, status_color);
            button(
                {660, 525, 240, 60},
                settings.disc_path.empty() ? "BOOT BIOS" : "START GAME",
                valid_bios(settings.bios_path) ? accent_dark : panel_light);
        } else if (tab == Tab::settings) {
            text(248, 52, "SETTINGS", 4, text_primary);
            text(250, 95, "LAUNCHER AND DEBUG OPTIONS", 1, text_secondary);

            rectangle({248, 160, 652, 112}, panel);
            text(274, 190, "INSTRUCTION TRACE", 2, text_primary);
            text(274, 224, "PRINT EVERY CPU INSTRUCTION TO THE TERMINAL", 1, text_secondary);
            button(
                {770, 204, 110, 42},
                settings.trace ? "ON" : "OFF",
                settings.trace ? success : panel_light,
                settings.trace ? background : text_primary);

            rectangle({248, 285, 652, 112}, panel);
            text(274, 315, "REMEMBER SELECTED FILES", 2, text_primary);
            text(274, 349, "RESTORE YOUR BIOS AND DISC NEXT TIME", 1, text_secondary);
            button(
                {770, 298, 110, 42},
                settings.remember_files ? "ON" : "OFF",
                settings.remember_files ? success : panel_light,
                settings.remember_files ? background : text_primary);

            button({660, 470, 220, 46}, "CLEAR SAVED FILES", panel_light);
            text(250, 550, status, 1, status_color);
        } else {
            text(248, 52, "ABOUT", 4, text_primary);
            text(250, 95, "AN EDUCATIONAL PLAYSTATION EMULATOR", 1, text_secondary);

            rectangle({248, 160, 652, 300}, panel);
            text(278, 195, "CURRENTLY IMPLEMENTED", 2, accent);
            text(278, 235, "R3000A CPU AND EXCEPTIONS", 1, text_primary);
            text(278, 263, "SOFTWARE GPU AND NATIVE DISPLAY", 1, text_primary);
            text(278, 291, "DMA TIMERS INTERRUPTS AND BIOS BOOT", 1, text_primary);
            text(278, 319, "MODE2 DISC READER AND EARLY CD ROM", 1, text_primary);

            text(278, 365, "STILL IN DEVELOPMENT", 2, warning);
            text(278, 405, "GTE TEXTURES CONTROLLERS AUDIO AND GAME SUPPORT", 1, text_secondary);
            text(250, 550, "SOURCE: GITHUB.COM/BOGDAN-06/PS1-EMULATOR", 1, text_secondary);
        }

        render_present_(renderer_);
    }

    [[noreturn]] void throw_sdl_error(const char* const message) const {
        const char* const error = get_error_ != nullptr ? get_error_() : nullptr;
        throw std::runtime_error{
            std::string{message} + (error != nullptr ? ": " + std::string{error} : "")};
    }

    void cleanup() noexcept {
        if (renderer_ != nullptr && destroy_renderer_ != nullptr) {
            destroy_renderer_(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr && destroy_window_ != nullptr) {
            destroy_window_(window_);
            window_ = nullptr;
        }
        if (initialized_ && quit_ != nullptr) {
            quit_();
            initialized_ = false;
        }
        if (library_ != nullptr) {
            dlclose(library_);
            library_ = nullptr;
        }
    }

    void* library_ = nullptr;
    bool initialized_ = false;
    SdlWindow* window_ = nullptr;
    SdlRenderer* renderer_ = nullptr;
    Init init_ = nullptr;
    Quit quit_ = nullptr;
    GetError get_error_ = nullptr;
    CreateWindow create_window_ = nullptr;
    DestroyWindow destroy_window_ = nullptr;
    CreateRenderer create_renderer_ = nullptr;
    DestroyRenderer destroy_renderer_ = nullptr;
    SetColor set_color_ = nullptr;
    RenderClear render_clear_ = nullptr;
    FillRect fill_rect_ = nullptr;
    DrawRect draw_rect_ = nullptr;
    RenderPresent render_present_ = nullptr;
    PollEvent poll_event_ = nullptr;
    Delay delay_ = nullptr;
};

}  // namespace

std::optional<AppConfig> Launcher::run() {
    LauncherWindow window;
    return window.run();
}

}  // namespace psx
