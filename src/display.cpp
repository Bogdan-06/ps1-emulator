#include "psx/display.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace psx {
namespace {

constexpr std::uint32_t sdl_init_video = 0x0000'0020;
constexpr std::uint32_t sdl_window_shown = 0x0000'0004;
constexpr std::uint32_t sdl_window_resizable = 0x0000'0020;
constexpr std::int32_t sdl_window_position_centered = 0x2fff'0000;
constexpr std::uint32_t sdl_renderer_accelerated = 0x0000'0002;
constexpr std::uint32_t sdl_renderer_present_vsync = 0x0000'0004;
constexpr std::uint32_t sdl_pixel_format_argb8888 = 0x1636'2004;
constexpr std::int32_t sdl_texture_access_streaming = 1;
constexpr std::uint32_t sdl_quit = 0x0000'0100;

struct SdlWindow;
struct SdlRenderer;
struct SdlTexture;
struct SdlRect;

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

[[nodiscard]] std::uint8_t expand_five_bits(const std::uint16_t value) noexcept {
    const auto shifted = static_cast<std::uint8_t>(value << 3);
    return static_cast<std::uint8_t>(shifted | (shifted >> 5));
}

}  // namespace

class Display::Impl {
public:
    using Init = int (*)(std::uint32_t);
    using Quit = void (*)();
    using GetError = const char* (*)();
    using CreateWindow = SdlWindow* (*)(
        const char*,
        int,
        int,
        int,
        int,
        std::uint32_t);
    using DestroyWindow = void (*)(SdlWindow*);
    using CreateRenderer = SdlRenderer* (*)(SdlWindow*, int, std::uint32_t);
    using DestroyRenderer = void (*)(SdlRenderer*);
    using CreateTexture = SdlTexture* (*)(
        SdlRenderer*,
        std::uint32_t,
        int,
        int,
        int);
    using DestroyTexture = void (*)(SdlTexture*);
    using UpdateTexture = int (*)(SdlTexture*, const SdlRect*, const void*, int);
    using RenderClear = int (*)(SdlRenderer*);
    using RenderCopy = int (*)(
        SdlRenderer*,
        SdlTexture*,
        const SdlRect*,
        const SdlRect*);
    using RenderPresent = void (*)(SdlRenderer*);
    using PollEvent = int (*)(void*);

