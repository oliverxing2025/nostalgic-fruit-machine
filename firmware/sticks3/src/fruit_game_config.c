#include "fruit_game_config.h"

const uint16_t fruit_base_payouts[FRUIT_CATEGORY_COUNT] = {
    6, 4, 4, 4, 3, 3, 3, 2,
};

/*
 * Clockwise from the upper-left. Version 1 uses equal weights. BAR cells use
 * explicit 50x/100x values; multiplier=3 marks the visible X3 cells.
 */
const fruit_track_cell_t fruit_track[FRUIT_TRACK_COUNT] = {
    {FRUIT_SYMBOL_ORANGE, 1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_BELL,   1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_BAR,    1, 6,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_BAR,    1, 12,  FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_APPLE,  3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_CYAN,   1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_MELON,  1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_MELON,  3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_LUCK,   1, 0,   FRUIT_LUCK_BLUE,  1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_ORANGE, 3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_ORANGE, 1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_BELL,   1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_SEVEN,  3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_SEVEN,  1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_CYAN,   3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_CYAN,   1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_STAR,   1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_STAR,   3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_LUCK,   1, 0,   FRUIT_LUCK_ORANGE,1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_BELL,   3, 0,   FRUIT_LUCK_NONE,  1},
};

const uint8_t fruit_luck_multipliers[] = {2, 2, 2, 3, 3, 5, 8, 10};
const uint8_t fruit_luck_multiplier_count =
    sizeof(fruit_luck_multipliers) / sizeof(fruit_luck_multipliers[0]);

const uint8_t fruit_gamble_percentages[FRUIT_GAMBLE_LEVEL_COUNT] = {
    25, 50, 75, 100,
};

const uint16_t fruit_jackpot_weights[FRUIT_JACKPOT_COUNT] = {
    5, 15, 25, 20, 35,
};

/*
 * Gameplay probabilities and animation timings live here so later balancing
 * does not require changes to the state machine or UI code.
 */
const fruit_game_tuning_t fruit_game_tuning = {
    .spin_start_interval_ms = 120,
    .spin_fast_interval_ms = 28,
    .spin_end_interval_ms = 230,
    .spin_accel_steps = 12,
    .spin_steady_laps = 2,
    .spin_decel_min_steps = 24,
    .blue_luck_step_ms = 220,
    .blue_luck_min_cells = 2,
    .blue_luck_max_cells = 3,
    .jackpot_trigger_basis_points = 50,
    .jackpot_random_min_multiplier = 50,
    .jackpot_random_max_multiplier = 200,
    .jackpot_big_three_multiplier = 100,
    .jackpot_small_three_multiplier = 30,
    .jackpot_eight_immortals_multiplier = 88,
    .jackpot_all_lights_bonus_multiplier = 10,
    .big_bang_free_spins = 3,
    .big_bang_flash_steps = 12,
    .big_bang_flash_interval_ms = 100,
    .nvs_flush_delay_ms = 3000,
    .motion_sample_interval_ms = 30,
    .motion_trigger_raw = 4200,
    .motion_release_raw = 2800,
    .motion_cooldown_ms = 240,
    .motion_rearm_samples = 4,
    .motion_confirm_samples = 2,
    .motion_axis_dominance_percent = 125,
    .motion_park_timeout_ms = 5000,
    .motion_stable_raw = 250,
    .motion_pickup_raw = 3000,
    .motion_park_samples = 30,
    .motion_pickup_settle_samples = 12,
};
