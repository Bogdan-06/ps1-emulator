#pragma once

#include "psx/cli.hpp"

#include <optional>

namespace psx {

class Launcher {
public:
    [[nodiscard]] static std::optional<AppConfig> run();
};

}  // namespace psx
