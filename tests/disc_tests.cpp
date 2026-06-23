#include "psx/disc.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

void expect(bool condition, std::string_view message);

void run_disc_tests() {
    const auto directory = std::filesystem::temp_directory_path()
        / "ps1-emulator-disc-test";
    std::filesystem::create_directories(directory);
    const auto image_path = directory / "test.bin";
    const auto cue_path = directory / "test.cue";

    {
        std::ofstream image{image_path, std::ios::binary};
        for (std::uint32_t sector_index = 0; sector_index < 2; ++sector_index) {
            std::array<std::uint8_t, psx::Disc::raw_sector_size> sector{};
            sector[15] = 2;
            sector[16] = 1;
            sector[17] = 2;
            sector[18] = 0x08;
            sector[19] = 4;
            sector[24] = static_cast<std::uint8_t>(0x40 + sector_index);
            image.write(
                reinterpret_cast<const char*>(sector.data()),
                static_cast<std::streamsize>(sector.size()));
        }
    }
    {
        std::ofstream cue{cue_path};
        cue << "FILE \"test.bin\" BINARY\n"
               "  TRACK 01 MODE2/2352\n"
               "    INDEX 01 00:00:00\n";
    }

    auto disc = psx::Disc::open(cue_path);
    expect(disc.sector_count() == 2, "disc should report raw sector count");
    expect(disc.image_path() == image_path, "CUE should resolve its binary track");
    expect(disc.read_data_sector(0)[0] == 0x40, "disc should extract first data sector");
    expect(disc.read_data_sector(1)[0] == 0x41, "disc should seek to requested sector");

    std::filesystem::remove_all(directory);
}
