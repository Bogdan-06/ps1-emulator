#pragma once

#include "psx/gpu.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace psx {

class Display {
public:
    Display();
    ~Display();

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    Display(Display&&) noexcept;
    Display& operator=(Display&&) noexcept;

    [[nodiscard]] bool present(const Gpu& gpu);

    static void convert_frame(
        const Gpu& gpu,
        std::span<std::uint32_t> destination,
        std::uint16_t width,
        std::uint16_t height);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace psx
