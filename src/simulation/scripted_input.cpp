#include <rollback_lab/simulation/scripted_input.hpp>

#include <rollback_lab/core/pcg32.hpp>

namespace rollback_lab {

auto scripted_input(const std::uint64_t scenario_seed,
                    const FrameNumber frame,
                    const PlayerId player) -> InputFrame {
    const auto player_value = static_cast<std::uint64_t>(player);
    const auto mixed_seed = scenario_seed ^
                            (static_cast<std::uint64_t>(frame.value) *
                             0x9E3779B97F4A7C15ULL) ^
                            (player_value * 0xD1B54A32D192ED03ULL);
    Pcg32 random{mixed_seed, scenario_seed + player_value + 1U};

    std::uint8_t buttons = (scenario_seed & 1U) == 0U
                               ? button_mask(Button::left)
                               : button_mask(Button::right);
    const auto vertical = random.bounded(6U);
    if (vertical == 0U) {
        buttons = static_cast<std::uint8_t>(buttons | button_mask(Button::up));
    } else if (vertical == 1U) {
        buttons = static_cast<std::uint8_t>(buttons | button_mask(Button::down));
    }
    if (random.bounded(11U) == 0U) {
        buttons = static_cast<std::uint8_t>(buttons | button_mask(Button::attack));
    }
    return InputFrame{frame, player, frame.value, buttons};
}

}  // namespace rollback_lab
