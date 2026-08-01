#include "fruit_game_config.h"

const uint16_t fruit_base_payouts[FRUIT_CATEGORY_COUNT] = {
    0, 40, 30, 20, 25, 15, 10, 5,
};

/*
 * Cost of adding one bet unit, in the same order as fruit_symbol_t:
 * BAR, 77, star, watermelon, bell, lemon, orange, apple.
 * These values are stake costs only; they are not payout multipliers.
 */
const uint8_t fruit_bet_costs[FRUIT_CATEGORY_COUNT] = {
    10, 8, 6, 4, 5, 3, 2, 1,
};

/*
 * Clockwise from the upper-left. The visible board remains unchanged, while
 * weights favor useful mid-tier outcomes and keep the one-unit all-symbol
 * visible symbol variety is kept broad by reducing repeated low-tier results.
 * BAR cells use explicit 50x/100x values; multiplier=3 triples the symbol's
 * normal payout on visible X3 cells.
 */
const fruit_track_cell_t fruit_track[FRUIT_TRACK_COUNT] = {
    {FRUIT_SYMBOL_ORANGE, 1, 0,   FRUIT_LUCK_NONE,  2},
    {FRUIT_SYMBOL_BELL,   1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_BAR,    1, 50,  FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_BAR,    1, 100, FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_APPLE,  3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_CYAN,   1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_MELON,  1, 0,   FRUIT_LUCK_NONE,  5},
    {FRUIT_SYMBOL_MELON,  3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_LUCK,   1, 0,   FRUIT_LUCK_BLUE,  1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_ORANGE, 3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_ORANGE, 1, 0,   FRUIT_LUCK_NONE,  2},
    {FRUIT_SYMBOL_BELL,   1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_SEVEN,  3, 0,   FRUIT_LUCK_NONE,  2},
    {FRUIT_SYMBOL_SEVEN,  1, 0,   FRUIT_LUCK_NONE,  6},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_CYAN,   3, 0,   FRUIT_LUCK_NONE,  1},
    {FRUIT_SYMBOL_CYAN,   1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_STAR,   1, 0,   FRUIT_LUCK_NONE,  5},
    {FRUIT_SYMBOL_STAR,   3, 0,   FRUIT_LUCK_NONE,  2},
    {FRUIT_SYMBOL_LUCK,   1, 0,   FRUIT_LUCK_ORANGE,1},
    {FRUIT_SYMBOL_APPLE,  1, 0,   FRUIT_LUCK_NONE,  3},
    {FRUIT_SYMBOL_BELL,   3, 0,   FRUIT_LUCK_NONE,  2},
};

const uint8_t fruit_luck_multipliers[] = {5, 10, 15, 20, 30, 40, 60};
const uint8_t fruit_luck_multiplier_weights[] = {40, 25, 15, 10, 5, 3, 2};
const uint8_t fruit_luck_multiplier_count =
    sizeof(fruit_luck_multipliers) / sizeof(fruit_luck_multipliers[0]);

const uint8_t fruit_gamble_percentages[FRUIT_GAMBLE_LEVEL_COUNT] = {
    25, 50, 75, 100,
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
    .blue_luck_step_ms = 650,
    .blue_luck_min_cells = 5,
    .blue_luck_max_cells = 5,
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
    .motion_park_timeout_ms = 1800,
    .motion_stable_raw = 600,
    .motion_park_samples = 8,
    .motion_pickup_settle_samples = 12,
};
