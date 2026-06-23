#include "psx/gpu.hpp"

#include <cstdint>
#include <string_view>

void expect(bool condition, std::string_view message);

namespace {

[[nodiscard]] constexpr std::uint32_t position(
    const std::uint16_t x,
    const std::uint16_t y) {
    return static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(y) << 16);
}

}  // namespace

void run_gpu_tests() {
    psx::Gpu gpu;

    expect((gpu.read_status() & (1U << 23)) != 0, "GPU display should start disabled");
    expect((gpu.read_status() & (1U << 26)) != 0, "GPU should start ready for commands");
    const auto initial_field = gpu.read_status() >> 31;
    expect(
        !gpu.tick(psx::Gpu::cpu_cycles_per_frame - 1),
        "GPU should not signal VBlank before a full frame");
    expect(gpu.tick(1), "GPU should signal VBlank at the frame boundary");
    expect(
        gpu.read_status() >> 31 != initial_field,
        "GPU field status should toggle at the frame boundary");

    gpu.write_gp0(0x0200'00ff);
    gpu.write_gp0(position(16, 20));
    gpu.write_gp0(position(3, 2));
    expect(gpu.pixel(16, 20) == 0x001f, "fill should convert red to 15-bit color");
    expect(gpu.pixel(31, 21) == 0x001f, "fill width should round up to 16 pixels");
    expect(gpu.pixel(32, 21) == 0, "fill should stop after rounded width");

    gpu.write_gp0(0xa000'0000);
    gpu.write_gp0(position(40, 30));
    gpu.write_gp0(position(3, 1));
    gpu.write_gp0(0x2222'1111);
    gpu.write_gp0(0x4444'3333);
    expect(gpu.pixel(40, 30) == 0x1111, "image upload should write first pixel");
    expect(gpu.pixel(41, 30) == 0x2222, "image upload should write second pixel");
    expect(gpu.pixel(42, 30) == 0x3333, "image upload should ignore padding pixel");

    gpu.write_gp0(0xc000'0000);
    gpu.write_gp0(position(40, 30));
    gpu.write_gp0(position(3, 1));
    expect(gpu.read_data() == 0x2222'1111, "image download should read packed pixels");
    expect((gpu.read_data() & 0xffffU) == 0x3333, "image download should return final odd pixel");

    gpu.write_gp0(0x6000'ff00);
    gpu.write_gp0(position(50, 40));
    gpu.write_gp0(position(4, 3));
    expect(gpu.pixel(53, 42) == 0x03e0, "rectangle command should draw its full area");

    gpu.write_gp0(0x2000'00ff);
    gpu.write_gp0(position(60, 50));
    gpu.write_gp0(position(70, 50));
    gpu.write_gp0(position(60, 60));
    expect(gpu.pixel(62, 52) == 0x001f, "flat triangle should rasterize its interior");
    expect(gpu.pixel(70, 60) == 0, "flat triangle should not fill outside its edges");

    gpu.write_gp1(0x0300'0000);
    expect(!gpu.display_disabled(), "GP1 display enable should clear display-disable status");
    gpu.write_gp1(0x0501'4010);
    expect(gpu.display_x() == 16, "display start should retain X coordinate");
    expect(gpu.display_y() == 80, "display start should retain Y coordinate");
}
