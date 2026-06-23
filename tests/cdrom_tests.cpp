#include "psx/cdrom.hpp"
#include "psx/disc.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

void expect(bool condition, std::string_view message);

namespace {

void select(psx::Cdrom& cdrom, const std::uint8_t index) {
    cdrom.write(0, index);
}

void acknowledge(psx::Cdrom& cdrom) {
    select(cdrom, 1);
    cdrom.write(3, 0x1f);
}

}  // namespace

void run_cdrom_tests() {
    const auto path = std::filesystem::temp_directory_path()
        / "ps1-emulator-cdrom-test.bin";
    {
        std::ofstream image{path, std::ios::binary};
        std::array<std::uint8_t, psx::Disc::raw_sector_size> sector{};
        sector[15] = 2;
        sector[18] = 0x08;
        sector[24] = 0x11;
        sector[25] = 0x22;
        sector[26] = 0x33;
        sector[27] = 0x44;
        image.write(
            reinterpret_cast<const char*>(sector.data()),
            static_cast<std::streamsize>(sector.size()));
    }

    psx::Cdrom cdrom{std::optional<psx::Disc>{psx::Disc::open(path)}};
    select(cdrom, 1);
    cdrom.write(2, 0x1f);
    select(cdrom, 0);
    cdrom.write(1, 0x01);
    expect(cdrom.interrupt_requested(), "GetStat should request a CD interrupt");
    expect(cdrom.read(1) == 0x02, "GetStat should report a disc with motor on");
    acknowledge(cdrom);

    select(cdrom, 0);
    cdrom.write(2, 0x00);
    cdrom.write(2, 0x02);
    cdrom.write(2, 0x00);
    cdrom.write(1, 0x02);
    acknowledge(cdrom);
    select(cdrom, 0);
    cdrom.write(1, 0x06);
    acknowledge(cdrom);
    select(cdrom, 0);
    cdrom.write(3, 0x80);
    expect(cdrom.read_dma_word() == 0x4433'2211, "ReadN should expose sector data");

    std::filesystem::remove(path);
}
