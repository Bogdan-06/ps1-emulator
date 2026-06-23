#include "psx/cpu.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>

namespace psx {
namespace {

[[nodiscard]] constexpr std::uint32_t opcode(const std::uint32_t instruction) noexcept {
    return instruction >> 26;
}

[[nodiscard]] constexpr std::uint32_t rs(const std::uint32_t instruction) noexcept {
    return (instruction >> 21) & 0x1f;
}

[[nodiscard]] constexpr std::uint32_t rt(const std::uint32_t instruction) noexcept {
    return (instruction >> 16) & 0x1f;
}

[[nodiscard]] constexpr std::uint32_t rd(const std::uint32_t instruction) noexcept {
    return (instruction >> 11) & 0x1f;
}

[[nodiscard]] constexpr std::uint32_t shift_amount(
    const std::uint32_t instruction) noexcept {
    return (instruction >> 6) & 0x1f;
}

[[nodiscard]] constexpr std::uint32_t function(
    const std::uint32_t instruction) noexcept {
    return instruction & 0x3f;
}

[[nodiscard]] constexpr std::uint16_t immediate(
    const std::uint32_t instruction) noexcept {
    return static_cast<std::uint16_t>(instruction);
}

[[nodiscard]] constexpr std::uint32_t sign_extend(
    const std::uint16_t value) noexcept {
    return (value & 0x8000U) != 0
        ? 0xffff'0000U | static_cast<std::uint32_t>(value)
        : static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t sign_extend8(
    const std::uint8_t value) noexcept {
    return (value & 0x80U) != 0
        ? 0xffff'ff00U | static_cast<std::uint32_t>(value)
        : static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t as_signed(const std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t as_unsigned(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

}  // namespace

Cpu::Cpu(Bus bus)
    : bus_(std::move(bus)) {
    // The R3000A starts with the boot exception vector selected.
    cop0_[12] = 1U << 22;
    cop0_[15] = 0x0000'0002;
}

StepResult Cpu::step() {
    const auto instruction_pc = pc_;
    bus_.tick(1);
    if (bus_.interrupt_pending()) {
        cop0_[13] |= 1U << 10;
    } else {
        cop0_[13] &= ~(1U << 10);
    }

    const bool interrupts_enabled = (cop0_[12] & 1U) != 0
        && (cop0_[12] & cop0_[13] & 0x0000'ff00U) != 0;
    if (interrupts_enabled && !next_instruction_in_delay_slot_) {
        current_instruction_in_delay_slot_ = false;
        enter_exception(0, instruction_pc);
        return StepResult{instruction_pc, 0};
    }

    const auto instruction = bus_.load32(instruction_pc);

    load_in_delay_slot_ = pending_load_;
    pending_load_.reset();
    written_register_.reset();
    current_instruction_in_delay_slot_ = next_instruction_in_delay_slot_;
    next_instruction_in_delay_slot_ = false;
    pc_ = next_pc_;
    next_pc_ += 4;

    execute(instruction, instruction_pc);

    if (load_in_delay_slot_.has_value()
        && load_in_delay_slot_->target != 0
        && written_register_ != load_in_delay_slot_->target) {
        registers_[load_in_delay_slot_->target] = load_in_delay_slot_->value;
    }
    load_in_delay_slot_.reset();
    registers_[0] = 0;

    return StepResult{instruction_pc, instruction};
}

std::uint32_t Cpu::pc() const noexcept {
    return pc_;
}

std::uint32_t Cpu::register_value(const std::size_t index) const {
    if (index >= registers_.size()) {
        throw std::out_of_range{"CPU register index out of range"};
    }
    return registers_[index];
}

std::uint32_t Cpu::cop0_register_value(const std::size_t index) const {
    if (index >= cop0_.size()) {
        throw std::out_of_range{"COP0 register index out of range"};
    }
    return cop0_[index];
}

std::uint32_t Cpu::hi() const noexcept {
    return hi_;
}

std::uint32_t Cpu::lo() const noexcept {
    return lo_;
}

const Bus& Cpu::bus() const noexcept {
    return bus_;
}

Bus& Cpu::bus() noexcept {
    return bus_;
}

void Cpu::execute(const std::uint32_t instruction, const std::uint32_t instruction_pc) {
    const auto source = load_register(rs(instruction));
    const auto target = load_register(rt(instruction));
    const auto signed_source = as_signed(source);

    switch (opcode(instruction)) {
    case 0x00:
        execute_special(instruction, instruction_pc);
        return;
    case 0x01:
        execute_regimm(instruction, instruction_pc);
        return;
    case 0x02:
        mark_delay_slot();
        next_pc_ = (pc_ & 0xf000'0000U) | ((instruction & 0x03ff'ffffU) << 2);
        return;
    case 0x03:
        mark_delay_slot();
        set_register(31, next_pc_);
        next_pc_ = (pc_ & 0xf000'0000U) | ((instruction & 0x03ff'ffffU) << 2);
        return;
    case 0x04:
        mark_delay_slot();
        if (source == target) {
            branch(immediate(instruction));
        }
        return;
    case 0x05:
        mark_delay_slot();
        if (source != target) {
            branch(immediate(instruction));
        }
        return;
    case 0x06:
        mark_delay_slot();
        if (signed_source <= 0) {
            branch(immediate(instruction));
        }
        return;
    case 0x07:
        mark_delay_slot();
        if (signed_source > 0) {
            branch(immediate(instruction));
        }
        return;
    case 0x08: {
        const auto operand = as_signed(sign_extend(immediate(instruction)));
        const auto result = static_cast<std::int64_t>(signed_source) + operand;
        if (result < std::numeric_limits<std::int32_t>::min()
            || result > std::numeric_limits<std::int32_t>::max()) {
            arithmetic_overflow(instruction_pc);
            return;
        }
        set_register(rt(instruction), as_unsigned(static_cast<std::int32_t>(result)));
        return;
    }
    case 0x09:
        set_register(rt(instruction), source + sign_extend(immediate(instruction)));
        return;
    case 0x0a:
        set_register(
            rt(instruction),
            signed_source < as_signed(sign_extend(immediate(instruction))) ? 1U : 0U);
        return;
    case 0x0b:
        set_register(
            rt(instruction),
            source < sign_extend(immediate(instruction)) ? 1U : 0U);
        return;
    case 0x0c:
        set_register(rt(instruction), source & immediate(instruction));
        return;
    case 0x0d:
        set_register(rt(instruction), source | immediate(instruction));
        return;
    case 0x0e:
        set_register(rt(instruction), source ^ immediate(instruction));
        return;
    case 0x0f:
        set_register(rt(instruction), static_cast<std::uint32_t>(immediate(instruction)) << 16);
        return;
    case 0x10:
        execute_cop0(instruction, instruction_pc);
        return;
    case 0x20:
        schedule_load(rt(instruction), sign_extend8(bus_.load8(effective_address(instruction))));
        return;
    case 0x21:
        schedule_load(
            rt(instruction),
            sign_extend(bus_.load16(effective_address(instruction))));
        return;
    case 0x22: {
        const auto address = effective_address(instruction);
        const auto word = bus_.load32(address & ~3U);
        const auto current = load_merge_register(rt(instruction));
        std::uint32_t value = 0;

        switch (address & 3U) {
        case 0:
            value = (current & 0x00ff'ffffU) | (word << 24);
            break;
        case 1:
            value = (current & 0x0000'ffffU) | (word << 16);
            break;
        case 2:
            value = (current & 0x0000'00ffU) | (word << 8);
            break;
        case 3:
            value = word;
            break;
        default:
            break;
        }

        schedule_load(rt(instruction), value);
        return;
    }
    case 0x23:
        schedule_load(rt(instruction), bus_.load32(effective_address(instruction)));
        return;
    case 0x24:
        schedule_load(rt(instruction), bus_.load8(effective_address(instruction)));
        return;
    case 0x25:
        schedule_load(rt(instruction), bus_.load16(effective_address(instruction)));
        return;
    case 0x26: {
        const auto address = effective_address(instruction);
        const auto word = bus_.load32(address & ~3U);
        const auto current = load_merge_register(rt(instruction));
        std::uint32_t value = 0;

        switch (address & 3U) {
        case 0:
            value = word;
            break;
        case 1:
            value = (current & 0xff00'0000U) | (word >> 8);
            break;
        case 2:
            value = (current & 0xffff'0000U) | (word >> 16);
            break;
        case 3:
            value = (current & 0xffff'ff00U) | (word >> 24);
            break;
        default:
            break;
        }

        schedule_load(rt(instruction), value);
        return;
    }
    case 0x28:
        if (!cache_isolated()) {
            bus_.store8(effective_address(instruction), static_cast<std::uint8_t>(target));
        }
        return;
    case 0x29:
        if (!cache_isolated()) {
            bus_.store16(effective_address(instruction), static_cast<std::uint16_t>(target));
        }
        return;
    case 0x2a:
        if (!cache_isolated()) {
            const auto address = effective_address(instruction);
            const auto aligned = address & ~3U;
            const auto current = bus_.load32(aligned);
            std::uint32_t value = 0;

            switch (address & 3U) {
            case 0:
                value = (current & 0xffff'ff00U) | (target >> 24);
                break;
            case 1:
                value = (current & 0xffff'0000U) | (target >> 16);
                break;
            case 2:
                value = (current & 0xff00'0000U) | (target >> 8);
                break;
            case 3:
                value = target;
                break;
            default:
                break;
            }

            bus_.store32(aligned, value);
        }
        return;
    case 0x2b:
        if (!cache_isolated()) {
            bus_.store32(effective_address(instruction), target);
        }
        return;
    case 0x2e:
        if (!cache_isolated()) {
            const auto address = effective_address(instruction);
            const auto aligned = address & ~3U;
            const auto current = bus_.load32(aligned);
            std::uint32_t value = 0;

            switch (address & 3U) {
            case 0:
                value = target;
                break;
            case 1:
                value = (current & 0x0000'00ffU) | (target << 8);
                break;
            case 2:
                value = (current & 0x0000'ffffU) | (target << 16);
                break;
            case 3:
                value = (current & 0x00ff'ffffU) | (target << 24);
                break;
            default:
                break;
            }

            bus_.store32(aligned, value);
        }
        return;
    default:
        unsupported(instruction, instruction_pc, "opcode");
    }
}

void Cpu::execute_special(
    const std::uint32_t instruction,
    const std::uint32_t instruction_pc) {
    const auto source = load_register(rs(instruction));
    const auto target = load_register(rt(instruction));
    const auto signed_source = as_signed(source);
    const auto signed_target = as_signed(target);

    switch (function(instruction)) {
    case 0x00:
        set_register(rd(instruction), target << shift_amount(instruction));
        return;
    case 0x02:
        set_register(rd(instruction), target >> shift_amount(instruction));
        return;
    case 0x03:
        set_register(
            rd(instruction),
            as_unsigned(signed_target >> shift_amount(instruction)));
        return;
    case 0x04:
        set_register(rd(instruction), target << (source & 0x1f));
        return;
    case 0x06:
        set_register(rd(instruction), target >> (source & 0x1f));
        return;
    case 0x07:
        set_register(rd(instruction), as_unsigned(signed_target >> (source & 0x1f)));
        return;
    case 0x08:
        mark_delay_slot();
        next_pc_ = source;
        return;
    case 0x09:
        mark_delay_slot();
        set_register(rd(instruction), next_pc_);
        next_pc_ = source;
        return;
    case 0x0c:
        enter_exception(8, instruction_pc);
        return;
    case 0x0d:
        enter_exception(9, instruction_pc);
        return;
    case 0x10:
        set_register(rd(instruction), hi_);
        return;
    case 0x11:
        hi_ = source;
        return;
    case 0x12:
        set_register(rd(instruction), lo_);
        return;
    case 0x13:
        lo_ = source;
        return;
    case 0x18: {
        const auto result = static_cast<std::int64_t>(signed_source)
            * static_cast<std::int64_t>(signed_target);
        const auto bits = std::bit_cast<std::uint64_t>(result);
        lo_ = static_cast<std::uint32_t>(bits);
        hi_ = static_cast<std::uint32_t>(bits >> 32);
        return;
    }
    case 0x19: {
        const auto result = static_cast<std::uint64_t>(source)
            * static_cast<std::uint64_t>(target);
        lo_ = static_cast<std::uint32_t>(result);
        hi_ = static_cast<std::uint32_t>(result >> 32);
        return;
    }
    case 0x1a:
        if (target == 0) {
            hi_ = source;
            lo_ = signed_source >= 0 ? 0xffff'ffffU : 1U;
        } else if (
            signed_source == std::numeric_limits<std::int32_t>::min()
            && signed_target == -1) {
            hi_ = 0;
            lo_ = as_unsigned(std::numeric_limits<std::int32_t>::min());
        } else {
            hi_ = as_unsigned(signed_source % signed_target);
            lo_ = as_unsigned(signed_source / signed_target);
        }
        return;
    case 0x1b:
        if (target == 0) {
            hi_ = source;
            lo_ = 0xffff'ffff;
        } else {
            hi_ = source % target;
            lo_ = source / target;
        }
        return;
    case 0x20:
    case 0x22: {
        const auto right = function(instruction) == 0x20
            ? static_cast<std::int64_t>(signed_target)
            : -static_cast<std::int64_t>(signed_target);
        const auto result = static_cast<std::int64_t>(signed_source) + right;
        if (result < std::numeric_limits<std::int32_t>::min()
            || result > std::numeric_limits<std::int32_t>::max()) {
            arithmetic_overflow(instruction_pc);
            return;
        }
        set_register(rd(instruction), as_unsigned(static_cast<std::int32_t>(result)));
        return;
    }
    case 0x21:
        set_register(rd(instruction), source + target);
        return;
    case 0x23:
        set_register(rd(instruction), source - target);
        return;
    case 0x24:
        set_register(rd(instruction), source & target);
        return;
    case 0x25:
        set_register(rd(instruction), source | target);
        return;
    case 0x26:
        set_register(rd(instruction), source ^ target);
        return;
    case 0x27:
        set_register(rd(instruction), ~(source | target));
        return;
    case 0x2a:
        set_register(rd(instruction), signed_source < signed_target ? 1U : 0U);
        return;
    case 0x2b:
        set_register(rd(instruction), source < target ? 1U : 0U);
        return;
    default:
        unsupported(instruction, instruction_pc, "SPECIAL function");
    }
}

void Cpu::execute_regimm(
    const std::uint32_t instruction,
    const std::uint32_t instruction_pc) {
    const auto source = as_signed(load_register(rs(instruction)));
    const auto operation = rt(instruction);
    const bool link = operation == 0x10 || operation == 0x11;
    bool taken = false;

    switch (operation) {
    case 0x00:
    case 0x10:
        taken = source < 0;
        break;
    case 0x01:
    case 0x11:
        taken = source >= 0;
        break;
    default:
        unsupported(instruction, instruction_pc, "REGIMM function");
        return;
    }

    mark_delay_slot();
    if (link) {
        set_register(31, next_pc_);
    }
    if (taken) {
        branch(immediate(instruction));
    }
}

void Cpu::execute_cop0(
    const std::uint32_t instruction,
    const std::uint32_t instruction_pc) {
    const auto operation = rs(instruction);

    switch (operation) {
    case 0x00:
        schedule_load(rt(instruction), cop0_[rd(instruction)]);
        return;
    case 0x04:
        cop0_[rd(instruction)] = load_register(rt(instruction));
        return;
    case 0x10:
        if (function(instruction) == 0x10) {
            auto& status = cop0_[12];
            status = (status & ~0x0fU) | ((status >> 2) & 0x0fU);
            return;
        }
        break;
    default:
        break;
    }

    unsupported(instruction, instruction_pc, "COP0 function");
}

void Cpu::branch(const std::uint16_t value) {
    const auto offset = sign_extend(value) << 2;
    next_pc_ = pc_ + offset;
}

void Cpu::enter_exception(
    const std::uint32_t code,
    const std::uint32_t instruction_pc) {
    auto& status = cop0_[12];
    auto& cause = cop0_[13];
    auto& epc = cop0_[14];

    cause = (cause & ~0x8000'007cU) | ((code & 0x1fU) << 2);
    epc = instruction_pc;
    if (current_instruction_in_delay_slot_) {
        cause |= 0x8000'0000U;
        epc -= 4;
    }

    status = (status & ~0x3fU) | ((status << 2) & 0x3fU);
    const auto vector = (status & (1U << 22)) != 0
        ? 0xbfc0'0180U
        : 0x8000'0080U;
    pc_ = vector;
    next_pc_ = vector + 4;
    next_instruction_in_delay_slot_ = false;
}

void Cpu::mark_delay_slot() noexcept {
    next_instruction_in_delay_slot_ = true;
}

void Cpu::set_register(const std::uint32_t index, const std::uint32_t value) noexcept {
    if (index == 0) {
        return;
    }
    registers_[index] = value;
    written_register_ = index;
}

void Cpu::schedule_load(const std::uint32_t index, const std::uint32_t value) noexcept {
    if (index != 0) {
        pending_load_ = PendingLoad{index, value};
    }
}

std::uint32_t Cpu::load_register(const std::uint32_t index) const noexcept {
    return registers_[index];
}

std::uint32_t Cpu::load_merge_register(const std::uint32_t index) const noexcept {
    if (load_in_delay_slot_.has_value() && load_in_delay_slot_->target == index) {
        return load_in_delay_slot_->value;
    }
    return load_register(index);
}

std::uint32_t Cpu::effective_address(const std::uint32_t instruction) const noexcept {
    return load_register(rs(instruction)) + sign_extend(immediate(instruction));
}

bool Cpu::cache_isolated() const noexcept {
    return (cop0_[12] & (1U << 16)) != 0;
}

void Cpu::unsupported(
    const std::uint32_t instruction,
    const std::uint32_t instruction_pc,
    const char* const category) {
    static_cast<void>(instruction);
    static_cast<void>(category);
    enter_exception(10, instruction_pc);
}

void Cpu::arithmetic_overflow(const std::uint32_t instruction_pc) {
    enter_exception(12, instruction_pc);
}

}  // namespace psx
