#include "psx/display.hpp"
#include "psx/gpu.hpp"

#include <array>
#include <cstdint>
#include <string_view>

void expect(bool condition, std::string_view message);

void run_display_tests() {
    psx::Gpu gpu;
    std::array<std::uint32_t, 4> frame{};

    psx::Display::convert_frame(gpu, frame, 2, 2);
    expect(frame[0] == 0xff00'0000, "disabled display should render black");

    gpu.write_gp0(0xa000'0000);
    gpu.write_gp0(0);
    gpu.write_gp0(0x0001'0002);
    gpu.write_gp0(0x03e0'001f);
    gpu.write_gp1(0x0300'0000);
    psx::Display::convert_frame(gpu, frame, 2, 1);
    expect(frame[0] == 0xffff'0000, "15-bit red should expand to ARGB");
    expect(frame[1] == 0xff00'ff00, "15-bit green should expand to ARGB");
}