    Impl() {
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
            create_texture_ = load_symbol<CreateTexture>(library_, "SDL_CreateTexture");
            destroy_texture_ = load_symbol<DestroyTexture>(library_, "SDL_DestroyTexture");
            update_texture_ = load_symbol<UpdateTexture>(library_, "SDL_UpdateTexture");
            render_clear_ = load_symbol<RenderClear>(library_, "SDL_RenderClear");
            render_copy_ = load_symbol<RenderCopy>(library_, "SDL_RenderCopy");
            render_present_ = load_symbol<RenderPresent>(library_, "SDL_RenderPresent");
            poll_event_ = load_symbol<PollEvent>(library_, "SDL_PollEvent");

            if (init_(sdl_init_video) != 0) {
                throw_sdl_error("unable to initialize SDL video");
            }
            initialized_ = true;

            window_ = create_window_(
                "PS1 Emulator",
                sdl_window_position_centered,
                sdl_window_position_centered,
                960,
                720,
                sdl_window_shown | sdl_window_resizable);
            if (window_ == nullptr) {
                throw_sdl_error("unable to create display window");
            }

            renderer_ = create_renderer_(
                window_,
                -1,
                sdl_renderer_accelerated | sdl_renderer_present_vsync);
            if (renderer_ == nullptr) {
                renderer_ = create_renderer_(window_, -1, 0);
            }
            if (renderer_ == nullptr) {
                throw_sdl_error("unable to create display renderer");
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() {
        cleanup();
    }

    [[nodiscard]] bool present(const Gpu& gpu) {
        alignas(std::max_align_t) std::array<std::byte, 128> event{};
        while (poll_event_(event.data()) != 0) {
            std::uint32_t type = 0;
            std::memcpy(&type, event.data(), sizeof(type));
            if (type == sdl_quit) {
                return false;
            }
        }

        const auto width = gpu.display_width();
        const auto height = gpu.display_height();
        ensure_texture(width, height);
        Display::convert_frame(gpu, pixels_, width, height);

        if (update_texture_(
                texture_,
                nullptr,
                pixels_.data(),
                static_cast<int>(width * sizeof(std::uint32_t))) != 0) {
            throw_sdl_error("unable to upload display frame");
        }
        if (render_clear_(renderer_) != 0) {
            throw_sdl_error("unable to clear display");
        }
        if (render_copy_(renderer_, texture_, nullptr, nullptr) != 0) {
            throw_sdl_error("unable to render display frame");
        }
        render_present_(renderer_);
        return true;
    }

private:
    [[noreturn]] void throw_sdl_error(const char* const message) const {
        const char* const error = get_error_ != nullptr ? get_error_() : nullptr;
        throw std::runtime_error{
            std::string{message} + (error != nullptr ? ": " + std::string{error} : "")};
    }

    void ensure_texture(const std::uint16_t width, const std::uint16_t height) {
        if (texture_ != nullptr && texture_width_ == width && texture_height_ == height) {
            return;
        }

        if (texture_ != nullptr) {
            destroy_texture_(texture_);
            texture_ = nullptr;
        }

        texture_ = create_texture_(
            renderer_,
            sdl_pixel_format_argb8888,
            sdl_texture_access_streaming,
            width,
            height);
        if (texture_ == nullptr) {
            throw_sdl_error("unable to create display texture");
        }

        texture_width_ = width;
        texture_height_ = height;
        pixels_.resize(static_cast<std::size_t>(width) * height);
    }

    void cleanup() noexcept {
        if (texture_ != nullptr && destroy_texture_ != nullptr) {
            destroy_texture_(texture_);
            texture_ = nullptr;
        }
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
    SdlTexture* texture_ = nullptr;
    std::uint16_t texture_width_ = 0;
    std::uint16_t texture_height_ = 0;
    std::vector<std::uint32_t> pixels_;

    Init init_ = nullptr;
    Quit quit_ = nullptr;
    GetError get_error_ = nullptr;
    CreateWindow create_window_ = nullptr;
    DestroyWindow destroy_window_ = nullptr;
    CreateRenderer create_renderer_ = nullptr;
    DestroyRenderer destroy_renderer_ = nullptr;
    CreateTexture create_texture_ = nullptr;
    DestroyTexture destroy_texture_ = nullptr;
    UpdateTexture update_texture_ = nullptr;
    RenderClear render_clear_ = nullptr;
    RenderCopy render_copy_ = nullptr;
    RenderPresent render_present_ = nullptr;
    PollEvent poll_event_ = nullptr;
};

Display::Display()
    : impl_(std::make_unique<Impl>()) {}

Display::~Display() = default;
Display::Display(Display&&) noexcept = default;
Display& Display::operator=(Display&&) noexcept = default;

bool Display::present(const Gpu& gpu) {
    return impl_->present(gpu);
}

void Display::convert_frame(
    const Gpu& gpu,
    const std::span<std::uint32_t> destination,
    const std::uint16_t width,
    const std::uint16_t height) {
    const auto required_size = static_cast<std::size_t>(width) * height;
    if (destination.size() < required_size) {
        throw std::invalid_argument{"display frame destination is too small"};
    }

    if (gpu.display_disabled()) {
        std::fill_n(destination.begin(), required_size, 0xff00'0000U);
        return;
    }

    const auto vram = gpu.vram();
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto source_y = (static_cast<std::uint32_t>(gpu.display_y()) + row)
            % Gpu::vram_height;
        for (std::uint32_t column = 0; column < width; ++column) {
            const auto source_x = (static_cast<std::uint32_t>(gpu.display_x()) + column)
                % Gpu::vram_width;
            const auto pixel = vram[source_y * Gpu::vram_width + source_x];
            const auto red = expand_five_bits(pixel & 0x1fU);
            const auto green = expand_five_bits((pixel >> 5) & 0x1fU);
            const auto blue = expand_five_bits((pixel >> 10) & 0x1fU);
            destination[static_cast<std::size_t>(row) * width + column] =
                0xff00'0000U
                | (static_cast<std::uint32_t>(red) << 16)
                | (static_cast<std::uint32_t>(green) << 8)
                | blue;
        }
    }
}

}  // namespace psx
