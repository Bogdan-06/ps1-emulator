#pragma once

#include "psx/bus.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace psx {

class CpuError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct StepResult {
    std::uint32_t pc;
    std::uint32_t instruction;
};

class Cpu {
public:
    static constexpr std::uint32_t reset_vector = 0xbfc0'0000;

    explicit Cpu(Bus bus);

    [[nodiscard]] StepResult step();

    [[nodiscard]] std::uint32_t pc() const noexcept;
    [[nodiscard]] std::uint32_t register_value(std::size_t index) const;
    [[nodiscard]] std::uint32_t cop0_register_value(std::size_t index) const;
    [[nodiscard]] std::uint32_t hi() const noexcept;
    [[nodiscard]] std::uint32_t lo() const noexcept;
    [[nodiscard]] const Bus& bus() const noexcept;
    [[nodiscard]] Bus& bus() noexcept;

private:
    struct PendingLoad {
        std::uint32_t target;
        std::uint32_t value;
    };

    void execute(std::uint32_t instruction, std::uint32_t instruction_pc);
    void execute_special(std::uint32_t instruction, std::uint32_t instruction_pc);
    void execute_regimm(std::uint32_t instruction, std::uint32_t instruction_pc);
    void execute_cop0(std::uint32_t instruction, std::uint32_t instruction_pc);

    void branch(std::uint16_t immediate);
    void enter_exception(std::uint32_t code, std::uint32_t instruction_pc);
    void mark_delay_slot() noexcept;
    void set_register(std::uint32_t index, std::uint32_t value) noexcept;
    void schedule_load(std::uint32_t index, std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t load_register(std::uint32_t index) const noexcept;
    [[nodiscard]] std::uint32_t load_merge_register(std::uint32_t index) const noexcept;
    [[nodiscard]] std::uint32_t effective_address(std::uint32_t instruction) const noexcept;
    [[nodiscard]] bool cache_isolated() const noexcept;

    void unsupported(
        std::uint32_t instruction,
        std::uint32_t instruction_pc,
        const char* category);
    void arithmetic_overflow(std::uint32_t instruction_pc);

    Bus bus_;
    std::array<std::uint32_t, 32> registers_{};
    std::array<std::uint32_t, 32> cop0_{};
    std::uint32_t hi_ = 0;
    std::uint32_t lo_ = 0;
    std::uint32_t pc_ = reset_vector;
    std::uint32_t next_pc_ = reset_vector + 4;
    std::optional<PendingLoad> pending_load_;
    std::optional<PendingLoad> load_in_delay_slot_;
    std::optional<std::uint32_t> written_register_;
    bool current_instruction_in_delay_slot_ = false;
    bool next_instruction_in_delay_slot_ = false;
};

}  // namespace psx
