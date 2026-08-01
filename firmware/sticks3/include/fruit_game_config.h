#pragma once

#include <stdint.h>

#define FRUIT_CATEGORY_COUNT 8
#define FRUIT_TRACK_COUNT 24
#define FRUIT_MAX_BET_PER_SYMBOL 99
#define FRUIT_STARTING_CREDIT 0
#define FRUIT_CREDIT_TOPUP_UNIT 5
#define FRUIT_MAX_CREDIT 999999
#define FRUIT_MIN_CREDIT (-99999)
#define FRUIT_BIG_BANG_THRESHOLD \
    (FRUIT_MAX_BET_PER_SYMBOL * FRUIT_CATEGORY_COUNT * 400)
#define FRUIT_GAMBLE_LEVEL_COUNT 4

typedef enum {
    FRUIT_SYMBOL_BAR,
    FRUIT_SYMBOL_SEVEN,
    FRUIT_SYMBOL_STAR,
    FRUIT_SYMBOL_MELON,
    FRUIT_SYMBOL_BELL,
    FRUIT_SYMBOL_CYAN,
    FRUIT_SYMBOL_ORANGE,
    FRUIT_SYMBOL_APPLE,
    FRUIT_SYMBOL_LUCK,
} fruit_symbol_t;

typedef enum {
    FRUIT_LUCK_NONE,
    FRUIT_LUCK_ORANGE,
    FRUIT_LUCK_BLUE,
} fruit_luck_t;

typedef struct {
    fruit_symbol_t symbol;
    uint8_t multiplier;
    uint16_t payout_override;
    fruit_luck_t luck;
    uint16_t weight;
} fruit_track_cell_t;

typedef struct {
    uint16_t spin_start_interval_ms;
    uint16_t spin_fast_interval_ms;
    uint16_t spin_end_interval_ms;
    uint8_t spin_accel_steps;
    uint8_t spin_steady_laps;
    uint8_t spin_decel_min_steps;
    uint16_t blue_luck_step_ms;
    uint8_t blue_luck_min_cells;
    uint8_t blue_luck_max_cells;
    uint8_t big_bang_free_spins;
    uint8_t big_bang_flash_steps;
    uint16_t big_bang_flash_interval_ms;
    uint16_t nvs_flush_delay_ms;
    uint16_t motion_sample_interval_ms;
    uint16_t motion_trigger_raw;
    uint16_t motion_release_raw;
    uint16_t motion_cooldown_ms;
    uint8_t motion_rearm_samples;
    uint8_t motion_confirm_samples;
    uint8_t motion_axis_dominance_percent;
    uint16_t motion_park_timeout_ms;
    uint16_t motion_stable_raw;
    uint8_t motion_park_samples;
    uint8_t motion_pickup_settle_samples;
} fruit_game_tuning_t;

extern const uint16_t fruit_base_payouts[FRUIT_CATEGORY_COUNT];
extern const uint8_t fruit_bet_costs[FRUIT_CATEGORY_COUNT];
extern const fruit_track_cell_t fruit_track[FRUIT_TRACK_COUNT];
extern const uint8_t fruit_luck_multipliers[];
extern const uint8_t fruit_luck_multiplier_weights[];
extern const uint8_t fruit_luck_multiplier_count;
extern const uint8_t fruit_gamble_percentages[FRUIT_GAMBLE_LEVEL_COUNT];
extern const fruit_game_tuning_t fruit_game_tuning;
